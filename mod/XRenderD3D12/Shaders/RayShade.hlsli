// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayShade.hlsli
//	Description: Rays into the scene (RayTracing.h) and the colour of what
//	they hit, for the programs with rays (Shader Model 6.5, inline
//	RayQuery).
//
//	TraceScene: the nearest hit of a ray among the instances of a mask. An
//	alpha tested triangle (not opaque) is a hit where its draw's alpha test
//	passes (the vertex colour's and texture 0's alpha, as ShadePixel),
//	sampled at the ray cone's mip level; after XR_RAY_MAX_ALPHA_TESTS
//	tests on one ray the rest count as they are.
//	RaySurface: the hit's point, normals, vertex colour and texture
//	coordinates from the instance's vertices and indices (g_Buffers), uv0
//	through its texgen and texture matrix, and the mip level of a texture
//	under a ray cone of that width there (ray cones: the cone's width over
//	the triangle's size in the texture's texels, the slant by the face's
//	angle to the ray).
//	RayShade: the colour of the draw at the hit as General.hlsl's
//	ShadePixel makes it, the part that does without the draw's lights and
//	its other textures: vColor and the vertex colour (DF_VCOLOR*), the
//	sun and the ambient colour for DF_LIGHTING (s * saturate(ambient / s +
//	sun N.L), DF2_MATERIAL_LIGHT saturate(ambient + vColor * sun N.L)),
//	texture 0 (DF_TEX0, DF_TEX0_ARRAY, DF_CUBE0 with the ray reflected),
//	DF2_ADD_TEX4, the light map (4 * sun * light map + ambient), texture 1,
//	DF2_ALPHA_GLOW; the detail texture and the cliffs' as their mean
//	(rgb * 2); the sun's visibility by a ray towards it (TraceSun) for the
//	draws the sun's shadow falls on (DF_SHADOWS, DF2_SUN_CASCADES), with
//	DF2_SHADOW_TINT. Not fogged: the caller fogs it by its path.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RAYSHADE_HLSLI
#define XRENDERD3D12_RAYSHADE_HLSLI

#include "Common.hlsli"
#include "RayConstants.h"

RaytracingAccelerationStructure g_RayScene : register(t0, space5);
StructuredBuffer<SRayInstance> g_RayInstances : register(t1, space5);
ByteAddressBuffer g_Buffers[] : register(t0, space4);

//! The nearest hit of a ray.
struct SRayHit
{
  bool bHit;
  float fT;
  uint nInstance;           //!< InstanceID: g_RayInstances
  uint nPrimitive;
  float2 vBary;
  float3x4 mObjectToWorld;
  uint nAlphaTests;         //!< the alpha tests on the way (XR_RAY_DEBUG_ALPHA)
};

//! A point of a triangle of the scene.
struct SRaySurface
{
  float3 vPos;              //!< world space
  float3 vNormal;           //!< the vertices' normal in world space, normalised (the default stream's (0, 0, 1) where the format has none)
  float3 vFaceNormal;       //!< the triangle's, on the side the ray comes from
  float4 vColor;            //!< the vertex colour, RGBA (white where the format has none)
  float2 vTex0;             //!< after the texgen and the texture matrix
  float2 vTex1;
  float fLod;               //!< uv0's mip level of a texture of one texel under the cone (RayTextureLevel)
  float fLod1;              //!< uv1's
};

uint RayLoad(uint nBuffer, uint nByte)
{
  return g_Buffers[NonUniformResourceIndex(nBuffer)].Load(nByte);
}

float2 RayLoad2(uint nBuffer, uint nByte)
{
  return asfloat(g_Buffers[NonUniformResourceIndex(nBuffer)].Load2(nByte));
}

float3 RayLoad3(uint nBuffer, uint nByte)
{
  return asfloat(g_Buffers[NonUniformResourceIndex(nBuffer)].Load3(nByte));
}

//! Index n of the instance's triangles (n itself without indices).
uint RayIndex(SRayInstance I, uint n)
{
  if (I.nGeom.z == XR_RAY_NONE)
    return n;
  if (I.nLayout.x & XR_RAY_INDEX32)
    return RayLoad(I.nGeom.z, I.nGeom.w + 4 * n);
  const uint nByte = I.nGeom.w + 2 * n;
  const uint nWord = RayLoad(I.nGeom.z, nByte & ~3u);
  return (nByte & 2) ? nWord >> 16 : nWord & 0xffff;
}

//! The engine's UCol (bytes B, G, R, A) as RGBA.
float4 RayColor(uint n)
{
  return float4((n >> 16) & 255, (n >> 8) & 255, n & 255, n >> 24) / 255.0;
}

//! Twice the area of a triangle in uv.
float UVArea(float2 vUV[3])
{
  const float2 a = vUV[1] - vUV[0], b = vUV[2] - vUV[0];
  return abs(a.x * b.y - a.y * b.x);
}

float2 RayTexGen(SRayInstance I, float2 vUV, float3 vWorld)
{
  if (I.nMaterial.x & DF_TEXGEN_WORLD)
    vUV = float2(dot(float4(vWorld, 1), I.vTexGenU), dot(float4(vWorld, 1), I.vTexGenV));
  if (I.nMaterial.x & DF_TEXMATRIX)
    vUV = float2(dot(float3(vUV, 1), I.vTexMat0.xyz), dot(float3(vUV, 1), I.vTexMat1.xyz));
  return vUV;
}

//! The point at barycentrics vBary of triangle nPrimitive of instance nInstance, its texture's mip
//! level under a ray cone fConeWidth wide there along vRayDir.
SRaySurface RaySurface(uint nInstance, uint nPrimitive, float2 vBary, float3x4 mObjectToWorld, float3 vRayDir, float fConeWidth)
{
  const SRayInstance I = g_RayInstances[nInstance];
  SRaySurface S;
  if (I.nGeom.x == XR_RAY_NONE)
  {
    // No view of its buffers (RayShade makes it grey).
    S.vPos = 0;
    S.vNormal = S.vFaceNormal = -vRayDir;
    S.vColor = 1;
    S.vTex0 = S.vTex1 = 0;
    S.fLod = S.fLod1 = 0;
    return S;
  }
  const uint3 vIndex = uint3(RayIndex(I, 3 * nPrimitive), RayIndex(I, 3 * nPrimitive + 1), RayIndex(I, 3 * nPrimitive + 2));
  const uint3 vByte = I.nGeom.y + vIndex * (I.nLayout.x & 0xffff);
  const float3 vW = float3(1 - vBary.x - vBary.y, vBary.x, vBary.y);
  float3 p[3];
  [unroll] for (uint i = 0; i < 3; i++)
    p[i] = mul(mObjectToWorld, float4(RayLoad3(I.nGeom.x, vByte[i]), 1));
  S.vPos = p[0] * vW.x + p[1] * vW.y + p[2] * vW.z;
  const float3 vCross = cross(p[1] - p[0], p[2] - p[0]);
  const float fArea = length(vCross);                  // twice the triangle's
  const float3 vFace = fArea > 0 ? vCross / fArea : float3(0, 0, 1);
  S.vFaceNormal = dot(vFace, vRayDir) > 0 ? -vFace : vFace;

  const uint nNormal = I.nLayout.y & 0xff, nColor = (I.nLayout.y >> 8) & 0xff, nTex0 = (I.nLayout.y >> 16) & 0xff, nTex1 = I.nLayout.y >> 24;
  float3 vN = float3(0, 0, 1);
  float4 vColor = 1;
  float2 vUV0[3] = { float2(0, 0), float2(0, 0), float2(0, 0) };
  float2 vUV1[3] = { float2(0, 0), float2(0, 0), float2(0, 0) };
  [unroll] for (uint j = 0; j < 3; j++)
  {
    if (nTex0 != 0xff)
      vUV0[j] = RayLoad2(I.nGeom.x, vByte[j] + nTex0);
    vUV0[j] = RayTexGen(I, vUV0[j], p[j]);
    if (I.nLayout.z != XR_RAY_NONE)
      vUV1[j] = RayLoad2(I.nLayout.z, I.nLayout.w + vIndex[j] * 8);
    else if (nTex1 != 0xff)
      vUV1[j] = RayLoad2(I.nGeom.x, vByte[j] + nTex1);
  }
  if (nNormal != 0xff)
    vN = RayLoad3(I.nGeom.x, vByte.x + nNormal) * vW.x + RayLoad3(I.nGeom.x, vByte.y + nNormal) * vW.y + RayLoad3(I.nGeom.x, vByte.z + nNormal) * vW.z;
  if (nColor != 0xff)
    vColor = RayColor(RayLoad(I.nGeom.x, vByte.x + nColor)) * vW.x + RayColor(RayLoad(I.nGeom.x, vByte.y + nColor)) * vW.y +
      RayColor(RayLoad(I.nGeom.x, vByte.z + nColor)) * vW.z;
  S.vNormal = SafeNormalize(mul((float3x3)mObjectToWorld, vN));
  if (dot(S.vNormal, S.vNormal) == 0)
    S.vNormal = S.vFaceNormal;
  S.vColor = vColor;
  S.vTex0 = vUV0[0] * vW.x + vUV0[1] * vW.y + vUV0[2] * vW.z;
  S.vTex1 = vUV1[0] * vW.x + vUV1[1] * vW.y + vUV1[2] * vW.z;

  // The cone's footprint in uv per texel of a texture 1 x 1: the triangle's area in uv over its area
  // in the world, the cone's width over it slanted by the face's angle.
  const float fCone = log2(max(fConeWidth, 1e-6) / max(abs(dot(vFace, vRayDir)), 1e-3)) - 0.5 * log2(max(fArea, 1e-20));
  S.fLod = fCone + 0.5 * log2(max(UVArea(vUV0), 1e-20));
  S.fLod1 = fCone + 0.5 * log2(max(UVArea(vUV1), 1e-20));
  return S;
}

//! The mip level of texture nIndex (2D) under the surface's ray cone.
float RayTextureLevel(uint nIndex, float fLod)
{
  uint w, h, nLevels;
  g_Textures[NonUniformResourceIndex(nIndex)].GetDimensions(0, w, h, nLevels);
  return max(fLod + 0.5 * log2(max(w * h, 1u)), 0);
}

float4 RaySampleTexture(uint nIndex, uint nSampler, float2 vUV, float fLod)
{
  const float fLevel = RayTextureLevel(nIndex, fLod);
  Texture2D Tex = g_Textures[NonUniformResourceIndex(nIndex)];
  switch (nSampler)
  {
  case XR_SAMPLER_LINEAR_CLAMP:
    return Tex.SampleLevel(g_SamplerLinearClamp, vUV, fLevel);
  case XR_SAMPLER_POINT_WRAP:
    return Tex.SampleLevel(g_SamplerPointWrap, vUV, fLevel);
  case XR_SAMPLER_POINT_CLAMP:
    return Tex.SampleLevel(g_SamplerPointClamp, vUV, fLevel);
  default:
    return Tex.SampleLevel(g_SamplerLinearWrap, vUV, fLevel);
  }
}

float4 RaySampleTextureArray(uint nIndex, uint nSampler, float3 vUVW, float fLod)
{
  Texture2DArray Tex = g_TextureArrays[NonUniformResourceIndex(nIndex)];
  uint w, h, nSlices, nLevels;
  Tex.GetDimensions(0, w, h, nSlices, nLevels);
  const float fLevel = max(fLod + 0.5 * log2(max(w * h, 1u)), 0);
  switch (nSampler)
  {
  case XR_SAMPLER_LINEAR_CLAMP:
    return Tex.SampleLevel(g_SamplerLinearClamp, vUVW, fLevel);
  case XR_SAMPLER_POINT_WRAP:
    return Tex.SampleLevel(g_SamplerPointWrap, vUVW, fLevel);
  case XR_SAMPLER_POINT_CLAMP:
    return Tex.SampleLevel(g_SamplerPointClamp, vUVW, fLevel);
  default:
    return Tex.SampleLevel(g_SamplerLinearWrap, vUVW, fLevel);
  }
}

//! Texture 0 of the draw at the surface (white without one); vRayDir for DF_CUBE0.
float4 RayTexture0(SRayInstance I, SRaySurface S, float3 vRayDir)
{
  const uint nFlags = I.nMaterial.x, nSampler = I.nMaterial2.x & 15;
  if (nFlags & DF_CUBE0)
    return g_Cubes[NonUniformResourceIndex(I.nMaterial.z)].SampleLevel(g_SamplerLinearWrap, reflect(vRayDir, S.vNormal), 0);
  if (nFlags & DF_TEX0_ARRAY)
  {
    const float fSlice = floor(S.vTex0.x * 0.5);
    return RaySampleTextureArray(I.nMaterial.z, nSampler, float3(S.vTex0.x - 2 * fSlice, S.vTex0.y, fSlice), S.fLod);
  }
  if (nFlags & DF_TEX0)
    return RaySampleTexture(I.nMaterial.z, nSampler, S.vTex0, S.fLod);
  return 1;
}

//! vColor times the vertex colour, as General.hlsl's vertex shader without the lighting.
float4 RayVertexColor(SRayInstance I, SRaySurface S)
{
  float4 vColor = I.vColor;
  if (I.nMaterial.x & (DF_VCOLOR | DF_VCOLOR_X2))
  {
    float4 vVertex = S.vColor;
    if (I.nMaterial.x & DF_VCOLOR_X2)
      vVertex.rgb *= 2;
    if (I.nMaterial.x & DF_VALPHA_ONLY)
      vVertex.rgb = 1;
    vColor *= vVertex;
  }
  return vColor;
}

//! Whether the draw's alpha test passes at the surface.
bool RayAlphaPasses(uint nInstance, SRaySurface S, float3 vRayDir)
{
  const SRayInstance I = g_RayInstances[nInstance];
  if (!(I.nMaterial.x & DF_ALPHATEST) || I.nGeom.x == XR_RAY_NONE)
    return true;
  const float4 vTex = RayTexture0(I, S, vRayDir);
  const float fAlpha = (I.nMaterial.x & DF_ALPHA_FROM_TEX) ? vTex.a : RayVertexColor(I, S).a * vTex.a;
  const float fRef = asfloat(I.nMaterial2.y);
  return (I.nMaterial.x & DF_ALPHATEST_LESS) ? fRef - fAlpha - 1.0 / 512 >= 0 : fAlpha - fRef >= 0;
}

//! The nearest hit of Ray among the instances of nMask; the ray cone fConeWidth wide at its origin,
//! widening by fConeSpread per metre (the alpha tests' mip levels).
SRayHit TraceScene(RayDesc Ray, uint nMask, float fConeWidth, float fConeSpread)
{
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, nMask, Ray);
  uint nTests = 0;
  while (Query.Proceed())
  {
    if (Query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
      continue;
    bool bHit = nTests >= XR_RAY_MAX_ALPHA_TESTS;
    if (!bHit)
    {
      nTests++;
      const float fT = Query.CandidateTriangleRayT();
      const SRaySurface S = RaySurface(Query.CandidateInstanceID(), Query.CandidatePrimitiveIndex(), Query.CandidateTriangleBarycentrics(),
        Query.CandidateObjectToWorld3x4(), Ray.Direction, fConeWidth + fT * fConeSpread);
      bHit = RayAlphaPasses(Query.CandidateInstanceID(), S, Ray.Direction);
    }
    if (bHit)
      Query.CommitNonOpaqueTriangleHit();
  }
  SRayHit Hit;
  Hit.bHit = Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
  Hit.fT = Hit.bHit ? Query.CommittedRayT() : Ray.TMax;
  Hit.nInstance = Hit.bHit ? Query.CommittedInstanceID() : 0;
  Hit.nPrimitive = Hit.bHit ? Query.CommittedPrimitiveIndex() : 0;
  Hit.vBary = Hit.bHit ? Query.CommittedTriangleBarycentrics() : 0;
  Hit.mObjectToWorld = Query.CommittedObjectToWorld3x4();
  Hit.nAlphaTests = nTests;
  return Hit;
}

//! How much of the sun reaches vPos (0 or 1): a ray towards it among the instances of nMask, from
//! a little off the surface along vNormal (the side the ray came from), alpha tested as TraceScene.
float TraceSun(float3 vPos, float3 vNormal, uint nMask, float fConeWidth)
{
  RayDesc Ray;
  Ray.Origin = vPos + vNormal * (0.02 + 0.5 * fConeWidth);
  Ray.Direction = g_Frame.vSunDir.xyz;
  Ray.TMin = 0.01;
  Ray.TMax = 2000;
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, nMask, Ray);
  uint nTests = 0;
  while (Query.Proceed())
  {
    if (Query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
      continue;
    bool bHit = nTests >= XR_RAY_MAX_ALPHA_TESTS;
    if (!bHit)
    {
      nTests++;
      const SRaySurface S = RaySurface(Query.CandidateInstanceID(), Query.CandidatePrimitiveIndex(), Query.CandidateTriangleBarycentrics(),
        Query.CandidateObjectToWorld3x4(), Ray.Direction, fConeWidth);
      bHit = RayAlphaPasses(Query.CandidateInstanceID(), S, Ray.Direction);
    }
    if (bHit)
      Query.CommitNonOpaqueTriangleHit();
  }
  return Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
}

//! The colour of the hit's draw there (see the head of the file), not fogged; bFog: the draw is
//! fogged (DF_FOG). fConeWidth: the ray cone's width at the hit; bShadows: the sun's visibility by a
//! ray (TraceSun among nMask), else lit.
float4 RayShade(SRayHit Hit, float3 vRayDir, float fConeWidth, bool bShadows, uint nMask, out bool bFog)
{
  const SRayInstance I = g_RayInstances[Hit.nInstance];
  const SRaySurface S = RaySurface(Hit.nInstance, Hit.nPrimitive, Hit.vBary, Hit.mObjectToWorld, vRayDir, fConeWidth);
  const uint nFlags = I.nMaterial.x, nFlags2 = I.nMaterial.y, nSampler1 = (I.nMaterial2.x >> 4) & 15;
  bFog = (nFlags & DF_FOG) != 0;
  if (I.nGeom.x == XR_RAY_NONE)
    return float4(0.5, 0.5, 0.5, 1);

  float fSun = 1;
  if (bShadows && ((nFlags & DF_SHADOWS) || (nFlags2 & DF2_SUN_CASCADES)) && dot(S.vNormal, g_Frame.vSunDir.xyz) > 0)
    fSun = TraceSun(S.vPos, S.vFaceNormal, nMask, fConeWidth);

  float4 vVertexColor = RayVertexColor(I, S);
  if (nFlags & DF_LIGHTING)
  {
    const float3 vSun = fSun * g_Frame.vSunColor.rgb * saturate(dot(S.vNormal, g_Frame.vSunDir.xyz));
    if (nFlags2 & DF2_MATERIAL_LIGHT)
      vVertexColor.rgb = saturate(I.vAmbient.rgb + I.vColor.rgb * vSun);
    else
    {
      const float fScale = I.vAmbient.w > 0 ? I.vAmbient.w : 1;
      vVertexColor.rgb *= fScale * saturate(I.vAmbient.rgb / fScale + vSun);
    }
  }

  const float4 vTex = RayTexture0(I, S, vRayDir);
  float4 vColor = (nFlags & DF_TEX0_ALPHA_ONLY) ? float4(vVertexColor.rgb, vVertexColor.a * vTex.a) : vVertexColor * vTex;
  if (nFlags2 & DF2_ADD_TEX4)
    vColor.rgb += RaySampleTexture(I.nMaterial2.z, XR_SAMPLER_LINEAR_WRAP, S.vTex0, S.fLod).rgb * I.vAddColor.rgb + I.vAddColor.w;
  if (nFlags & DF_ALPHA_FROM_TEX)
    vColor.a = vTex.a;
  if (nFlags & DF_TEX1_LIGHTMAP)
    vColor.rgb *= 4 * fSun * RaySampleTexture(I.nMaterial.w, nSampler1, S.vTex1, S.fLod1).rgb + I.vAmbient.rgb;
  else if (nFlags & DF_TEX1_DETAIL)
    vColor.rgb *= 2;
  else if (nFlags & DF_TEX1)
    vColor *= RaySampleTexture(I.nMaterial.w, nSampler1, S.vTex1, S.fLod1);
  if (nFlags2 & DF2_ALPHA_GLOW)
    vColor.rgb += vTex.rgb * vTex.a;
  if (nFlags & DF_TERRAIN_CLIFF)
    vColor.rgb *= 2;
  if (nFlags2 & DF2_SHADOW_TINT)
    vColor.rgb *= lerp(g_Frame.vShadowTint.rgb, 1, fSun);
  return vColor;
}

#endif
