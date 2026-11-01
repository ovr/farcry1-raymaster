// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathTrace.hlsl
//	Description: The path tracer's kernels (d3d12_PathTracing,
//	PathTrace.cpp), over the main view's depth buffer and its G-buffer
//	(PathGBuffer.hlsli), a thread for each of their pixels. Shader Model
//	6.5 (inline RayQuery), with PathShade.hlsli. Linear light, as the HDR
//	target's after HdrDecode; the light on a surface is without its
//	albedo, the composite (PathComposite.hlsl) multiplies it in.
//	  CSTrace: a path from each pixel's surface. Its lamps' light by a ray
//	    (PathSampleLamp), then nPath.y bounces in directions of the
//	    hemisphere as the cosine spreads them: a hit gives its emission
//	    and its albedo times the sun's light and a lamp's there, the next
//	    bounce's light comes times its albedo; where a path finds nothing,
//	    the sky's light (vSky). The game's ambient light fills in: a ray
//	    that finds nothing nearer than vFill.y brings the ambient light of
//	    the point it leaves (the G-buffer's, then the hit's) times vFill.x,
//	    as the raster's ambient times its occlusion (where it finds nothing
//	    at all, the brighter of that and the sky). The sun's light on the
//	    pixel's own surface is not in it: the composite takes the
//	    visibility the raster found
//	    (the G-buffer's). rgb the light, w the pixel's view depth (0: no
//	    surface the path tracer lights).
//	  CSTemporal: blended into the history where the surface was in the
//	    frame before, vParams.z frames at most (the reference: all of them
//	    while the view stays).
//	  CSFilter: the history filtered on the pixel's surface (its plane and
//	    its normal), wider where it is young; rgb the composite's light, w
//	    the light baked into the terrain's texture there (TerrainBakedLight;
//	    1 elsewhere), which the composite takes out of its albedo.
//	  CSSpecular: the highlights of the sun and the lamps, as the raster
//	    makes them (General.hlsl, DF_SPECULAR), in the game's colour: the
//	    G-buffer's highlights' colour (2 * vSpecular * gloss) times the
//	    sum of the sun's (vSpecular.rgb times the visibility the raster
//	    found) and each lamp's specular colour (times vSpecular.w;
//	    saturate(1 - d / radius) of its sphere, a projector's cube) times
//	    saturate(R.V)^32, R the light reflected about the normal; a lamp's
//	    where its ray reaches it, SPECULAR_RAYS of them at most (the
//	    nearest), only where it gives SPECULAR_MIN or more. The same in the
//	    reference: one frame has it all.
//
//////////////////////////////////////////////////////////////////////

#include "PathShade.hlsli"
#include "PathGBuffer.hlsli"

ConstantBuffer<SPathTraceConstants> g_PT : register(b2);
RWTexture2D<float4> g_Out : register(u0);
RWTexture2D<unorm float> g_Frames : register(u1);
#define RAY_VIEW g_PT
#include "RayView.hlsli"

//! How fast a bounce's ray cone widens (per metre): the mip levels of what it finds (a bounce wants no detail).
static const float PT_CONE_SPREAD = 0.25;
//! CSFilter's taps over a young history and over one of FILTER_OLD frames, and their radius (pixels).
static const uint FILTER_TAPS_YOUNG = 24;
static const uint FILTER_TAPS_OLD = 12;
static const float FILTER_RADIUS_YOUNG = 24;
static const float FILTER_RADIUS_OLD = 8;
static const float FILTER_OLD = 16;
//! CSSpecular's rays towards the lamps a pixel at most, and the least highlight of a lamp it traces one for (the game's colour).
static const uint SPECULAR_RAYS = 4;
static const float SPECULAR_MIN = 1.0 / 512;

//! A 32 bit hash.
uint Hash(uint x)
{
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

//! The shading of the paths (SPathShade) from the pass's constants; nMaxTests a ray's alpha tests at most.
SPathShade PathShadeParams(uint nMaxTests)
{
  SPathShade P;
  P.vSun = g_PT.vSun.rgb;
  P.fLamps = g_PT.vSun.w;
  P.fEmission = g_PT.vSky.w;
  P.fDelight = g_PT.vShade.x;
  P.fBakeLight = g_PT.vShade.y;
  P.fLightGap = g_PT.vShade.z;
  P.nLights = g_PT.nPath.x;
  P.nMask = g_PT.nFlags.w & 0xff;
  P.nMaxTests = nMaxTests;
  return P;
}

//! The G-buffer's normal texel at pixel p (PathGBufferNormal).
float4 GBufferNormalAt(int2 p)
{
  return g_Textures[g_PT.nTextures2.x].Load(int3(p, 0));
}

//! The G-buffer's normal (texel vGBuffer) of the surface at vPos on the camera's side (the leaves' two sides).
float3 PixelNormal(float4 vGBuffer, float3 vPos)
{
  const float3 vN = PathOctDecode(vGBuffer.rg);
  return dot(vN, g_PT.vCameraPos.xyz - vPos) < 0 ? -vN : vN;
}

//! A direction of the hemisphere about vN as the cosine spreads them, from two random numbers u, v of 0..1.
float3 CosineDirection(float3 vN, float u, float v)
{
  const float r = sqrt(u), a = 6.2831853 * v;
  const float3 vT = normalize(abs(vN.z) < 0.9 ? cross(vN, float3(0, 0, 1)) : cross(vN, float3(1, 0, 0)));
  const float3 vB = cross(vN, vT);
  return normalize(r * cos(a) * vT + r * sin(a) * vB + sqrt(saturate(1 - u)) * vN);
}

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSTrace(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 vGBuffer = GBufferNormalAt(p);
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  if (PathGBufferMaterial(vGBuffer) == 0 || z >= 1 || d <= 0)
  {
    g_Out[p] = 0;
    return;
  }
  const float3 vPos = PixelPos(p + 0.5, d);
  const float3 vFace = FaceNormal(p, vPos, d);
  const float3 vN = PixelNormal(vGBuffer, vPos);
  const float fPixel = d * g_PT.vParams.y;
  // Each pixel's own numbers, others each frame.
  uint nSeed = Hash((uint)p.x | ((uint)p.y << 16)) ^ Hash(g_PT.nFlags.y * 0x9e3779b9u + 1);
  SPathShade P = PathShadeParams(XR_RAY_MAX_ALPHA_TESTS);
  float3 vLight = PathSampleLamp(vPos, vN, vFace, P, nSeed, fPixel);
  // The game's ambient light the paths fill in with, at the point a ray leaves: the pixel's, then each hit's.
  float3 vFill = PathAmbientLight(g_Textures[g_PT.nTextures2.w].Load(int3(p, 0)).gba) * g_PT.vFill.x;

  // The bounces.
  P.nMaxTests = g_PT.nPath.z;
  float3 vThrough = 1;
  float3 vOrigin = vPos, vNormal = vN, vFaceNormal = vFace;
  float fWidth = fPixel;
  [loop]
  for (uint b = 0; b < g_PT.nPath.y; b++)
  {
    const float u = PathRandom(nSeed), v = PathRandom(nSeed);
    const float3 vDir = CosineDirection(vNormal, u, v);
    // Under the face (a normal map's normal leaning over): the path ends.
    if (dot(vDir, vFaceNormal) <= 0)
      break;
    RayDesc Ray;
    Ray.Origin = vOrigin + vFaceNormal * (0.01 + fWidth);
    Ray.Direction = vDir;
    Ray.TMin = 0.005;
    Ray.TMax = g_PT.vParams.x;
    const SRayHit Hit = TraceScene(Ray, P.nMask, fWidth, PT_CONE_SPREAD, P.nMaxTests);
    if (!Hit.bHit)
    {
      vLight += vThrough * max(g_PT.vSky.rgb, vFill);
      break;
    }
    if (Hit.fT > g_PT.vFill.y)
      vLight += vThrough * vFill;
    const float fHitWidth = fWidth + Hit.fT * PT_CONE_SPREAD;
    const SPathSurface H = PathSurface(Hit, vDir, fHitWidth, P);
    const float3 vHitLight = PathSun(H.vPos, H.vNormal, H.vFaceNormal, P, fHitWidth) +
                             PathSampleLamp(H.vPos, H.vNormal, H.vFaceNormal, P, nSeed, fHitWidth);
    vLight += vThrough * (H.vEmission + H.vAlbedo * vHitLight);
    vThrough *= H.vAlbedo;
    // The reference's long paths: on with the chance of their light, made up for by it (Russian roulette).
    if (g_PT.nPath.w && b >= 2)
    {
      const float q = min(max(vThrough.r, max(vThrough.g, vThrough.b)), 0.95);
      if (PathRandom(nSeed) >= q)
        break;
      vThrough /= q;
    }
    if (all(vThrough < 1e-3))
      break;
    vFill = H.vAmbient * g_PT.vFill.x;
    vOrigin = H.vPos;
    vNormal = H.vNormal;
    vFaceNormal = H.vFaceNormal;
    fWidth = fHitWidth;
  }
  // In real time a sample's light at most: what a path rarely finds (a lamp by a bounce) would stay a blotch.
  const float fLuminance = PathLuminance(vLight);
  if (g_PT.vParams.w > 0 && fLuminance > g_PT.vParams.w)
    vLight *= g_PT.vParams.w / fLuminance;
  g_Out[p] = float4(max(vLight, 0), d);
}

//! The frames in a history at texel q of texture n (R16_UNORM, frames / 65535).
float PathFramesAt(uint n, int2 q)
{
  return round(g_Textures[n].Load(int3(q, 0)).r * 65535);
}

//! The history of the frame before (nTextures.z: rgb the light, w the view depth; nTextures2.y the frames) at the point
//! of vPixel (view depth d), where it was then: rgb, w the frames (0: none).
float4 PathHistoryAt(float2 vPixel, float d)
{
  const float4 c = mul(float4(PixelPos(vPixel, d), 1), g_PT.mPrevViewProj);
  if (c.w <= 1e-3)
    return 0;
  const float2 vNDC = c.xy / c.w + g_PT.vJitter.zw;
  const float2 t = float2(0.5 + 0.5 * vNDC.x, 0.5 - 0.5 * vNDC.y) * g_PT.vSize.xy - 0.5;
  const int2 b = (int2)floor(t);
  const float2 f = t - b;
  float4 vSum = 0;
  float fFrames = 0;
  [unroll]
  for (int k = 0; k < 4; k++)
  {
    const int2 q = b + int2(k & 1, k >> 1);
    if (any(q < 0) || any(q >= (int2)g_PT.vSize.xy))
      continue;
    const float4 h = g_Textures[g_PT.nTextures.z].Load(int3(q, 0));
    const float w = ((k & 1) ? f.x : 1 - f.x) * ((k >> 1) ? f.y : 1 - f.y);
    if (w > 0 && h.w > 0 && abs(h.w - c.w) <= SAME_DEPTH * c.w + 0.02)
    {
      vSum += w * float4(h.rgb, 1);
      fFrames += w * PathFramesAt(g_PT.nTextures2.y, q);
    }
  }
  return vSum.w > 1e-3 ? float4(vSum.rgb / vSum.w, max(fFrames / vSum.w, 1)) : 0;
}

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSTemporal(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 r = g_Textures[g_PT.nTextures.y].Load(int3(p, 0));
  if (r.w <= 0)
  {
    g_Out[p] = 0;
    g_Frames[p] = 0;
    return;
  }
  const float4 vHistory = g_PT.nFlags.x ? PathHistoryAt(p + 0.5, r.w) : 0;
  const float fFrames = min(vHistory.w + 1, g_PT.vParams.z);
  g_Out[p] = float4(lerp(vHistory.rgb, r.rgb, 1 / fFrames), r.w);
  g_Frames[p] = fFrames / 65535;
}

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSFilter(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 c = g_Textures[g_PT.nTextures.w].Load(int3(p, 0));
  if (c.w <= 0)
  {
    g_Out[p] = float4(0, 0, 0, 1);
    return;
  }
  const float d = c.w;
  const float4 vGBuffer = GBufferNormalAt(p);
  const float3 vPos = PixelPos(p + 0.5, d);
  const float3 vN = PixelNormal(vGBuffer, vPos);
  const float3 vFace = FaceNormal(p, vPos, d);
  float3 vLight = c.rgb;
  if (g_PT.nFlags.z)
  {
    const float fOld = saturate((PathFramesAt(g_PT.nTextures2.z, p) - 1) / (FILTER_OLD - 1));
    const float fRadius = lerp(FILTER_RADIUS_YOUNG, FILTER_RADIUS_OLD, fOld);
    const uint nTaps = fOld >= 1 ? FILTER_TAPS_OLD : FILTER_TAPS_YOUNG;
    const float fPlane = 1 / (0.02 + 2 * d * g_PT.vParams.y);
    const float fTurn = 6.2831853 * Noise(p, g_PT.nFlags.y + 13);
    float4 vSum = float4(vLight, 1);
    [loop]
    for (uint k = 0; k < nTaps; k++)
    {
      // A spiral of golden angles, its points equally spread over the disc.
      const float r = sqrt((k + 0.5) / nTaps);
      const float a = fTurn + 2.3999632 * k;
      const int2 q = p + (int2)round(r * fRadius * float2(cos(a), sin(a)));
      if (any(q < 0) || any(q >= (int2)g_PT.vSize.xy))
        continue;
      const float4 h = g_Textures[g_PT.nTextures.w].Load(int3(q, 0));
      if (h.w <= 0)
        continue;
      const float3 vQ = PixelPos(q + 0.5, h.w);
      const float fNormal = saturate(dot(PixelNormal(GBufferNormalAt(q), vQ), vN));
      const float w = exp(-2 * r * r) * saturate(1 - abs(dot(vQ - vPos, vFace)) * fPlane) * pow(fNormal, 8);
      vSum += w * float4(h.rgb, 1);
    }
    vLight = vSum.rgb / vSum.w;
  }
  // The terrain: the light its texture has baked in, which its albedo is divided by.
  float fBaked = 1;
  if (PathGBufferMaterial(vGBuffer) == XR_PT_MAT_TERRAIN + 1 && g_PT.vShade.x > 1)
    fBaked = max(TerrainBakedLight(vPos, vN, vFace, PathShadeParams(0), d * g_PT.vParams.y), 0.01);
  g_Out[p] = float4(max(vLight, 0), fBaked);
}

//! The raster's highlight (General.hlsl Specular): saturate(R.V)^32, R the light vToLight reflected about vN, 0 where N.L <= 0.
float PathPhong(float3 vN, float3 vToLight, float3 vToEye)
{
  const float fNL = dot(vN, vToLight);
  const float fRV = saturate(dot(2 * fNL * vN - vToLight, vToEye));
  const float f2 = fRV * fRV, f4 = f2 * f2, f8 = f4 * f4, f16 = f8 * f8;
  return fNL > 0 ? f16 * f16 : 0;
}

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSSpecular(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 vGBuffer = GBufferNormalAt(p);
  // The raster's 2 * vSpecular * gloss: the G-buffer has half of it.
  const float3 vColor = 2 * g_Textures[g_PT.nTextures3.x].Load(int3(p, 0)).rgb;
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  if (PathGBufferMaterial(vGBuffer) == 0 || all(vColor <= 0) || (all(g_PT.vSpecular <= 0)) || z >= 1 || d <= 0)
  {
    g_Out[p] = 0;
    return;
  }
  const float3 vPos = PixelPos(p + 0.5, d);
  const float3 vFace = FaceNormal(p, vPos, d);
  const float3 vN = PixelNormal(vGBuffer, vPos);
  const float3 vToEye = normalize(g_PT.vCameraPos.xyz - vPos);
  // The sun's, with the visibility the raster found.
  float3 vSpecular = g_PT.vSpecular.rgb * (g_Textures[g_PT.nTextures2.w].Load(int3(p, 0)).r * PathPhong(vN, g_Frame.vSunDir.xyz, vToEye));
  // The lamps', each by a ray towards it.
  const SPathShade P = PathShadeParams(g_PT.nPath.z);
  uint nRays = 0;
  [loop]
  for (uint i = 0; i < P.nLights && g_PT.vSpecular.w > 0 && nRays < SPECULAR_RAYS; i++)
  {
    const SPTLight L = g_PTLights[i];
    float3 vToLight = L.vPos.xyz;
    float fDistance = 2000, fFalloff = 1;
    if (L.nParams.x != XR_PT_LIGHT_DIRECTIONAL)
    {
      vToLight = L.vPos.xyz - vPos;
      fDistance = length(vToLight);
      fFalloff = saturate(1 - fDistance / L.vPos.w);
      vToLight /= max(fDistance, 1e-4);
    }
    if (fFalloff <= 0)
      continue;
    float3 vLamp = saturate(L.vSpecular.rgb) * (g_PT.vSpecular.w * fFalloff * PathPhong(vN, vToLight, vToEye));
    if (L.nParams.x == XR_PT_LIGHT_PROJECTOR && L.nParams.z != XR_RAY_NONE && any(vLamp > 0))
      vLamp *= g_Cubes[NonUniformResourceIndex(L.nParams.z)].SampleLevel(g_SamplerLinearClamp, mul(float4(vPos, 1), L.mProject).xyz, 0).rgb;
    const float3 vGives = vColor * vLamp;
    if (max(vGives.r, max(vGives.g, vGives.b)) < SPECULAR_MIN)
      continue;
    float fHit;
    vSpecular += vLamp * TraceLight(vPos, vFace, vToLight, fDistance, P.fLightGap, P.nMask, d * g_PT.vParams.y, fHit, P.nMaxTests);
    nRays++;
  }
  g_Out[p] = float4(vColor * vSpecular, 0);
}
