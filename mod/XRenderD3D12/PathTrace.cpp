// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathTrace.cpp
//	Description: The path tracer of the main view's lighting
//	(d3d12_PathTracing, on a device with DXR 1.1).
//
//	The lights: once a frame, before the main view's opaque list, the
//	lights besides the sun become SPTLight in per frame memory
//	(Shaders/RayConstants.h), bound to the kernels with rays as the root SRV
//	t2 space5 (CD3D12Compute::ROOT_RAY_LIGHTS) and to the programs with rays
//	(ROOT_RAY_LIGHTS of D3D12Pipeline.h): the point lights, the directional
//	ones and the projectors with their cube. They are the scene's lights of
//	the frame and the level's static lights (I3DEngine::GetStaticLightSources,
//	the lamps of the light maps: the frame has only those whose sphere
//	reaches the view, one or two of a hall's), as the path tracer lights in
//	the light maps' place; a static light the frame has too is the frame's
//	(its colour animated). The nearest first (the distance to their sphere),
//	those within d3d12_PTLightDistance, XR_PT_MAX_LIGHTS at most.
//	Not among them: the fake lights (DLF_FAKE: flares, beams; never in the
//	scene's list) but those the light maps were baked with, a heat source
//	that gives no light (heat vision), the characters' fill
//	lights (DLF_AMBIENT_LIGHT) without d3d12_PTFillLights: those are in
//	their ambient light, which the paths fill in with
//	(CD3D12Renderer::SetPathFillLights, General.hlsl XR_PT).
//	The materials: the scene for rays' instances have them (SRayInstance
//	vAlbedo, vSpecular, nPath; DrawLeafItem), Shaders/PathShade.hlsli
//	shades its hits; while the path tracer is on the vegetation's vertex
//	colours are baked without light (Geometry::SetUnlitVegetation).
//	d3d12_RTDebug 8 to 10 show the hits as it takes them (RayDebug.hlsl).
//	The G-buffer (Shaders/PathGBuffer.hlsli): the main view's opaque list
//	writes it beside the colour with the general program's XR_PT
//	permutation (BeginPathTargets .. EndPathTargets; DrawLeafItem takes the
//	light off its lit draws, D3D12Pipeline the programs and the blending of
//	the targets), at the scene's size; d3d12_PTDebug shows it.
//	DrawPathTrace, after the opaque list: the kernels of
//	Shaders/PathTrace.hlsl (CSTrace the paths, CSTemporal their history,
//	CSFilter the denoise, CSSpecular the highlights of the sun and the
//	lamps, d3d12_PTSpecular) with the depth buffer and the G-buffer, then
//	the composite (Shaders/PathComposite.hlsl) adds their light, the sun's
//	and the highlights to the colour. d3d12_PathTracing 2, the reference,
//	adds up its frames while the view and the settings stay.
//	d3d12_PTDenoiser nrd (the default when NVIDIA NRD is built in,
//	NrdHost.h): CSPack (Shaders/PathNrd.hlsl) makes NRD's inputs from the
//	paths, the G-buffer's normal and the scene's motion vectors, NRD's
//	REBLUR_DIFFUSE denoises them, CSResolve makes its light the history
//	CSFilter takes without its own filter (it still finds the terrain's
//	baked light). Without NRD, or when it fails, simple.
//	All of it waits for r_HDRRendering: its light is beyond an LDR target.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "RayTracing.h"
#include "Scene.h"
#include "Texture.h"
#include "Geometry.h"
#include "NrdHost.h"
#include <list2.h>

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! As HdrDecode of Shaders/HdrLight.hlsli: the game's colour as light.
float HdrDecode(float c)
{
  return c > 1 ? 1 + 2.2f * (c - 1) : powf(std::max(c, 0.0f), 2.2f);
}

//! As PathMultiplier of Shaders/PathShade.hlsli: the game's light (what its colours are multiplied by) as light.
float PathMultiplier(float c)
{
  return powf(std::max(c, 0.0f), 2.2f);
}

float Luminance(float r, float g, float b)
{
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

//! The G-buffer between the main view's opaque list and the next one: read by the pixel shaders and the kernels.
constexpr D3D12_RESOURCE_STATES PATH_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
//! The kernels' textures between their dispatches; the depth buffer while they read it.
constexpr D3D12_RESOURCE_STATES PATH_KERNEL_READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES PATH_DEPTH_READ = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
//! The paths' light and the composite's (rgb, w the view depth or the terrain's share of its baked light), the history (the reference
//! adds up thousands of frames), its frames (/ 65535).
constexpr DXGI_FORMAT PATH_LIGHT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT PATH_HISTORY_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr DXGI_FORMAT PATH_FRAMES_FORMAT = DXGI_FORMAT_R16_UNORM;
//! The frames the real time history holds at most; a sample's luminance at most (its light, linear) in real time.
constexpr float PATH_HISTORY_FRAMES = 32;
constexpr float PATH_SAMPLE_MAX = 16;
//! A bounce's alpha tests at most in real time (the reference: XR_RAY_MAX_ALPHA_TESTS).
constexpr uint32 PATH_BOUNCE_TESTS = 4;
//! The radiance cache (Shaders/PathCache.hlsli): its cells' size near the camera (m) and per metre of distance, the frames
//! its light holds at most, a lookup's jitter (cells), CSCacheUpdate's tile (a path for each of these x these pixels).
constexpr float PATH_CACHE_CELL = 0.25f;
constexpr float PATH_CACHE_CELL_PER_METRE = 0.02f;
constexpr float PATH_CACHE_FRAMES = 32;
constexpr float PATH_CACHE_JITTER = 1;
constexpr uint32 PATH_CACHE_TILE = 8;
//! The kernels' UAV tables (D3D12Compute.h): the paths', the history's of each texture, the composite's light, the
//! highlights.
enum { PATH_TABLE_TRACE, PATH_TABLE_TEMPORAL0, PATH_TABLE_TEMPORAL1, PATH_TABLE_FILTER, PATH_TABLE_SPECULAR, PATH_TABLES };
//! The first bounce's hit distance (CSTrace's u2, NRD's), NRD's inputs (CSPack's u0..u3: IN_DIFF_RADIANCE_HITDIST,
//! IN_NORMAL_ROUGHNESS, IN_VIEWZ, IN_MV) and its light.
constexpr DXGI_FORMAT PATH_HIT_FORMAT = DXGI_FORMAT_R16_FLOAT;
constexpr DXGI_FORMAT PATH_NRD_FORMATS[4] = { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R32_FLOAT,
                                              DXGI_FORMAT_R16G16_FLOAT };
constexpr DXGI_FORMAT PATH_NRD_OUT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
//! NRD's denoising range (m; beyond the view's far) and the view depth CSPack gives it where there is no surface: beyond
//! it, within FP16.
constexpr float PATH_NRD_RANGE = 30000;
constexpr float PATH_NRD_NO_SURFACE = 60000;

D3D12_SHADER_RESOURCE_VIEW_DESC PathTextureSRV(DXGI_FORMAT Format)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Format = Format;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.Texture2D.MipLevels = 1;
  return SRV;
}

const char *LightKind(uint32 nKind)
{
  switch (nKind)
  {
  case XR_PT_LIGHT_POINT:
    return "point";
  case XR_PT_LIGHT_DIRECTIONAL:
    return "directional";
  case XR_PT_LIGHT_PROJECTOR:
    return "projector";
  default:
    return "?";
  }
}

}

bool CD3D12Renderer::WantsPathTracing() const
{
  return m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_PathTracing > 0 && IsHdrOn();
}

bool CD3D12Renderer::WantsPathDebug() const
{
  return m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_RTDebug >= XR_RAY_DEBUG_PT_ALBEDO && CV_d3d12_RTDebug <= XR_RAY_DEBUG_PT_DIRECT;
}

void CD3D12Renderer::GetPathShade(SPathShadeParams &P) const
{
  // The sun: twice its colour on a face towards it, as the raster lights; the sky: the level's outdoor ambient. Neither
  // where the frame has no sun (indoors: what keeps the sky out is beyond the scene for rays, as the roof above).
  const bool bOutdoors = m_pScene && m_pScene->GetSun();
  const float fSun = bOutdoors ? std::max(CV_d3d12_PTSun, 0.0f) : 0.0f, fSky = bOutdoors ? std::max(CV_d3d12_PTSky, 0.0f) : 0.0f;
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  const Vec3 vOutdoor = p3D ? p3D->GetOutdoorAmbientColor() : Vec3(0.5f, 0.5f, 0.5f);
  for (int c = 0; c < 3; c++)
  {
    P.vSun[c] = PathMultiplier(2 * m_SunColor[c]) * fSun;
    P.vSky[c] = HdrDecode((&vOutdoor.x)[c]) * fSky;
  }
  P.fLamps = std::max(CV_d3d12_PTLamps, 0.0f);
  P.fEmission = std::max(CV_d3d12_PTEmission, 0.0f);
  P.fDelight = CV_d3d12_PTTerrainDelight;
  // The editor's light on a lit face of the terrain towards the sun, as the path tracer's own: the sun and the open sky
  // (its texture there lit as the path tracer lights it), each channel: the sector's colour has that light's colour too
  // (Rebellion's blue moon), the albedo is without it. Its luminance at least 0.05, a channel at least a tenth of it.
  float vOpen[3];
  for (int c = 0; c < 3; c++)
    vOpen[c] = P.vSun[c] + P.vSky[c];
  const float fOpen = Luminance(vOpen[0], vOpen[1], vOpen[2]), fBake = std::max(fOpen, 0.05f);
  for (int c = 0; c < 3; c++)
    P.vBakeLight[c] = fOpen > 0 ? std::max(vOpen[c] / fOpen, 0.1f) * fBake : fBake;
  P.fLightGap = std::max(CV_d3d12_RTLightShadowGap, 0.0f);
  P.fBakedFalloff = std::max(CV_d3d12_PTBakedFalloff, 0.0f);
  P.nLights = m_PathTrace.nLightsFrame == m_nFrames ? m_PathTrace.nLightCount : 0;
}

void CD3D12Renderer::UpdatePathLights(std::span<CDLight *const> Lights)
{
  // Its light is beyond the colours of an LDR target: none without r_HDRRendering (once in the log).
  const bool bNoHdr = m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_PathTracing > 0 && !IsHdrOn();
  if (bNoHdr && !m_PathTrace.bNoHdrLogged)
    iLog->LogWarning("XRenderD3D12: d3d12_PathTracing waits for r_HDRRendering 1");
  m_PathTrace.bNoHdrLogged = bNoHdr;
  // The vegetation without the light baked into its vertex colours while the path tracer lights it.
  Geometry::SetUnlitVegetation(WantsPathTracing());
  if (!(WantsPathTracing() || WantsPathDebug()) || !m_pUpload || m_PathTrace.nLightsFrame == m_nFrames)
    return;
  m_PathTrace.nLightsFrame = m_nFrames;
  m_PathTrace.nLights = 0;
  m_PathTrace.nLightCount = 0;

  // The candidates: the frame's lights, then the level's static ones the frame does not have.
  struct SCandidate
  {
    const CDLight *pLight;
    uint32 nScene;      //!< its index in the frame's lights, XR_RAY_NONE for a static light alone
    float fKey;         //!< the distance from the camera to its sphere (0 inside it, directional lights 0)
  };
  std::vector<SCandidate> Candidates;
  int nSkippedHeat = 0, nSkippedFake = 0, nSkippedFill = 0, nSkippedImage = 0, nStatic = 0, nStaticFrame = 0;
  const Vec3 vCamera = m_Camera.GetPos();
  const auto Accept = [&](const CDLight *pLight, uint32 nScene)
  {
    if (!pLight || (pLight->m_Flags & DLF_SUN))
      return;
    const uint32 nFlags = (uint32)pLight->m_Flags;
    // A heat source alone gives no light (heat vision, ItemHeat), as SetObjectLights.
    if ((nFlags & (DLF_HEATSOURCE | DLF_LIGHTSOURCE)) == DLF_HEATSOURCE)
    {
      nSkippedHeat++;
      return;
    }
    // A fake light (a flare, a beam: render elements alone) gives none, unless the light maps have its light (many static
    // lamps of the level are DLF_FAKE | DLF_LM).
    if ((nFlags & DLF_FAKE) && !(nFlags & DLF_LM))
    {
      nSkippedFake++;
      return;
    }
    if ((nFlags & DLF_AMBIENT_LIGHT) && !CV_d3d12_PTFillLights)
    {
      nSkippedFill++;
      return;
    }
    if (nFlags & DLF_PROJECT)
    {
      if (!LightImageAt(*pLight, 0, false))
      {
        nSkippedImage++;
        return;
      }
    }
    float fKey = 0;
    if (!(nFlags & DLF_DIRECTIONAL) || (nFlags & DLF_PROJECT))
      fKey = std::max((pLight->m_Origin - vCamera).GetLength() - std::max(pLight->m_fRadius, 0.01f), 0.0f);
    if (fKey > std::max(CV_d3d12_PTLightDistance, 0.0f))
      return;
    Candidates.push_back({ pLight, nScene, fKey });
  };
  for (size_t i = 0; i < Lights.size(); i++)
    Accept(Lights[i], (uint32)i);
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  if (const list2<CDLight *> *pStatic = p3D ? p3D->GetStaticLightSources() : nullptr)
  {
    nStatic = pStatic->Count();
    for (int i = 0; i < pStatic->Count(); i++)
    {
      const CDLight *pLight = (*pStatic)[i];
      if (!pLight)
        continue;
      // The frame's copy of it: the same sphere.
      const bool bInFrame = std::any_of(Lights.begin(), Lights.end(), [pLight](const CDLight *pFrame)
      {
        return pFrame && (pFrame == pLight || ((pFrame->m_Origin - pLight->m_Origin).GetLengthSquared() < 1e-4f &&
                                               fabsf(pFrame->m_fRadius - pLight->m_fRadius) < 1e-3f));
      });
      if (bInFrame)
      {
        nStaticFrame++;
        continue;
      }
      Accept(pLight, XR_RAY_NONE);
    }
  }
  std::stable_sort(Candidates.begin(), Candidates.end(), [](const SCandidate &a, const SCandidate &b) { return a.fKey < b.fKey; });
  const size_t nCandidates = Candidates.size();
  if (Candidates.size() > XR_PT_MAX_LIGHTS)
    Candidates.resize(XR_PT_MAX_LIGHTS);

  std::vector<SPTLight> Out;
  Out.reserve(Candidates.size());
  for (const SCandidate &C : Candidates)
  {
    const CDLight *pLight = C.pLight;
    const uint32 nFlags = (uint32)pLight->m_Flags;
    SPTLight L = {};
    uint32 nKind = XR_PT_LIGHT_POINT;
    L.nParams[2] = XR_RAY_NONE;
    if (nFlags & DLF_PROJECT)
    {
      // The projectors with a cube (the picture on +X, or on every side), the frame of its sequence, as the passes of
      // DrawLeafItem.
      CTexture *pCube = LightImageAt(*pLight, iTimer ? iTimer->GetCurrTime() : 0.0f, CV_d3d12_LightImages > 0);
      nKind = XR_PT_LIGHT_PROJECTOR;
      L.nParams[2] = (uint32)pCube->GetSRV();
      memcpy(L.mProject, pLight->m_TextureMatrix.data, sizeof(L.mProject));
    }
    else if (nFlags & DLF_DIRECTIONAL)
      nKind = XR_PT_LIGHT_DIRECTIONAL;
    if (nKind == XR_PT_LIGHT_DIRECTIONAL)
    {
      // From its origin towards the camera, as SetObjectLights does towards the object.
      Vec3 vDir = pLight->m_Origin - vCamera;
      const float fLen = vDir.GetLength();
      vDir = fLen > 0 ? vDir / fLen : Vec3(0, 0, 1);
      Set4(L.vPos, vDir.x, vDir.y, vDir.z, 0);
    }
    else
      Set4(L.vPos, pLight->m_Origin.x, pLight->m_Origin.y, pLight->m_Origin.z, std::max(pLight->m_fRadius, 0.01f));
    const CFColor &c = pLight->m_Color, &s = pLight->m_SpecColor;
    // w: a projector's rays end further short of it (its picture draws its fitting's shade; PathLampGap).
    Set4(L.vColor, c.r, c.g, c.b, nKind == XR_PT_LIGHT_PROJECTOR ? std::max(CV_d3d12_RTProjectorShadowGap, 0.0f) : 0.0f);
    Set4(L.vSpecular, s.r, s.g, s.b, 0);
    L.nParams[0] = nKind;
    L.nParams[1] = ((nFlags & DLF_LM) ? XR_PT_LIGHT_LM : 0u) | ((nFlags & DLF_AMBIENT_LIGHT) ? XR_PT_LIGHT_FILL : 0u) |
                   ((nFlags & DLF_HEATSOURCE) ? XR_PT_LIGHT_HEAT : 0u) | (C.nScene == XR_RAY_NONE ? XR_PT_LIGHT_STATIC : 0u);
    L.nParams[3] = C.nScene;
    Out.push_back(L);
  }

  if (!Out.empty())
  {
    const uint32 nBytes = (uint32)(Out.size() * sizeof(SPTLight));
    const SD3D12FrameAlloc Mem = m_pUpload->AllocFrame(nBytes, 256);
    if (Mem.pCPU)
    {
      memcpy(Mem.pCPU, Out.data(), nBytes);
      m_PathTrace.nLights = Mem.nGPU;
      m_PathTrace.nLightCount = (uint32)Out.size();
    }
  }

  if (FILE *pDump = m_pFrameDump.get())
  {
    int nLM = 0, nOnlyStatic = 0;
    for (const SPTLight &L : Out)
    {
      nLM += (L.nParams[1] & XR_PT_LIGHT_LM) ? 1 : 0;
      nOnlyStatic += (L.nParams[1] & XR_PT_LIGHT_STATIC) ? 1 : 0;
    }
    fprintf(pDump, "pt lights: %u of %zu within %.0f m (%d scene lights; %d static ones, %d of them in the frame), %d baked into the light maps, "
            "%d static alone; left out: %d heat sources, %d fake lights, %d fill lights, %d projectors without a cube, the sun\n",
            m_PathTrace.nLightCount, nCandidates, CV_d3d12_PTLightDistance, (int)Lights.size(), nStatic, nStaticFrame, nLM, nOnlyStatic,
            nSkippedHeat, nSkippedFake, nSkippedFill, nSkippedImage);
    for (size_t i = 0; i < Out.size(); i++)
    {
      const SPTLight &L = Out[i];
      const CDLight *pLight = Candidates[i].pLight;
      fprintf(pDump, "  pt light %zu: %s, flags 0x%08x, %s (%.2f %.2f %.2f), radius %.2f, colour (%.3f %.3f %.3f), specular (%.3f %.3f %.3f)%s%s%s, "
              "%.1f m away, \"%.63s\"\n",
              i, LightKind(L.nParams[0]), (unsigned)pLight->m_Flags, L.nParams[0] == XR_PT_LIGHT_DIRECTIONAL ? "towards" : "at",
              L.vPos[0], L.vPos[1], L.vPos[2], L.vPos[3], L.vColor[0], L.vColor[1], L.vColor[2], L.vSpecular[0], L.vSpecular[1], L.vSpecular[2],
              (L.nParams[1] & XR_PT_LIGHT_LM) ? ", baked" : "", (L.nParams[1] & XR_PT_LIGHT_FILL) ? ", fill" : "",
              (L.nParams[1] & XR_PT_LIGHT_STATIC) ? ", static" : "", Candidates[i].fKey, pLight->m_Name);
    }
  }
}

bool CD3D12Renderer::IsPathView()
{
  return WantsPathTracing() && (m_nSceneFlags & SHDF_ALLOWHDR) && m_nRecurseLevel <= 1 && !m_bReflectionPass && !m_bSpritePass &&
         !m_bShadowMapPass && !m_bEnvCubePass && !m_bCascadePass && !m_bHeatVision && m_pDevice && EnsureFrame() && m_pDevice->IsSceneBound() &&
         m_pDevice->GetTargetColorFormat() == D3D12_HDR_FORMAT;
}

bool CD3D12Renderer::UpdatePathTargets()
{
  if (!m_pDevice || !m_pUpload || !m_pDescriptors)
    return false;
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  if (m_PathTrace.pAlbedo && m_PathTrace.pAlbedo->m_nWidth == w && m_PathTrace.pAlbedo->m_nHeight == h)
    return true;
  ReleasePathTargets();
  m_PathTrace.pAlbedo = m_pUpload->CreateRenderTarget(w, h, D3D12_PT_ALBEDO_FORMAT);
  m_PathTrace.pNormal = m_pUpload->CreateRenderTarget(w, h, D3D12_PT_NORMAL_FORMAT);
  m_PathTrace.pSun = m_pUpload->CreateRenderTarget(w, h, D3D12_PT_SUN_FORMAT);
  m_PathTrace.pSpecular = m_pUpload->CreateRenderTarget(w, h, D3D12_PT_SPECULAR_FORMAT);
  m_PathTrace.pLightmap = m_pUpload->CreateRenderTarget(w, h, D3D12_PT_LIGHTMAP_FORMAT);
  if (!m_PathTrace.pAlbedo || !m_PathTrace.pNormal || !m_PathTrace.pSun || !m_PathTrace.pSpecular || !m_PathTrace.pLightmap ||
      m_PathTrace.pAlbedo->m_nSRV < 0 || m_PathTrace.pNormal->m_nSRV < 0 || m_PathTrace.pSun->m_nSRV < 0 || m_PathTrace.pSpecular->m_nSRV < 0 ||
      m_PathTrace.pLightmap->m_nSRV < 0)
  {
    iLog->LogWarning("XRenderD3D12: the path tracer's G-buffer is not available at %d x %d", w, h);
    ReleasePathTargets();
    return false;
  }
  // Made in PIXEL_SHADER_RESOURCE; their states kept by the device (Begin/EndPathTargets ask for theirs).
  for (CD3D12Texture *pTex : { m_PathTrace.pAlbedo, m_PathTrace.pNormal, m_PathTrace.pSun, m_PathTrace.pSpecular, m_PathTrace.pLightmap })
    m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  iLog->Log("XRenderD3D12: the path tracer's G-buffer, %d x %d", w, h);
  return true;
}

void CD3D12Renderer::ReleasePathTargets()
{
  if (m_PathTrace.bBound && m_pDevice)
    m_pDevice->BindPathTargets({}, {}, {}, {}, {});
  m_PathTrace.bBound = false;
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : { m_PathTrace.pAlbedo, m_PathTrace.pNormal, m_PathTrace.pSun, m_PathTrace.pSpecular, m_PathTrace.pLightmap })
      if (pTex)
        m_pUpload->ReleaseTexture(pTex);
  }
  m_PathTrace.pAlbedo = m_PathTrace.pNormal = m_PathTrace.pSun = m_PathTrace.pSpecular = m_PathTrace.pLightmap = nullptr;
  m_PathTrace.nGBufferFrame = -1;
  ReleasePathKernels();
}

void CD3D12Renderer::BeginPathTargets()
{
  if (m_PathTrace.bBound || !IsPathView() || !UpdatePathTargets())
    return;
  // Cleared to no surface (material 0) and no albedo: the sky and the far stay as the colour has them.
  // The transitions first, the clears after them: the queue is given once (d3d12_BarrierBatch), not before each clear.
  static const float Zero[4] = {};
  CD3D12Texture *const Targets[] = { m_PathTrace.pAlbedo, m_PathTrace.pNormal, m_PathTrace.pSun, m_PathTrace.pSpecular, m_PathTrace.pLightmap };
  for (CD3D12Texture *pTex : Targets)
    m_pDevice->Require(pTex->m_pResource, D3D12_RESOURCE_STATE_RENDER_TARGET);
  for (CD3D12Texture *pTex : Targets)
    m_pDevice->ClearRenderTargetView(pTex->m_RTV, Zero);
  ID3D12Resource *const Resources[] = { m_PathTrace.pAlbedo->m_pResource, m_PathTrace.pNormal->m_pResource, m_PathTrace.pSun->m_pResource,
                                        m_PathTrace.pSpecular->m_pResource, m_PathTrace.pLightmap->m_pResource };
  m_pDevice->BindPathTargets(m_PathTrace.pAlbedo->m_RTV, m_PathTrace.pNormal->m_RTV, m_PathTrace.pSun->m_RTV, m_PathTrace.pSpecular->m_RTV,
                             m_PathTrace.pLightmap->m_RTV, Resources);
  m_PathTrace.bBound = true;
}

void CD3D12Renderer::EndPathTargets()
{
  if (!m_PathTrace.bBound)
    return;
  m_PathTrace.bBound = false;
  m_pDevice->BindPathTargets({}, {}, {}, {}, {});
  // Read by the kernels and the composite's pixels.
  for (CD3D12Texture *pTex : { m_PathTrace.pAlbedo, m_PathTrace.pNormal, m_PathTrace.pSun, m_PathTrace.pSpecular, m_PathTrace.pLightmap })
    m_pDevice->Require(pTex->m_pResource, PATH_READ);
  m_PathTrace.nGBufferFrame = m_nFrames;
  m_PathTrace.bTraceDue = true;
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "pt g-buffer: %d x %d, written by the opaque list\n", m_PathTrace.pAlbedo->m_nWidth, m_PathTrace.pAlbedo->m_nHeight);
}

bool CD3D12Renderer::UpdatePathKernels(int w, int h)
{
  SPathTraceState &s = m_PathTrace;
  if (s.pRays && s.pRays->m_nWidth == w && s.pRays->m_nHeight == h)
    return true;
  ReleasePathKernels();
  s.pRays = m_pUpload->CreateComputeTarget(w, h, 1, PATH_LIGHT_FORMAT, L"XRenderD3D12 path tracer's paths");
  for (int i = 0; i < 2; i++)
  {
    s.pHistory[i] = m_pUpload->CreateComputeTarget(w, h, 1, PATH_HISTORY_FORMAT, L"XRenderD3D12 path tracer's history");
    s.pFrames[i] = m_pUpload->CreateComputeTarget(w, h, 1, PATH_FRAMES_FORMAT, L"XRenderD3D12 path tracer's history frames");
  }
  s.pLight = m_pUpload->CreateComputeTarget(w, h, 1, PATH_LIGHT_FORMAT, L"XRenderD3D12 path tracer's light");
  s.pHighlights = m_pUpload->CreateComputeTarget(w, h, 1, PATH_LIGHT_FORMAT, L"XRenderD3D12 path tracer's highlights");
  bool bOk = s.pRays && s.pHistory[0] && s.pHistory[1] && s.pFrames[0] && s.pFrames[1] && s.pLight && s.pHighlights;
  if (bOk)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = PathTextureSRV(PATH_LIGHT_FORMAT);
    for (CD3D12Texture *pTex : { s.pRays, s.pLight, s.pHighlights })
      bOk &= (pTex->m_nSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV)) >= 0;
    SRV = PathTextureSRV(PATH_HISTORY_FORMAT);
    for (CD3D12Texture *pTex : { s.pHistory[0], s.pHistory[1] })
      bOk &= (pTex->m_nSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV)) >= 0;
    SRV = PathTextureSRV(PATH_FRAMES_FORMAT);
    for (CD3D12Texture *pTex : s.pFrames)
      bOk &= (pTex->m_nSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV)) >= 0;
    // The kernels' UAV tables: u0 the paths, a history, the light or the highlights, u1 the history's frames (the slots they
    // do not use null).
    ID3D12Resource *const Outputs[PATH_TABLES] = { s.pRays->m_pResource, s.pHistory[0]->m_pResource, s.pHistory[1]->m_pResource,
                                                   s.pLight->m_pResource, s.pHighlights->m_pResource };
    for (int t = 0; t < PATH_TABLES && bOk; t++)
    {
      if ((s.nTables[t] = m_pDescriptors->AllocRange(CD3D12Compute::COMPUTE_UAVS)) < 0)
      {
        bOk = false;
        break;
      }
      D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
      View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      const bool bTemporal = t == PATH_TABLE_TEMPORAL0 || t == PATH_TABLE_TEMPORAL1;
      for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
      {
        View.Format = i == 1 ? PATH_FRAMES_FORMAT : i == 2 ? PATH_HIT_FORMAT : bTemporal ? PATH_HISTORY_FORMAT : PATH_LIGHT_FORMAT;
        ID3D12Resource *pUAV = i == 0 ? Outputs[t] : i == 1 && bTemporal ? s.pFrames[t - PATH_TABLE_TEMPORAL0]->m_pResource : nullptr;
        m_pDescriptors->WriteUAV(s.nTables[t] + i, pUAV, &View);
      }
    }
  }
  if (!bOk)
  {
    iLog->LogWarning("XRenderD3D12: the path tracer's kernels have no targets at %d x %d", w, h);
    ReleasePathKernels();
    return false;
  }
  // Made in UNORDERED_ACCESS; their states kept by the device (each kernel and the composite ask for theirs, none moved
  // back after: the history read by the next frame stays readable).
  for (CD3D12Texture *pTex : { s.pRays, s.pHistory[0], s.pHistory[1], s.pFrames[0], s.pFrames[1], s.pLight, s.pHighlights })
    m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  s.nHistoryFrame = -1;
  return true;
}

bool CD3D12Renderer::UpdatePathNrd(int w, int h)
{
  SPathTraceState &s = m_PathTrace;
  if (s.bNrdFailed || !CD3D12Nrd::IsBuilt() || s.nTables[PATH_TABLE_TRACE] < 0)
    return false;
  if (s.pNrdOut && m_pNrd && m_pNrd->IsReady() && m_pNrd->GetWidth() == w && m_pNrd->GetHeight() == h)
    return true;
  ReleasePathNrd();
  bool bOk = true;
  s.pHitDistance = m_pUpload->CreateComputeTarget(w, h, 1, PATH_HIT_FORMAT, L"XRenderD3D12 path tracer's hit distance");
  static const wchar_t *const s_Names[4] = { L"XRenderD3D12 NRD's diffuse light", L"XRenderD3D12 NRD's normal and roughness",
                                             L"XRenderD3D12 NRD's view depth", L"XRenderD3D12 NRD's motion" };
  for (int i = 0; i < 4; i++)
    bOk &= (s.pNrdIn[i] = m_pUpload->CreateComputeTarget(w, h, 1, PATH_NRD_FORMATS[i], s_Names[i])) != nullptr;
  s.pNrdOut = m_pUpload->CreateComputeTarget(w, h, 1, PATH_NRD_OUT_FORMAT, L"XRenderD3D12 NRD's denoised light");
  bOk &= s.pHitDistance && s.pNrdOut;
  if (bOk)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = PathTextureSRV(PATH_HIT_FORMAT);
    bOk &= (s.pHitDistance->m_nSRV = m_pDescriptors->AllocSRV(s.pHitDistance->m_pResource, &SRV)) >= 0;
    SRV = PathTextureSRV(PATH_NRD_OUT_FORMAT);
    bOk &= (s.pNrdOut->m_nSRV = m_pDescriptors->AllocSRV(s.pNrdOut->m_pResource, &SRV)) >= 0;
    bOk &= (s.nNrdTable = m_pDescriptors->AllocRange(CD3D12Compute::COMPUTE_UAVS)) >= 0;
  }
  if (bOk)
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
    View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      View.Format = i < 4 ? PATH_NRD_FORMATS[i] : PATH_LIGHT_FORMAT;
      m_pDescriptors->WriteUAV(s.nNrdTable + i, i < 4 ? s.pNrdIn[i]->m_pResource : nullptr, &View);
    }
    if (!m_pNrd)
      m_pNrd = std::make_unique<CD3D12Nrd>();
    bOk = m_pNrd->Init(m_pDevice->GetDevice(), m_pDescriptors.get(), m_pUpload.get(), w, h);
  }
  if (!bOk)
  {
    iLog->LogWarning("XRenderD3D12: NVIDIA NRD is not available at %d x %d: the path tracer denoises with simple", w, h);
    ReleasePathNrd();
    s.bNrdFailed = true;
    return false;
  }
  // Their states kept by the device (made in UNORDERED_ACCESS): the inputs' and output's asked for by the kernels and
  // NRD's passes (NRD's host keeps its own textures so too). CSTrace's hit distance: u2 of its table (a null view before).
  for (CD3D12Texture *pTex : { s.pHitDistance, s.pNrdIn[0], s.pNrdIn[1], s.pNrdIn[2], s.pNrdIn[3], s.pNrdOut })
    m_pDevice->TrackState(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
  View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  View.Format = PATH_HIT_FORMAT;
  m_pDescriptors->WriteUAV(s.nTables[PATH_TABLE_TRACE] + 2, s.pHitDistance->m_pResource, &View);
  s.nNrdFrame = -1;
  return true;
}

void CD3D12Renderer::ReleasePathNrd()
{
  SPathTraceState &s = m_PathTrace;
  if (m_pDescriptors && s.nTables[PATH_TABLE_TRACE] >= 0 && s.pHitDistance)
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
    View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    View.Format = PATH_HIT_FORMAT;
    m_pDescriptors->WriteUAV(s.nTables[PATH_TABLE_TRACE] + 2, nullptr, &View);
  }
  for (CD3D12Texture **pp : { &s.pHitDistance, &s.pNrdIn[0], &s.pNrdIn[1], &s.pNrdIn[2], &s.pNrdIn[3], &s.pNrdOut })
  {
    if (*pp && m_pUpload)
      m_pUpload->ReleaseTexture(*pp);
    *pp = nullptr;
  }
  if (s.nNrdTable >= 0 && m_pDescriptors)
    m_pDescriptors->FreeRange(s.nNrdTable, CD3D12Compute::COMPUTE_UAVS);
  s.nNrdTable = -1;
  s.nNrdFrame = -1;
  if (m_pNrd)
    m_pNrd->Release();
}

bool CD3D12Renderer::UpdatePathCache()
{
  SPathTraceState &s = m_PathTrace;
  if (s.bCacheFailed || s.nTables[PATH_TABLE_TRACE] < 0)
    return false;
  if (!s.pCache)
  {
    // Zeroed as made: no keys, no light.
    const D3D12_HEAP_PROPERTIES Heap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = XR_PT_CACHE_BYTES;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(m_pDevice->GetDevice()->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                               IID_PPV_ARGS(&s.pCache))))
    {
      iLog->LogWarning("XRenderD3D12: the path tracer's radiance cache (%u KB) is not available: its paths without it",
                       XR_PT_CACHE_BYTES / 1024);
      s.pCache.Reset();
      s.bCacheFailed = true;
      return false;
    }
    s.pCache->SetName(L"XRenderD3D12 path tracer's radiance cache");
    s.nCacheTable = -1;
    s.nCacheFrame = -1;
  }
  // u3 of CSTrace's table (which the cache's kernels take too), written again where the table is new.
  if (s.nCacheTable != s.nTables[PATH_TABLE_TRACE])
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
    View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    View.Format = DXGI_FORMAT_R32_TYPELESS;
    View.Buffer.NumElements = XR_PT_CACHE_BYTES / 4;
    View.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    m_pDescriptors->WriteUAV(s.nTables[PATH_TABLE_TRACE] + 3, s.pCache.Get(), &View);
    s.nCacheTable = s.nTables[PATH_TABLE_TRACE];
  }
  return true;
}

void CD3D12Renderer::ReleasePathCache()
{
  SPathTraceState &s = m_PathTrace;
  if (m_pDescriptors && s.nCacheTable >= 0 && s.nCacheTable == s.nTables[PATH_TABLE_TRACE])
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC View = {};
    View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    View.Format = PATH_LIGHT_FORMAT;
    m_pDescriptors->WriteUAV(s.nCacheTable + 3, nullptr, &View);
  }
  s.nCacheTable = -1;
  s.nCacheFrame = -1;
  if (s.pCache && m_pDevice)
    m_pDevice->DeferredRelease(s.pCache);
  s.pCache.Reset();
}

void CD3D12Renderer::ReleasePathKernels()
{
  SPathTraceState &s = m_PathTrace;
  ReleasePathNrd();
  ReleasePathCache();
  s.bCacheFailed = false;
  s.bNrdFailed = false;
  for (CD3D12Texture **pp : { &s.pRays, &s.pHistory[0], &s.pHistory[1], &s.pFrames[0], &s.pFrames[1], &s.pLight, &s.pHighlights })
  {
    if (*pp && m_pUpload)
      m_pUpload->ReleaseTexture(*pp);
    *pp = nullptr;
  }
  if (m_pDescriptors)
  {
    for (int &nTable : s.nTables)
      if (nTable >= 0)
        m_pDescriptors->FreeRange(nTable, CD3D12Compute::COMPUTE_UAVS);
    if (s.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(s.nDepthSRV);
    if (s.nVelocitySRV >= 0)
      m_pDescriptors->FreeSRV(s.nVelocitySRV);
  }
  std::fill(std::begin(s.nTables), std::end(s.nTables), -1);
  s.nDepthSRV = -1;
  s.pDepth.Reset();
  s.nVelocitySRV = -1;
  s.pVelocity.Reset();
  s.nHistoryFrame = -1;
  s.nLightFrame = -1;
}

void CD3D12Renderer::DrawPathTrace()
{
  SPathTraceState &s = m_PathTrace;
  const bool bDue = s.bTraceDue;
  s.bTraceDue = false;
  if (!bDue || s.nGBufferFrame != m_nFrames || !s.pAlbedo || !m_pCompute || !m_pRayScene || m_pRayScene->GetBuiltFrame() != m_nFrames ||
      !m_pRayScene->GetTLAS() || !m_pDevice->IsSceneBound())
    return;
  const int w = s.pAlbedo->m_nWidth, h = s.pAlbedo->m_nHeight;
  ID3D12Resource *pDepth = m_pDevice->GetMainDepth();
  // The view over the whole depth buffer (the pixels' positions from it), as the G-buffer's.
  float sx = 1, sy = 1;
  GetSceneScale(sx, sy);
  if (!pDepth || w != m_pDevice->GetMainWidth() || h != m_pDevice->GetMainHeight() || m_Viewport[0] != 0 || m_Viewport[1] != 0 ||
      fabsf(m_Viewport[2] * sx - w) > 1 || fabsf(m_Viewport[3] * sy - h) > 1 || !UpdatePathKernels(w, h))
    return;
  if (pDepth != s.pDepth.Get())
  {
    if (s.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(s.nDepthSRV);
    s.pDepth = pDepth;
    const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = PathTextureSRV(m_pDevice->GetMainDepthSRVFormat());
    s.nDepthSRV = m_pDescriptors->AllocSRV(pDepth, &SRV);
    s.nHistoryFrame = -1;
  }
  if (s.nDepthSRV < 0)
    return;
  // The scene's motion vectors for NRD (none: the camera's motion alone).
  ID3D12Resource *pVelocity = m_pDevice->GetSceneVelocity();
  if (pVelocity != s.pVelocity.Get())
  {
    if (s.nVelocitySRV >= 0)
      m_pDescriptors->FreeSRV(s.nVelocitySRV);
    s.nVelocitySRV = -1;
    s.pVelocity = pVelocity;
    if (pVelocity && pVelocity->GetDesc().Width == (UINT64)w && pVelocity->GetDesc().Height == (UINT)h)
    {
      const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = PathTextureSRV(D3D12_VELOCITY_FORMAT);
      s.nVelocitySRV = m_pDescriptors->AllocSRV(pVelocity, &SRV);
    }
  }
  GpuSection("pt");
  const D3D12_GPU_VIRTUAL_ADDRESS nTlas = UseRayScene();
  if (!nTlas)
    return;
  SFrameConstants fc;
  MakeFrameConstants(fc, true);
  const SD3D12FrameAlloc FrameCB = m_pUpload->AllocFrame(sizeof(fc), 256);
  memcpy(FrameCB.pCPU, &fc, sizeof(fc));

  // The reference starts again where the view or the path tracer's settings change.
  const bool bReference = CV_d3d12_PathTracing >= 2;
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  SPathShadeParams P;
  GetPathShade(P);
  const int nBounces = std::clamp(bReference ? CV_d3d12_PTReferenceBounces : CV_d3d12_PTBounces, 0, 64);
  const float *vSky = P.vSky;
  const char *szDenoiser = m_pPTDenoiserVar && m_pPTDenoiserVar->GetString() ? m_pPTDenoiserVar->GetString() : "simple";
  const bool bDenoise = !bReference && _stricmp(szDenoiser, "none") != 0;
  // NRD in real time, where it is built in and available; otherwise simple.
  const bool bNrd = bDenoise && !_stricmp(szDenoiser, "nrd") && UpdatePathNrd(w, h);
  // The radiance cache in real time, where the paths bounce (its buffer let go with d3d12_PTCache 0).
  if (!CV_d3d12_PTCache && s.pCache)
    ReleasePathCache();
  const bool bCache = !bReference && CV_d3d12_PTCache && nBounces > 0 && UpdatePathCache();
  uint32 nKey = (uint32)nBounces * 0x9e3779b1u;
  const float fAmbient = std::max(CV_d3d12_PTAmbient, 0.0f), fAmbientRange = std::max(CV_d3d12_PTAmbientRange, 0.0f);
  // d3d12_PTLightMaps where the frame has no sun (indoors: the light maps outdoors have the sun the path tracer brings).
  const bool bOutdoors = m_pScene && m_pScene->GetSun();
  const float fLightMaps = bOutdoors ? 0.0f : std::max(CV_d3d12_PTLightMaps, 0.0f);
  for (float f : { P.vSun[0], P.vSun[1], P.vSun[2], P.fLamps, P.fEmission, P.fDelight, vSky[0], vSky[1], vSky[2], fAmbient, fAmbientRange,
                   P.fBakedFalloff, fLightMaps })
  {
    uint32 n;
    memcpy(&n, &f, sizeof(n));
    nKey = (nKey ^ n) * 16777619u;
  }
  const bool bSameView = !memcmp(ViewProj.data, s.PrevViewProj, sizeof(s.PrevViewProj)) && nKey == s.nSettingsKey;
  const bool bHistory = s.nHistoryFrame == m_nFrames - 1 && (bReference ? bSameView && s.bReference : !s.bReference);
  s.nAccumulated = bHistory ? s.nAccumulated + 1 : 1;

  SPathTraceConstants pc = {};
  memcpy(pc.mPrevViewProj, s.PrevViewProj, sizeof(pc.mPrevViewProj));
  GetRayView(pc.vCameraPos, pc.vRayX, pc.vRayY, pc.vRayO);
  Set4(pc.vSize, (float)w, (float)h, 1.0f / w, 1.0f / h);
  Set4(pc.vJitter, fc.vJitter[0], fc.vJitter[1], s.PrevJitter[0], s.PrevJitter[1]);
  Set4(pc.vParams, std::max(CV_d3d12_RTDistance, 1.0f), 2 / (fabsf(m_ProjMatrix.data[5]) * h), bReference ? 65535.0f : PATH_HISTORY_FRAMES,
       bReference ? 0.0f : PATH_SAMPLE_MAX);
  Set4(pc.vSun, P.vSun[0], P.vSun[1], P.vSun[2], P.fLamps);
  Set4(pc.vSky, vSky[0], vSky[1], vSky[2], P.fEmission);
  Set4(pc.vShade, P.fDelight, P.fLightGap, P.fBakedFalloff, 0);
  Set4(pc.vBakeLight, P.vBakeLight[0], P.vBakeLight[1], P.vBakeLight[2], 0);
  Set4(pc.vFill, fAmbient, fAmbientRange, fLightMaps, 0);
  // The cache's light starts again after a frame without it or where the settings change.
  const bool bCacheRestart = bCache && (s.nCacheFrame != m_nFrames - 1 || nKey != s.nSettingsKey);
  Set4(pc.vCache, PATH_CACHE_CELL, PATH_CACHE_CELL_PER_METRE, PATH_CACHE_FRAMES, PATH_CACHE_JITTER);
  pc.nCache[0] = bCache ? 1 : 0;
  pc.nCache[1] = bCacheRestart ? 1 : 0;
  pc.nCache[2] = bCache && CV_d3d12_PTDebug == 11 ? 1 : 0;
  pc.nCache[3] = PATH_CACHE_TILE;
  // The highlights in the game's colour, as the raster's (its sun's specular colour, the lamps'), the scales of the path
  // tracer's sun and lamps as such a colour; the sun's none indoors, as its light.
  const float fSpecular = std::max(CV_d3d12_PTSpecular, 0.0f);
  const float fSunSpecular = bOutdoors ? fSpecular * powf(std::max(CV_d3d12_PTSun, 0.0f), 1 / 2.2f) : 0.0f;
  Set4(pc.vSpecular, std::clamp(m_SunSpec[0], 0.0f, 1.0f) * fSunSpecular, std::clamp(m_SunSpec[1], 0.0f, 1.0f) * fSunSpecular,
       std::clamp(m_SunSpec[2], 0.0f, 1.0f) * fSunSpecular, fSpecular * powf(P.fLamps, 1 / 2.2f));
  const int nCur = s.nHistory ^= 1;
  CD3D12Texture *pHistory = s.pHistory[nCur], *pPrevHistory = s.pHistory[nCur ^ 1];
  CD3D12Texture *pFrames = s.pFrames[nCur], *pPrevFrames = s.pFrames[nCur ^ 1];
  pc.nTextures[0] = (uint32)s.nDepthSRV;
  pc.nTextures[1] = (uint32)s.pRays->m_nSRV;
  pc.nTextures[2] = (uint32)pPrevHistory->m_nSRV;
  pc.nTextures[3] = (uint32)pHistory->m_nSRV;
  pc.nTextures2[0] = (uint32)s.pNormal->m_nSRV;
  pc.nTextures2[1] = (uint32)pPrevFrames->m_nSRV;
  pc.nTextures2[2] = (uint32)pFrames->m_nSRV;
  pc.nTextures2[3] = (uint32)s.pSun->m_nSRV;
  pc.nTextures3[0] = (uint32)s.pSpecular->m_nSRV;
  pc.nTextures4[0] = (uint32)s.pLightmap->m_nSRV;
  // The checkerboard: CSTrace traces half the pixels, the others each frame; the kernels after it fill those it left out
  // from their neighbours. Not for the reference, which has a path from each pixel.
  const bool bCheckerboard = !bReference && CV_d3d12_PTCheckerboard;
  pc.nCheckerboard[0] = bCheckerboard ? 1 : 0;
  pc.nCheckerboard[1] = bCheckerboard ? (uint32)(m_nFrames & 1) : 0;
  const SNrdSettings Nrd = CD3D12Nrd::GetDefaults();
  if (bNrd)
  {
    pc.nTextures3[1] = (uint32)s.pNrdOut->m_nSRV;
    pc.nTextures3[2] = s.nVelocitySRV >= 0 ? (uint32)s.nVelocitySRV : XR_RAY_NONE;
    pc.nTextures3[3] = (uint32)s.pHitDistance->m_nSRV;
    Set4(pc.vNrd, Nrd.fHitDistance[0], Nrd.fHitDistance[1], Nrd.fHitDistance[2], PATH_NRD_NO_SURFACE);
  }
  pc.nTrace[0] = 1;
  pc.nFlags[0] = bHistory ? 1 : 0;
  pc.nFlags[1] = (uint32)m_nFrames;
  // CSFilter's own filter: not after NRD.
  pc.nFlags[2] = bDenoise && !bNrd ? 1 : 0;
  pc.nFlags[3] = RayMask() & 0xff;
  pc.nPath[0] = P.nLights;
  pc.nPath[1] = (uint32)nBounces;
  pc.nPath[2] = bReference ? XR_RAY_MAX_ALPHA_TESTS : PATH_BOUNCE_TESTS;
  pc.nPath[3] = bReference ? 1 : 0;
  const SD3D12FrameAlloc PassCB = m_pUpload->AllocFrame(sizeof(pc), 256);
  memcpy(PassCB.pCPU, &pc, sizeof(pc));

  // The depth buffer readable, the paths and the histories UAVs between the dispatches; the light read by the composite.
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  const D3D12_GPU_VIRTUAL_ADDRESS nInstances = m_pRayScene->GetInstances();
  const D3D12_GPU_VIRTUAL_ADDRESS nLights = s.nLightsFrame == m_nFrames ? s.nLights : 0;
  const UINT gx = (UINT)(w + XR_PT_GROUP - 1) / XR_PT_GROUP, gy = (UINT)(h + XR_PT_GROUP - 1) / XR_PT_GROUP;
  constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  m_pDevice->Transition(pDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, PATH_DEPTH_READ);
  // The targets' states kept by the device (UpdatePathKernels): each kernel asks for its outputs and inputs, nothing moved
  // back after (the frame before's history, read by this one, stays readable). The table of the cache's kernels and
  // CSTrace has the paths and the hit distance.
  m_pDevice->Require(s.pRays->m_pResource, UAV);
  if (s.pHitDistance)
    m_pDevice->Require(s.pHitDistance->m_pResource, UAV);
  if (bCache)
  {
    // The cache's samples from a path for each PATH_CACHE_TILE x PATH_CACHE_TILE pixels, blended into its light, which
    // CSTrace reads.
    GpuSection("pt cache");
    const UINT cx = ((UINT)(w + PATH_CACHE_TILE - 1) / PATH_CACHE_TILE + XR_PT_GROUP - 1) / XR_PT_GROUP;
    const UINT cy = ((UINT)(h + PATH_CACHE_TILE - 1) / PATH_CACHE_TILE + XR_PT_GROUP - 1) / XR_PT_GROUP;
    m_pCompute->DispatchRays(pList, eD3D12K_PathCacheUpdate, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nTables[PATH_TABLE_TRACE], cx, cy,
                             nLights);
    m_pDevice->UavBarrier(s.pCache.Get());
    m_pCompute->DispatchRays(pList, eD3D12K_PathCacheResolve, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nTables[PATH_TABLE_TRACE],
                             XR_PT_CACHE_ENTRIES / 256, 1, nLights);
    m_pDevice->UavBarrier(s.pCache.Get());
  }
  s.nCacheFrame = bCache ? m_nFrames : -1;
  GpuSection("pt paths");
  const UINT tx = bCheckerboard ? (UINT)((w + 1) / 2 + XR_PT_GROUP - 1) / XR_PT_GROUP : gx;
  m_pCompute->DispatchRays(pList, eD3D12K_PathTrace, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nTables[PATH_TABLE_TRACE], tx, gy, nLights);
  m_pDevice->Require(s.pRays->m_pResource, PATH_KERNEL_READ);
  bool bNrdDone = false;
  int nNrdPasses = 0;
  if (bNrd)
  {
    // NRD's inputs from the paths (the motion vectors readable meanwhile), NRD, its light as this frame's history.
    GpuSection("pt nrd");
    m_pDevice->Require(s.pHitDistance->m_pResource, PATH_KERNEL_READ);
    for (CD3D12Texture *pTex : s.pNrdIn)
      m_pDevice->Require(pTex->m_pResource, UAV);
    if (s.nVelocitySRV >= 0)
      m_pDevice->Transition(pVelocity, D3D12_RESOURCE_STATE_RENDER_TARGET, PATH_KERNEL_READ);
    m_pCompute->DispatchRays(pList, eD3D12K_PathNrdPack, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nNrdTable, gx, gy, nLights);
    if (s.nVelocitySRV >= 0)
      m_pDevice->Transition(pVelocity, PATH_KERNEL_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
    SNrdView View = {};
    memcpy(View.mWorldToView, m_CameraView.data, sizeof(View.mWorldToView));
    memcpy(View.mViewToClip, m_ProjMatrix.data, sizeof(View.mViewToClip));
    // NRD's jitter is where a pixel's sample is (pixels); the engine's moves the picture.
    View.vJitter[0] = -m_UpscaleJitter[0];
    View.vJitter[1] = -m_UpscaleJitter[1];
    View.fDenoisingRange = PATH_NRD_RANGE;
    View.bRestart = !bHistory || s.nNrdFrame != m_nFrames - 1;
    const SNrdTextures Textures = { s.pNrdIn[3], s.pNrdIn[1], s.pNrdIn[2], s.pNrdIn[0], s.pNrdOut };
    bNrdDone = m_pNrd->Denoise(pList, View, Nrd, Textures);
    if (!bNrdDone)
    {
      // Simple from the next frame on (this one has NRD's light of the frame before).
      iLog->LogWarning("XRenderD3D12: NVIDIA NRD failed at %d x %d: the path tracer denoises with simple", w, h);
      s.bNrdFailed = true;
    }
    int nTables = 0, nTextures = 0;
    m_pNrd->GetStats(nNrdPasses, nTables, nTextures);
    m_pDevice->ForgetDrawState();
    s.nNrdFrame = bNrdDone ? m_nFrames : -1;
    m_pDevice->Require(s.pNrdOut->m_pResource, PATH_KERNEL_READ);
    m_pDevice->Require(pHistory->m_pResource, UAV);
    m_pDevice->Require(pFrames->m_pResource, UAV);
    m_pCompute->DispatchRays(pList, eD3D12K_PathNrdResolve, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances,
                             s.nTables[PATH_TABLE_TEMPORAL0 + nCur], gx, gy, nLights);
  }
  else
  {
    m_pDevice->Require(pPrevHistory->m_pResource, PATH_KERNEL_READ);
    m_pDevice->Require(pPrevFrames->m_pResource, PATH_KERNEL_READ);
    m_pDevice->Require(pHistory->m_pResource, UAV);
    m_pDevice->Require(pFrames->m_pResource, UAV);
    GpuSection("pt history");
    m_pCompute->DispatchRays(pList, eD3D12K_PathTemporal, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nTables[PATH_TABLE_TEMPORAL0 + nCur],
                             gx, gy, nLights);
  }
  m_pDevice->Require(pHistory->m_pResource, PATH_KERNEL_READ);
  m_pDevice->Require(pFrames->m_pResource, PATH_KERNEL_READ);
  m_pDevice->Require(s.pLight->m_pResource, UAV);
  m_pDevice->Require(s.pHighlights->m_pResource, UAV);
  GpuSection("pt filter");
  m_pCompute->DispatchRays(pList, eD3D12K_PathFilter, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nTables[PATH_TABLE_FILTER], gx, gy, nLights);
  GpuSection("pt highlights");
  m_pCompute->DispatchRays(pList, eD3D12K_PathSpecular, FrameCB.nGPU, PassCB.nGPU, nTlas, nInstances, s.nTables[PATH_TABLE_SPECULAR], gx, gy,
                           nLights);
  m_pDevice->Require(s.pLight->m_pResource, PATH_READ);
  m_pDevice->Require(s.pHighlights->m_pResource, PATH_READ);
  m_pDevice->Transition(pDepth, PATH_DEPTH_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_pDevice->BindRootSignature();
  InvalidateFrameConstants();

  // The composite: the light and the highlights added to the scene's colour where the G-buffer has a surface the path
  // tracer lights (the viewport in the back buffer's pixels, scaled to the scene's by the draw).
  GpuSection("pt composite");
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = m_pDevice->GetWidth();
  m_Viewport[3] = m_pDevice->GetHeight();
  m_bScissor = m_bItemScissor = false;
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_PathComposite;
  Key.nState = GS_NODEPTHTEST | GS_COLMASKONLYRGB | GS_BLSRC_ONE | GS_BLDST_ONE;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  Set4(dc.vParams0, P.vSun[0], P.vSun[1], P.vSun[2], 0);
  // The terrain's albedo divided by its baked light: CSFilter's share of it times this (none without the delight).
  const bool bDelight = P.fDelight > 1;
  Set4(dc.vParams1, bDelight ? P.vBakeLight[0] : 1, bDelight ? P.vBakeLight[1] : 1, bDelight ? P.vBakeLight[2] : 1, 0);
  dc.nTex0 = (uint32)s.pAlbedo->m_nSRV;
  dc.nTex1 = (uint32)s.pNormal->m_nSRV;
  dc.nTex2 = (uint32)s.pSun->m_nSRV;
  dc.nTex3 = (uint32)s.pLight->m_nSRV;
  dc.nTex4 = (uint32)s.pHighlights->m_nSRV;
  // r_Skin: the skin of the G-buffer's light map target (its alpha) lit by the sun as the raster lights it, wrapped round
  // the line where the sun passes it by the scatter (Skin.hlsli's SkinWrap).
  dc.nTex5 = (uint32)s.pLightmap->m_nSRV;
  Set4(dc.vSkin, 1, 0, 0, std::max(CV_r_SkinScatter, 0.0f));
  static const float s_Triangle[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_Triangle, 3);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;

  // This frame for the next one's history.
  memcpy(s.PrevViewProj, ViewProj.data, sizeof(s.PrevViewProj));
  s.PrevJitter[0] = fc.vJitter[0];
  s.PrevJitter[1] = fc.vJitter[1];
  s.nSettingsKey = nKey;
  s.bReference = bReference;
  s.szDenoiser = bNrd ? "NRD" : bDenoise ? "simple" : "none";
  s.nBounces = nBounces;
  s.bCache = bCache;
  s.bCheckerboard = bCheckerboard;
  s.nHistoryFrame = m_nFrames;
  s.nLightFrame = m_nFrames;
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "pt: %s, %d x %d, %s, %d bounces, %u lamps, sky %.3f %.3f %.3f, sun %.3f %.3f %.3f%s, terrain's baked light %.3f %.3f %.3f, "
            "ambient %.2f beyond %.2f m, light maps %.2f, highlights: sun %.3f %.3f %.3f, lamps %.3f, denoiser %s, history %d (%d frames)\n",
            bReference ? "reference" : "real time", w, h,
            bCheckerboard ? (m_nFrames & 1 ? "paths of the odd checkerboard's pixels" : "paths of the even checkerboard's pixels") : "a path from each pixel",
            nBounces, P.nLights, vSky[0], vSky[1], vSky[2], P.vSun[0], P.vSun[1], P.vSun[2],
            bOutdoors ? "" : " (indoors: no sun in the frame)", P.vBakeLight[0], P.vBakeLight[1], P.vBakeLight[2], fAmbient, fAmbientRange, fLightMaps, pc.vSpecular[0], pc.vSpecular[1],
            pc.vSpecular[2], pc.vSpecular[3], bNrd ? "nrd" : bDenoise ? "simple" : "none", bHistory ? 1 : 0, s.nAccumulated);
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "pt cache: %s, %u entries (%u KB), cells %.2f m and %.3f m per metre, %.0f frames, a path for each %u x %u "
            "pixels%s\n", bCache ? "on" : bReference ? "off (the reference)" : CV_d3d12_PTCache ? (nBounces > 0 ? "not available" : "off (no bounces)") : "off",
            XR_PT_CACHE_ENTRIES, XR_PT_CACHE_BYTES / 1024, PATH_CACHE_CELL, PATH_CACHE_CELL_PER_METRE, PATH_CACHE_FRAMES, PATH_CACHE_TILE,
            PATH_CACHE_TILE, bCacheRestart ? ", starting again" : "");
  if (m_pFrameDump && bNrd)
  {
    int nPasses = 0, nTables = 0, nTextures = 0;
    m_pNrd->GetStats(nPasses, nTables, nTextures);
    fprintf(m_pFrameDump.get(), "pt nrd: NVIDIA NRD %s REBLUR_DIFFUSE, %s, %d passes, %d descriptor tables, %d textures of its own, motion vectors: %s\n",
            CD3D12Nrd::GetVersion(), bNrdDone ? "denoised" : "failed", nNrdPasses, nTables, nTextures,
            s.nVelocitySRV >= 0 ? "the scene's" : "the camera's");
  }
}

void CD3D12Renderer::DrawPathDebug()
{
  if (CV_d3d12_PTDebug <= 0 || CV_d3d12_PTDebug > 12 || m_PathTrace.nGBufferFrame != m_nFrames || !m_PathTrace.pAlbedo || !m_pDevice ||
      m_pDevice->IsOffscreen() || !m_pRayScene || !m_pRayScene->GetTLAS())
    return;
  // The paths' light (12: its w, the terrain's share of its baked light) and the highlights: of this frame's (the radiance
  // cache's: CSTrace's output in the paths' place).
  CD3D12Texture *pLight = CV_d3d12_PTDebug == 5 || CV_d3d12_PTDebug == 12 ? m_PathTrace.pLight :
                          CV_d3d12_PTDebug == 6 || CV_d3d12_PTDebug == 11 ? m_PathTrace.pRays :
                          CV_d3d12_PTDebug == 10 ? m_PathTrace.pHighlights : nullptr;
  const bool bKernel = CV_d3d12_PTDebug == 5 || CV_d3d12_PTDebug == 6 || CV_d3d12_PTDebug == 10 || CV_d3d12_PTDebug == 11 || CV_d3d12_PTDebug == 12;
  if (bKernel && (!pLight || m_PathTrace.nLightFrame != m_nFrames))
    return;
  // (its state the device's: readable, left so)
  if (pLight)
    m_pDevice->Require(pLight->m_pResource, PATH_READ);
  // Over the whole back buffer (the ray debug program, which reads the G-buffer for these views).
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = w;
  m_Viewport[3] = h;
  m_bScissor = m_bItemScissor = false;
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_RayDebug;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, (float)(XR_RAY_DEBUG_GB_ALBEDO + CV_d3d12_PTDebug - 1), 1);
  Set4(dc.vParams2, (float)m_PathTrace.pAlbedo->m_nWidth, (float)m_PathTrace.pAlbedo->m_nHeight, 0, 0);
  dc.nTex0 = (uint32)m_PathTrace.pAlbedo->m_nSRV;
  dc.nTex1 = (uint32)m_PathTrace.pNormal->m_nSRV;
  dc.nTex2 = pLight ? (uint32)pLight->m_nSRV : CV_d3d12_PTDebug == 9 ? (uint32)m_PathTrace.pSpecular->m_nSRV : (uint32)m_PathTrace.pSun->m_nSRV;
  static const float s_Triangle[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_Triangle, 3);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
}
