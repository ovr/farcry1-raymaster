// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Hdr.cpp
//	Description: The HDR pipeline of the main view (r_HDRRendering not 0).
//
//	The main view's 3D scene (BeginScene3D .. the end of its EF_EndEf3D)
//	draws into D3D12_HDR_FORMAT: the device's HDR target of the back
//	buffer's size with the back buffer's depth buffer, or with an upscaler
//	its scene targets. The materials keep the game's maths, but what the
//	back buffer clamped at 1 (overbright light, light maps, specular, the
//	additive particles, flares and glows) stays. EndMainScene then, at the
//	scene's size (Shaders/Hdr.hlsl):
//	  - the meter: the mean log2 luminance of the scene's light (centre
//	    weighted), reduced to 1 x 1;
//	  - the adaptation: the exposure (EV) towards the meter's from the one
//	    of the frame before, at the frame time's rate (d3d12_ExposureSpeed),
//	    within d3d12_ExposureMin .. Max (d3d12_AutoExposure);
//	  - the bloom (d3d12_Bloom): the exposed light above d3d12_BloomThreshold
//	    at half the size, BLOOM_LEVELS levels down (13 taps), then back up
//	    (a 3 x 3 tent), each added to the one above;
//	  - the resolve: the light exposed (plus d3d12_Exposure), with the
//	    bloom, tone mapped (d3d12_ToneMap), colour graded (ColorGrading.cpp)
//	    into the back buffer, or for an upscaler into an LDR target of the
//	    scene's size, which it upscales as it would the scene (its flags
//	    unchanged).
//	The screen effects and the 2D come after it on the back buffer, as
//	without HDR. Heat vision's colours are a palette, not light: while it
//	is on the scene is only clamped (the exposure held).
//
//	What the game draws as light sources is brighter in the HDR targets
//	(IsHdrTarget: the main scene, and with it the water's reflection and
//	the environment cubes): the sun's flare (d3d12_HDRSun), the sky towards
//	the sun (d3d12_HDRSky, DF2_SKY_GLOW), and what glows (d3d12_HDRGlow):
//	the additive particles and materials (fire, muzzle flashes), the
//	coronas and procedural flares of the lights, the glow maps of lamps and
//	screens. Each is a factor of the light; 1 is the game's. The flares,
//	particles and additive materials get it in their cores (DF2_HDR_CORE:
//	where their texture is white), so that they keep their soft edges; the
//	glow maps, which light a surface, all over (HdrBoost).
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

//! The meter's first level is the scene reduced by METER_FACTOR, each next one by METER_REDUCE,
//! down to 1 x 1.
constexpr int METER_FACTOR = 8;
constexpr int METER_REDUCE = 4;
//! The mean log2 luminance the exposure brings to EV 0: the game's daylight (Training's beach and
//! hills from above read -2.56 and -2.69).
constexpr float EXPOSURE_KEY_LOG2 = -2.6f;
//! The bloom's levels below the scene: half its size, then each half the one before.
constexpr int BLOOM_LEVELS = 6;
//! The soft knee under the bloom's threshold, a part of the threshold.
constexpr float BLOOM_KNEE = 0.5f;

const char *const s_ToneMapNames[] = { "clamp", "soft shoulder", "ACES fitted", "AgX" };

//! A half float (IEEE 754 binary16) as a float.
float HalfToFloat(uint16 h)
{
  const int nSign = h >> 15, nExp = (h >> 10) & 31, nMantissa = h & 1023;
  float f;
  if (nExp == 0)
    f = ldexpf((float)nMantissa, -24);
  else if (nExp == 31)
    f = nMantissa ? NAN : INFINITY;
  else
    f = ldexpf((float)(nMantissa | 1024), nExp - 25);
  return nSign ? -f : f;
}

}

void CD3D12Renderer::BeginMainScene()
{
  // r_HDRRendering is read every frame: it can change at any time.
  const bool bHdr = IsHdrOn();
  if (!bHdr && (m_Hdr.nWidth || m_pDevice->GetHdrColor()))
    ReleaseHdr();
  m_bHdrScene = bHdr;
  BeginUpscale();
  if (!m_bUpscaling && bHdr && m_pDevice->SetHdrTarget(true))
  {
    // The HDR target in the texture table (again when the device made it anew).
    if (m_pDevice->GetHdrColor() != m_Hdr.pHdrColor.Get())
    {
      if (m_Hdr.nHdrColorSRV >= 0)
        m_pDescriptors->FreeSRV(m_Hdr.nHdrColorSRV);
      m_Hdr.pHdrColor = m_pDevice->GetHdrColor();
      m_Hdr.nHdrColorSRV = m_pDescriptors->AllocSRV(m_Hdr.pHdrColor.Get(), nullptr);
    }
    if (m_Hdr.nHdrColorSRV >= 0)
      m_pDevice->BeginHdr();
  }
  else if (m_Hdr.pHdrColor)
    ReleaseHdrColor();        // an upscaler's scene targets instead
  m_bHdrScene = m_pDevice->GetMainColorFormat() == D3D12_HDR_FORMAT;
}

void CD3D12Renderer::EndMainScene()
{
  if (m_bUpscaling)
    UpscaleScene();
  else if (m_pDevice && m_pDevice->IsInHdr())
  {
    GpuSection("hdr");
    m_pDevice->EndHdr();
    InvalidateFrameConstants();
    ResolveHdr(m_Hdr.nHdrColorSRV, m_pDevice->GetWidth(), m_pDevice->GetHeight(), false);
  }
  if (m_bHdrScene && m_pFrameDump)
    DumpHdr();
  m_bHdrScene = false;
}

CD3D12Texture *CD3D12Renderer::ResolveHdr(int nSceneSRV, int nWidth, int nHeight, bool bToTexture)
{
  if (nSceneSRV < 0 || !EnsureHdrTargets(nWidth, nHeight, bToTexture))
  {
    // Nothing to tone map with: the scene as it is (clamped by the back buffer).
    if (!bToTexture && nSceneSRV >= 0)
    {
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = HDR_RESOLVE;
      dc.nTex0 = (uint32)nSceneSRV;
      Set4(dc.vParams2, 0, 1, 0, 0);
      DrawHdrToBackBuffer(dc);
    }
    return nullptr;
  }
  const bool bHeat = m_bHeatVision;
  const bool bAuto = CV_d3d12_AutoExposure != 0;
  const float fComp = bHeat ? 0.0f : CV_d3d12_Exposure;
  const float fBloom = bHeat ? 0.0f : std::max(CV_d3d12_Bloom, 0.0f);
  const float fFrameTime = iTimer ? std::clamp(iTimer->GetFrameTime(), 0.0f, 1.0f) : 0.02f;

  // The meter and the adaptation; none in heat vision (the exposure stays as it was). Without a
  // frame just before (the menu, a load) or after the automatic exposure was off, the exposure
  // is the meter's at once.
  if (!bAuto || m_Hdr.nResolvedFrame != m_nFrames - 1)
    m_Hdr.bAdapted = false;
  m_Hdr.nResolvedFrame = m_nFrames;
  if (bAuto && !bHeat)
  {
    int nSrcSRV = nSceneSRV;
    for (size_t i = 0; i < m_Hdr.Meter.size(); i++)
    {
      CD3D12Texture *pDst = m_Hdr.Meter[i];
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = i == 0 ? HDR_LUMINANCE : HDR_REDUCE;
      dc.nTex0 = (uint32)nSrcSRV;
      Set4(dc.vParams0, 1.0f / pDst->m_nWidth, 1.0f / pDst->m_nHeight, (float)(((i == 0 ? METER_FACTOR : METER_REDUCE) + 1) / 2), 0);
      DrawHdrPass(pDst, dc);
      nSrcSRV = pDst->m_nSRV;
    }
    const float fRate = std::max(CV_d3d12_ExposureSpeed, 0.0f) * fFrameTime;
    SDrawConstants dc = DefaultDrawConstants();
    dc.nFlags = HDR_ADAPT;
    dc.nTex0 = (uint32)m_Hdr.Meter.back()->m_nSRV;
    dc.nTex1 = (uint32)m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV;
    Set4(dc.vParams0, EXPOSURE_KEY_LOG2, std::min(CV_d3d12_ExposureMin, CV_d3d12_ExposureMax), std::max(CV_d3d12_ExposureMin, CV_d3d12_ExposureMax),
         m_Hdr.bAdapted ? 0.0f : 1.0f);
    Set4(dc.vParams1, 1 - expf(-2 * fRate), 1 - expf(-fRate), 0, 0);
    m_Hdr.nExposure ^= 1;
    DrawHdrPass(m_Hdr.pExposure[m_Hdr.nExposure], dc);
    m_Hdr.bAdapted = true;
  }
  const bool bAdapted = bAuto && m_Hdr.bAdapted && !bHeat;
  const int nExposureSRV = m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV;

  // The bloom: down from the scene, then up again, each level added to the one above.
  if (fBloom > 0)
  {
    int nSrcSRV = nSceneSRV, nSrcWidth = nWidth, nSrcHeight = nHeight;
    for (size_t i = 0; i < m_Hdr.Bloom.size(); i++)
    {
      CD3D12Texture *pDst = m_Hdr.Bloom[i];
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = HDR_BLOOM_DOWN | (i == 0 ? HDR_BLOOM_PREFILTER : 0);
      dc.nTex0 = (uint32)nSrcSRV;
      dc.nTex1 = (uint32)nExposureSRV;
      Set4(dc.vParams0, 1.0f / pDst->m_nWidth, 1.0f / pDst->m_nHeight, 1.0f / nSrcWidth, 1.0f / nSrcHeight);
      const float fThreshold = std::max(CV_d3d12_BloomThreshold, 0.0f);
      Set4(dc.vParams1, fComp, bAdapted ? 1.0f : 0.0f, fThreshold, fThreshold * BLOOM_KNEE);
      DrawHdrPass(pDst, dc);
      nSrcSRV = pDst->m_nSRV;
      nSrcWidth = pDst->m_nWidth;
      nSrcHeight = pDst->m_nHeight;
    }
    for (size_t i = m_Hdr.Bloom.size() - 1; i-- > 0;)
    {
      CD3D12Texture *pDst = m_Hdr.Bloom[i], *pSrc = m_Hdr.Bloom[i + 1];
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = HDR_BLOOM_UP;
      dc.nTex0 = (uint32)pSrc->m_nSRV;
      Set4(dc.vParams0, 1.0f / pDst->m_nWidth, 1.0f / pDst->m_nHeight, 1.0f / pSrc->m_nWidth, 1.0f / pSrc->m_nHeight);
      DrawHdrPass(pDst, dc, GS_BLSRC_ONE | GS_BLDST_ONE);
    }
  }

  // The resolve; the bloom's first level holds the sum of all of them. The colour grading's table
  // (none in heat vision).
  const int nLutSRV = bHeat ? -1 : UpdateColorGrading();
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = HDR_RESOLVE;
  dc.nTex0 = (uint32)nSceneSRV;
  dc.nTex1 = fBloom > 0 ? (uint32)m_Hdr.Bloom[0]->m_nSRV : D3D12_SRV_WHITE;
  dc.nTex2 = (uint32)nExposureSRV;
  dc.nTex3 = nLutSRV >= 0 ? (uint32)nLutSRV : D3D12_SRV_WHITE;
  Set4(dc.vParams0, 1.0f / nWidth, 1.0f / nHeight, 0, 0);
  Set4(dc.vParams1, fComp, bAdapted ? 1.0f : 0.0f, fBloom / m_Hdr.Bloom.size(), (float)std::clamp(CV_d3d12_ToneMap, 0, 3));
  Set4(dc.vParams2, CV_d3d12_ToneMapKnee, bHeat ? 1.0f : 0.0f, (float)CV_d3d12_HDRDebug, nLutSRV >= 0 ? 1.0f : 0.0f);
  if (bToTexture)
  {
    DrawHdrPass(m_Hdr.pLdr, dc);
    m_pDevice->SetBackBufferTargets();
    return m_Hdr.pLdr;
  }
  DrawHdrToBackBuffer(dc);
  return nullptr;
}

void CD3D12Renderer::DrawHdrToBackBuffer(const SDrawConstants &dc)
{
  // Over the whole back buffer, whatever the engine's viewport and scissors.
  m_pDevice->SetBackBufferTargets();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = m_pDevice->GetWidth();
  m_Viewport[3] = m_pDevice->GetHeight();
  m_bScissor = m_bItemScissor = false;
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Hdr;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
}

void CD3D12Renderer::DrawHdrPass(CD3D12Texture *pTarget, const SDrawConstants &dc, uint32 nState)
{
  m_pDevice->SetOffscreenTargets(pTarget->m_pResource, pTarget->m_RTV, {}, pTarget->m_nWidth, pTarget->m_nHeight);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Hdr;
  Key.nState = GS_NODEPTHTEST | nState;
  Key.nCull = R_CULL_NONE;
  Key.nTarget = 1;
  Key.nColorFormat = pTarget->m_Format == D3D12_HDR_FORMAT ? PIPELINE_COLOR_HDR :
                     pTarget->m_Format == DXGI_FORMAT_R32_FLOAT ? PIPELINE_COLOR_R32F : PIPELINE_COLOR_DEFAULT;
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
}

bool CD3D12Renderer::EnsureHdrTargets(int nWidth, int nHeight, bool bLdr)
{
  if (!m_pUpload || nWidth <= 0 || nHeight <= 0)
    return false;
  if (nWidth != m_Hdr.nWidth || nHeight != m_Hdr.nHeight)
  {
    ReleaseHdrTargets();
    // The meter's levels, down to 1 x 1.
    int w = std::max((nWidth + METER_FACTOR - 1) / METER_FACTOR, 1), h = std::max((nHeight + METER_FACTOR - 1) / METER_FACTOR, 1);
    for (;;)
    {
      m_Hdr.Meter.push_back(m_pUpload->CreateRenderTarget(w, h, D3D12_HDR_FORMAT));
      if (w == 1 && h == 1)
        break;
      w = std::max((w + METER_REDUCE - 1) / METER_REDUCE, 1);
      h = std::max((h + METER_REDUCE - 1) / METER_REDUCE, 1);
    }
    for (CD3D12Texture *&pTex : m_Hdr.pExposure)
      pTex = m_pUpload->CreateRenderTarget(1, 1, DXGI_FORMAT_R32_FLOAT);
    // The bloom's, while they are at least 2 texels high.
    w = nWidth;
    h = nHeight;
    for (int i = 0; i < BLOOM_LEVELS && (h + 1) / 2 >= 2; i++)
    {
      w = std::max((w + 1) / 2, 1);
      h = (h + 1) / 2;
      m_Hdr.Bloom.push_back(m_pUpload->CreateRenderTarget(w, h, D3D12_HDR_FORMAT));
    }
    m_Hdr.nWidth = nWidth;
    m_Hdr.nHeight = nHeight;
    m_Hdr.bAdapted = false;
    const auto Bad = [](const CD3D12Texture *p) { return !p || p->m_nSRV < 0; };
    if (m_Hdr.Bloom.empty() || std::any_of(m_Hdr.Meter.begin(), m_Hdr.Meter.end(), Bad) ||
        std::any_of(m_Hdr.Bloom.begin(), m_Hdr.Bloom.end(), Bad) || Bad(m_Hdr.pExposure[0]) || Bad(m_Hdr.pExposure[1]))
    {
      iLog->LogError("XRenderD3D12: no HDR targets for %d x %d", nWidth, nHeight);
      ReleaseHdrTargets();
      return false;
    }
  }
  if (bLdr && !m_Hdr.pLdr)
  {
    m_Hdr.pLdr = m_pUpload->CreateRenderTarget(nWidth, nHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!m_Hdr.pLdr || m_Hdr.pLdr->m_nSRV < 0)
    {
      m_pUpload->ReleaseTexture(m_Hdr.pLdr);
      m_Hdr.pLdr = nullptr;
      return false;
    }
  }
  return true;
}

void CD3D12Renderer::ReleaseHdrTargets()
{
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : m_Hdr.Meter)
      m_pUpload->ReleaseTexture(pTex);
    for (CD3D12Texture *pTex : m_Hdr.Bloom)
      m_pUpload->ReleaseTexture(pTex);
    for (CD3D12Texture *pTex : m_Hdr.pExposure)
      m_pUpload->ReleaseTexture(pTex);
    m_pUpload->ReleaseTexture(m_Hdr.pLdr);
  }
  m_Hdr.Meter.clear();
  m_Hdr.Bloom.clear();
  m_Hdr.pExposure[0] = m_Hdr.pExposure[1] = nullptr;
  m_Hdr.pLdr = nullptr;
  m_Hdr.nWidth = m_Hdr.nHeight = 0;
  m_Hdr.bAdapted = false;
}

void CD3D12Renderer::ReleaseHdrColor()
{
  if (m_pDescriptors && m_Hdr.nHdrColorSRV >= 0)
    m_pDescriptors->FreeSRV(m_Hdr.nHdrColorSRV);
  m_Hdr.nHdrColorSRV = -1;
  m_Hdr.pHdrColor.Reset();
  if (m_pDevice)
    m_pDevice->SetHdrTarget(false);
}

void CD3D12Renderer::ReleaseHdr()
{
  ReleaseHdrTargets();
  ReleaseHdrColor();
  ReleaseColorGrading();
}

void CD3D12Renderer::DumpHdr()
{
  const int nToneMap = std::clamp(CV_d3d12_ToneMap, 0, 3);
  fprintf(m_pFrameDump.get(), "hdr: %d x %d, tone map %s (knee %g), exposure %+g EV, auto %d (%g to %g EV, speed %g), bloom %g (threshold %g)%s\n",
          m_Hdr.nWidth, m_Hdr.nHeight, s_ToneMapNames[nToneMap], CV_d3d12_ToneMapKnee, CV_d3d12_Exposure, CV_d3d12_AutoExposure,
          CV_d3d12_ExposureMin, CV_d3d12_ExposureMax, CV_d3d12_ExposureSpeed, CV_d3d12_Bloom, CV_d3d12_BloomThreshold,
          m_bHeatVision ? ", heat vision: clamped" : "");
  fprintf(m_pFrameDump.get(), "hdr: brighter than the game: sun %g, sky %g, glow %g; colour grading %s: saturation %g, contrast %g, temperature %g, tint %g, "
          "lut \"%s\" (%d, amount %g)\n", CV_d3d12_HDRSun, CV_d3d12_HDRSky, CV_d3d12_HDRGlow, m_Grading.pLut ? "on" : "off", m_Grading.fSaturation,
          m_Grading.fContrast, m_Grading.fTemperature, m_Grading.fTint, m_Grading.LutFile.c_str(), m_Grading.nCubeSize, m_Grading.fLutAmount);
  // The meter's mean and the adapted EV, read back (this frame is executed so far).
  if (!m_Hdr.bAdapted || m_Hdr.Meter.empty())
    return;
  uint16 Meter[2] = {};
  float fEV = 0;
  if (m_pDevice->ReadBackTexture(m_Hdr.Meter.back()->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)Meter, 1, 1) &&
      m_pDevice->ReadBackTexture(m_Hdr.pExposure[m_Hdr.nExposure]->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)&fEV, 1, 1))
  {
    const float fWeight = HalfToFloat(Meter[1]);
    const float fMeanLog = fWeight > 0 ? HalfToFloat(Meter[0]) / fWeight : 0.0f;
    fprintf(m_pFrameDump.get(), "hdr: meter mean log2 luminance %.3f (target %+.3f EV), adapted %+.3f EV\n", fMeanLog, EXPOSURE_KEY_LOG2 - fMeanLog, fEV);
  }
}

bool CD3D12Renderer::IsHdrTarget() const
{
  return !m_bHeatVision && m_pDevice && m_pDevice->GetTargetColorFormat() == D3D12_HDR_FORMAT;
}

float CD3D12Renderer::HdrBoost(float fLight) const
{
  if (!IsHdrTarget())
    return 1.0f;
  // HdrEncode (Common.hlsli)
  return fLight > 1 ? 1 + (fLight - 1) / 2.2f : powf(std::max(fLight, 0.0f), 1.0f / 2.2f);
}

void CD3D12Renderer::SetHdrCore(SDrawConstants &dc, float fLight, uint32 nState) const
{
  if (fLight == 1 || !IsHdrTarget())
    return;
  dc.nFlags2 |= DF2_HDR_CORE;
  Set4(dc.vHdrCore, std::max(fLight, 0.0f), (nState & GS_BLSRC_MASK) == GS_BLSRC_SRCALPHA ? 1.0f : 0.0f, 0, 0);
}

float CD3D12Renderer::HdrSkyGlow() const
{
  if (!IsHdrTarget() || !m_pScene || !m_pScene->GetSun())
    return 1.0f;
  return std::max(CV_d3d12_HDRSky, 0.0f);
}
