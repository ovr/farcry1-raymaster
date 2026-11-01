// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathTrace.cpp
//	Description: The path tracer of the main view's lighting
//	(d3d12_PathTracing, on a device with DXR 1.1).
//
//	The lights: once a frame, before the main view's opaque list, the
//	scene's lights besides the sun become SPTLight in per frame memory
//	(Shaders/RayConstants.h), bound to the kernels with rays as the root SRV
//	t2 space5 (CD3D12Compute::ROOT_RAY_LIGHTS): the point lights, the
//	directional ones and the projectors with their cube, those baked into
//	the light maps (DLF_LM) too, as the path tracer lights in their place.
//	Not among them: the fake lights (DLF_FAKE, never in the scene's list),
//	a heat source that gives no light (heat vision), the characters' fill
//	lights (DLF_AMBIENT_LIGHT) without d3d12_PTFillLights.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Upload.h"
#include "RayTracing.h"
#include "Texture.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
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

void CD3D12Renderer::UpdatePathLights(std::span<CDLight *const> Lights)
{
  if (!WantsPathTracing() || !m_pUpload || m_PathTrace.nLightsFrame == m_nFrames)
    return;
  m_PathTrace.nLightsFrame = m_nFrames;
  m_PathTrace.nLights = 0;
  m_PathTrace.nLightCount = 0;

  std::vector<SPTLight> Out;
  Out.reserve(std::min<size_t>(Lights.size(), XR_PT_MAX_LIGHTS));
  int nSkippedHeat = 0, nSkippedFill = 0, nSkippedImage = 0;
  for (size_t i = 0; i < Lights.size() && Out.size() < XR_PT_MAX_LIGHTS; i++)
  {
    const CDLight *pLight = Lights[i];
    if (!pLight || (pLight->m_Flags & DLF_SUN))
      continue;
    const uint32 nFlags = (uint32)pLight->m_Flags;
    // A heat source alone gives no light (heat vision, ItemHeat), as SetObjectLights.
    if ((nFlags & (DLF_HEATSOURCE | DLF_LIGHTSOURCE)) == DLF_HEATSOURCE)
    {
      nSkippedHeat++;
      continue;
    }
    if ((nFlags & DLF_AMBIENT_LIGHT) && !CV_d3d12_PTFillLights)
    {
      nSkippedFill++;
      continue;
    }
    SPTLight L = {};
    uint32 nKind = XR_PT_LIGHT_POINT;
    L.nParams[2] = XR_RAY_NONE;
    if (nFlags & DLF_PROJECT)
    {
      // The projectors with a cube (the picture on +X), as the passes of DrawLeafItem.
      CTexture *pCube = static_cast<CTexture *>(pLight->m_pLightImage);
      if (!pCube || !pCube->IsCube() || !pCube->IsTextureLoaded())
      {
        nSkippedImage++;
        continue;
      }
      nKind = XR_PT_LIGHT_PROJECTOR;
      L.nParams[2] = (uint32)pCube->GetSRV();
      memcpy(L.mProject, pLight->m_TextureMatrix.data, sizeof(L.mProject));
    }
    else if (nFlags & DLF_DIRECTIONAL)
      nKind = XR_PT_LIGHT_DIRECTIONAL;
    if (nKind == XR_PT_LIGHT_DIRECTIONAL)
    {
      // From its origin towards the camera, as SetObjectLights does towards the object.
      Vec3 vDir = pLight->m_Origin - m_Camera.GetPos();
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
                   ((nFlags & DLF_HEATSOURCE) ? XR_PT_LIGHT_HEAT : 0u);
    L.nParams[3] = (uint32)i;
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
    int nLM = 0;
    for (const SPTLight &L : Out)
      nLM += (L.nParams[1] & XR_PT_LIGHT_LM) ? 1 : 0;
    fprintf(pDump, "pt lights: %u of %d scene lights (%d baked into the light maps; left out: %d heat sources, %d fill lights, %d projectors "
            "without a cube, the sun)\n", m_PathTrace.nLightCount, (int)Lights.size(), nLM, nSkippedHeat, nSkippedFill, nSkippedImage);
    for (const SPTLight &L : Out)
    {
      const CDLight *pLight = Lights[L.nParams[3]];
      fprintf(pDump, "  pt light %u: %s, flags 0x%08x, %s (%.2f %.2f %.2f), radius %.2f, colour (%.3f %.3f %.3f), specular %.3f%s%s, \"%.63s\"\n",
              L.nParams[3], LightKind(L.nParams[0]), (unsigned)pLight->m_Flags, L.nParams[0] == XR_PT_LIGHT_DIRECTIONAL ? "towards" : "at",
              L.vPos[0], L.vPos[1], L.vPos[2], L.vPos[3], L.vColor[0], L.vColor[1], L.vColor[2], L.vColor[3],
              (L.nParams[1] & XR_PT_LIGHT_LM) ? ", baked" : "", (L.nParams[1] & XR_PT_LIGHT_FILL) ? ", fill" : "", pLight->m_Name);
    }
  }
}
