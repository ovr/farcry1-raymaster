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
//	  - the meter (d3d12_ExposureMeter): 1, a histogram of the scene's light
//	    by two kernels (Shaders/Exposure.hlsl, HdrExposure.hlsli): the key
//	    over the mean log2 luminance between two of its percentiles (centre
//	    weighted; d3d12_ExposureLow, High), lowered where the sky is in the
//	    frame (the depth buffer at the far plane) so that its bright part
//	    (d3d12_ExposureHighlights) stays below the tone map's knee; 0, the
//	    mean log2 luminance of the scene's light (centre weighted) reduced
//	    to 1 x 1, its first level by a kernel (Shaders/Meter.hlsl), whose
//	    threads read taps side by side;
//	  - the adaptation: the exposure (EV) towards the meter's from the one
//	    of the frame before, at the frame time's rate (d3d12_ExposureSpeed),
//	    within d3d12_ExposureMin .. Max (d3d12_AutoExposure);
//	  - the bloom (d3d12_Bloom): the exposed light above d3d12_BloomThreshold
//	    at half the size, BLOOM_LEVELS levels down (13 taps), then back up
//	    (a 3 x 3 tent), each added to the one above; a level from one twice
//	    its size by a kernel (Shaders/Bloom.hlsl), which reads each of the
//	    taps it shares with its neighbours once;
//	  - the resolve: the light (through the volumetric light,
//	    Volumetric.cpp) exposed (plus d3d12_Exposure), with the
//	    bloom, tone mapped (d3d12_ToneMap), colour graded (ColorGrading.cpp)
//	    into the back buffer.
//	With an upscaler (Upscale.cpp, d3d12_UpscaleHdr 1) the meter takes the
//	scene at its size, the upscaler its light through the volumetric light
//	(HdrSceneLight: not exposed, with the exposure's factor), and the bloom
//	and the resolve its picture at the display's size, with the highlight
//	layer; with d3d12_UpscaleHdr 0 all of it is at the scene's size into an
//	LDR target, which the upscaler takes as it would the scene (as up to
//	step 259).
//	The screen effects and the 2D come after it on the back buffer, as
//	without HDR. Heat vision's colours are a palette, not light: while it
//	is on the scene is only clamped (the exposure held).
//
//	What the game draws as light sources is brighter in the HDR targets
//	(IsHdrTarget: the main scene, and with it the water's reflection and
//	the environment cubes): the sun's flare (d3d12_HDRSun), the sky towards
//	the sun (d3d12_HDRSky, half way at d3d12_HDRSkyAngle from it,
//	DF2_SKY_GLOW), and what glows (d3d12_HDRGlow):
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
#include "D3D12Compute.h"
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
//! The bilinear taps a side of its texel: each of 2 x 2 texels of the scene.
constexpr int METER_TAPS = (METER_FACTOR + 1) / 2;
static_assert(METER_TAPS == 4, "Shaders/Meter.hlsl's TAPS");
constexpr int METER_REDUCE = 4;
//! The bloom's levels below the scene: half its size, then each half the one before.
constexpr int BLOOM_LEVELS = 6;
//! The soft knee under the bloom's threshold, a part of the threshold.
constexpr float BLOOM_KNEE = 0.5f;
//! A table of a kernel: its SRVs and UAVs (D3D12Compute.h).
constexpr int KERNEL_TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;

//! nTable's views null (D3D12_HDR_FORMAT): those the kernel uses are written before each dispatch.
void WriteNullTable(CD3D12Descriptors *pDescriptors, int nTable)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC NullSRV = {};
  NullSRV.Format = D3D12_HDR_FORMAT;
  NullSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  NullSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  NullSRV.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC NullUAV = {};
  NullUAV.Format = D3D12_HDR_FORMAT;
  NullUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
    pDescriptors->WriteSRV(nTable + i, nullptr, &NullSRV);
  for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, nullptr, &NullUAV);
}

bool BadTarget(const CD3D12Texture *p)
{
  return !p || p->m_nSRV < 0;
}
//! The automatic exposure's histogram (d3d12_ExposureMeter 1) takes about this many samples of the scene, whatever its size
//! (a sample every so many pixels across and down: 4 at 1920 x 1080, 8 at 3840 x 2160).
constexpr float EXPOSURE_SAMPLES = 131072.0f;
constexpr UINT EXPOSURE_WORDS_BYTES = XR_EXPOSURE_WORDS * sizeof(uint32);
//! A depth buffer that a compute kernel reads.
constexpr D3D12_RESOURCE_STATES DEPTH_READ_COMPUTE = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

const char *const s_ToneMapNames[] = { "clamp", "soft shoulder", "ACES fitted", "AgX", "neutral" };

}

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

void CD3D12Renderer::BeginMainScene()
{
  // r_HDRRendering is read every frame: it can change at any time.
  const bool bHdr = IsHdrOn();
  // Off: all of it, also when only the meter and the upscaler's light were made (d3d12_UpscaleHdr with d3d12_UpscaleDebug, no
  // resolve).
  if (!bHdr && (m_Hdr.nWidth || m_Hdr.nMeterWidth || m_Hdr.pLight || m_Hdr.pExposureTarget || m_Hdr.pDepth || m_pDevice->GetHdrColor()))
    ReleaseHdr();
  // The sky mask's depth (d3d12_HDRDebug 3) only while it is shown: the depth buffer it was of stays with it after a change of
  // the size.
  if (CV_d3d12_HDRDebug != 3 && m_Hdr.pDepth)
    ReleaseHdrDepth();
  m_bHdrScene = bHdr;
  NoteCameraCut();
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
      // Made anew with the depth buffer (a new size).
      ReleaseHdrDepth();
    }
    if (m_Hdr.nHdrColorSRV >= 0)
      m_pDevice->BeginHdr();
  }
  else if (m_Hdr.pHdrColor)
    ReleaseHdrColor();        // an upscaler's scene targets instead
  m_bHdrScene = m_pDevice->GetMainColorFormat() == D3D12_HDR_FORMAT;
  // d3d12_AA's MSAA in the main view's place (Msaa.cpp).
  BeginMsaa();
}

void CD3D12Renderer::NotePbrReflView()
{
  if (m_nPbrReflFrame == m_nFrames || !m_pDevice)
    return;
  m_nPbrReflFrame = m_nFrames;
  // As MakeFrameConstants' main view.
  const bool bScene = m_pDevice->IsSceneBound();
  float sx = 1, sy = 1;
  if (bScene)
    GetSceneScale(sx, sy);
  const float w = (float)std::max(m_Viewport[2], 1) * sx, h = (float)std::max(m_Viewport[3], 1) * sy;
  m_PbrReflViewProj = m_CameraView * m_ProjMatrix;
  Set4(m_PbrReflParams, bScene ? 2 * m_UpscaleJitter[0] / w : 0.0f, bScene ? -2 * m_UpscaleJitter[1] / h : 0.0f, m_ProjMatrix.data[10],
       m_ProjMatrix.data[14]);
  m_PbrReflSize[0] = (int)w;
  m_PbrReflSize[1] = (int)h;
}

bool CD3D12Renderer::WantsSceneCopyForReflections() const
{
  return CV_d3d12_RTPbrScreen && m_nPbrReflFrame == m_nFrames && m_nReflCopyFrame != m_nFrames && m_pDevice && m_pDevice->IsInFrame();
}

void CD3D12Renderer::CopySceneForReflections()
{
  if (!WantsSceneCopyForReflections() || !EnsureSceneCopy(m_ReflCopy))
    return;
  m_pDevice->CopyTargets(m_ReflCopy.pColor.Get(), m_ReflCopy.pDepth.Get());
  // The view as the reflecting draws had it (NotePbrReflView: the projection here may be another's, the weapon's).
  m_ReflCopyViewProj = m_PbrReflViewProj;
  memcpy(m_ReflCopyParams, m_PbrReflParams, sizeof(m_ReflCopyParams));
  m_ReflCopySize[0] = m_PbrReflSize[0];
  m_ReflCopySize[1] = m_PbrReflSize[1];
  m_nReflCopyFrame = m_nFrames;
  // A draw after it this frame: not this frame's own picture.
  InvalidateFrameConstants();
}

void CD3D12Renderer::EndMainScene()
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  // The path tracer's G-buffer, if a list left it bound.
  EndPathTargets();
  // MSAA's samples into the main view's targets (Msaa.cpp).
  EndMsaa();
  // d3d12_RTPbrScreen: the picture for the next frame's reflections by rays.
  CopySceneForReflections();
  if (m_bUpscaling)
    UpscaleScene();
  else if (m_pDevice && m_pDevice->IsInHdr())
  {
    GpuSection("hdr");
    m_pDevice->EndHdr();
    InvalidateFrameConstants();
    // The volumetric light's froxels (Volumetric.cpp), which the resolve takes.
    if (UpdateVolumetricLight(m_pDevice->GetMainDepth(), m_pDevice->GetMainDepthSRVFormat()))
      GpuSection("hdr");
    ResolveHdr(m_Hdr.nHdrColorSRV, m_Hdr.pHdrColor.Get(), m_pDevice->GetWidth(), m_pDevice->GetHeight(), false, m_pDevice->GetMainDepth(),
               m_pDevice->GetMainDepthSRVFormat());
  }
  if (m_bHdrScene && m_pFrameDump)
    DumpHdr();
  m_bHdrScene = false;
}

int CD3D12Renderer::HdrDepthSRV(ID3D12Resource *pDepth, DXGI_FORMAT DepthFormat)
{
  if (pDepth != m_Hdr.pDepth.Get())
  {
    if (m_Hdr.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_Hdr.nDepthSRV);
    m_Hdr.pDepth = pDepth;
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = DepthFormat;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2D.MipLevels = 1;
    m_Hdr.nDepthSRV = pDepth ? m_pDescriptors->AllocSRV(pDepth, &SRV) : -1;
  }
  return m_Hdr.nDepthSRV;
}

bool CD3D12Renderer::MeterHdr(int nSceneSRV, ID3D12Resource *pScene, int nWidth, int nHeight, ID3D12Resource *pDepth, DXGI_FORMAT DepthFormat,
                              bool bDepthReadable)
{
  if (nSceneSRV < 0 || !EnsureMeterTargets(nWidth, nHeight))
    return false;
  const bool bHeat = m_bHeatVision;
  const bool bAuto = CV_d3d12_AutoExposure != 0;
  const float fComp = bHeat ? 0.0f : CV_d3d12_Exposure;
  const float fFrameTime = iTimer ? std::clamp(iTimer->GetFrameTime(), 0.0f, 1.0f) : 0.02f;

  // The meter and the adaptation; none in heat vision (the exposure stays as it was). Without a
  // frame just before (the menu, a load) or after the automatic exposure was off, the exposure
  // is the meter's at once.
  if (!bAuto || m_Hdr.nMeteredFrame != m_nFrames - 1)
    m_Hdr.bAdapted = false;
  m_Hdr.nMeteredFrame = m_nFrames;
  m_Hdr.bExposureHistogram = false;
  if (bAuto && !bHeat)
  {
    // The meter: the histogram's target (d3d12_ExposureMeter 1), else (or without its kernels) the mean of the levels.
    m_Hdr.bExposureHistogram = CV_d3d12_ExposureMeter > 0 && DispatchExposure(pScene, nWidth, nHeight, pDepth, DepthFormat, bDepthReadable);
    int nSrcSRV = nSceneSRV;
    for (size_t i = 0; !m_Hdr.bExposureHistogram && i < m_Hdr.Meter.size(); i++)
    {
      CD3D12Texture *pDst = m_Hdr.Meter[i];
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = i == 0 ? HDR_LUMINANCE : HDR_REDUCE;
      dc.nTex0 = (uint32)nSrcSRV;
      Set4(dc.vParams0, 1.0f / pDst->m_nWidth, 1.0f / pDst->m_nHeight, (float)(i == 0 ? METER_TAPS : (METER_REDUCE + 1) / 2), 0);
      if (i > 0 || !DispatchMeter(pScene, dc))
        DrawHdrPass(pDst, dc);
      nSrcSRV = pDst->m_nSRV;
    }
    const float fRate = std::max(CV_d3d12_ExposureSpeed, 0.0f) * fFrameTime;
    SDrawConstants dc = DefaultDrawConstants();
    dc.nFlags = HDR_ADAPT;
    dc.nTex0 = (uint32)(m_Hdr.bExposureHistogram ? m_Hdr.pExposureTarget->m_nSRV : m_Hdr.Meter.back()->m_nSRV);
    dc.nTex1 = (uint32)m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV;
    Set4(dc.vParams0, HDR_EXPOSURE_KEY_LOG2, std::min(CV_d3d12_ExposureMin, CV_d3d12_ExposureMax), std::max(CV_d3d12_ExposureMin, CV_d3d12_ExposureMax),
         m_Hdr.bAdapted ? 0.0f : 1.0f);
    Set4(dc.vParams1, 1 - expf(-2 * fRate), 1 - expf(-fRate), m_Hdr.bExposureHistogram ? 1.0f : 0.0f, 0);
    m_Hdr.nExposure ^= 1;
    DrawHdrPass(m_Hdr.pExposure[m_Hdr.nExposure], dc);
    m_Hdr.bAdapted = true;
  }
  // The HDR statistics' histogram of the scene (HdrStats.cpp: d3d12_DisplayHDR, d3d12_HDRLog).
  if (!bHeat && WantHdrStats())
    DispatchHdrHistogram(pScene, nWidth, nHeight, fComp, bAuto && m_Hdr.bAdapted, bAuto, CV_d3d12_VolumetricLight > 0);
  return true;
}

CD3D12Texture *CD3D12Renderer::HdrSceneLight(int nSceneSRV, int nWidth, int nHeight, ID3D12Resource *pDepth, DXGI_FORMAT DepthFormat)
{
  if (nSceneSRV < 0 || m_Hdr.nMeteredFrame != m_nFrames || !EnsureSceneLightTargets(nWidth, nHeight))
    return nullptr;
  const bool bHeat = m_bHeatVision;
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = HDR_SCENE_LIGHT;
  dc.nTex0 = (uint32)nSceneSRV;
  Set4(dc.vParams0, 1.0f / nWidth, 1.0f / nHeight, 0, 0);
  // d3d12_HDRDebug 3: the sky mask, from the depth (readable already) instead of the volumetric light's; else this frame's
  // froxels at the depth buffer's depth (Volumetric.cpp), as the resolve takes them without an upscaler.
  const bool bSkyMask = !bHeat && CV_d3d12_HDRDebug == 3 && HdrDepthSRV(pDepth, DepthFormat) >= 0;
  Set4(dc.vParams2, 0, bHeat ? 1.0f : 0.0f, bSkyMask ? 3.0f : 0.0f, 0);
  if (bSkyMask)
    dc.nTex5 = (uint32)m_Hdr.nDepthSRV;
  else if (!bHeat)
    SetVolumetricConstants(dc);
  DrawHdrPass(m_Hdr.pLight, dc);
  // The exposure's factor, which the upscaler gets.
  SDrawConstants ec = DefaultDrawConstants();
  ec.nFlags = HDR_EXPOSURE_SCALE;
  ec.nTex0 = (uint32)m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV;
  Set4(ec.vParams0, 1, 1, 0, 0);
  Set4(ec.vParams1, bHeat ? 0.0f : CV_d3d12_Exposure, CV_d3d12_AutoExposure != 0 && m_Hdr.bAdapted && !bHeat ? 1.0f : 0.0f, 0, 0);
  DrawHdrPass(m_Hdr.pExposureScale, ec);
  m_pDevice->SetBackBufferTargets();
  return m_Hdr.pLight;
}

CD3D12Texture *CD3D12Renderer::ResolveHdr(int nSceneSRV, ID3D12Resource *pScene, int nWidth, int nHeight, bool bToTexture, ID3D12Resource *pDepth,
                                          DXGI_FORMAT DepthFormat, bool bLight)
{
  // The meter first; with bLight (the scene's light upscaled) it took the scene before the upscale.
  const bool bMetered = nSceneSRV >= 0 &&
                        (bLight ? m_Hdr.nMeteredFrame == m_nFrames : MeterHdr(nSceneSRV, pScene, nWidth, nHeight, pDepth, DepthFormat, bToTexture));
  if (!bMetered || !EnsureHdrTargets(nWidth, nHeight, bToTexture))
  {
    // Nothing to tone map with: the scene as it is (clamped by the back buffer).
    if (!bToTexture && nSceneSRV >= 0)
    {
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = HDR_RESOLVE | (bLight ? HDR_LINEAR : 0);
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
  const bool bAdapted = bAuto && m_Hdr.bAdapted && !bHeat;
  const int nExposureSRV = m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV;
  // The scene's light (upscaled) instead of the game's colours.
  const uint32 nLinear = bLight ? HDR_LINEAR : 0;

  // The bloom: down from the scene, then up again, each level added to the one above.
  if (fBloom > 0)
  {
    int nSrcSRV = nSceneSRV, nSrcWidth = nWidth, nSrcHeight = nHeight;
    ID3D12Resource *pSrc = pScene;
    for (size_t i = 0; i < m_Hdr.Bloom.size(); i++)
    {
      CD3D12Texture *pDst = m_Hdr.Bloom[i];
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = HDR_BLOOM_DOWN | (i == 0 ? (HDR_BLOOM_PREFILTER | nLinear) : 0);
      dc.nTex0 = (uint32)nSrcSRV;
      dc.nTex1 = (uint32)nExposureSRV;
      Set4(dc.vParams0, 1.0f / pDst->m_nWidth, 1.0f / pDst->m_nHeight, 1.0f / nSrcWidth, 1.0f / nSrcHeight);
      const float fThreshold = std::max(CV_d3d12_BloomThreshold, 0.0f);
      Set4(dc.vParams1, fComp, bAdapted ? 1.0f : 0.0f, fThreshold, fThreshold * BLOOM_KNEE);
      if (!DispatchBloomLevel(i, pSrc, nSrcWidth, nSrcHeight, dc))
        DrawHdrPass(pDst, dc);
      pSrc = pDst->m_pResource;
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
  dc.nFlags = HDR_RESOLVE | nLinear;
  dc.nTex0 = (uint32)nSceneSRV;
  dc.nTex1 = fBloom > 0 ? (uint32)m_Hdr.Bloom[0]->m_nSRV : D3D12_SRV_WHITE;
  dc.nTex2 = (uint32)nExposureSRV;
  dc.nTex3 = nLutSRV >= 0 ? (uint32)nLutSRV : D3D12_SRV_WHITE;
  // z: the ACES fit's soft toe (d3d12_ToneMapToe; HdrToneMap.hlsli's AcesToe), 0 the fit as it is.
  Set4(dc.vParams0, 1.0f / nWidth, 1.0f / nHeight, ToneMapToe(), 0);
  Set4(dc.vParams1, fComp, bAdapted ? 1.0f : 0.0f, fBloom / m_Hdr.Bloom.size(), (float)std::clamp(CV_d3d12_ToneMap, 0, 4));
  Set4(dc.vParams2, CV_d3d12_ToneMapKnee, bHeat ? 1.0f : 0.0f, (float)CV_d3d12_HDRDebug, nLutSRV >= 0 ? 1.0f : 0.0f);
  SetHighlightConstants(dc);
  dc.vParams3[2] = std::max(CV_d3d12_ToneMapDesaturation, 0.0f);
  // Under the water the eye's white balance (d3d12_WaterUnderAdapt, Underwater.cpp): each channel's factor in vUnderColor.
  UnderBalance(dc.vUnderColor);
  // d3d12_HDRDebug 3, the sky mask: white where the depth buffer is at the far plane (nothing but
  // the sky drawn there), black elsewhere; the depth instead of the volumetric light's. The light
  // upscaled has it already (HdrSceneLight), and the volumetric light.
  const bool bSkyMask = !bHeat && !bLight && CV_d3d12_HDRDebug == 3 && HdrDepthSRV(pDepth, DepthFormat) >= 0;
  if (bSkyMask)
    dc.nTex5 = (uint32)m_Hdr.nDepthSRV;
  // The volumetric light, this frame's froxels at the depth buffer's depth (Volumetric.cpp). The
  // upscaler's scene depth is readable already (CD3D12Device::EndScene); the back buffer's is
  // bound read only meanwhile.
  const bool bVolumetric = !bHeat && !bSkyMask && !bLight && SetVolumetricConstants(dc);
  if (bToTexture)
  {
    DrawHdrPass(m_Hdr.pLdr, dc, 0, m_pDevice->HasLayer() ? m_Hdr.pRatio : nullptr);
    m_pDevice->SetBackBufferTargets();
    return m_Hdr.pLdr;
  }
  if (bVolumetric)
    SetVolumetricDepthRead(true);
  else if (bSkyMask)
    SetDepthRead(pDepth, true);
  DrawHdrToBackBuffer(dc);
  if (bVolumetric)
    SetVolumetricDepthRead(false);
  else if (bSkyMask)
    SetDepthRead(pDepth, false);
  return nullptr;
}

int CD3D12Renderer::GetHdrKernelTable(size_t nPass) const
{
  const size_t nTable = nPass * D3D12_FRAMES + m_pDevice->GetFrameSlot();
  return nTable < m_Hdr.KernelTables.size() ? m_Hdr.KernelTables[nTable] : -1;
}

bool CD3D12Renderer::DispatchMeter(ID3D12Resource *pScene, const SDrawConstants &dc)
{
  CD3D12Texture *pLevel = m_Hdr.Meter[0];
  const size_t nSlot = m_pDevice->GetFrameSlot();
  const int nFirst = nSlot < m_Hdr.MeterTables.size() ? m_Hdr.MeterTables[nSlot] : -1;
  if (!m_pCompute || !pScene || dc.vParams0[2] != METER_TAPS || nFirst < 0)
    return false;
  // The scene, the level.
  m_pDescriptors->WriteSRV(nFirst, pScene, nullptr);
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.Format = D3D12_HDR_FORMAT;
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  m_pDescriptors->WriteUAV(nFirst + CD3D12Compute::COMPUTE_SRVS, pLevel->m_pResource, &UAV);

  SMeterConstants mc = {};
  Set4(mc.vSize, (float)pLevel->m_nWidth, (float)pLevel->m_nHeight, dc.vParams0[0], dc.vParams0[1]);
  mc.vTaps[0] = METER_TAPS;
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(mc), 256);
  memcpy(CB.pCPU, &mc, sizeof(mc));
  m_pDevice->EndOffscreen();
  // The level's state kept by the device (an upload's render target): written, then read by the next level's pass.
  m_pDevice->Require(pLevel->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  // 2 x 2 texels a group.
  m_pCompute->Dispatch(m_pDevice->GetCmdList(), eD3D12K_Meter, CB.nGPU, nFirst, nFirst + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)(pLevel->m_nWidth + 1) / 2, (UINT)(pLevel->m_nHeight + 1) / 2);
  m_pDevice->Require(pLevel->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  return true;
}

bool CD3D12Renderer::DispatchExposure(ID3D12Resource *pScene, int nWidth, int nHeight, ID3D12Resource *pDepth, DXGI_FORMAT DepthFormat,
                                      bool bDepthReadable)
{
  if (!pScene || nWidth <= 0 || nHeight <= 0 || !EnsureExposureTargets())
    return false;
  // The scene, the depth (or none: no sky); the words and the target are in the table already.
  const int nTable = m_Hdr.ExposureTables[m_pDevice->GetFrameSlot()];
  m_pDescriptors->WriteSRV(nTable, pScene, nullptr);
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Format = pDepth ? DepthFormat : D3D12_HDR_FORMAT;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture2D.MipLevels = 1;
  m_pDescriptors->WriteSRV(nTable + 1, pDepth, &SRV);

  SExposureConstants ec = {};
  Set4(ec.vSize, (float)nWidth, (float)nHeight, 1.0f / nWidth, 1.0f / nHeight);
  const float fLow = std::clamp(CV_d3d12_ExposureLow, 0.0f, 1.0f), fHigh = std::clamp(CV_d3d12_ExposureHigh, 0.0f, 1.0f);
  Set4(ec.vParams, HDR_EXPOSURE_KEY_LOG2, std::min(fLow, fHigh), std::max(fLow, fHigh), std::clamp(CV_d3d12_ExposureHighlights, 0.0f, 1.0f));
  // The knee as light: the display value decoded (HdrDecode); the clamp's is white. From 0.01, not the tone map's 0 (Hdr.hlsl,
  // as the statistics take it): its log2; below, the light of the knee is under 4e-5 of white either way.
  const float fKnee = std::clamp(CV_d3d12_ToneMapKnee, 0.01f, 0.99f);
  Set4(ec.vParams1, CV_d3d12_ToneMap == 0 ? 0.0f : 2.2f * log2f(fKnee), pDepth ? 1.0f : 0.0f, 0, 0);
  const UINT nStride = (UINT)std::max(lroundf(sqrtf((float)nWidth * nHeight / EXPOSURE_SAMPLES)), 1L);
  ec.vStride[0] = nStride;
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(ec), 256);
  memcpy(CB.pCPU, &ec, sizeof(ec));

  m_pDevice->EndOffscreen();
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  ID3D12Resource *pTarget = m_Hdr.pExposureTarget->m_pResource;
  // The depth buffer's and the target's states kept by the device: the depth buffer read here stays so (the next draw
  // bound to it gets it back), the target written, then read by the adaptation's pass. The words are not tracked.
  if (pDepth && !bDepthReadable)
    m_pDevice->Require(pDepth, DEPTH_READ_COMPUTE);
  m_pDevice->Require(pTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  // 16 x 16 samples a group, then one group.
  const UINT nGroupPixels = 16 * nStride;
  m_pCompute->Dispatch(pList, eD3D12K_ExposureHistogram, CB.nGPU, nTable, nTable + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)(nWidth + nGroupPixels - 1) / nGroupPixels, (UINT)(nHeight + nGroupPixels - 1) / nGroupPixels);
  m_pDevice->UavBarrier(m_Hdr.pExposureWords.Get());
  m_pCompute->Dispatch(pList, eD3D12K_ExposureTarget, CB.nGPU, nTable, nTable + CD3D12Compute::COMPUTE_SRVS, 1, 1);
  m_pDevice->UavBarrier(m_Hdr.pExposureWords.Get());
  m_pDevice->Require(pTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  return true;
}

bool CD3D12Renderer::EnsureExposureTargets()
{
  if (m_Hdr.pExposureWords)
    return true;
  if (m_Hdr.bExposureFailed || !m_pCompute || !m_pUpload || !m_pDevice->IsInFrame())
    return false;
  ID3D12Device *pDevice = m_pDevice->GetDevice();
  const D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = EXPOSURE_WORDS_BYTES;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  bool bOk = m_pCompute->HasKernel(eD3D12K_ExposureHistogram) && m_pCompute->HasKernel(eD3D12K_ExposureTarget) &&
             SUCCEEDED(pDevice->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                        IID_PPV_ARGS(&m_Hdr.pExposureWords)));
  m_Hdr.pExposureTarget = bOk ? m_pUpload->CreateRenderTarget(1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, true) : nullptr;
  bOk = bOk && m_Hdr.pExposureTarget && m_Hdr.pExposureTarget->m_nSRV >= 0;
  // The tables: t0 the scene and t1 the depth each frame, u0 the words, u1 the target; the views the kernels do not use null.
  D3D12_SHADER_RESOURCE_VIEW_DESC NullSRV = {};
  NullSRV.Format = D3D12_HDR_FORMAT;
  NullSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  NullSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  NullSRV.Texture2D.MipLevels = 1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC NullUAV = {};
  NullUAV.Format = D3D12_HDR_FORMAT;
  NullUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  D3D12_UNORDERED_ACCESS_VIEW_DESC WordsUAV = {};
  WordsUAV.Format = DXGI_FORMAT_R32_TYPELESS;
  WordsUAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  WordsUAV.Buffer.NumElements = XR_EXPOSURE_WORDS;
  WordsUAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC TargetUAV = {};
  TargetUAV.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  TargetUAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  m_Hdr.ExposureTables.assign(D3D12_FRAMES, -1);
  for (int &nTable : m_Hdr.ExposureTables)
  {
    if (!bOk || (nTable = m_pDescriptors->AllocRange(KERNEL_TABLE)) < 0)
    {
      bOk = false;
      continue;
    }
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
      m_pDescriptors->WriteSRV(nTable + i, nullptr, &NullSRV);
    m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS, m_Hdr.pExposureWords.Get(), &WordsUAV);
    m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + 1, m_Hdr.pExposureTarget->m_pResource, &TargetUAV);
    for (int i = 2; i < CD3D12Compute::COMPUTE_UAVS; i++)
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, nullptr, &NullUAV);
  }
  // The words zero (a copy of zeros from the frame memory: a buffer is created common, the copy promotes it), then the
  // kernels' for good.
  const SD3D12FrameAlloc Zeros = bOk ? m_pUpload->AllocFrame(EXPOSURE_WORDS_BYTES, 256) : SD3D12FrameAlloc{};
  if (!bOk || !Zeros.pCPU)
  {
    iLog->LogWarning("XRenderD3D12: no histogram for the automatic exposure (d3d12_ExposureMeter 1): no buffer, target or kernels; the mean");
    ReleaseExposureTargets();
    m_Hdr.bExposureFailed = true;
    return false;
  }
  m_Hdr.pExposureWords->SetName(L"XRenderD3D12 exposure histogram");
  memset(Zeros.pCPU, 0, EXPOSURE_WORDS_BYTES);
  m_pDevice->EndOffscreen();
  m_pDevice->CopyBufferRegion(m_Hdr.pExposureWords.Get(), 0, Zeros.pResource, Zeros.nOffset, EXPOSURE_WORDS_BYTES);
  m_pDevice->Transition(m_Hdr.pExposureWords.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return true;
}

void CD3D12Renderer::ReleaseExposureTargets()
{
  if (m_pDescriptors)
  {
    for (int nTable : m_Hdr.ExposureTables)
    {
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, KERNEL_TABLE);
    }
  }
  m_Hdr.ExposureTables.clear();
  if (m_pDevice && m_Hdr.pExposureWords)
    m_pDevice->DeferredRelease(m_Hdr.pExposureWords);
  m_Hdr.pExposureWords.Reset();
  if (m_pUpload)
    m_pUpload->ReleaseTexture(m_Hdr.pExposureTarget);
  m_Hdr.pExposureTarget = nullptr;
  m_Hdr.bExposureHistogram = false;
}

bool CD3D12Renderer::DispatchBloomLevel(size_t nLevel, ID3D12Resource *pSource, int nSourceWidth, int nSourceHeight, const SDrawConstants &dc)
{
  // Its taps at the centres and corners of its texels: where the level before is twice its size.
  CD3D12Texture *pLevel = m_Hdr.Bloom[nLevel];
  const int nFirst = GetHdrKernelTable(nLevel);
  if (!m_pCompute || !pSource || nSourceWidth != 2 * pLevel->m_nWidth || nSourceHeight != 2 * pLevel->m_nHeight || nFirst < 0)
    return false;
  // The level before, the EV, the level.
  const bool bScene = (dc.nFlags & HDR_BLOOM_PREFILTER) != 0;
  ID3D12Resource *pExposure = m_Hdr.pExposure[m_Hdr.nExposure]->m_pResource;
  m_pDescriptors->WriteSRV(nFirst, pSource, nullptr);
  m_pDescriptors->WriteSRV(nFirst + 1, pExposure, nullptr);
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.Format = D3D12_HDR_FORMAT;
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  m_pDescriptors->WriteUAV(nFirst + CD3D12Compute::COMPUTE_SRVS, pLevel->m_pResource, &UAV);

  SBloomConstants bc = {};
  Set4(bc.vSize, (float)pLevel->m_nWidth, (float)pLevel->m_nHeight, dc.vParams0[0], dc.vParams0[1]);
  memcpy(bc.vParams, dc.vParams1, sizeof(bc.vParams));
  bc.vFlags[0] = bScene ? 1 : 0;
  bc.vFlags[1] = bScene && (dc.nFlags & HDR_LINEAR) ? 1 : 0;
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(bc), 256);
  memcpy(CB.pCPU, &bc, sizeof(bc));
  // The pass before drew into the EV or the level before.
  m_pDevice->EndOffscreen();
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  // The levels' states kept by the device (EnsureHdrTargets): the level before readable by this kernel and by the pass
  // that adds this one to it (one transition, not back to PIXEL_SHADER_RESOURCE after), this one written, then readable
  // by the passes; the device's offscreen targets ask for theirs too. The EV (an upload's render target) readable by this
  // kernel and the passes after it.
  ID3D12Resource *pLevelRes = pLevel->m_pResource;
  if (bScene)
    m_pDevice->Require(pExposure, READ);
  else
    m_pDevice->Require(pSource, READ);
  m_pDevice->Require(pLevelRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(m_pDevice->GetCmdList(), eD3D12K_Bloom, CB.nGPU, nFirst, nFirst + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)(pLevel->m_nWidth + 7) / 8, (UINT)(pLevel->m_nHeight + 7) / 8);
  m_pDevice->Require(pLevelRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  return true;
}

void CD3D12Renderer::DrawHdrToBackBuffer(const SDrawConstants &dc)
{
  m_pDevice->SetBackBufferTargets();
  if (!m_pDevice->HasLayer())
  {
    DrawHdrFullScreen(dc, 0, PIPELINE_COLOR_DEFAULT);
    return;
  }
  m_pDevice->BindHighlight(true);
  DrawHdrFullScreen(dc, 4, PIPELINE_COLOR_DEFAULT);
  m_pDevice->BindHighlight(false);
}

void CD3D12Renderer::DrawHdrFullScreen(const SDrawConstants &dc, uint8 nTarget, uint8 nColorFormat)
{
  // Over the whole back buffer, whatever the engine's viewport and scissors.
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
  Key.nTarget = nTarget;
  Key.nColorFormat = nColorFormat;
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
}

void CD3D12Renderer::DrawHdrPass(CD3D12Texture *pTarget, const SDrawConstants &dc, uint32 nState, CD3D12Texture *pRatio)
{
  m_pDevice->SetOffscreenTargets(pTarget->m_pResource, pTarget->m_RTV, {}, pTarget->m_nWidth, pTarget->m_nHeight);
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Hdr;
  Key.nState = GS_NODEPTHTEST | nState;
  Key.nCull = R_CULL_NONE;
  Key.nTarget = 1;
  Key.nColorFormat = pTarget->m_Format == D3D12_HDR_FORMAT ? PIPELINE_COLOR_HDR :
                     pTarget->m_Format == DXGI_FORMAT_R32_FLOAT ? PIPELINE_COLOR_R32F :
                     pTarget->m_Format == D3D12_HDR10_FORMAT ? PIPELINE_COLOR_HDR10 : PIPELINE_COLOR_DEFAULT;
  if (pRatio)
  {
    // Both targets (readable again when the device unbinds them, EndOffscreen).
    m_pDevice->BindSecondTarget(pRatio->m_pResource, pRatio->m_RTV);
    Key.nTarget = 4;
    Key.nColorFormat = pTarget->m_Format == DXGI_FORMAT_R8G8B8A8_UNORM ? PIPELINE_COLOR_RGBA8 : PIPELINE_COLOR_DEFAULT;
  }
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
}

bool CD3D12Renderer::EnsureMeterTargets(int nWidth, int nHeight)
{
  if (!m_pUpload || nWidth <= 0 || nHeight <= 0)
    return false;
  if (nWidth == m_Hdr.nMeterWidth && nHeight == m_Hdr.nMeterHeight)
    return true;
  ReleaseMeterTargets();
  // The meter's levels, down to 1 x 1 (the kernel writes the first one).
  int w = std::max((nWidth + METER_FACTOR - 1) / METER_FACTOR, 1), h = std::max((nHeight + METER_FACTOR - 1) / METER_FACTOR, 1);
  for (;;)
  {
    m_Hdr.Meter.push_back(m_pUpload->CreateRenderTarget(w, h, D3D12_HDR_FORMAT, m_Hdr.Meter.empty()));
    if (w == 1 && h == 1)
      break;
    w = std::max((w + METER_REDUCE - 1) / METER_REDUCE, 1);
    h = std::max((h + METER_REDUCE - 1) / METER_REDUCE, 1);
  }
  for (CD3D12Texture *&pTex : m_Hdr.pExposure)
    pTex = m_pUpload->CreateRenderTarget(1, 1, DXGI_FORMAT_R32_FLOAT);
  // The first level's kernel's tables, D3D12_FRAMES.
  m_Hdr.MeterTables.assign(D3D12_FRAMES, -1);
  for (int &nTable : m_Hdr.MeterTables)
  {
    if ((nTable = m_pDescriptors->AllocRange(KERNEL_TABLE)) >= 0)
      WriteNullTable(m_pDescriptors.get(), nTable);
  }
  m_Hdr.nMeterWidth = nWidth;
  m_Hdr.nMeterHeight = nHeight;
  m_Hdr.bAdapted = false;
  if (std::any_of(m_Hdr.Meter.begin(), m_Hdr.Meter.end(), BadTarget) || BadTarget(m_Hdr.pExposure[0]) || BadTarget(m_Hdr.pExposure[1]))
  {
    iLog->LogError("XRenderD3D12: no HDR meter targets for %d x %d", nWidth, nHeight);
    ReleaseMeterTargets();
    return false;
  }
  return true;
}

void CD3D12Renderer::ReleaseMeterTargets()
{
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : m_Hdr.Meter)
      m_pUpload->ReleaseTexture(pTex);
    for (CD3D12Texture *pTex : m_Hdr.pExposure)
      m_pUpload->ReleaseTexture(pTex);
  }
  m_Hdr.Meter.clear();
  if (m_pDescriptors)
  {
    for (int nTable : m_Hdr.MeterTables)
    {
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, KERNEL_TABLE);
    }
  }
  m_Hdr.MeterTables.clear();
  m_Hdr.pExposure[0] = m_Hdr.pExposure[1] = nullptr;
  m_Hdr.nMeterWidth = m_Hdr.nMeterHeight = 0;
  m_Hdr.bAdapted = false;
}

bool CD3D12Renderer::EnsureHdrTargets(int nWidth, int nHeight, bool bLdr)
{
  if (!m_pUpload || nWidth <= 0 || nHeight <= 0)
    return false;
  if (nWidth != m_Hdr.nWidth || nHeight != m_Hdr.nHeight)
  {
    ReleaseHdrTargets();
    // The bloom's, while they are at least 2 texels high (the kernel writes them too).
    int w = nWidth, h = nHeight;
    for (int i = 0; i < BLOOM_LEVELS && (h + 1) / 2 >= 2; i++)
    {
      w = std::max((w + 1) / 2, 1);
      h = (h + 1) / 2;
      m_Hdr.Bloom.push_back(m_pUpload->CreateRenderTarget(w, h, D3D12_HDR_FORMAT, true));
      // Made in PIXEL_SHADER_RESOURCE; their states kept by the device (the kernel's and the passes' ask).
      if (m_Hdr.Bloom.back())
        m_pDevice->TrackState(m_Hdr.Bloom.back()->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    // The kernel's tables, D3D12_FRAMES a level.
    m_Hdr.KernelTables.assign(m_Hdr.Bloom.size() * D3D12_FRAMES, -1);
    for (int &nTable : m_Hdr.KernelTables)
    {
      if ((nTable = m_pDescriptors->AllocRange(KERNEL_TABLE)) >= 0)
        WriteNullTable(m_pDescriptors.get(), nTable);
    }
    m_Hdr.nWidth = nWidth;
    m_Hdr.nHeight = nHeight;
    if (m_Hdr.Bloom.empty() || std::any_of(m_Hdr.Bloom.begin(), m_Hdr.Bloom.end(), BadTarget))
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
  // The highlight layer's ratio at the scene's size (HdrOutput.cpp); without it the layer gets none.
  if (bLdr && m_pDevice->HasLayer() && !m_Hdr.pRatio)
  {
    m_Hdr.pRatio = m_pUpload->CreateRenderTarget(nWidth, nHeight, D3D12_HIGHLIGHT_FORMAT);
    if (m_Hdr.pRatio && m_Hdr.pRatio->m_nSRV < 0)
    {
      m_pUpload->ReleaseTexture(m_Hdr.pRatio);
      m_Hdr.pRatio = nullptr;
    }
  }
  return true;
}

void CD3D12Renderer::ReleaseHdrTargets()
{
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : m_Hdr.Bloom)
      m_pUpload->ReleaseTexture(pTex);
    m_pUpload->ReleaseTexture(m_Hdr.pLdr);
    m_pUpload->ReleaseTexture(m_Hdr.pRatio);
  }
  m_Hdr.Bloom.clear();
  if (m_pDescriptors)
  {
    for (int nTable : m_Hdr.KernelTables)
    {
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, KERNEL_TABLE);
    }
  }
  m_Hdr.KernelTables.clear();
  m_Hdr.pLdr = nullptr;
  m_Hdr.pRatio = nullptr;
  m_Hdr.nWidth = m_Hdr.nHeight = 0;
}

bool CD3D12Renderer::EnsureSceneLightTargets(int nWidth, int nHeight)
{
  if (!m_pUpload || nWidth <= 0 || nHeight <= 0)
    return false;
  if (m_Hdr.pLight && (m_Hdr.pLight->m_nWidth != nWidth || m_Hdr.pLight->m_nHeight != nHeight))
    ReleaseSceneLightTargets();
  if (!m_Hdr.pLight)
    m_Hdr.pLight = m_pUpload->CreateRenderTarget(nWidth, nHeight, D3D12_HDR_FORMAT);
  if (!m_Hdr.pExposureScale)
    m_Hdr.pExposureScale = m_pUpload->CreateRenderTarget(1, 1, DXGI_FORMAT_R32_FLOAT, true);  // TAA's resolve reads it as a UAV
  if (BadTarget(m_Hdr.pLight) || BadTarget(m_Hdr.pExposureScale))
  {
    iLog->LogError("XRenderD3D12: no target of %d x %d for the upscaler's HDR light (d3d12_UpscaleHdr); the tone mapped scene", nWidth, nHeight);
    ReleaseSceneLightTargets();
    return false;
  }
  return true;
}

void CD3D12Renderer::ReleaseSceneLightTargets()
{
  if (m_pUpload)
  {
    m_pUpload->ReleaseTexture(m_Hdr.pLight);
    m_pUpload->ReleaseTexture(m_Hdr.pExposureScale);
  }
  m_Hdr.pLight = m_Hdr.pExposureScale = nullptr;
}

void CD3D12Renderer::ReleaseHdrColor()
{
  if (m_pDescriptors && m_Hdr.nHdrColorSRV >= 0)
    m_pDescriptors->FreeSRV(m_Hdr.nHdrColorSRV);
  m_Hdr.nHdrColorSRV = -1;
  m_Hdr.pHdrColor.Reset();
  ReleaseHdrDepth();
  if (m_pDevice)
    m_pDevice->SetHdrTarget(false);
}

void CD3D12Renderer::ReleaseHdrDepth()
{
  if (m_pDescriptors && m_Hdr.nDepthSRV >= 0)
    m_pDescriptors->FreeSRV(m_Hdr.nDepthSRV);
  m_Hdr.nDepthSRV = -1;
  // The GPU may still read it in the frames under way.
  if (m_pDevice && m_Hdr.pDepth)
    m_pDevice->DeferredRelease(m_Hdr.pDepth);
  m_Hdr.pDepth.Reset();
}

void CD3D12Renderer::ReleaseHdr()
{
  ReleaseHdrTargets();
  ReleaseMeterTargets();
  ReleaseSceneLightTargets();
  ReleaseExposureTargets();
  m_Hdr.bExposureFailed = false;
  ReleaseHdrColor();
  ReleaseColorGrading();
}

void CD3D12Renderer::DumpHdrTargets()
{
  if (!m_pFrameDump)
    return;
  char szDepth[64] = "none";
  if (m_Hdr.pDepth)
  {
    const D3D12_RESOURCE_DESC Desc = m_Hdr.pDepth->GetDesc();
    sprintf_s(szDepth, "%llu x %u", (unsigned long long)Desc.Width, Desc.Height);
  }
  fprintf(m_pFrameDump.get(), "hdr targets: resolve %d x %d, meter %d x %d, upscaler's light %s, histogram meter %s, sky mask depth %s, colour %s\n",
          m_Hdr.nWidth, m_Hdr.nHeight, m_Hdr.nMeterWidth, m_Hdr.nMeterHeight,
          m_Hdr.pLight ? "made" : "none", m_Hdr.pExposureTarget ? "made" : "none", szDepth, m_Hdr.pHdrColor ? "made" : "none");
}

void CD3D12Renderer::DumpHdr()
{
  const int nToneMap = std::clamp(CV_d3d12_ToneMap, 0, 4);
  fprintf(m_pFrameDump.get(), "hdr: %d x %d (the meter %d x %d), tone map %s (knee %g, toe %g), exposure %+g EV, auto %d (%g to %g EV, speed %g), bloom %g "
          "(threshold %g)%s\n", m_Hdr.nWidth, m_Hdr.nHeight, m_Hdr.nMeterWidth, m_Hdr.nMeterHeight, s_ToneMapNames[nToneMap], CV_d3d12_ToneMapKnee,
          ToneMapToe(), CV_d3d12_Exposure, CV_d3d12_AutoExposure, CV_d3d12_ExposureMin, CV_d3d12_ExposureMax, CV_d3d12_ExposureSpeed, CV_d3d12_Bloom,
          CV_d3d12_BloomThreshold, m_bHeatVision ? ", heat vision: clamped" : "");
  fprintf(m_pFrameDump.get(), "hdr: meter %d (%s; window %g to %g, sky's highlights %g)\n", CV_d3d12_ExposureMeter,
          m_Hdr.bExposureHistogram ? "histogram" : "mean", CV_d3d12_ExposureLow, CV_d3d12_ExposureHigh, CV_d3d12_ExposureHighlights);
  fprintf(m_pFrameDump.get(), "hdr: brighter than the game: sun %g, sky %g (half way at %g degrees), glow %g; colour grading %s: saturation %g, contrast %g, "
          "temperature %g, tint %g, lut \"%s\" (%d, amount %g)\n", CV_d3d12_HDRSun, CV_d3d12_HDRSky, CV_d3d12_HDRSkyAngle, CV_d3d12_HDRGlow,
          m_Grading.pLut ? "on" : "off", m_Grading.fSaturation,
          m_Grading.fContrast, m_Grading.fTemperature, m_Grading.fTint, m_Grading.LutFile.c_str(), m_Grading.nCubeSize, m_Grading.fLutAmount);
  // The meter's mean and the adapted EV, read back (this frame is executed so far).
  if (!m_Hdr.bAdapted || m_Hdr.Meter.empty())
    return;
  float fEV = 0;
  if (m_Hdr.bExposureHistogram)
  {
    float Target[4] = {};
    if (m_pDevice->ReadBackTexture(m_Hdr.pExposureTarget->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)Target, 1, 1) &&
        m_pDevice->ReadBackTexture(m_Hdr.pExposure[m_Hdr.nExposure]->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)&fEV, 1, 1))
      fprintf(m_pFrameDump.get(), "hdr: histogram mean log2 luminance %.3f, target %+.3f EV, sky %.1f %% (ceiling %+.3f EV), adapted %+.3f EV\n",
              Target[1], Target[0], 100 * Target[3], Target[2], fEV);
    return;
  }
  uint16 Meter[4] = {};
  if (m_pDevice->ReadBackTexture(m_Hdr.Meter.back()->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)Meter, 1, 1) &&
      m_pDevice->ReadBackTexture(m_Hdr.pExposure[m_Hdr.nExposure]->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, (uint8 *)&fEV, 1, 1))
  {
    const float fWeight = HalfToFloat(Meter[1]);
    const float fMeanLog = fWeight > 0 ? HalfToFloat(Meter[0]) / fWeight : 0.0f;
    fprintf(m_pFrameDump.get(), "hdr: meter mean log2 luminance %.3f (target %+.3f EV), adapted %+.3f EV\n", fMeanLog, HDR_EXPOSURE_KEY_LOG2 - fMeanLog, fEV);
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
  // HdrEncode (HdrLight.hlsli)
  return fLight > 1 ? 1 + (fLight - 1) / 2.2f : powf(std::max(fLight, 0.0f), 1.0f / 2.2f);
}

void CD3D12Renderer::SetHdrCore(SDrawConstants &dc, float fLight, uint32 nState, float fHue) const
{
  if ((fLight == 1 && fHue <= 0) || !IsHdrTarget())
    return;
  dc.nFlags2 |= DF2_HDR_CORE;
  Set4(dc.vHdrCore, std::max(fLight, 0.0f), (nState & GS_BLSRC_MASK) == GS_BLSRC_SRCALPHA ? 1.0f : 0.0f, 0, std::max(fHue, 0.0f));
}

float CD3D12Renderer::HdrGlowHue() const
{
  if (!IsHdrTarget() || CV_d3d12_HDRGlowHue <= 0)
    return 0.0f;
  // With the HDR output the highlights go on up to the display's peak: as many times more, the peak over paper white.
  const float fPeak = m_pDevice->HasLayer() && m_HdrOut.fPaperWhite > 0 ? std::max(m_HdrOut.fPeak / m_HdrOut.fPaperWhite, 1.0f) : 1.0f;
  return std::max(CV_d3d12_HDRGlowHue, 0.25f) * fPeak;
}

void CD3D12Renderer::HdrExposureRef(float &fTex, float &fComp) const
{
  fTex = fComp = 0;
  if (!IsHdrTarget())
    return;
  fComp = CV_d3d12_Exposure;
  if (CV_d3d12_AutoExposure && m_Hdr.bAdapted && m_Hdr.pExposure[m_Hdr.nExposure])
    fTex = (float)(m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV + 1);
}

float CD3D12Renderer::HdrSkyGlow() const
{
  if (!IsHdrTarget() || !m_pScene || !m_pScene->GetSun())
    return 1.0f;
  return std::max(CV_d3d12_HDRSky, 0.0f);
}

float CD3D12Renderer::HdrSkyGlowPower() const
{
  // cos(a)^n is a half at the angle a: n = ln 0.5 / ln cos a. 0: the lobe up to step 257, 32 (half
  // way at 11.9 degrees).
  if (CV_d3d12_HDRSkyAngle <= 0)
    return 32.0f;
  const double fAngle = std::clamp((double)CV_d3d12_HDRSkyAngle, 0.25, 60.0) * 3.14159265358979323846 / 180;
  return (float)(std::log(0.5) / std::log(std::cos(fAngle)));
}
