// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AmbientOcclusion.cpp
//	Description: The ambient occlusion of the main view (d3d12_AO,
//	AmbientOcclusion.h). After the main view's opaque list (CScene::
//	EndEf3D), not in its reflection, sprites, shadow maps, cubes or heat
//	vision:
//	  1. the depth buffer goes to DEPTH_READ | NON_PIXEL_SHADER_RESOURCE
//	  2. the backend writes the visibility (m_pAOOutput)
//	  3. AoResolve.hlsl makes it the factor (m_pAOFactor): the visibility
//	     to the power d3d12_AOStrength / 2.2, faded out with the fog, 1 on
//	     the sky
//	  4. the scene drawn so far is multiplied by it (POST_BLEND, DESTCOLOR
//	     x 0, the colour only: the alpha the screen effects read stays)
//	The first person objects, the water, the particles and the rest of the
//	blended list are drawn after it and get none; the water's refraction
//	copies the scene after it, so it shows the occlusion under the surface.
//	With an upscaler it works at the render size on the scene's depth, and
//	the noise moves every frame for the upscaler to gather.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include "D3D12Compute.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;  //!< the resolve's tables: its SRVs, then its UAVs
constexpr D3D12_RESOURCE_STATES DEPTH_READ = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

//! The backend of a d3d12_AO value; nullptr for "none" and unknown names.
std::unique_ptr<IAmbientOcclusion> CreateAO(const char *szName)
{
  if (!_stricmp(szName, "gtao") || !_stricmp(szName, "xegtao"))
    return CreateAOXeGtao();
  if (!_stricmp(szName, "cacao"))
    return CreateAOCacao();
  return nullptr;
}

}

bool CD3D12Renderer::UpdateAO()
{
  if (!m_pDevice || !m_pDescriptors || !m_pUpload || !m_pCompute)
    return false;
  if (!m_pAOVar)
    m_pAOVar = iConsole->GetCVar("d3d12_AO");
  const char *szName = m_pAOVar && m_pAOVar->GetString() ? m_pAOVar->GetString() : "none";
  ID3D12Resource *pDepth = m_pDevice->GetMainDepth();
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  const bool bNew = _stricmp(szName, m_AOName.c_str()) != 0;
  if (!bNew && pDepth == m_pAODepth.Get() && w == m_nAOWidth && h == m_nAOHeight)
    return m_pAO != nullptr;

  // Made again for the new name, depth buffer or size; kept as it is when that fails, until one of
  // them changes.
  ReleaseAO();
  m_AOName = szName;
  m_pAODepth = pDepth;
  m_nAOWidth = w;
  m_nAOHeight = h;
  if (!_stricmp(szName, "none"))
    return false;
  m_pAO = CreateAO(szName);
  if (!m_pAO)
  {
    if (bNew)
      iLog->LogWarning("XRenderD3D12: d3d12_AO %s: no such ambient occlusion (none, gtao, cacao); none", szName);
    return false;
  }
  m_pAOOutput = m_pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8_TYPELESS, L"AO visibility");
  m_pAOFactor = m_pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8G8B8A8_UNORM, L"AO factor");
  if (m_pAOFactor)
    m_pAOFactor->m_nSRV = m_pDescriptors->AllocSRV(m_pAOFactor->m_pResource, nullptr);
  m_nAOResolveTable = m_pDescriptors->AllocRange(TABLE);
  SAOTargets Targets;
  Targets.pDepth = pDepth;
  Targets.DepthFormat = m_pDevice->GetMainDepthSRVFormat();
  Targets.pOutput = m_pAOOutput ? m_pAOOutput->m_pResource : nullptr;
  Targets.nWidth = w;
  Targets.nHeight = h;
  if (!pDepth || !m_pAOOutput || !m_pAOFactor || m_pAOFactor->m_nSRV < 0 || m_nAOResolveTable < 0 ||
      !m_pAO->Create(m_pDevice->GetDevice(), Targets, CV_d3d12_debug != 0))
  {
    if (bNew)
      iLog->LogWarning("XRenderD3D12: d3d12_AO %s is not available at %d x %d; none", szName, w, h);
    ReleaseAO(false);
    return false;
  }

  // The resolve's tables: t0 the visibility, t1 the depth buffer; u0 the factor.
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture2D.MipLevels = 1;
  SRV.Format = DXGI_FORMAT_R8_UNORM;
  m_pDescriptors->WriteSRV(m_nAOResolveTable, m_pAOOutput->m_pResource, &SRV);
  SRV.Format = Targets.DepthFormat;
  m_pDescriptors->WriteSRV(m_nAOResolveTable + 1, pDepth, &SRV);
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  UAV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    m_pDescriptors->WriteUAV(m_nAOResolveTable + CD3D12Compute::COMPUTE_SRVS + i, i == 0 ? m_pAOFactor->m_pResource : nullptr, &UAV);
  iLog->Log("XRenderD3D12: ambient occlusion %s, %d x %d", m_pAO->GetName(), w, h);
  return true;
}

void CD3D12Renderer::ReleaseAO(bool bForget)
{
  // A backend's own objects may be in use by the frames in flight.
  if (m_pAO && m_pDevice)
    m_pDevice->WaitForGPU();
  m_pAO.reset();
  if (m_pUpload)
  {
    if (m_pAOOutput)
      m_pUpload->ReleaseTexture(m_pAOOutput);
    if (m_pAOFactor)
      m_pUpload->ReleaseTexture(m_pAOFactor);
  }
  m_pAOOutput = m_pAOFactor = nullptr;
  if (m_nAOResolveTable >= 0 && m_pDescriptors)
    m_pDescriptors->FreeRange(m_nAOResolveTable, TABLE);
  m_nAOResolveTable = -1;
  m_nAODrawnFrame = -1;
  if (bForget)
  {
    m_AOName.clear();
    m_pAODepth.Reset();
    m_nAOWidth = m_nAOHeight = 0;
  }
}

void CD3D12Renderer::DrawAmbientOcclusion()
{
  if (!(m_nSceneFlags & SHDF_ALLOWHDR) || m_nRecurseLevel > 1 || m_bReflectionPass || m_bSpritePass || m_bShadowMapPass || m_bEnvCubePass ||
      m_bCascadePass || m_bHeatVision || !m_pDevice || !EnsureFrame() || m_pDevice->IsOffscreen() || !UpdateAO())
    return;
  GpuSection("ao");
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  ID3D12Resource *pDepth = m_pAODepth.Get();
  const int w = m_nAOWidth, h = m_nAOHeight;
  m_pDevice->Transition(pDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, DEPTH_READ);

  SAOFrame Frame;
  Frame.pCmdList = pList;
  memcpy(Frame.Proj, m_ProjMatrix.data, sizeof(Frame.Proj));
  Frame.fRadius = std::max(CV_d3d12_AORadius, 0.01f);
  Frame.nQuality = std::clamp(CV_d3d12_AOQuality, 0, 3);
  Frame.nDenoise = CV_d3d12_AODenoise;
  Frame.nNoiseIndex = m_bUpscaling ? m_nAOFrames % 64 : 0;
  // Beyond the fog's end nothing is left to see of it; without fog, the far half of the view.
  const float *p = m_ProjMatrix.data;
  const float fFar = p[10] != -1 ? p[14] / (p[10] + 1) : 1000.0f;
  Frame.fFadeStart = m_bFog && m_fFogEnd > m_fFogStart ? m_fFogEnd : fFar * 0.5f;
  Frame.fFadeEnd = std::max(m_bFog && m_fFogEnd > m_fFogStart ? m_fFogEnd * 1.1f : fFar, Frame.fFadeStart + 1);
  m_nAOFrames++;
  const bool bOk = m_pAO->Dispatch(Frame);
  // A backend may have bound a heap of its own.
  m_pDevice->BindRootSignature();
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "ao: %s, %d x %d, quality %d, radius %g, strength %g, denoise %d, noise %d%s\n", m_pAO->GetName(), w, h,
            Frame.nQuality, Frame.fRadius, CV_d3d12_AOStrength, Frame.nDenoise, Frame.nNoiseIndex, bOk ? "" : ", failed");
  if (bOk)
  {
    m_pDevice->Transition(m_pAOOutput->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    SAoResolveConstants rc = {};
    Set4(rc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
    Set4(rc.vDepthParams, p[10], p[14], 0, 0);
    const float fRange = m_fFogEnd - m_fFogStart;
    Set4(rc.vFogParams, m_fFogStart, m_fFogEnd, fRange > 0 ? 1.0f / fRange : 0.0f, m_bFog ? 1.0f : 0.0f);
    Set4(rc.vParams, std::max(CV_d3d12_AOStrength, 0.0f) / 2.2f, 0, 0, 0);
    const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(rc), 256);
    memcpy(CB.pCPU, &rc, sizeof(rc));
    m_pCompute->Dispatch(pList, eD3D12K_AoResolve, CB.nGPU, m_nAOResolveTable, m_nAOResolveTable + CD3D12Compute::COMPUTE_SRVS,
                         (UINT)(w + 7) / 8, (UINT)(h + 7) / 8);
    m_pDevice->Transition(m_pAOOutput->m_pResource, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  m_pDevice->Transition(pDepth, DEPTH_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  InvalidateFrameConstants();
  if (!bOk)
    return;

  // The scene times the factor, over the whole target (the engine's viewport may be smaller).
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = m_pDevice->GetWidth();
  m_Viewport[3] = m_pDevice->GetHeight();
  m_bScissor = m_bItemScissor = false;
  m_pDevice->Transition(m_pAOFactor->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST | GS_BLSRC_DSTCOL | GS_BLDST_ZERO | GS_COLMASKONLYRGB;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_BLEND;
  dc.nTex0 = (uint32)m_pAOFactor->m_nSRV;
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, 0, 0);
  Set4(dc.vParams1, 1, 1, 1, 1);
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  m_pDevice->Transition(m_pAOFactor->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
  m_nAODrawnFrame = m_nFrames;
}

void CD3D12Renderer::DrawAODebug()
{
  if (!CV_d3d12_AODebug || m_nAODrawnFrame != m_nFrames || !m_pAOFactor || m_pAOFactor->m_nSRV < 0 || !m_pDevice || m_pDevice->IsOffscreen())
    return;
  // The factor stretched over the whole back buffer.
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = w;
  m_Viewport[3] = h;
  m_bScissor = m_bItemScissor = false;
  m_pDevice->Transition(m_pAOFactor->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_BLEND;
  dc.nTex0 = (uint32)m_pAOFactor->m_nSRV;
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, 0, 0);
  Set4(dc.vParams1, 1, 1, 1, 1);
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  m_pDevice->Transition(m_pAOFactor->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
}
