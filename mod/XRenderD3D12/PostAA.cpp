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
#include "Scene.h"

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
    iLog->LogWarning("XRenderD3D12: d3d12_AA %s: no such anti-aliasing (none, fxaa); none", szValue);
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
  m_pDevice->SetBackBufferTargets();

  const int nQuality = std::clamp(CV_d3d12_AAQuality, 0, 3);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_AntiAlias;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = AA_FXAA | (CV_d3d12_AADebug ? AA_DEBUG : 0);
  dc.nTex0 = (uint32)m_nSceneColorSRV;
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

void CD3D12Renderer::ReleasePostAA()
{
  if (m_pUpload && m_pAaLayer)
    m_pUpload->ReleaseTexture(m_pAaLayer);
  m_pAaLayer = nullptr;
  m_ePostAA = EAntiAlias::None;
  m_nPostAAFrame = -1;
}
