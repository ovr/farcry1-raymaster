// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayAO.cpp
//	Description: The ambient occlusion by rays (d3d12_AO rt, on a device
//	with DXR 1.1): a backend of the ambient occlusion (AmbientOcclusion.h)
//	on the compute passes with rays (D3D12Compute.h, Shaders/RayAO.hlsl):
//	  1. from the surfaces of the depth buffer, rays d3d12_AORadius long in
//	     directions of the hemisphere about their faces as the cosine
//	     spreads them, among this frame's scene for rays (RayTracing.h, with
//	     what is out of the view: d3d12_RTOffscreen): a ray for each 2 x 2
//	     pixels, each of them in turn (d3d12_AOQuality 0 to 2), or for each
//	     pixel (3)
//	  2. each pixel takes the rays round it on its surface, blended into the
//	     history where the surface was in the frame before (HISTORY_FRAMES
//	     at most)
//	  3. filtered on the pixel's plane, wider where the history is young,
//	     into the visibility the resolve takes.
//	d3d12_AODenoise 0: no history and no filter, the rays round the pixel
//	as they are. Up to where the AO fades out (SAOFrame::fFadeEnd); beyond
//	it all is open.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "RayTracing.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! CSTrace's rays: the visibility (-1: no surface), the view depth of the pixel traced.
constexpr DXGI_FORMAT RAYS_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//! The history: the visibility, the frames in it, the view depth.
constexpr DXGI_FORMAT HISTORY_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//! The visibility (AmbientOcclusion.h: R8_TYPELESS) as CSFilter writes it.
constexpr DXGI_FORMAT VISIBILITY_FORMAT = DXGI_FORMAT_R8_UNORM;
//! The frames a history holds at most (what moves leaves its occlusion behind for about as long).
constexpr float HISTORY_FRAMES = 16;
constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
//! The kernels' UAV tables (D3D12Compute.h): the rays', the history's of each texture, the visibility's.
enum { TABLE_TRACE, TABLE_TEMPORAL0, TABLE_TEMPORAL1, TABLE_FILTER, TABLE_COUNT };

D3D12_SHADER_RESOURCE_VIEW_DESC TextureSRV(DXGI_FORMAT Format)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Format = Format;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture2D.MipLevels = 1;
  return SRV;
}

void Barrier(ID3D12GraphicsCommandList *pList, ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = pRes;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = Before;
  b.Transition.StateAfter = After;
  pList->ResourceBarrier(1, &b);
}

class CAORays : public IAmbientOcclusion
{
public:
  ~CAORays() override { Release(); }
  bool Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug) override;
  bool Dispatch(const SAOFrame &Frame) override;
  const char *GetName() const override { return "rays"; }

private:
  void Release();

  SAOTargets m_Targets;
  CD3D12Texture *m_pRays = nullptr;       //!< CSTrace's rays (UNORDERED_ACCESS between the dispatches)
  CD3D12Texture *m_pHistory[2] = {};      //!< this frame's history and the frame before's (the same)
  int m_nHistory = 0;                     //!< m_pHistory's current one
  int m_nDepthSRV = -1;                   //!< the depth buffer in the texture table
  int m_nTables[TABLE_COUNT] = { -1, -1, -1, -1 };
  float m_PrevViewProj[16] = {};          //!< the view's world to clip of the last dispatch
  float m_PrevJitter[2] = {};             //!< and its jitter
  int m_nFrame = -1;                      //!< m_nFrames of the last dispatch with a history (-1: none)
};

void CAORays::Release()
{
  CD3D12Descriptors *pDesc = g_pRenderer ? g_pRenderer->m_pDescriptors.get() : nullptr;
  CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
  // (with their views in the texture table)
  for (CD3D12Texture **pp : { &m_pRays, &m_pHistory[0], &m_pHistory[1] })
  {
    if (*pp && pUpload)
      pUpload->ReleaseTexture(*pp);
    *pp = nullptr;
  }
  if (pDesc)
  {
    for (int &nTable : m_nTables)
      if (nTable >= 0)
        pDesc->FreeRange(nTable, CD3D12Compute::COMPUTE_UAVS);
    if (m_nDepthSRV >= 0)
      pDesc->FreeSRV(m_nDepthSRV);
  }
  std::fill(std::begin(m_nTables), std::end(m_nTables), -1);
  m_nDepthSRV = -1;
  m_nFrame = -1;
}

bool CAORays::Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug)
{
  Release();
  CD3D12Upload *pUpload = g_pRenderer->m_pUpload.get();
  CD3D12Descriptors *pDesc = g_pRenderer->m_pDescriptors.get();
  const int w = Targets.nWidth, h = Targets.nHeight;
  if (!pDevice || !pUpload || !pDesc || !g_pRenderer->m_pRayScene || !g_pRenderer->m_pRayScene->IsSupported() || !Targets.pDepth ||
      !Targets.pOutput || w <= 0 || h <= 0)
    return false;
  m_Targets = Targets;
  m_pRays = pUpload->CreateComputeTarget(w, h, 1, RAYS_FORMAT, L"XRenderD3D12 AO rays");
  m_pHistory[0] = pUpload->CreateComputeTarget(w, h, 1, HISTORY_FORMAT, L"XRenderD3D12 AO history");
  m_pHistory[1] = pUpload->CreateComputeTarget(w, h, 1, HISTORY_FORMAT, L"XRenderD3D12 AO history");
  if (!m_pRays || !m_pHistory[0] || !m_pHistory[1])
  {
    Release();
    return false;
  }
  // The views in the texture table, the kernels' UAV tables (the slots they do not use null).
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = TextureSRV(RAYS_FORMAT);
  m_pRays->m_nSRV = pDesc->AllocSRV(m_pRays->m_pResource, &SRV);
  SRV = TextureSRV(HISTORY_FORMAT);
  for (CD3D12Texture *pTex : m_pHistory)
    pTex->m_nSRV = pDesc->AllocSRV(pTex->m_pResource, &SRV);
  SRV = TextureSRV(Targets.DepthFormat);
  m_nDepthSRV = pDesc->AllocSRV(Targets.pDepth, &SRV);
  ID3D12Resource *const Outputs[TABLE_COUNT] = { m_pRays->m_pResource, m_pHistory[0]->m_pResource, m_pHistory[1]->m_pResource, nullptr };
  for (int t = 0; t < TABLE_COUNT; t++)
  {
    if ((m_nTables[t] = pDesc->AllocRange(CD3D12Compute::COMPUTE_UAVS)) < 0)
      break;
    D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
    View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      // u0 the rays or the history (RGBA16F), u1 the visibility.
      View.Format = i == 1 ? VISIBILITY_FORMAT : RAYS_FORMAT;
      ID3D12Resource *pUAV = i == 0 ? Outputs[t] : i == 1 && t == TABLE_FILTER ? Targets.pOutput : nullptr;
      pDesc->WriteUAV(m_nTables[t] + i, pUAV, &View);
    }
  }
  if (m_pRays->m_nSRV < 0 || m_pHistory[0]->m_nSRV < 0 || m_pHistory[1]->m_nSRV < 0 || m_nDepthSRV < 0 ||
      std::any_of(std::begin(m_nTables), std::end(m_nTables), [](int n) { return n < 0; }))
  {
    Release();
    return false;
  }
  return true;
}

bool CAORays::Dispatch(const SAOFrame &Frame)
{
  CD3D12Renderer *r = g_pRenderer;
  ID3D12GraphicsCommandList *pList = Frame.pCmdList;
  const int w = m_Targets.nWidth, h = m_Targets.nHeight;
  // This frame's scene for rays (its builds done before the first pass that traces).
  if (m_nDepthSRV < 0 || !pList || !r->m_pCompute || !r->m_pRayScene || r->m_pRayScene->GetBuiltFrame() != r->m_nFrames ||
      !r->m_pRayScene->GetTLAS())
    return false;
  r->GpuSection("rt ao");
  const D3D12_GPU_VIRTUAL_ADDRESS nTlas = r->UseRayScene();
  if (!nTlas)
    return false;
  SFrameConstants fc;
  r->MakeFrameConstants(fc, r->m_pDevice->IsSceneBound());
  const SD3D12FrameAlloc FrameCB = r->m_pUpload->AllocFrame(sizeof(fc), 256);
  memcpy(FrameCB.pCPU, &fc, sizeof(fc));

  SRayAOConstants rc = {};
  memcpy(rc.mPrevViewProj, m_PrevViewProj, sizeof(rc.mPrevViewProj));
  r->GetRayView(rc.vCameraPos, rc.vRayX, rc.vRayY, rc.vRayO);
  Set4(rc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
  Set4(rc.vJitter, fc.vJitter[0], fc.vJitter[1], m_PrevJitter[0], m_PrevJitter[1]);
  Set4(rc.vParams, std::max(Frame.fRadius, 0.01f), std::max(Frame.fFadeEnd, 1.0f), 2 / (fabsf(Frame.Proj[5]) * h), HISTORY_FRAMES);
  const bool bDenoise = Frame.nDenoise > 0;
  const bool bHistory = bDenoise && m_nFrame == r->m_nFrames - 1;
  const int nCur = m_nHistory ^= 1;
  CD3D12Texture *pHistory = m_pHistory[nCur], *pPrevHistory = m_pHistory[nCur ^ 1];
  rc.nTextures[0] = (uint32)m_nDepthSRV;
  rc.nTextures[1] = (uint32)m_pRays->m_nSRV;
  rc.nTextures[2] = (uint32)pPrevHistory->m_nSRV;
  rc.nTextures[3] = (uint32)pHistory->m_nSRV;
  rc.nFlags[0] = bHistory ? 1 : 0;
  rc.nFlags[1] = (uint32)r->m_nFrames;
  rc.nFlags[2] = bDenoise ? 1 : 0;
  rc.nFlags[3] = r->RayMask() & 0xff;
  // A ray for each 2 x 2 pixels, from each of them in turn over 4 frames (the history has them all), or for each pixel.
  const int nStep = Frame.nQuality >= 3 ? 1 : 2;
  static constexpr uint32 CellPixels[4][2] = { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };
  rc.nTrace[0] = (uint32)nStep;
  rc.nTrace[1] = nStep == 2 ? CellPixels[r->m_nFrames & 3][0] : 0;
  rc.nTrace[2] = nStep == 2 ? CellPixels[r->m_nFrames & 3][1] : 0;
  const SD3D12FrameAlloc PassCB = r->m_pUpload->AllocFrame(sizeof(rc), 256);
  memcpy(PassCB.pCPU, &rc, sizeof(rc));

  // The depth buffer is readable here (DEPTH_READ | NON_PIXEL_SHADER_RESOURCE), the output a UAV; the
  // rays and the histories are UAVs between the dispatches.
  CD3D12Compute *pCompute = r->m_pCompute.get();
  const UINT nGroupsX = (UINT)(w + XR_RAY_AO_GROUP - 1) / XR_RAY_AO_GROUP, nGroupsY = (UINT)(h + XR_RAY_AO_GROUP - 1) / XR_RAY_AO_GROUP;
  const int nCellsX = (w + nStep - 1) / nStep, nCellsY = (h + nStep - 1) / nStep;
  pCompute->DispatchRays(pList, eD3D12K_RayAOTrace, FrameCB.nGPU, PassCB.nGPU, nTlas, r->m_pRayScene->GetInstances(), m_nTables[TABLE_TRACE],
                         (UINT)(nCellsX + XR_RAY_AO_GROUP - 1) / XR_RAY_AO_GROUP, (UINT)(nCellsY + XR_RAY_AO_GROUP - 1) / XR_RAY_AO_GROUP);
  Barrier(pList, m_pRays->m_pResource, UAV, READ);
  Barrier(pList, pPrevHistory->m_pResource, UAV, READ);
  r->GpuSection("rt ao history");
  pCompute->DispatchRays(pList, eD3D12K_RayAOTemporal, FrameCB.nGPU, PassCB.nGPU, 0, 0, m_nTables[TABLE_TEMPORAL0 + nCur], nGroupsX, nGroupsY);
  Barrier(pList, pHistory->m_pResource, UAV, READ);
  r->GpuSection("rt ao filter");
  pCompute->DispatchRays(pList, eD3D12K_RayAOFilter, FrameCB.nGPU, PassCB.nGPU, 0, 0, m_nTables[TABLE_FILTER], nGroupsX, nGroupsY);
  Barrier(pList, m_pRays->m_pResource, READ, UAV);
  Barrier(pList, pPrevHistory->m_pResource, READ, UAV);
  Barrier(pList, pHistory->m_pResource, READ, UAV);
  // The resolve after it is the AO's.
  r->GpuSection("ao");

  // The camera of this frame for the next one's history.
  const Matrix44 ViewProj = r->m_CameraView * r->m_ProjMatrix;
  memcpy(m_PrevViewProj, ViewProj.data, sizeof(m_PrevViewProj));
  m_PrevJitter[0] = fc.vJitter[0];
  m_PrevJitter[1] = fc.vJitter[1];
  m_nFrame = bDenoise ? r->m_nFrames : -1;
  if (r->m_pFrameDump)
    fprintf(r->m_pFrameDump.get(), "rt ao: %d x %d, a ray for each %d x %d pixels (%d %d of them), %g m, to %.1f m, denoise %d, history %d\n", w, h,
            nStep, nStep, (int)rc.nTrace[1], (int)rc.nTrace[2], rc.vParams[0], rc.vParams[1], bDenoise ? 1 : 0, bHistory ? 1 : 0);
  return true;
}

}

std::unique_ptr<IAmbientOcclusion> CreateAORays()
{
  return std::make_unique<CAORays>();
}
