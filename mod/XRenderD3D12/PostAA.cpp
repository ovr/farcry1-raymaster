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
//	  - cmaa2: CMAA2 (third_party/cmaa2) at d3d12_AAQuality's preset: its
//	    four compute kernels (Shaders/AntiAliasCmaa2.hlsl, the wide root
//	    signature, two of them dispatched by the arguments it writes) over
//	    a copy of the picture in place, then the pass writes the pixels it
//	    changed.
//	  - taa: TAA is an upscaler of the display's size (UpscalerTaa.cpp,
//	    Upscale.cpp): the pass does nothing for it.
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
  { "cmaa2", CD3D12Renderer::EAntiAlias::Cmaa2, "CMAA2" },
  { "taa", CD3D12Renderer::EAntiAlias::Taa, "TAA" },
  { "msaa2", CD3D12Renderer::EAntiAlias::Msaa2, "MSAA 2x" },
  { "msaa4", CD3D12Renderer::EAntiAlias::Msaa4, "MSAA 4x" },
  { "msaa8", CD3D12Renderer::EAntiAlias::Msaa8, "MSAA 8x" },
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
    iLog->LogWarning("XRenderD3D12: d3d12_AA %s: no such anti-aliasing (none, fxaa, smaa, cmaa2, taa, msaa2, msaa4, msaa8); none", szValue);
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
  const bool bTaa = m_pUpscaler && m_pUpscaler->IsAntiAliasing();
  if (m_pUpscaler && m_pUpscaler->GathersFrames() && !bTaa && m_nUpscaledFrame == m_nFrames)
  {
    if (!m_bAAUpscalerLogged)
      iLog->Log("XRenderD3D12: d3d12_AA %s is not used: the upscaler %s anti-aliases", AntiAliasName(eAA).szValue, m_pUpscaler->GetName());
    m_bAAUpscalerLogged = true;
    return;
  }
  m_bAAUpscalerLogged = false;
  // MSAA anti-aliased the scene (Msaa.cpp).
  if (MsaaSamples(eAA))
  {
    if (m_nMsaaFrame == m_nFrames && m_pFrameDump)
    {
      fprintf(m_pFrameDump.get(), "anti-aliasing: %s", AntiAliasName(eAA).szName);
      if (CV_d3d12_AlphaToCoverage > 0)
        fprintf(m_pFrameDump.get(), ", alpha to coverage: %d draws", m_nFrameAlphaCoverage);
      fprintf(m_pFrameDump.get(), "\n");
    }
    return;
  }
  const int nQuality = std::clamp(CV_d3d12_AAQuality, 0, 3);
  // TAA anti-aliased the scene as an upscaler (UpscaleScene).
  if (eAA == EAntiAlias::Taa)
  {
    if (bTaa && m_nUpscaledFrame == m_nFrames && m_pFrameDump)
      fprintf(m_pFrameDump.get(), "anti-aliasing: TAA, quality %d (%s), sharpness %.2f%s%s\n", nQuality, s_QualityNames[nQuality],
              std::clamp(CV_d3d12_UpscaleSharpness, 0.0f, 1.0f), m_bUpscalerHdrInput ? ", HDR input" : "", CV_d3d12_AADebug ? ", debug view" : "");
    return;
  }

  GpuSection("anti-aliasing");
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  // The picture alone (no depth): the copy is the pass's source while it draws into the back buffer.
  const int Rect[4] = { 0, 0, w, h };
  ID3D12Resource *pLayer = m_pDevice->HasLayer() ? m_pDevice->GetHighlight() : nullptr;
  if (!CopySceneForWater(Rect) || (pLayer && !EnsureLayerCopy(m_pAaLayer)))
    return;
  if (pLayer)
    CopyLayer(m_pAaLayer);
  // SMAA's edges and weights, CMAA2's picture, from the copy (the back buffer's, CopySceneForWater).
  ID3D12Resource *pCopy = m_SceneCopies[m_pDevice->IsInScene() || m_pDevice->IsInHdr() ? 1 : 0].pColor.Get();
  if ((eAA == EAntiAlias::Smaa && !DrawSmaaPasses(pCopy, w, h, nQuality)) || (eAA == EAntiAlias::Cmaa2 && !DrawCmaa2Passes(pCopy, w, h, nQuality)))
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
  dc.nFlags = (eAA == EAntiAlias::Smaa ? AA_SMAA : eAA == EAntiAlias::Cmaa2 ? AA_CMAA2 : AA_FXAA) | (CV_d3d12_AADebug ? AA_DEBUG : 0);
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  if (eAA == EAntiAlias::Smaa)
  {
    dc.nTex1 = (uint32)m_pSmaaWeights->m_nSRV;
    dc.nTex2 = (uint32)m_pSmaaEdges->m_nSRV;
  }
  else if (eAA == EAntiAlias::Cmaa2)
  {
    dc.nTex1 = (uint32)m_pCmaa2Color->m_nSRV;
    dc.nTex2 = (uint32)m_pCmaa2Edges->m_nSRV;
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
  // MSAA has no quality preset.
  if (MsaaSamples(eAA))
  {
    const bool bThisFrame = m_nMsaaFrame == m_nFrames;
    snprintf(szText, sizeof(szText), "%s%s%s%s", AntiAliasName(eAA).szName, bThisFrame ? CV_d3d12_AlphaToCoverage > 0 ? ", alpha to coverage" : "" : " (",
             bThisFrame ? "" : m_szMsaaOff ? m_szMsaaOff : "no 3D scene now", bThisFrame ? "" : ")");
    return szText;
  }
  if (eAA == EAntiAlias::Taa)
  {
    // TAA as the upscaler (UpscaleScene; the statistics run before Update counts the frame). Not made yet (the menu before a 3D
    // scene with it) is not made for want of a 3D scene; made and failed, UpdateUpscaler keeps its name ("taa", Upscale.cpp).
    const bool bTaa = m_pUpscaler && m_pUpscaler->IsAntiAliasing();
    const bool bFailed = !bTaa && !_stricmp(m_UpscalerName.c_str(), "taa");
    snprintf(szText, sizeof(szText), "TAA (%s%s%s)", s_QualityNames[std::clamp(CV_d3d12_AAQuality, 0, 3)],
             bTaa && m_bUpscalerHdrInput ? ", HDR input" : "",
             bFailed ? ", not available" : bTaa && m_nUpscaledFrame == m_nFrames ? "" : ", no 3D scene now");
    return szText;
  }
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
  // Both kept by the device: the layer drawn on next (BindHighlight asks for its state), the copy read.
  ID3D12Resource *pLayer = m_pDevice->GetHighlight();
  m_pDevice->Require(pLayer, D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_pDevice->Require(pCopy->m_pResource, D3D12_RESOURCE_STATE_COPY_DEST);
  m_pDevice->CopyResource(pCopy->m_pResource, pLayer);
  m_pDevice->Require(pCopy->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

bool CD3D12Renderer::DrawSmaaPasses(ID3D12Resource *pCopy, int w, int h, int nQuality)
{
  constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
  const ED3D12Kernel eEdges = (ED3D12Kernel)(eD3D12K_SmaaEdgesLow + nQuality);
  const ED3D12Kernel eWeights = (ED3D12Kernel)(eD3D12K_SmaaWeightsLow + nQuality);
  if (!pCopy || !m_pCompute || !m_pUpload || !m_pDescriptors || !m_pCompute->HasKernel(eEdges) || !m_pCompute->HasKernel(eWeights))
    return false;
  // The precomputed textures once, in the formats their headers name; the targets of the display's size. Their states
  // kept by the device: made in PIXEL_SHADER_RESOURCE (the upload leaves the precomputed ones so), each kernel asks.
  constexpr D3D12_RESOURCE_STATES PS_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  if (!m_pSmaaArea)
  {
    const SD3D12SubData Area = { areaTexBytes, AREATEX_PITCH };
    m_pSmaaArea = m_pUpload->CreateTexture(AREATEX_WIDTH, AREATEX_HEIGHT, 1, DXGI_FORMAT_R8G8_UNORM, false, &Area);
    if (m_pSmaaArea)
      m_pDevice->TrackState(m_pSmaaArea->m_pResource, PS_READ);
  }
  if (!m_pSmaaSearch)
  {
    const SD3D12SubData Search = { searchTexBytes, SEARCHTEX_PITCH };
    m_pSmaaSearch = m_pUpload->CreateTexture(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, DXGI_FORMAT_R8_UNORM, false, &Search);
    if (m_pSmaaSearch)
      m_pDevice->TrackState(m_pSmaaSearch->m_pResource, PS_READ);
  }
  if (m_pSmaaEdges && (m_pSmaaEdges->m_nWidth != w || m_pSmaaEdges->m_nHeight != h))
    ReleaseSmaa(false);
  if (!m_pSmaaEdges && (m_pSmaaEdges = m_pUpload->CreateRenderTarget(w, h, DXGI_FORMAT_R8G8_UNORM, true)))
    m_pDevice->TrackState(m_pSmaaEdges->m_pResource, PS_READ);
  if (!m_pSmaaWeights && (m_pSmaaWeights = m_pUpload->CreateRenderTarget(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true)))
    m_pDevice->TrackState(m_pSmaaWeights->m_pResource, PS_READ);
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
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const UINT nGroupsX = (UINT)(w + 7) / 8, nGroupsY = (UINT)(h + 7) / 8;
  // The copy readable by the kernel and the pass (the device keeps its state: CreateTargetCopies). The edges read by the
  // weights' kernel and the pass (one transition), the weights by the pass; the precomputed textures readable by both
  // kinds once.
  m_pDevice->Require(pCopy, READ);
  m_pDevice->Require(pEdges, UAV);
  m_pCompute->Dispatch(pList, eEdges, CB.nGPU, m_nSmaaTables, m_nSmaaTables + CD3D12Compute::COMPUTE_SRVS, nGroupsX, nGroupsY);
  m_pDevice->Require(pEdges, READ);
  m_pDevice->Require(pArea, READ);
  m_pDevice->Require(pSearch, READ);
  m_pDevice->Require(pWeights, UAV);
  m_pCompute->Dispatch(pList, eWeights, CB.nGPU, m_nSmaaTables + TABLE, m_nSmaaTables + TABLE + CD3D12Compute::COMPUTE_SRVS, nGroupsX,
                       nGroupsY);
  m_pDevice->Require(pWeights, PS_READ);
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

namespace
{

//! A barrier on every UAV access: the next CMAA2 kernel reads what the last one wrote.
void UavBarrierAll(ID3D12GraphicsCommandList *pList)
{
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  g_pRenderer->m_pDevice->ResourceBarriersOn(pList, 1, &b);
}

//! CMAA2's working buffers (m_pCmaa2Buffers), the UAVs u2, u3, u4, u6, u7 of its table.
enum ECmaa2Buffer
{
  CMAA2_SHAPES,       //!< the shapes to process (uint each, a pixel's position)
  CMAA2_QUADS,        //!< the 2 x 2 quads with new colours (uint each)
  CMAA2_COLOURS,      //!< the lists of their colours (uint2 each)
  CMAA2_COUNTERS,     //!< its counters (raw, 16 words)
  CMAA2_ARGUMENTS,    //!< the indirect dispatches' arguments (raw, 4 words)
  CMAA2_BUFFERS
};

}

bool CD3D12Renderer::DrawCmaa2Passes(ID3D12Resource *pCopy, int w, int h, int nQuality)
{
  constexpr int TABLE = CD3D12Compute::WIDE_SRVS + CD3D12Compute::WIDE_UAVS;
  const ED3D12Kernel eEdges = (ED3D12Kernel)(eD3D12K_Cmaa2EdgesLow + nQuality);
  if (!pCopy || !m_pCompute || !m_pUpload || !m_pDescriptors || !m_pDevice->GetDevice())
    return false;
  for (ED3D12Kernel e : { eEdges, eD3D12K_Cmaa2DispatchArgs, eD3D12K_Cmaa2Candidates, eD3D12K_Cmaa2Apply })
  {
    if (!m_pCompute->HasKernel(e))
      return false;
  }
  // The picture in the copy's format family: its sRGB view for CMAA2's reads (it works in linear light), its UNORM
  // view for its writes (it converts them to sRGB) and for the pass.
  const DXGI_FORMAT Format = pCopy->GetDesc().Format;
  DXGI_FORMAT Typeless, Srgb;
  if (Format == DXGI_FORMAT_B8G8R8A8_UNORM)
  {
    Typeless = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    Srgb = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  }
  else if (Format == DXGI_FORMAT_R8G8B8A8_UNORM)
  {
    Typeless = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    Srgb = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  }
  else
    return false;
  if (m_pCmaa2Color && (m_pCmaa2Color->m_nWidth != w || m_pCmaa2Color->m_nHeight != h || m_pCmaa2Color->m_Format != Typeless))
    ReleaseCmaa2();

  ID3D12Device *pDevice = m_pDevice->GetDevice();
  if (!m_pCmaa2Color)
  {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = { Format };
    if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))) ||
        !(Support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) || !(Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
      return false;
    // The sizes of CMAA2's integration guide (its sample's "99.99 % safe" ones): the shapes a quarter of the pixels,
    // the colours half of them, the quads a sixth; the edges 4 bits a pixel, two pixels a texel.
    const UINT64 nPixels = (UINT64)w * h;
    const UINT64 Bytes[CMAA2_BUFFERS] = { nPixels / 4 * 4, (nPixels + 5) / 6 * 4, nPixels / 2 * 8, 16 * 4, 4 * 4 };
    const wchar_t *const Names[CMAA2_BUFFERS] = { L"XRenderD3D12 CMAA2 shapes", L"XRenderD3D12 CMAA2 quads", L"XRenderD3D12 CMAA2 colours",
                                                 L"XRenderD3D12 CMAA2 counters", L"XRenderD3D12 CMAA2 arguments" };
    bool bOk = true;
    for (int i = 0; i < CMAA2_BUFFERS && bOk; i++)
    {
      D3D12_RESOURCE_DESC Desc = {};
      Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      Desc.Width = Bytes[i];
      Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
      Desc.SampleDesc.Count = 1;
      Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
      // Common, as a buffer is after each list: the kernels promote them to UNORDERED_ACCESS.
      bOk = SUCCEEDED(m_pDevice->CreateResource(D3D12_HEAP_TYPE_DEFAULT, Desc, D3D12_RESOURCE_STATE_COMMON, nullptr, ED3D12Memory::Committed,
                                                Names[i], m_pCmaa2Buffers[i]));
    }
    m_pCmaa2Color = bOk ? m_pUpload->CreateComputeTarget(w, h, 1, Typeless, L"XRenderD3D12 CMAA2 picture") : nullptr;
    m_pCmaa2Edges = m_pCmaa2Color ? m_pUpload->CreateComputeTarget((w + 1) / 2, h, 1, DXGI_FORMAT_R8_TYPELESS, L"XRenderD3D12 CMAA2 edges") : nullptr;
    m_pCmaa2Heads = m_pCmaa2Edges ? m_pUpload->CreateComputeTarget((w + 1) / 2, (h + 1) / 2, 1, DXGI_FORMAT_R32_UINT, L"XRenderD3D12 CMAA2 heads")
                                  : nullptr;
    if (!m_pCmaa2Heads)
    {
      ReleaseCmaa2();
      return false;
    }
    // The pass's views in the texture table.
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = Format;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2D.MipLevels = 1;
    m_pCmaa2Color->m_nSRV = m_pDescriptors->AllocSRV(m_pCmaa2Color->m_pResource, &SRV);
    SRV.Format = DXGI_FORMAT_R8_UNORM;
    m_pCmaa2Edges->m_nSRV = m_pDescriptors->AllocSRV(m_pCmaa2Edges->m_pResource, &SRV);
    m_nCmaa2Table = m_pDescriptors->AllocRange(TABLE);
    if (m_pCmaa2Color->m_nSRV < 0 || m_pCmaa2Edges->m_nSRV < 0 || m_nCmaa2Table < 0)
    {
      ReleaseCmaa2();
      return false;
    }
    // The kernels' table: t0 the picture (sRGB), t1..t3 null; u0 the picture (UNORM), u1 the edges, u2 the shapes, u3 the
    // quads, u4 the colours, u5 the heads, u6 the counters, u7 the arguments.
    SRV.Format = Srgb;
    m_pDescriptors->WriteSRV(m_nCmaa2Table, m_pCmaa2Color->m_pResource, &SRV);
    SRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    for (int i = 1; i < CD3D12Compute::WIDE_SRVS; i++)
      m_pDescriptors->WriteSRV(m_nCmaa2Table + i, nullptr, &SRV);
    const int u = m_nCmaa2Table + CD3D12Compute::WIDE_SRVS;
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
    UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    UAV.Format = Format;
    m_pDescriptors->WriteUAV(u + 0, m_pCmaa2Color->m_pResource, &UAV);
    UAV.Format = DXGI_FORMAT_R8_UINT;
    m_pDescriptors->WriteUAV(u + 1, m_pCmaa2Edges->m_pResource, &UAV);
    UAV.Format = DXGI_FORMAT_R32_UINT;
    m_pDescriptors->WriteUAV(u + 5, m_pCmaa2Heads->m_pResource, &UAV);
    UAV = {};
    UAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    UAV.Format = DXGI_FORMAT_UNKNOWN;
    const int Structured[3][3] = { { CMAA2_SHAPES, 2, 4 }, { CMAA2_QUADS, 3, 4 }, { CMAA2_COLOURS, 4, 8 } };
    for (const int *s : Structured)
    {
      UAV.Buffer.StructureByteStride = (UINT)s[2];
      UAV.Buffer.NumElements = (UINT)(Bytes[s[0]] / s[2]);
      m_pDescriptors->WriteUAV(u + s[1], m_pCmaa2Buffers[s[0]].Get(), &UAV);
    }
    UAV.Buffer.StructureByteStride = 0;
    UAV.Format = DXGI_FORMAT_R32_TYPELESS;
    UAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    UAV.Buffer.NumElements = 16;
    m_pDescriptors->WriteUAV(u + 6, m_pCmaa2Buffers[CMAA2_COUNTERS].Get(), &UAV);
    UAV.Buffer.NumElements = 4;
    m_pDescriptors->WriteUAV(u + 7, m_pCmaa2Buffers[CMAA2_ARGUMENTS].Get(), &UAV);
    // The picture's and the edges' states kept by the device (made in UNORDERED_ACCESS; each frame asks for its own), the
    // heads always in UNORDERED_ACCESS.
    m_pDevice->TrackState(m_pCmaa2Color->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pDevice->TrackState(m_pCmaa2Edges->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_bCmaa2Cleared = false;
  }

  constexpr D3D12_RESOURCE_STATES PS_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  constexpr D3D12_RESOURCE_STATES ARGS = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  ID3D12Resource *pColor = m_pCmaa2Color->m_pResource, *pEdges = m_pCmaa2Edges->m_pResource;
  ID3D12Resource *pArgs = m_pCmaa2Buffers[CMAA2_ARGUMENTS].Get();
  // The copy into CMAA2's picture, which its kernels read until the last one writes it; the copy then read by the pass.
  m_pDevice->Require(pCopy, D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_pDevice->Require(pColor, D3D12_RESOURCE_STATE_COPY_DEST);
  m_pDevice->CopyResource(pColor, pCopy);
  m_pDevice->Require(pColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_pDevice->Require(pCopy, PS_READ);
  m_pDevice->Require(pEdges, UAV);
  m_pDevice->Transition(pArgs, D3D12_RESOURCE_STATE_COMMON, UAV);
  // Its counters zero once (the kernel of the second arguments clears them, as after each frame).
  if (!m_bCmaa2Cleared)
  {
    m_pCompute->DispatchWide(pList, eD3D12K_Cmaa2DispatchArgs, m_nCmaa2Table, 1, 2);
    UavBarrierAll(pList);
    m_bCmaa2Cleared = true;
  }
  // The edges: a group 16 x 16 threads of 2 x 2 pixels, a border of one around its 14 x 14 output.
  constexpr UINT OUTPUT = 2 * 14;
  m_pCompute->DispatchWide(pList, eEdges, m_nCmaa2Table, ((UINT)w + OUTPUT - 1) / OUTPUT, ((UINT)h + OUTPUT - 1) / OUTPUT);
  UavBarrierAll(pList);
  // The shapes by the arguments of their count.
  m_pCompute->DispatchWide(pList, eD3D12K_Cmaa2DispatchArgs, m_nCmaa2Table, 2, 1);
  m_pDevice->Transition(pArgs, UAV, ARGS);
  UavBarrierAll(pList);
  m_pCompute->DispatchWideIndirect(pList, eD3D12K_Cmaa2Candidates, m_nCmaa2Table, pArgs);
  m_pDevice->Transition(pArgs, ARGS, UAV);
  UavBarrierAll(pList);
  // Their colours into the picture by the arguments of the quads' count.
  m_pCompute->DispatchWide(pList, eD3D12K_Cmaa2DispatchArgs, m_nCmaa2Table, 1, 2);
  m_pDevice->Transition(pArgs, UAV, ARGS);
  UavBarrierAll(pList);
  m_pDevice->Require(pColor, UAV);
  m_pCompute->DispatchWideIndirect(pList, eD3D12K_Cmaa2Apply, m_nCmaa2Table, pArgs);
  // The arguments as a buffer is after its list (not tracked: the kernels promote the other buffers from COMMON); the
  // picture and the edges read by the pass.
  m_pDevice->Transition(pArgs, ARGS, D3D12_RESOURCE_STATE_COMMON);
  m_pDevice->Require(pColor, PS_READ);
  m_pDevice->Require(pEdges, PS_READ);
  return true;
}

void CD3D12Renderer::ReleaseCmaa2()
{
  CD3D12Texture **const Textures[3] = { &m_pCmaa2Color, &m_pCmaa2Edges, &m_pCmaa2Heads };
  for (CD3D12Texture **p : Textures)
  {
    if (m_pUpload && *p)
      m_pUpload->ReleaseTexture(*p);
    *p = nullptr;
  }
  for (CD3D12Resource &p : m_pCmaa2Buffers)
  {
    if (p && m_pDevice)
      m_pDevice->DeferredRelease(p);
    p.Reset();
  }
  if (m_nCmaa2Table >= 0 && m_pDescriptors)
    m_pDescriptors->FreeRange(m_nCmaa2Table, CD3D12Compute::WIDE_SRVS + CD3D12Compute::WIDE_UAVS);
  m_nCmaa2Table = -1;
  m_bCmaa2Cleared = false;
}

void CD3D12Renderer::ReleasePostAA()
{
  if (m_pUpload && m_pAaLayer)
    m_pUpload->ReleaseTexture(m_pAaLayer);
  m_pAaLayer = nullptr;
  ReleaseSmaa(true);
  ReleaseCmaa2();
  ReleaseMsaa();
  m_ePostAA = EAntiAlias::None;
  m_nPostAAFrame = -1;
}
