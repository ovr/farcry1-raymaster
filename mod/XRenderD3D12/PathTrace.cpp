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
//	lights (DLF_AMBIENT_LIGHT) without d3d12_PTFillLights.
//	The materials: the scene for rays' instances have them (SRayInstance
//	vAlbedo, vSpecular, nPath; DrawLeafItem), Shaders/PathShade.hlsli
//	shades its hits; while the path tracer is on the vegetation's vertex
//	colours are baked without light (Geometry::SetUnlitVegetation).
//	d3d12_RTDebug 8 to 10 show the hits as it takes them (RayDebug.hlsl).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Upload.h"
#include "RayTracing.h"
#include "Texture.h"
#include "Geometry.h"
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

float Luminance(float r, float g, float b)
{
  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
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
  return m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_PathTracing > 0;
}

bool CD3D12Renderer::WantsPathDebug() const
{
  return m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_RTDebug >= XR_RAY_DEBUG_PT_ALBEDO && CV_d3d12_RTDebug <= XR_RAY_DEBUG_PT_DIRECT;
}

void CD3D12Renderer::GetPathShade(SPathShadeParams &P) const
{
  const float fSun = std::max(CV_d3d12_PTSun, 0.0f);
  for (int c = 0; c < 3; c++)
    P.vSun[c] = HdrDecode(2 * m_SunColor[c]) * fSun;
  P.fLamps = std::max(CV_d3d12_PTLamps, 0.0f);
  P.fEmission = std::max(CV_d3d12_PTEmission, 0.0f);
  P.fDelight = CV_d3d12_PTTerrainDelight;
  // The editor's light on a lit face of the terrain towards the sun, as the game's other lights take it: the ambient plus
  // twice the sun.
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  const Vec3 vOutdoor = p3D ? p3D->GetOutdoorAmbientColor() : Vec3(0.5f, 0.5f, 0.5f);
  P.fBakeLight = std::max(Luminance(vOutdoor.x + 2 * m_SunColor[0], vOutdoor.y + 2 * m_SunColor[1], vOutdoor.z + 2 * m_SunColor[2]), 0.05f);
  P.fLightGap = std::max(CV_d3d12_RTLightShadowGap, 0.0f);
  P.nLights = m_PathTrace.nLightsFrame == m_nFrames ? m_PathTrace.nLightCount : 0;
}

void CD3D12Renderer::UpdatePathLights(std::span<CDLight *const> Lights)
{
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
      CTexture *pCube = static_cast<CTexture *>(pLight->m_pLightImage);
      if (!pCube || !pCube->IsCube() || !pCube->IsTextureLoaded())
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
      // The projectors with a cube (the picture on +X), as the passes of DrawLeafItem.
      CTexture *pCube = static_cast<CTexture *>(pLight->m_pLightImage);
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
    Set4(L.vColor, c.r, c.g, c.b, (s.r + s.g + s.b) / 3);
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
      fprintf(pDump, "  pt light %zu: %s, flags 0x%08x, %s (%.2f %.2f %.2f), radius %.2f, colour (%.3f %.3f %.3f), specular %.3f%s%s%s, %.1f m away, \"%.63s\"\n",
              i, LightKind(L.nParams[0]), (unsigned)pLight->m_Flags, L.nParams[0] == XR_PT_LIGHT_DIRECTIONAL ? "towards" : "at",
              L.vPos[0], L.vPos[1], L.vPos[2], L.vPos[3], L.vColor[0], L.vColor[1], L.vColor[2], L.vColor[3],
              (L.nParams[1] & XR_PT_LIGHT_LM) ? ", baked" : "", (L.nParams[1] & XR_PT_LIGHT_FILL) ? ", fill" : "",
              (L.nParams[1] & XR_PT_LIGHT_STATIC) ? ", static" : "", Candidates[i].fKey, pLight->m_Name);
    }
  }
}
