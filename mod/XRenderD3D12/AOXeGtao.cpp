// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AOXeGtao.cpp
//	Description: Intel XeGTAO (third_party/xegtao) as a backend of the
//	ambient occlusion (AmbientOcclusion.h), on the renderer's compute
//	passes (D3D12Compute.h, Shaders/XeGtao.hlsl):
//	  1. the view space depth and its MIPs from the depth buffer
//	  2. the view space normals from the depth buffer
//	  3. the main pass at the quality's slices and steps: the occlusion
//	     term and the edges between surfaces
//	  4. the denoiser, d3d12_AODenoise passes from one term to the other,
//	     the last one into the output. With none XeGTAO still runs the last
//	     one, its blur turned off (DenoiseBlurBeta), to unpack the term.
//	The constants come from XeGTAO's own GTAOUpdateConstants, its tuning
//	as XeGTAO ships it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Compute.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"

#include <cassert>
#include "XeGTAO.h"

namespace
{

constexpr int SRVS = CD3D12Compute::COMPUTE_SRVS;
constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;  //!< slots of a binding: its SRVs, then its UAVs

//! The bindings of the passes, a run of TABLE slots each.
enum EBinding
{
  eB_Prefilter = 0,   //!< t0 the depth buffer; u0..u4 the view depth's MIPs
  eB_Normals,         //!< t0 the depth buffer; u0 the normals
  eB_Main,            //!< t0 the view depth, t1 the normals; u0 term 0, u1 the edges
  eB_Denoise01,       //!< t0 term 0, t1 the edges; u0 term 1
  eB_Denoise10,       //!< t0 term 1, t1 the edges; u0 term 0
  eB_Last0,           //!< t0 term 0, t1 the edges; u0 the output
  eB_Last1,           //!< t0 term 1, t1 the edges; u0 the output
  eB_Num
};

const ED3D12Kernel s_MainKernels[4] = { eD3D12K_GtaoLow, eD3D12K_GtaoMedium, eD3D12K_GtaoHigh, eD3D12K_GtaoUltra };

D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc(DXGI_FORMAT Format, UINT nMips)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
  Desc.Format = Format;
  Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  Desc.Texture2D.MipLevels = nMips;
  return Desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc(DXGI_FORMAT Format, UINT nMip)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC Desc = {};
  Desc.Format = Format;
  Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  Desc.Texture2D.MipSlice = nMip;
  return Desc;
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

constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

class CAOXeGtao : public IAmbientOcclusion
{
public:
  ~CAOXeGtao() override { Release(); }
  bool Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug) override;
  bool Dispatch(const SAOFrame &Frame) override;
  const char *GetName() const override { return "Intel XeGTAO 1.30"; }

private:
  void Release();
  //! The first slot of a binding's SRV table, and of its UAV table.
  int SRVTable(EBinding e) const { return m_nTable + e * TABLE; }
  int UAVTable(EBinding e) const { return m_nTable + e * TABLE + SRVS; }
  //! The two terms the denoiser reads and writes, bound in the binding's tables.
  void WriteDenoise(EBinding e, int nIn, ID3D12Resource *pOut, DXGI_FORMAT OutFormat);

  SAOTargets m_Targets;
  CD3D12Texture *m_pViewDepth = nullptr;  //!< R32_FLOAT, XE_GTAO_DEPTH_MIP_LEVELS MIPs
  CD3D12Texture *m_pNormals = nullptr;    //!< R32_UINT, R11G11B10 packed
  CD3D12Texture *m_pTerm[2] = {};         //!< R8_UINT
  CD3D12Texture *m_pEdges = nullptr;      //!< R8_UNORM
  int m_nTable = -1;                      //!< eB_Num bindings (CD3D12Descriptors::AllocRange)
};

void CAOXeGtao::Release()
{
  CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
  for (CD3D12Texture **pp : { &m_pViewDepth, &m_pNormals, &m_pTerm[0], &m_pTerm[1], &m_pEdges })
  {
    if (*pp && pUpload)
      pUpload->ReleaseTexture(*pp);
    *pp = nullptr;
  }
  if (m_nTable >= 0 && g_pRenderer && g_pRenderer->m_pDescriptors)
    g_pRenderer->m_pDescriptors->FreeRange(m_nTable, eB_Num * TABLE);
  m_nTable = -1;
}

void CAOXeGtao::WriteDenoise(EBinding e, int nIn, ID3D12Resource *pOut, DXGI_FORMAT OutFormat)
{
  CD3D12Descriptors *pDesc = g_pRenderer->m_pDescriptors.get();
  const D3D12_SHADER_RESOURCE_VIEW_DESC Term = SRVDesc(DXGI_FORMAT_R8_UINT, 1), Edges = SRVDesc(DXGI_FORMAT_R8_UNORM, 1);
  pDesc->WriteSRV(SRVTable(e), m_pTerm[nIn]->m_pResource, &Term);
  pDesc->WriteSRV(SRVTable(e) + 1, m_pEdges->m_pResource, &Edges);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC Out = UAVDesc(OutFormat, 0);
  pDesc->WriteUAV(UAVTable(e), pOut, &Out);
}

bool CAOXeGtao::Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug)
{
  Release();
  CD3D12Upload *pUpload = g_pRenderer->m_pUpload.get();
  CD3D12Descriptors *pDesc = g_pRenderer->m_pDescriptors.get();
  const int w = Targets.nWidth, h = Targets.nHeight;
  if (!pDevice || !pUpload || !pDesc || !Targets.pDepth || !Targets.pOutput || w <= 0 || h <= 0)
    return false;
  m_Targets = Targets;
  m_pViewDepth = pUpload->CreateComputeTarget(w, h, XE_GTAO_DEPTH_MIP_LEVELS, DXGI_FORMAT_R32_FLOAT, L"XeGTAO view depth");
  m_pNormals = pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R32_UINT, L"XeGTAO normals");
  m_pTerm[0] = pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8_UINT, L"XeGTAO term 0");
  m_pTerm[1] = pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8_UINT, L"XeGTAO term 1");
  m_pEdges = pUpload->CreateComputeTarget(w, h, 1, DXGI_FORMAT_R8_UNORM, L"XeGTAO edges");
  m_nTable = pDesc->AllocRange(eB_Num * TABLE);
  if (!m_pViewDepth || !m_pNormals || !m_pTerm[0] || !m_pTerm[1] || !m_pEdges || m_nTable < 0)
  {
    Release();
    return false;
  }

  // Null UAVs where a binding has none (the SRV slots are null SRVs already).
  const D3D12_UNORDERED_ACCESS_VIEW_DESC NullUAV = UAVDesc(DXGI_FORMAT_R32_FLOAT, 0);
  for (int e = 0; e < eB_Num; e++)
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
      pDesc->WriteUAV(UAVTable((EBinding)e) + i, nullptr, &NullUAV);

  const D3D12_SHADER_RESOURCE_VIEW_DESC Depth = SRVDesc(Targets.DepthFormat, 1);
  pDesc->WriteSRV(SRVTable(eB_Prefilter), Targets.pDepth, &Depth);
  for (int i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; i++)
  {
    const D3D12_UNORDERED_ACCESS_VIEW_DESC Mip = UAVDesc(DXGI_FORMAT_R32_FLOAT, (UINT)i);
    pDesc->WriteUAV(UAVTable(eB_Prefilter) + i, m_pViewDepth->m_pResource, &Mip);
  }

  pDesc->WriteSRV(SRVTable(eB_Normals), Targets.pDepth, &Depth);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC Normals = UAVDesc(DXGI_FORMAT_R32_UINT, 0);
  pDesc->WriteUAV(UAVTable(eB_Normals), m_pNormals->m_pResource, &Normals);

  const D3D12_SHADER_RESOURCE_VIEW_DESC ViewDepth = SRVDesc(DXGI_FORMAT_R32_FLOAT, XE_GTAO_DEPTH_MIP_LEVELS);
  const D3D12_SHADER_RESOURCE_VIEW_DESC NormalsSRV = SRVDesc(DXGI_FORMAT_R32_UINT, 1);
  pDesc->WriteSRV(SRVTable(eB_Main), m_pViewDepth->m_pResource, &ViewDepth);
  pDesc->WriteSRV(SRVTable(eB_Main) + 1, m_pNormals->m_pResource, &NormalsSRV);
  const D3D12_UNORDERED_ACCESS_VIEW_DESC Term = UAVDesc(DXGI_FORMAT_R8_UINT, 0), Edges = UAVDesc(DXGI_FORMAT_R8_UNORM, 0);
  pDesc->WriteUAV(UAVTable(eB_Main), m_pTerm[0]->m_pResource, &Term);
  pDesc->WriteUAV(UAVTable(eB_Main) + 1, m_pEdges->m_pResource, &Edges);

  WriteDenoise(eB_Denoise01, 0, m_pTerm[1]->m_pResource, DXGI_FORMAT_R8_UINT);
  WriteDenoise(eB_Denoise10, 1, m_pTerm[0]->m_pResource, DXGI_FORMAT_R8_UINT);
  WriteDenoise(eB_Last0, 0, Targets.pOutput, DXGI_FORMAT_R8_UINT);
  WriteDenoise(eB_Last1, 1, Targets.pOutput, DXGI_FORMAT_R8_UINT);
  return true;
}

bool CAOXeGtao::Dispatch(const SAOFrame &Frame)
{
  CD3D12Compute *pCompute = g_pRenderer->m_pCompute.get();
  ID3D12GraphicsCommandList *pList = Frame.pCmdList;
  if (m_nTable < 0 || !pCompute || !pList)
    return false;
  const int w = m_Targets.nWidth, h = m_Targets.nHeight;
  XeGTAO::GTAOSettings Settings;
  Settings.QualityLevel = std::clamp(Frame.nQuality, 0, 3);
  Settings.DenoisePasses = std::clamp(Frame.nDenoise, 0, 3);
  Settings.Radius = std::max(Frame.fRadius, 0.01f);
  XeGTAO::GTAOConstants Consts = {};
  XeGTAO::GTAOUpdateConstants(Consts, w, h, Settings, Frame.Proj, true, 0);
  // The noise moves only while an upscaler gathers the frames (XeGTAO's own: with any denoising).
  Consts.NoiseIndex = Frame.nNoiseIndex % 64;
  const SD3D12FrameAlloc CB = g_pRenderer->m_pUpload->AllocFrame(sizeof(Consts), 256);
  memcpy(CB.pCPU, &Consts, sizeof(Consts));

  const UINT gx = (UINT)(w + 7) / 8, gy = (UINT)(h + 7) / 8;
  pCompute->Dispatch(pList, eD3D12K_GtaoPrefilterDepths, CB.nGPU, SRVTable(eB_Prefilter), UAVTable(eB_Prefilter), (UINT)(w + 15) / 16,
                     (UINT)(h + 15) / 16);
  pCompute->Dispatch(pList, eD3D12K_GtaoNormals, CB.nGPU, SRVTable(eB_Normals), UAVTable(eB_Normals), gx, gy);
  Barrier(pList, m_pViewDepth->m_pResource, UAV, READ);
  Barrier(pList, m_pNormals->m_pResource, UAV, READ);
  pCompute->Dispatch(pList, s_MainKernels[Settings.QualityLevel], CB.nGPU, SRVTable(eB_Main), UAVTable(eB_Main), gx, gy);
  Barrier(pList, m_pTerm[0]->m_pResource, UAV, READ);
  Barrier(pList, m_pEdges->m_pResource, UAV, READ);

  // Two pixels across per thread.
  const int nPasses = std::max(Settings.DenoisePasses, 1);
  int nIn = 0;
  for (int i = 0; i < nPasses; i++)
  {
    const bool bLast = i == nPasses - 1;
    const EBinding e = bLast ? (nIn ? eB_Last1 : eB_Last0) : (nIn ? eB_Denoise10 : eB_Denoise01);
    pCompute->Dispatch(pList, bLast ? eD3D12K_GtaoDenoiseLast : eD3D12K_GtaoDenoise, CB.nGPU, SRVTable(e), UAVTable(e), (UINT)(w + 15) / 16, gy);
    if (!bLast)
    {
      Barrier(pList, m_pTerm[1 - nIn]->m_pResource, UAV, READ);
      Barrier(pList, m_pTerm[nIn]->m_pResource, READ, UAV);
      nIn = 1 - nIn;
    }
  }
  Barrier(pList, m_pViewDepth->m_pResource, READ, UAV);
  Barrier(pList, m_pNormals->m_pResource, READ, UAV);
  Barrier(pList, m_pTerm[nIn]->m_pResource, READ, UAV);
  Barrier(pList, m_pEdges->m_pResource, READ, UAV);
  return true;
}

}

std::unique_ptr<IAmbientOcclusion> CreateAOXeGtao()
{
  return std::make_unique<CAOXeGtao>();
}
