// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathShade.hlsli
//	Description: The path tracer's materials and direct light at the
//	hits of its rays (d3d12_PathTracing, PathTrace.cpp). Shader Model 6.5
//	(inline RayQuery), with RayShade.hlsli.
//
//	Its light is linear, as the HDR target's after HdrDecode: a surface
//	gives back emission + albedo * the light on it.
//	PathSurface: the hit's albedo and emission, linear:
//	  - the albedo: the draw's colour as RayShade makes it, without the
//	    light: vAlbedo times the vertex colour (DF_VCOLOR, DF_VCOLOR_X2),
//	    texture 0 (DF_CUBE0's reflection too), a texture 1 that is not a
//	    light map, the detail texture's and the cliffs' mean (times 2), at
//	    most 1, then c^2.2. By the material (SRayInstance::nPath.x):
//	    XR_PT_MAT_TERRAIN divided by the light baked into the sector's
//	    texture first (TerrainBakedLight); XR_PT_MAT_VEGETATION the vertex
//	    colours the renderer bakes without light with d3d12_PathTracing
//	    (LeafBuffer.cpp); XR_PT_MAT_UNLIT none, its colour is its emission.
//	  - the emission: DF2_ADD_TEX4 (the glow maps), DF2_ALPHA_GLOW and all
//	    of an unlit draw's colour, HdrDecode of the game's colour, times
//	    SPathShade::fEmission.
//	PathDirect: the light of the sun and of the frame's lamps (g_PTLights)
//	on the hit, each by a ray towards it among the instances of
//	SPathShade::nMask (TraceSun, TraceLight):
//	  - the sun: vSun * N.L;
//	  - a lamp: HdrDecode(2 * its colour) * fLamps * N.L * (1 -
//	    smoothstep(0, 0.9, d / radius))^2.2, the falloff the raster fits
//	    the lamps of the static objects to (2 * (1 - smoothstep) in the
//	    game's colour, SetPointLight), in light; directional ones without
//	    the falloff; a projector times the picture of its cube (2 * colour
//	    * cube before HdrDecode, as its pass).
//	TerrainBakedLight: the light the editor baked into a sector's texture
//	at a point, the game's colour: fBakeLight * (1 + (r - 1) N.L V) / r, r
//	= d3d12_PTTerrainDelight (the lit over the shadowed of a face towards
//	the sun), V the sun's visibility over the terrain alone (XR_RAY_TERRAIN:
//	its own shadows are in the texture, those of the objects not).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PATHSHADE_HLSLI
#define XRENDERD3D12_PATHSHADE_HLSLI

#include "RayShade.hlsli"
#include "HdrLight.hlsli"

StructuredBuffer<SPTLight> g_PTLights : register(t2, space5);

//! What the path tracer's shading takes (from its constants, or a program's).
struct SPathShade
{
  float3 vSun;          //!< the sun's light on a face towards it (HdrDecode(2 * its colour) * d3d12_PTSun)
  float fLamps;         //!< d3d12_PTLamps
  float fEmission;      //!< d3d12_PTEmission
  float fDelight;       //!< d3d12_PTTerrainDelight (at most 1: the sector's colour is the albedo)
  float fBakeLight;     //!< the light baked into the terrain on a lit face towards the sun, the game's colour
  float fLightGap;      //!< how far short of a lamp its ray ends (d3d12_RTLightShadowGap)
  uint nLights;         //!< the lamps in g_PTLights
  uint nMask;           //!< the instances the rays towards the sun and the lamps see
  uint nMaxTests;       //!< their alpha tests at most
};

//! A point of the scene as the path tracer takes it.
struct SPathSurface
{
  float3 vPos;
  float3 vNormal;       //!< the vertices' normal, on the side the ray comes from
  float3 vFaceNormal;   //!< the triangle's, on that side
  float3 vAlbedo;       //!< linear, 0..1
  float3 vEmission;     //!< linear
  float4 vSpecular;     //!< SRayInstance::vSpecular (the game's colour, the shininess)
  uint nMaterial;       //!< XR_PT_MAT_*
  bool bFog;            //!< fogged (DF_FOG)
};

//! The game's colour as light (HdrDecode of each channel).
float3 PathLight(float3 c)
{
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

float PathLuminance(float3 c)
{
  return dot(c, float3(0.2126, 0.7152, 0.0722));
}

//! The light baked into a terrain sector's texture at S (the game's colour; see the head of the file).
float TerrainBakedLight(SRaySurface S, float3 vNormal, float3 vFace, SPathShade P, float fConeWidth)
{
  const float fNL = saturate(dot(vNormal, g_Frame.vSunDir.xyz));
  const float fVis = fNL > 0 ? TraceSun(S.vPos, vFace, XR_RAY_TERRAIN, fConeWidth, 0) : 0;
  return P.fBakeLight * (1 + (P.fDelight - 1) * fNL * fVis) / P.fDelight;
}

//! The material at the hit (see the head of the file); fConeWidth the ray cone's width there.
SPathSurface PathSurface(SRayHit Hit, float3 vRayDir, float fConeWidth, SPathShade P)
{
  const SRayInstance I = g_RayInstances[Hit.nInstance];
  const SRaySurface S = RaySurface(Hit.nInstance, Hit.nPrimitive, Hit.vBary, Hit.mObjectToWorld, vRayDir, fConeWidth);
  const uint nFlags = I.nMaterial.x, nFlags2 = I.nMaterial.y, nSampler1 = (I.nMaterial2.x >> 4) & 15;
  SPathSurface O;
  O.vPos = S.vPos;
  O.vFaceNormal = S.vFaceNormal;
  O.vNormal = dot(S.vNormal, S.vFaceNormal) < 0 ? -S.vNormal : S.vNormal;
  O.vSpecular = I.vSpecular;
  O.nMaterial = I.nPath.x;
  O.bFog = (nFlags & DF_FOG) != 0;
  O.vEmission = 0;
  if (I.nGeom.x == XR_RAY_NONE)
  {
    O.vAlbedo = 0.2;
    return O;
  }

  float4 vColor = I.vAlbedo;
  if (nFlags & (DF_VCOLOR | DF_VCOLOR_X2))
  {
    float4 vVertex = S.vColor;
    if (nFlags & DF_VCOLOR_X2)
      vVertex.rgb *= 2;
    if (nFlags & DF_VALPHA_ONLY)
      vVertex.rgb = 1;
    vColor *= vVertex;
  }
  const float4 vTex = RayTexture0(I, S, vRayDir);
  vColor = (nFlags & DF_TEX0_ALPHA_ONLY) ? float4(vColor.rgb, vColor.a * vTex.a) : vColor * vTex;
  float3 vEmission = 0;
  if (nFlags2 & DF2_ADD_TEX4)
    vEmission += RaySampleTexture(I.nMaterial2.z, XR_SAMPLER_LINEAR_WRAP, S.vTex0, S.fLod).rgb * I.vAddColor.rgb + I.vAddColor.w;
  if (nFlags & DF_TEX1_DETAIL)
    vColor.rgb *= 2;
  else if ((nFlags & DF_TEX1) && !(nFlags & DF_TEX1_LIGHTMAP))
    vColor *= RaySampleTexture(I.nMaterial.w, nSampler1, S.vTex1, S.fLod1);
  if (nFlags2 & DF2_ALPHA_GLOW)
    vEmission += vTex.rgb * vTex.a;
  if (nFlags & DF_TERRAIN_CLIFF)
    vColor.rgb *= 2;

  float3 vAlbedo = vColor.rgb;
  if (O.nMaterial == XR_PT_MAT_UNLIT)
  {
    vEmission += vAlbedo;
    vAlbedo = 0;
  }
  else if (O.nMaterial == XR_PT_MAT_TERRAIN && P.fDelight > 1)
    vAlbedo /= max(TerrainBakedLight(S, O.vNormal, O.vFaceNormal, P, fConeWidth), 0.05);
  O.vAlbedo = pow(saturate(vAlbedo), HDR_GAMMA);
  O.vEmission = PathLight(max(vEmission, 0)) * P.fEmission;
  return O;
}

//! The light of the sun and the frame's lamps on the surface (see the head of the file); fConeWidth the ray cone's width there.
float3 PathDirect(SPathSurface H, SPathShade P, float fConeWidth)
{
  float3 vLight = 0;
  const float fSunNL = dot(H.vNormal, g_Frame.vSunDir.xyz);
  if (fSunNL > 0 && any(P.vSun > 0))
    vLight += P.vSun * fSunNL * TraceSun(H.vPos, H.vFaceNormal, P.nMask, fConeWidth, P.nMaxTests);
  [loop]
  for (uint i = 0; i < P.nLights; i++)
  {
    const SPTLight L = g_PTLights[i];
    float3 vToLight;
    float fDistance, fFalloff = 1;
    if (L.nParams.x == XR_PT_LIGHT_DIRECTIONAL)
    {
      vToLight = L.vPos.xyz;
      fDistance = 2000;
    }
    else
    {
      vToLight = L.vPos.xyz - H.vPos;
      fDistance = length(vToLight);
      const float x = fDistance / L.vPos.w;
      if (x >= 0.9)
        continue;
      vToLight /= max(fDistance, 1e-4);
      fFalloff = pow(1 - smoothstep(0, 0.9, x), HDR_GAMMA);
    }
    const float fNL = dot(H.vNormal, vToLight);
    if (fNL <= 0)
      continue;
    float3 vColor = 2 * L.vColor.rgb;
    if (L.nParams.x == XR_PT_LIGHT_PROJECTOR && L.nParams.z != XR_RAY_NONE)
      vColor *= g_Cubes[NonUniformResourceIndex(L.nParams.z)].SampleLevel(g_SamplerLinearClamp, mul(float4(H.vPos, 1), L.mProject).xyz, 0).rgb;
    const float3 vLamp = PathLight(vColor) * (P.fLamps * fFalloff * fNL);
    if (all(vLamp <= 0))
      continue;
    float fHit;
    vLight += vLamp * TraceLight(H.vPos, H.vFaceNormal, vToLight, fDistance, P.fLightGap, P.nMask, fConeWidth, fHit, P.nMaxTests);
  }
  return vLight;
}

#endif
