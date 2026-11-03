// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PostAA.cpp
//	Description: The anti-aliasing of the main view without an upscaler
//	(d3d12_AA; with one, FSR, the upscaler anti-aliases). After the main
//	view's EndMainScene the back buffer holds the finished picture in every
//	path (the scene drawn into it, the HDR scene's resolve, the path
//	tracer's scene targets): DrawPostAA copies it and draws the program
//	eD3D12P_AntiAlias (Shaders/AntiAlias.hlsl) from the copy into it, before
//	the render mode, the screen effects and the 2D, which stay sharp.
//	  - fxaa: FXAA (Shaders/Fxaa.hlsli) at d3d12_AAQuality, one pass; the
//	    pixels it leaves as they are are not written.
//	  - smaa: SMAA 1x (third_party/smaa) at d3d12_AAQuality's preset: its
//	    edges and blending weights by two compute kernels
//	    (Shaders/AntiAliasSmaa.hlsl, Smaa.hlsli) into targets of the
//	    display's size, then its neighbourhood blending (SmaaBlend.hlsli)
//	    as the pass; the pixels without weights are not written.
//	The back buffer's alpha stays (the HUD masks through it). With the
//	HDR output's highlight layer the pass writes it too, from its copy:
//	the ratio of each pixel blended as its colour is, the fingerprint of the
//	new colour. d3d12_AADebug 1 shows the anti-aliasing's view of the edges.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"
#include "D3D12Compute.h"
#include "Scene.h"
// SMAA's precomputed textures (third_party/smaa).
#include "Textures/AreaTex.h"
#include "Textures/SearchTex.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

//! The values of d3d12_AA and their names for the statistics.
struct SAntiAliasName
{
  const char *szValue;
  CD3D12Renderer::EAntiAlias eAA;
  const char *szName;
};
const SAntiAliasName s_AntiAliasNames[] = {
  { "none", CD3D12Renderer::EAntiAlias::None, "none" },
  { "fxaa", CD3D12Renderer::EAntiAlias::Fxaa, "FXAA" },
  { "smaa", CD3D12Renderer::EAntiAlias::Smaa, "SMAA 1x" },
};

const char *const s_QualityNames[4] = { "Low", "Medium", "High", "Ultra" };

const SAntiAliasName &AntiAliasName(CD3D12Renderer::EAntiAlias eAA)
{
  for (const SAntiAliasName &n : s_AntiAliasNames)
  {
    if (n.eAA == eAA)
      return n;
  }
  return s_AntiAliasNames[0];
}

}

CD3D12Renderer::EAntiAlias CD3D12Renderer::AntiAliasing()
{
  if (!m_pAAVar && iConsole)
    m_pAAVar = iConsole->GetCVar("d3d12_AA");
  const char *szValue = m_pAAVar && m_pAAVar->GetString() ? m_pAAVar->GetString() : "none";
  for (const SAntiAliasName &n : s_AntiAliasNames)
  {
    if (!_stricmp(szValue, n.szValue))
      return n.eAA;
  }
  if (_stricmp(szValue, m_AAWarned.c_str()) != 0)
  {
    iLog->LogWarning("XRenderD3D12: d3d12_AA %s: no such anti-aliasing (none, fxaa, smaa); none", szValue);
    m_AAWarned = szValue;
  }
  return EAntiAlias::None;
}

void CD3D12Renderer::DrawPostAA()
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  // The main view of the game only (not the scenes drawn into textures), as the screen effects.
  if (!m_pScene || !(m_nSceneFlags & SHDF_ALLOWHDR) || m_nRecurseLevel > 1 || m_bReflectionPass || m_bSpritePass ||
      m_bShadowMapPass || !EnsureFrame())
    return;
  const EAntiAlias eAA = AntiAliasing();
  if (eAA == EAntiAlias::None)
  {
    m_bAAUpscalerLogged = false;
    return;
  }
  // An upscaler that gathers the frames (FSR) anti-aliased this frame's scene.
  if (m_pUpscaler && m_pUpscaler->GathersFrames() && m_nUpscaledFrame == m_nFrames)
  {
    if (!m_bAAUpscalerLogged)
      iLog->Log("XRenderD3D12: d3d12_AA %s is not used: the upscaler %s anti-aliases", AntiAliasName(eAA).szValue, m_pUpscaler->GetName());
    m_bAAUpscalerLogged = true;
    return;
  }
  m_bAAUpscalerLogged = false;

  GpuSection("anti-aliasing");
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  // The picture alone (no depth): the copy is the pass's source while it draws into the back buffer.
  const int Rect[4] = { 0, 0, w, h };
  ID3D12Resource *pLayer = m_pDevice->HasLayer() ? m_pDevice->GetHighlight() : nullptr;
  if (!CopySceneForWater(Rect) || (pLayer && !EnsureLayerCopy(m_pAaLayer)))
    return;
  if (pLayer)
    CopyLayer(m_pAaLayer);
  const int nQuality = std::clamp(CV_d3d12_AAQuality, 0, 3);
  // SMAA's edges and weights from the copy (the back buffer's, CopySceneForWater).
  if (eAA == EAntiAlias::Smaa &&
      !DrawSmaaPasses(m_SceneCopies[m_pDevice->IsInScene() || m_pDevice->IsInHdr() ? 1 : 0].pColor.Get(), w, h, nQuality))
  {
    if (!m_bAAFailedLogged)
      iLog->LogWarning("XRenderD3D12: d3d12_AA %s is not available at %d x %d; none", AntiAliasName(eAA).szValue, w, h);
    m_bAAFailedLogged = true;
    return;
  }
  m_bAAFailedLogged = false;
  m_pDevice->SetBackBufferTargets();

  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_AntiAlias;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = (eAA == EAntiAlias::Smaa ? AA_SMAA : AA_FXAA) | (CV_d3d12_AADebug ? AA_DEBUG : 0);
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  if (eAA == EAntiAlias::Smaa)
  {
    dc.nTex1 = (uint32)m_pSmaaWeights->m_nSRV;
    dc.nTex2 = (uint32)m_pSmaaEdges->m_nSRV;
  }
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, (float)(w - 1), (float)(h - 1));
  Set4(dc.vParams1, (float)nQuality, 0, 0, 0);
  if (pLayer)
  {
    dc.nTex4 = (uint32)m_pAaLayer->m_nSRV;
    Key.nTarget = 4;
    m_pDevice->BindHighlight(true);
  }
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  if (pLayer)
    m_pDevice->BindHighlight(false);
  m_ePostAA = eAA;
  m_nPostAAFrame = m_nFrames;
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "anti-aliasing: %s, quality %d (%s)%s%s\n", AntiAliasName(eAA).szName, nQuality, s_QualityNames[nQuality],
            pLayer ? ", the highlight layer written" : "", CV_d3d12_AADebug ? ", debug view" : "");
}

std::string CD3D12Renderer::PostAAInfo()
{
  const EAntiAlias eAA = AntiAliasing();
  if (eAA == EAntiAlias::None)
    return "none";
  char szText[64];
  snprintf(szText, sizeof(szText), "%s (%s%s)", AntiAliasName(eAA).szName, s_QualityNames[std::clamp(CV_d3d12_AAQuality, 0, 3)],
           m_ePostAA == eAA && m_nPostAAFrame == m_nFrames ? "" : ", no 3D scene now");
  return szText;
}

bool CD3D12Renderer::EnsureLayerCopy(CD3D12Texture *&pCopy)
{
  ID3D12Resource *pLayer = m_pDevice && m_pUpload && m_pDevice->HasLayer() ? m_pDevice->GetHighlight() : nullptr;
  if (!pLayer)
    return false;
  const D3D12_RESOURCE_DESC Desc = pLayer->GetDesc();
  if (pCopy && (pCopy->m_nWidth != (int)Desc.Width || pCopy->m_nHeight != (int)Desc.Height))
  {
    m_pUpload->ReleaseTexture(pCopy);
    pCopy = nullptr;
  }
  if (!pCopy)
    pCopy = m_pUpload->CreateRenderTarget((int)Desc.Width, (int)Desc.Height, D3D12_HIGHLIGHT_FORMAT);
  return pCopy && pCopy->m_nSRV >= 0;
}

void CD3D12Renderer::CopyLayer(CD3D12Texture *pCopy)
{
  ID3D12Resource *pLayer = m_pDevice->GetHighlight();
  m_pDevice->Transition(pLayer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_pDevice->Transition(pCopy->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  m_pDevice->GetCmdList()->CopyResource(pCopy->m_pResource, pLayer);
  m_pDevice->Transition(pCopy->m_pResource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pLayer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

bool CD3D12Renderer::DrawSmaaPasses(ID3D12Resource *pCopy, int w, int h, int nQuality)
{
  constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
  const ED3D12Kernel eEdges = (ED3D12Kernel)(eD3D12K_SmaaEdgesLow + nQuality);
  const ED3D12Kernel eWeights = (ED3D12Kernel)(eD3D12K_SmaaWeightsLow + nQuality);
  if (!pCopy || !m_pCompute || !m_pUpload || !m_pDescriptors || !m_pCompute->HasKernel(eEdges) || !m_pCompute->HasKernel(eWeights))
    return false;
  // The precomputed textures once, in the formats their headers name; the targets of the display's size.
  if (!m_pSmaaArea)
  {
    const SD3D12SubData Area = { areaTexBytes, AREATEX_PITCH };
    m_pSmaaArea = m_pUpload->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 1, DXGI_FORMAT_R8G8_UNORM, false, &Area);
  }
  if (!m_pSmaaSearch)
  {
    const SD3D12SubData Search = { searchTexBytes, SEARCHTEX_PITCH };
    m_pSmaaSearch = m_pUpload->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, DXGI_FORMAT_R8_UNORM, false, &Search);
  }
  if (m_pSmaaEdges && (m_pSmaaEdges->m_nWidth != w || m_pSmaaEdges->m_nHeight != h))
    ReleaseSmaa(false);
  if (!m_pSmaaEdges)
    m_pSmaaEdges = m_pUpload->CreateRenderTarget(w, h, DXGI_FORMAT_R8G8_UNORM, true);
  if (!m_pSmaaWeights)
    m_pSmaaWeights = m_pUpload->CreateRenderTarget(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
  if (!m_pSmaaArea || !m_pSmaaSearch || !m_pSmaaEdges || m_pSmaaEdges->m_nSRV < 0 || !m_pSmaaWeights || m_pSmaaWeights->m_nSRV < 0)
    return false;
  ID3D12Resource *pEdges = m_pSmaaEdges->m_pResource, *pWeights = m_pSmaaWeights->m_pResource;
  ID3D12Resource *pArea = m_pSmaaArea->m_pResource, *pSearch = m_pSmaaSearch->m_pResource;

  // The tables, made again for another copy (the old slots freed after the frames that use them): the edges kernel's
  // t0 the copy, u0 the edges; the weights kernel's t0 the edges, t1 the area texture, t2 the search texture, u0 the
  // weights. The copy is held, so that a new one is never at its address.
  if (m_nSmaaTables < 0 || pCopy != m_pSmaaSource.Get())
  {
    if (m_nSmaaTables >= 0)
      m_pDescriptors->FreeRange(m_nSmaaTables, 2 * TABLE);
    m_pSmaaSource.Reset();
    m_nSmaaTables = m_pDescriptors->AllocRange(2 * TABLE);
    if (m_nSmaaTables < 0)
      return false;
    m_pSmaaSource = pCopy;
    ID3D12Resource *const SRVs[2][CD3D12Compute::COMPUTE_SRVS] = { { pCopy }, { pEdges, pArea, pSearch } };
    ID3D12Resource *const UAVs[2] = { pEdges, pWeights };
    for (int t = 0; t < 2; t++)
    {
      const int nTable = m_nSmaaTables + t * TABLE;
      for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
      {
        D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
        SRV.Format = SRVs[t][i] ? SRVs[t][i]->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
        SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SRV.Texture2D.MipLevels = 1;
        m_pDescriptors->WriteSRV(nTable + i, SRVs[t][i], &SRV);
      }
      for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
      {
        D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
        UAV.Format = i == 0 ? UAVs[t]->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
        UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, i == 0 ? UAVs[t] : nullptr, &UAV);
      }
    }
  }

  // AntiAliasSmaa.hlsl's b0: SMAA_RT_METRICS.
  const float vMetrics[4] = { 1.0f / w, 1.0f / h, (float)w, (float)h };
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(vMetrics), 256);
  memcpy(CB.pCPU, vMetrics, sizeof(vMetrics));
  constexpr D3D12_RESOURCE_STATES PS_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const UINT nGroupsX = (UINT)(w + 7) / 8, nGroupsY = (UINT)(h + 7) / 8;
  m_pDevice->Transition(pCopy, PS_READ, READ);
  m_pDevice->Transition(pEdges, PS_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(pList, eEdges, CB.nGPU, m_nSmaaTables, m_nSmaaTables + CD3D12Compute::COMPUTE_SRVS, nGroupsX, nGroupsY);
  m_pDevice->Transition(pEdges, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, READ);
  m_pDevice->Transition(pArea, PS_READ, READ);
  m_pDevice->Transition(pSearch, PS_READ, READ);
  m_pDevice->Transition(pWeights, PS_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(pList, eWeights, CB.nGPU, m_nSmaaTables + TABLE, m_nSmaaTables + TABLE + CD3D12Compute::COMPUTE_SRVS, nGroupsX,
                       nGroupsY);
  m_pDevice->Transition(pWeights, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, PS_READ);
  m_pDevice->Transition(pSearch, READ, PS_READ);
  m_pDevice->Transition(pArea, READ, PS_READ);
  m_pDevice->Transition(pEdges, READ, PS_READ);
  m_pDevice->Transition(pCopy, READ, PS_READ);
  return true;
}

void CD3D12Renderer::ReleaseSmaa(bool bAll)
{
  CD3D12Texture **const Textures[4] = { &m_pSmaaEdges, &m_pSmaaWeights, &m_pSmaaArea, &m_pSmaaSearch };
  for (int i = 0; i < (bAll ? 4 : 2); i++)
  {
    if (m_pUpload && *Textures[i])
      m_pUpload->ReleaseTexture(*Textures[i]);
    *Textures[i] = nullptr;
  }
  if (m_nSmaaTables >= 0 && m_pDescriptors)
    m_pDescriptors->FreeRange(m_nSmaaTables, 2 * (CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS));
  m_nSmaaTables = -1;
  m_pSmaaSource.Reset();
}

void CD3D12Renderer::ReleasePostAA()
{
  if (m_pUpload && m_pAaLayer)
    m_pUpload->ReleaseTexture(m_pAaLayer);
  m_pAaLayer = nullptr;
  ReleaseSmaa(true);
  m_ePostAA = EAntiAlias::None;
  m_nPostAAFrame = -1;
}
