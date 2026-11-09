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
//	SPathShade::nMask (TraceSun, TraceLight; PathSun, PathLampLight):
//	  - the sun: vSun * N.L;
//	  - a lamp: (2 * its colour)^2.2 * fLamps * N.L * (1 - smoothstep(0,
//	    0.9, d / radius))^2.2, the falloff the raster fits the lamps of the
//	    static objects to (2 * (1 - smoothstep) in the game's colour,
//	    SetPointLight), in light; directional ones without the falloff; a
//	    projector times the picture of its cube (2 * colour * cube before
//	    the power, as its pass), its ray ending SPTLight::vColor.w short of
//	    it where that is more than fLightGap (PathLampGap: the picture draws
//	    its fitting's shade).
//	    With d3d12_PTBakedFalloff > 0 a lamp baked into the light maps
//	    (XR_PT_LIGHT_LM) falls off as they do, slowly over its whole radius:
//	    ((1 - d / radius)^d3d12_PTBakedFalloff)^2.2 (PathLampFalloff).
//	The game's lights multiply its colours (the raster's colour * light):
//	in light, colour^2.2 * light^2.2 is that product's light below 1
//	(PathMultiplier), what most of the picture is.
//	The game's ambient light on a surface (SPathSurface::vAmbient): what its
//	ambient colour adds to the light 1 of its albedo in the raster, as light;
//	the paths fill in with it (PathTrace.hlsl).
//	PathLightmapFill: where the frame has no sun, what a light map lights a
//	surface with beyond the path tracer's own light: the raster's light
//	(4 * the map + the ambient light, the game's light) as light, less the
//	ambient light and the light of its lamps baked into the maps
//	(XR_PT_LIGHT_LM) without their shadows. Near those lamps the path
//	tracer's light and shadows alone; away from them the maps' flat light.
//	PathSampleLamp: the lamps' light by one ray, towards a lamp picked among
//	PT_LAMP_CANDIDATES of them by how much each gives without its shadow
//	(resampled importance sampling: all of them where there are no more).
//	TerrainBakedLight: the light the editor baked into a sector's texture
//	at a point, as light: vBakeLight * its share there (TerrainBakedShare)
//	((1 + (r - 1) N.L V) / r)^2.2, r = d3d12_PTTerrainDelight (the lit over
//	the shadowed of a face towards the sun, the game's colours), V the
//	sun's visibility over the terrain alone (XR_RAY_TERRAIN: its own
//	shadows are in the texture, those of the objects not), vBakeLight the
//	path tracer's own light on an open face towards the sun (the sun and
//	the sky), each channel: the texture's colour as light divided by it is
//	the albedo (a lit face as bright as the editor had it, and of its
//	colour: the texture has the colour of the light baked in, Rebellion's
//	blue moon, which the path tracer's light brings again).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PATHSHADE_HLSLI
#define XRENDERD3D12_PATHSHADE_HLSLI

#include "RayShade.hlsli"
#include "HdrLight.hlsli"

StructuredBuffer<SPTLight> g_PTLights : register(t2, space5);

//! The lamps PathSampleLamp weighs against each other at most.
static const uint PT_LAMP_CANDIDATES = 16;

//! What the path tracer's shading takes (from its constants, or a program's).
struct SPathShade
{
  float3 vSun;          //!< the sun's light on a face towards it (HdrDecode(2 * its colour) * d3d12_PTSun)
  float fLamps;         //!< d3d12_PTLamps
  float fEmission;      //!< d3d12_PTEmission
  float fDelight;       //!< d3d12_PTTerrainDelight (at most 1: the sector's colour is the albedo)
  float3 vBakeLight;    //!< the light baked into the terrain on a lit face towards the sun (the path tracer's sun and sky)
  float fLightGap;      //!< how far short of a lamp its ray ends (d3d12_RTLightShadowGap)
  float fBakedFalloff;  //!< d3d12_PTBakedFalloff: 0 the lamps baked into the light maps fall off as the dynamic ones, > 0 the
                        //!< power of (1 - d / r), the light maps' falloff
  float fLampSize;      //!< d3d12_PTLampSize: a lamp's radius as a share of its light's (PathLampTarget; 0: a point)
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
  float3 vAmbient;      //!< the game's ambient light there, as light (PathAmbientLight of SRayInstance::nPath.y)
  uint nMaterial;       //!< XR_PT_MAT_*
  bool bFog;            //!< fogged (DF_FOG)
};

//! The game's colour as light (HdrDecode of each channel).
float3 PathLight(float3 c)
{
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

//! The game's light (what its colours are multiplied by) as light.
float3 PathMultiplier(float3 c)
{
  return pow(max(c, 0), HDR_GAMMA);
}

//! The game's ambient light / 2 as the G-buffer (gba) and SRayInstance::nPath.y (PathPackAmbient) keep it, as light.
float3 PathAmbientLight(float3 vHalf)
{
  return PathMultiplier(2 * vHalf);
}

float3 PathUnpackAmbient(uint n)
{
  return PathAmbientLight(float3(n & 0xff, (n >> 8) & 0xff, (n >> 16) & 0xff) / 255);
}

float PathLuminance(float3 c)
{
  return dot(c, float3(0.2126, 0.7152, 0.0722));
}

//! A random number of 0..1 (24 bits) from the state nSeed, which it moves on (PCG).
float PathRandom(inout uint nSeed)
{
  nSeed = nSeed * 747796405u + 2891336453u;
  uint n = ((nSeed >> ((nSeed >> 28u) + 4u)) ^ nSeed) * 277803737u;
  n = (n >> 22u) ^ n;
  return (float)(n >> 8) * (1.0 / 16777216.0);
}

//! The share of SPathShade::vBakeLight baked into a terrain sector's texture at vPos (1 on a lit face; see the head of the file).
float TerrainBakedShare(float3 vPos, float3 vNormal, float3 vFace, SPathShade P, float fConeWidth)
{
  const float fNL = saturate(dot(vNormal, g_Frame.vSunDir.xyz));
  const float fVis = fNL > 0 ? TraceSun(vPos, vFace, XR_RAY_TERRAIN, fConeWidth, 0) : 0;
  return pow((1 + (P.fDelight - 1) * fNL * fVis) / P.fDelight, HDR_GAMMA);
}

//! The light baked into a terrain sector's texture at vPos, each channel (the game's colour; see the head of the file).
float3 TerrainBakedLight(float3 vPos, float3 vNormal, float3 vFace, SPathShade P, float fConeWidth)
{
  return P.vBakeLight * TerrainBakedShare(vPos, vNormal, vFace, P, fConeWidth);
}

//! A surface's normal for the paths at a hit of a draw of flags nFlags (DF_*), the vertices' normal vNormal and the face vFace
//! (on the ray's side): the vertices' turned to the face's side; the vegetation's opaque parts' (a trunk, its branches:
//! DF_VCOLOR_X2 without the alpha test) the face's, their vertices' are the crown's, straight up (a trunk's paths went up
//! into the crown, or, turned to the face's side, down into the ground: the trunks were black), as the G-buffer's
//! (General.hlsl).
float3 PathSurfaceNormal(uint nFlags, float3 vNormal, float3 vFace)
{
  if ((nFlags & (DF_VCOLOR_X2 | DF_ALPHATEST)) == DF_VCOLOR_X2)
    return vFace;
  return dot(vNormal, vFace) < 0 ? -vNormal : vNormal;
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
  O.vNormal = PathSurfaceNormal(I.nMaterial.x, S.vNormal, S.vFaceNormal);
  O.vSpecular = I.vSpecular;
  O.nMaterial = I.nPath.x;
  O.vAmbient = PathUnpackAmbient(I.nPath.y);
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
  O.vAlbedo = pow(saturate(vAlbedo), HDR_GAMMA);
  if (O.nMaterial == XR_PT_MAT_TERRAIN && P.fDelight > 1)
    O.vAlbedo = saturate(O.vAlbedo / max(TerrainBakedLight(S.vPos, O.vNormal, O.vFaceNormal, P, fConeWidth), 0.01));
  O.vEmission = PathLight(max(vEmission, 0)) * P.fEmission;
  return O;
}

//! The sun's light on a point at vPos (normal vN, face vFace) by a ray towards it; fConeWidth the ray cone's width there.
float3 PathSun(float3 vPos, float3 vN, float3 vFace, SPathShade P, float fConeWidth)
{
  const float fNL = dot(vN, g_Frame.vSunDir.xyz);
  if (fNL <= 0 || all(P.vSun <= 0))
    return 0;
  return P.vSun * fNL * TraceSun(vPos, vFace, P.nMask, fConeWidth, P.nMaxTests);
}

//! A lamp's falloff, as light, at x = its distance over its radius: as the raster's dynamic light (1 - smoothstep(0, 0.9,
//! x))^2.2, nothing from 0.9 on; a lamp baked into the light maps (bLightMap) with fBakedFalloff > 0 (d3d12_PTBakedFalloff)
//! as they light the level, falling off slowly over its whole radius: ((1 - x)^fBakedFalloff)^2.2 (the game's colour).
float PathLampFalloff(float x, bool bLightMap, float fBakedFalloff)
{
  const bool bBaked = fBakedFalloff > 0 && bLightMap;
  if (x >= (bBaked ? 1 : 0.9))
    return 0;
  return pow(bBaked ? pow(1 - x, fBakedFalloff) : 1 - smoothstep(0, 0.9, x), HDR_GAMMA);
}

//! Lamp i's light on a point at vPos with the normal vN without its shadow (see the head of the file), 0 where it does
//! not reach; vToLight the direction towards it, fDistance how far it is.
float3 PathLampLight(uint i, float3 vPos, float3 vN, SPathShade P, out float3 vToLight, out float fDistance)
{
  const SPTLight L = g_PTLights[i];
  float fFalloff = 1;
  if (L.nParams.x == XR_PT_LIGHT_DIRECTIONAL)
  {
    vToLight = L.vPos.xyz;
    fDistance = 2000;
  }
  else
  {
    vToLight = L.vPos.xyz - vPos;
    fDistance = length(vToLight);
    fFalloff = PathLampFalloff(fDistance / L.vPos.w, (L.nParams.y & XR_PT_LIGHT_LM) != 0, P.fBakedFalloff);
    if (fFalloff <= 0)
      return 0;
    vToLight /= max(fDistance, 1e-4);
  }
  const float fNL = dot(vN, vToLight);
  if (fNL <= 0)
    return 0;
  float3 vColor = 2 * L.vColor.rgb;
  if (L.nParams.x == XR_PT_LIGHT_PROJECTOR && L.nParams.z != XR_RAY_NONE)
    vColor *= g_Cubes[NonUniformResourceIndex(L.nParams.z)].SampleLevel(g_SamplerLinearClamp, mul(float4(vPos, 1), L.mProject).xyz, 0).rgb;
  return PathMultiplier(vColor) * (P.fLamps * fFalloff * fNL);
}

//! How far short of lamp L the rays towards it end: d3d12_RTLightShadowGap, or the lamp's own where that is more (a
//! projector's d3d12_RTProjectorShadowGap: its picture draws its fitting's shade).
float PathLampGap(SPTLight L, SPathShade P)
{
  return max(P.fLightGap, L.vColor.w);
}

//! What a light map lights a point at vPos with the normal vN with beyond the path tracer's own light (see the head of the
//! file): vMap the map's light / 4 (the G-buffer's, D3D12_PT_LIGHTMAP_FORMAT), vHalfAmbient the game's ambient light / 2;
//! as light, 0 where its lamps give as much.
float3 PathLightmapFill(float3 vMap, float3 vHalfAmbient, float3 vPos, float3 vN, SPathShade P)
{
  if (all(vMap <= 0))
    return 0;
  float3 vFill = PathMultiplier(4 * vMap + 2 * vHalfAmbient) - PathAmbientLight(vHalfAmbient);
  [loop]
  for (uint i = 0; i < P.nLights && any(vFill > 0); i++)
  {
    if (!(g_PTLights[i].nParams.y & XR_PT_LIGHT_LM))
      continue;
    float3 vToLight;
    float fDistance;
    vFill -= PathLampLight(i, vPos, vN, P, vToLight, fDistance);
  }
  return max(vFill, 0);
}

//! The light of the sun and the frame's lamps on the surface (see the head of the file); fConeWidth the ray cone's width there.
float3 PathDirect(SPathSurface H, SPathShade P, float fConeWidth)
{
  float3 vLight = PathSun(H.vPos, H.vNormal, H.vFaceNormal, P, fConeWidth);
  [loop]
  for (uint i = 0; i < P.nLights; i++)
  {
    float3 vToLight;
    float fDistance;
    const float3 vLamp = PathLampLight(i, H.vPos, H.vNormal, P, vToLight, fDistance);
    if (all(vLamp <= 0))
      continue;
    float fHit;
    vLight += vLamp * TraceLight(H.vPos, H.vFaceNormal, vToLight, fDistance, PathLampGap(g_PTLights[i], P), P.nMask, fConeWidth, fHit,
                                 P.nMaxTests);
  }
  return vLight;
}

//! d3d12_PTLampSize: the way from a point towards a lamp with a size, for its shadow's ray. vToLight (normalised) and
//! fDistance the way to its centre, fRadius the lamp's radius (its light's, SPTLight::vPos.w, times the share), u, v
//! random numbers of 0..1: towards a point of the disc of that radius across the way, picked evenly (a sphere as the
//! point sees it), and the gap short of it at least the radius (what is inside the lamp casts no shadow); the radius
//! at most half the distance. The lamp's light stays its centre's (PathLampLight); its shadow's edge, the share of the
//! disc a caster hides, is soft.
void PathLampTarget(inout float3 vToLight, inout float fDistance, inout float fGap, float fRadius, float u, float v)
{
  const float r = min(fRadius, 0.5 * fDistance);
  if (r <= 0)
    return;
  const float3 vAxis = abs(vToLight.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0);
  const float3 t = normalize(cross(vAxis, vToLight)), b = cross(vToLight, t);
  const float a = 6.28318530718 * u, s = r * sqrt(v);
  const float3 vTo = vToLight * fDistance + (t * cos(a) + b * sin(a)) * s;
  fDistance = length(vTo);
  vToLight = vTo / fDistance;
  fGap = max(fGap, r);
}

//! The lamps' light on a point at vPos (normal vN, face vFace) by one ray (see the head of the file); nSeed the random
//! numbers' state, fConeWidth the ray cone's width there. With d3d12_PTLampSize the ray goes towards a point of the
//! lamp's disc (PathLampTarget): soft shadows.
float3 PathSampleLamp(float3 vPos, float3 vN, float3 vFace, SPathShade P, inout uint nSeed, float fConeWidth)
{
  if (P.nLights == 0)
    return 0;
  const bool bAll = P.nLights <= PT_LAMP_CANDIDATES;
  const uint nCandidates = bAll ? P.nLights : PT_LAMP_CANDIDATES;
  float fSum = 0, fPicked = 0, fPickedDistance = 0, fPickedGap = 0, fPickedRadius = 0;
  float3 vPicked = 0, vPickedDir = 0;
  [loop]
  for (uint k = 0; k < nCandidates; k++)
  {
    const uint i = bAll ? k : min((uint)(PathRandom(nSeed) * P.nLights), P.nLights - 1);
    float3 vToLight;
    float fDistance;
    const float3 vLamp = PathLampLight(i, vPos, vN, P, vToLight, fDistance);
    const float w = PathLuminance(vLamp);
    if (w <= 0)
      continue;
    fSum += w;
    if (PathRandom(nSeed) * fSum < w)
    {
      fPicked = w;
      vPicked = vLamp;
      vPickedDir = vToLight;
      fPickedDistance = fDistance;
      fPickedGap = PathLampGap(g_PTLights[i], P);
      fPickedRadius = g_PTLights[i].nParams.x == XR_PT_LIGHT_DIRECTIONAL ? 0 : P.fLampSize * g_PTLights[i].vPos.w;
    }
  }
  if (fPicked <= 0)
    return 0;
  if (fPickedRadius > 0)
  {
    const float u = PathRandom(nSeed), v = PathRandom(nSeed);
    PathLampTarget(vPickedDir, fPickedDistance, fPickedGap, fPickedRadius, u, v);
  }
  // The picked one's light over its chance: fSum / fPicked among the candidates, they nCandidates of all the lamps.
  const float fScale = fSum / fPicked * (bAll ? 1 : (float)P.nLights / nCandidates);
  float fHit;
  return vPicked * fScale * TraceLight(vPos, vFace, vPickedDir, fPickedDistance, fPickedGap, P.nMask, fConeWidth, fHit, P.nMaxTests);
}

#endif
