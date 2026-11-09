// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayAO.hlsl
//	Description: The ambient occlusion by rays (d3d12_AO rt, RayAO.cpp):
//	how much of the hemisphere above each pixel of the main view's depth
//	buffer is open, into the visibility the ambient occlusion's resolve
//	takes (AmbientOcclusion.h). Shader Model 6.5 (inline RayQuery), the
//	compute root signature with rays (D3D12Compute.h).
//
//	CSTrace: a ray for each cell of a grid over the view (a pixel, or 2 x 2
//	of them with the pixel traced another in each frame of 4), from the
//	point of the cell's pixel (RayView.hlsli), a little off its face, in a
//	direction of the hemisphere about the face's normal as the cosine
//	spreads them (another for each pixel and frame), d3d12_AORadius long,
//	among all the instances the rays see (alpha tested as TraceScene): 1
//	where nothing is that near, 0 where the nearest is within the first
//	1 - AO_FALLOFF of the radius, rising to 1 over the rest (as XeGTAO's
//	falloff); -1 where there is no surface or it is beyond the occlusion's
//	range.
//	CSTemporal: at each pixel the rays of the cells round it on its surface
//	(the same view depth), the nearer the more, blended into the history
//	(where the point was in the frame before, the history's texels there
//	whose depth is the point's, at most vParams.w frames of them). Without
//	a ray on its surface the history as it is.
//	CSFilter: a spiral of taps over the history on the pixel's plane, wider
//	and more of them where the history is young, into the visibility (R8,
//	1 open).
//	Without the denoise CSTemporal has no history and CSFilter takes the
//	rays round the pixel as they are.
//	RAY_GI (d3d12_RTGI, the kernels RayGI*_CS): the rays d3d12_RTGIDistance
//	long, what they find shaded (RayShade, the sun's visibility at the hit by
//	a ray towards it) and taken as the light it gives back over its own
//	ambient colour (the outdoor one where it has none; at most GI_GIVEN_MAX,
//	times d3d12_RTGIBounce): the factor of the pixel's ambient light (rgb, 1
//	where they find nothing, less in a corner, more by a lit wall), with
//	the same falloff; its history and filter in rgb (the frames in a texture
//	of their own), into the factor (R16G16B16A16_FLOAT) the ambient
//	occlusion's draws take from or add to the ambient light with.
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"

ConstantBuffer<SRayAOConstants> g_AO : register(b2);
RWTexture2D<float4> g_Out : register(u0);
#ifdef RAY_GI
RWTexture2D<float4> g_Factor : register(u1);
RWTexture2D<unorm float> g_Frames : register(u2);
#else
RWTexture2D<unorm float> g_Visibility : register(u1);
#endif
#define RAY_VIEW g_AO
#include "RayView.hlsli"

//! The alpha tests of a ray at most (the candidates after these are taken as they are).
static const uint AO_ALPHA_TESTS = 8;
//! The part of the radius at its end over which what the ray finds occludes less and less (XeGTAO's
//! default falloff range).
static const float AO_FALLOFF = 0.615;
//! CSFilter's taps over a young history and over one of FILTER_OLD frames, and their radius (pixels).
static const uint FILTER_TAPS_YOUNG = 16;
static const uint FILTER_TAPS_OLD = 8;
static const float FILTER_RADIUS_YOUNG = 16;
static const float FILTER_RADIUS_OLD = 5;
static const float FILTER_OLD = 8;
#ifdef RAY_GI
//! How fast a GI ray's cone widens (per metre): the mip levels of what it finds (a bounce wants no detail).
static const float GI_CONE_SPREAD = 0.25;
//! The most light a hit gives back, over its ambient colour (a light map's or the sun's against a dark ambient).
static const float GI_GIVEN_MAX = 4;
//! The alpha tests of a GI ray and of its hit's ray towards the sun at most (the candidates after these
//! are taken as they are: in a crown of leaves a bounce does without its holes).
static const uint GI_ALPHA_TESTS = 4;
#endif

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

//! The view depth at pixel p, 0 where there is no surface (the sky) or it is beyond the occlusion's range.
float SurfaceDepth(int2 p)
{
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  return z < 1 && d > 0 && d <= g_AO.vParams.y ? d : 0;
}

//! The distance along Ray to the nearest of the instances of nMask, 0 where there is none; fConeWidth
//! the ray cone's width at the origin (the alpha tests' mip levels); nCoverage RayCandidatePasses' seed.
float AOHit(RayDesc Ray, uint nMask, float fConeWidth, uint nCoverage)
{
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, nMask, Ray);
  uint nTests = 0;
  while (Query.Proceed())
  {
    if (Query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
      continue;
    bool bHit = nTests >= AO_ALPHA_TESTS;
    if (!bHit)
    {
      nTests++;
      bHit = RayCandidatePasses(Query.CandidateInstanceID(), Query.CandidatePrimitiveIndex(), Query.CandidateTriangleBarycentrics(),
        Query.CandidateObjectToWorld3x4(), Ray.Direction, fConeWidth, nCoverage);
    }
    if (bHit)
      Query.CommitNonOpaqueTriangleHit();
  }
  return Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? max(Query.CommittedRayT(), 1e-3) : 0;
}

#ifdef RAY_GI
//! The factor of the ambient light Ray brings: 1 where it finds nothing, else the light what it finds
//! gives back over its ambient colour, towards 1 over the falloff; fPixel the ray cone's width at
//! its origin, nCoverage RayCandidatePasses' seed.
float3 GIRay(RayDesc Ray, float fPixel, uint nCoverage)
{
  const uint nMask = g_AO.nFlags.w & 0xff;
  const SRayHit Hit = TraceScene(Ray, nMask, fPixel, GI_CONE_SPREAD, GI_ALPHA_TESTS, nCoverage);
  if (!Hit.bHit)
    return 1;
  bool bFog;
  const float3 vLight = RayShade(Hit, Ray.Direction, fPixel + Hit.fT * GI_CONE_SPREAD, (g_AO.nFlags.w & 0x100) != 0, nMask, bFog, GI_ALPHA_TESTS,
                                 nCoverage ? RayCoverageHash(nCoverage) | 1 : 0).rgb;
  const float3 vAmbient = g_RayInstances[Hit.nInstance].vAmbient.rgb;
  const float3 vOwn = max(any(vAmbient > 1.0 / 255) ? vAmbient : g_AO.vOutdoorAmbient.rgb, 0.02);
  const float3 vGiven = min(max(vLight, 0) / vOwn, GI_GIVEN_MAX) * g_AO.vGI.x;
  return lerp(vGiven, 1, saturate((Hit.fT / g_AO.vParams.x - (1 - AO_FALLOFF)) / AO_FALLOFF));
}
#endif

[numthreads(XR_RAY_AO_GROUP, XR_RAY_AO_GROUP, 1)]
void CSTrace(uint3 vId : SV_DispatchThreadID)
{
  const int2 q = (int2)vId.xy;
  if (any(q >= RaysSize()))
    return;
  const int2 p = RayPixel(q);
  const float d = all(p < (int2)g_AO.vSize.xy) ? SurfaceDepth(p) : 0;
  if (d <= 0)
  {
    g_Out[q] = float4(-1, 0, 0, 0);
    return;
  }
  const float3 vPos = PixelPos(p + 0.5, d);
  const float3 vN = FaceNormal(p, vPos, d);
  const float fPixel = d * g_AO.vParams.z;
  // A direction of the hemisphere, as the cosine spreads them: a point of the disc under it, uniform;
  // each pixel's own (a hash: no pattern across them, which the filter would leave), moved along R2 by
  // the pixel's rays so far (they spread evenly over the frames the history holds).
  const float2 vSeed = float2(Hash(p.x | (p.y << 16)), Hash(p.x | (p.y << 16) | 0x8000)) * (1.0 / 4294967296.0);
  const float fRay = (float)((g_AO.nFlags.y / (uint)(TraceStep() * TraceStep())) & 4095);
  const float2 vUV = frac(vSeed + fRay * float2(0.7548777, 0.5698403));
  const float u = vUV.x, v = vUV.y;
  const float r = sqrt(u), a = 6.2831853 * v;
  const float3 vT = normalize(abs(vN.z) < 0.9 ? cross(vN, float3(0, 0, 1)) : cross(vN, float3(1, 0, 0)));
  const float3 vB = cross(vN, vT);
  RayDesc Ray;
  Ray.Origin = vPos + vN * (0.01 + fPixel);
  Ray.Direction = normalize(r * cos(a) * vT + r * sin(a) * vB + sqrt(saturate(1 - u)) * vN);
  Ray.TMin = 0.005;
  Ray.TMax = g_AO.vParams.x;
  // d3d12_RTAlphaCoverage: the leaves by their shares, another draw for each pixel and frame.
  const uint nCoverage = (g_AO.nFlags.w & XR_RAY_COVERAGE_RAYS) ? (Hash(p.x | (p.y << 16) | 0x4000) ^ Hash(g_AO.nFlags.y)) | 1 : 0;
#ifdef RAY_GI
  g_Out[q] = float4(GIRay(Ray, fPixel, nCoverage), d);
#else
  const float t = AOHit(Ray, g_AO.nFlags.w & 0xff, fPixel, nCoverage);
  g_Out[q] = float4(t > 0 ? saturate((t / g_AO.vParams.x - (1 - AO_FALLOFF)) / AO_FALLOFF) : 1, 0, d, 0);
#endif
}

#ifndef RAY_GI

//! CSTemporal's group's rays: the cells within 2 (a ray a pixel) or 1 (2 x 2 pixels a ray) of its pixels',
//! a tile of 12 x 12 or 6 x 6 from a cell q00: x the visibility (< 0: none), y the view depth.
groupshared float2 s_Rays[12 * 12];

//! The visibility of the rays of the cells within nCells of pixel p's (view depth d) on its surface, the
//! nearer the more, from s_Rays (its tile from cell q00); y their weight (0: none). nCells a constant.
float2 NearRays(int2 p, float d, int nCells, int2 q00)
{
  const int nTile = XR_RAY_AO_GROUP / (3 - nCells) + 2 * nCells;
  const int2 q0 = p / (3 - nCells);
  float2 vSum = 0;
  [unroll]
  for (int y = -nCells; y <= nCells; y++)
  {
    [unroll]
    for (int x = -nCells; x <= nCells; x++)
    {
      const int2 t = q0 + int2(x, y) - q00;
      const float2 r = s_Rays[t.y * nTile + t.x];
      if (r.x < 0 || abs(r.y - d) > SAME_DEPTH * d)
        continue;
      const float2 o = RayPixel(q0 + int2(x, y)) - p;
      const float w = exp(-0.25 * dot(o, o));
      vSum += float2(w * r.x, w);
    }
  }
  return vSum.y > 0 ? float2(vSum.x / vSum.y, vSum.y) : 0;
}

[numthreads(XR_RAY_AO_GROUP, XR_RAY_AO_GROUP, 1)]
void CSTemporal(uint3 vId : SV_DispatchThreadID, uint3 vGroup : SV_GroupID, uint nThread : SV_GroupIndex)
{
  // The rays round the group's pixels, each loaded once.
  const int nCells = 3 - TraceStep();
  const int nTile = XR_RAY_AO_GROUP / TraceStep() + 2 * nCells;
  const int2 q00 = (int2)(vGroup.xy * XR_RAY_AO_GROUP) / TraceStep() - nCells;
  for (int i = (int)nThread; i < nTile * nTile; i += XR_RAY_AO_GROUP * XR_RAY_AO_GROUP)
    s_Rays[i] = RaysAt(q00 + int2(i % nTile, i / nTile)).xz;
  GroupMemoryBarrierWithGroupSync();
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_AO.vSize.xy))
    return;
  // x the visibility, z the frames in it (0: no surface), w the view depth.
  const float d = SurfaceDepth(p);
  if (d <= 0)
  {
    g_Out[p] = float4(1, 0, 0, 0);
    return;
  }
  float2 vNear;
  if (TraceStep() == 1)
    vNear = NearRays(p, d, 2, q00);
  else
    vNear = NearRays(p, d, 1, q00);
  const float3 vHistory = g_AO.nFlags.x ? HistoryAt(p + 0.5, d) : 0;
  if (vNear.y <= 0)
  {
    // No ray on its surface this frame: the history as it is, else all open (young: the filter spreads
    // the others over it).
    g_Out[p] = vHistory.z > 0 ? float4(vHistory.x, 0, vHistory.z, d) : float4(1, 0, 0.5, d);
    return;
  }
  const float fFrames = min(vHistory.z + 1, g_AO.vParams.w);
  g_Out[p] = float4(lerp(vHistory.x, vNear.x, 1 / fFrames), 0, fFrames, d);
}

[numthreads(XR_RAY_AO_GROUP, XR_RAY_AO_GROUP, 1)]
void CSFilter(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_AO.vSize.xy))
    return;
  const float4 c = g_Textures[g_AO.nTextures.w].Load(int3(p, 0));
  if (c.z <= 0)
  {
    g_Visibility[p] = 1;
    return;
  }
  const float d = c.w;
  float fVis = c.x;
  if (g_AO.nFlags.z)
  {
    const float fOld = saturate((c.z - 1) / (FILTER_OLD - 1));
    const float fRadius = lerp(FILTER_RADIUS_YOUNG, FILTER_RADIUS_OLD, fOld);
    const uint nTaps = fOld >= 1 ? FILTER_TAPS_OLD : FILTER_TAPS_YOUNG;
    const float3 vPos = PixelPos(p + 0.5, d);
    const float3 vN = FaceNormal(p, vPos, d);
    const float fPlane = 1 / (0.02 + 2 * d * g_AO.vParams.z);
    const float fTurn = 6.2831853 * Noise(p, g_AO.nFlags.y + 13);
    float2 vSum = float2(fVis, 1);
    [loop]
    for (uint k = 0; k < nTaps; k++)
    {
      // A spiral of golden angles, its points equally spread over the disc.
      const float r = sqrt((k + 0.5) / nTaps);
      const float a = fTurn + 2.3999632 * k;
      const int2 q = p + (int2)round(r * fRadius * float2(cos(a), sin(a)));
      if (any(q < 0) || any(q >= (int2)g_AO.vSize.xy))
        continue;
      const float4 h = g_Textures[g_AO.nTextures.w].Load(int3(q, 0));
      if (h.z <= 0)
        continue;
      const float w = exp(-2 * r * r) * saturate(1 - abs(dot(PixelPos(q + 0.5, h.w) - vPos, vN)) * fPlane);
      vSum += w * float2(h.x, 1);
    }
    fVis = vSum.x / vSum.y;
  }
  g_Visibility[p] = saturate(fVis);
}
#endif

#ifdef RAY_GI
//! CSTemporal's group's rays as s_Rays: rgb the factor (r < 0: none), w the view depth.
groupshared float4 s_GIRays[12 * 12];

//! The factor of the rays of the cells within nCells of pixel p's (view depth d) on its surface, the
//! nearer the more, from s_GIRays (its tile from cell q00); w their weight (0: none). nCells a constant.
float4 NearGIRays(int2 p, float d, int nCells, int2 q00)
{
  const int nTile = XR_RAY_AO_GROUP / (3 - nCells) + 2 * nCells;
  const int2 q0 = p / (3 - nCells);
  float4 vSum = 0;
  [unroll]
  for (int y = -nCells; y <= nCells; y++)
  {
    [unroll]
    for (int x = -nCells; x <= nCells; x++)
    {
      const int2 t = q0 + int2(x, y) - q00;
      const float4 r = s_GIRays[t.y * nTile + t.x];
      if (r.x < 0 || abs(r.w - d) > SAME_DEPTH * d)
        continue;
      const float2 o = RayPixel(q0 + int2(x, y)) - p;
      const float w = exp(-0.25 * dot(o, o));
      vSum += float4(w * r.rgb, w);
    }
  }
  return vSum.w > 0 ? float4(vSum.rgb / vSum.w, vSum.w) : 0;
}

//! The frames in a GI history at texel q of texture n (R8_UNORM, frames / 255).
float GIFramesAt(uint n, int2 q)
{
  return round(g_Textures[n].Load(int3(q, 0)).r * 255);
}

//! The GI's history of the frame before (nTextures.z: rgb the factor, w the view depth; nTextures2.x
//! the frames) at the point of vPixel (view depth d), where it was then: rgb, w the frames (0: none).
float4 GIHistoryAt(float2 vPixel, float d)
{
  const float4 c = mul(float4(PixelPos(vPixel, d), 1), RAY_VIEW.mPrevViewProj);
  if (c.w <= 1e-3)
    return 0;
  const float2 vNDC = c.xy / c.w + RAY_VIEW.vJitter.zw;
  const float2 t = float2(0.5 + 0.5 * vNDC.x, 0.5 - 0.5 * vNDC.y) * RAY_VIEW.vSize.xy - 0.5;
  const int2 b = (int2)floor(t);
  const float2 f = t - b;
  float4 vSum = 0;
  float fFrames = 0;
  [unroll]
  for (int k = 0; k < 4; k++)
  {
    const int2 q = b + int2(k & 1, k >> 1);
    if (any(q < 0) || any(q >= (int2)RAY_VIEW.vSize.xy))
      continue;
    const float4 h = g_Textures[RAY_VIEW.nTextures.z].Load(int3(q, 0));
    const float w = ((k & 1) ? f.x : 1 - f.x) * ((k >> 1) ? f.y : 1 - f.y);
    if (h.w > 0 && abs(h.w - c.w) <= SAME_DEPTH * c.w + 0.02)
    {
      vSum += w * float4(h.rgb, 1);
      fFrames += w * GIFramesAt(g_AO.nTextures2.x, q);
    }
  }
  return vSum.w > 1e-3 ? float4(vSum.rgb / vSum.w, max(fFrames / vSum.w, 1)) : 0;
}

[numthreads(XR_RAY_AO_GROUP, XR_RAY_AO_GROUP, 1)]
void CSTemporal(uint3 vId : SV_DispatchThreadID, uint3 vGroup : SV_GroupID, uint nThread : SV_GroupIndex)
{
  // The rays round the group's pixels, each loaded once.
  const int nCells = 3 - TraceStep();
  const int nTile = XR_RAY_AO_GROUP / TraceStep() + 2 * nCells;
  const int2 q00 = (int2)(vGroup.xy * XR_RAY_AO_GROUP) / TraceStep() - nCells;
  for (int i = (int)nThread; i < nTile * nTile; i += XR_RAY_AO_GROUP * XR_RAY_AO_GROUP)
  {
    const int2 q = clamp(q00 + int2(i % nTile, i / nTile), 0, RaysSize() - 1);
    s_GIRays[i] = g_Textures[g_AO.nTextures.y].Load(int3(q, 0));
  }
  GroupMemoryBarrierWithGroupSync();
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_AO.vSize.xy))
    return;
  // rgb the factor, w the view depth (0: no surface); the frames apart.
  const float d = SurfaceDepth(p);
  if (d <= 0)
  {
    g_Out[p] = float4(1, 1, 1, 0);
    g_Frames[p] = 0;
    return;
  }
  float4 vNear;
  if (TraceStep() == 1)
    vNear = NearGIRays(p, d, 2, q00);
  else
    vNear = NearGIRays(p, d, 1, q00);
  const float4 vHistory = g_AO.nFlags.x ? GIHistoryAt(p + 0.5, d) : 0;
  if (vNear.w <= 0)
  {
    // No ray on its surface this frame: the history as it is, else as it was (young: the filter spreads
    // the others over it).
    g_Out[p] = float4(vHistory.w > 0 ? vHistory.rgb : 1, d);
    g_Frames[p] = vHistory.w / 255;
    return;
  }
  const float fFrames = min(vHistory.w + 1, g_AO.vParams.w);
  g_Out[p] = float4(lerp(vHistory.rgb, vNear.rgb, 1 / fFrames), d);
  g_Frames[p] = fFrames / 255;
}

[numthreads(XR_RAY_AO_GROUP, XR_RAY_AO_GROUP, 1)]
void CSFilter(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_AO.vSize.xy))
    return;
  const float4 c = g_Textures[g_AO.nTextures.w].Load(int3(p, 0));
  if (c.w <= 0)
  {
    g_Factor[p] = 1;
    return;
  }
  const float d = c.w;
  float3 vFactor = c.rgb;
  if (g_AO.nFlags.z)
  {
    const float fOld = saturate((GIFramesAt(g_AO.nTextures2.y, p) - 1) / (FILTER_OLD - 1));
    const float fRadius = lerp(FILTER_RADIUS_YOUNG, FILTER_RADIUS_OLD, fOld);
    const uint nTaps = fOld >= 1 ? FILTER_TAPS_OLD : FILTER_TAPS_YOUNG;
    const float3 vPos = PixelPos(p + 0.5, d);
    const float3 vN = FaceNormal(p, vPos, d);
    const float fPlane = 1 / (0.02 + 2 * d * g_AO.vParams.z);
    const float fTurn = 6.2831853 * Noise(p, g_AO.nFlags.y + 13);
    float4 vSum = float4(vFactor, 1);
    [loop]
    for (uint k = 0; k < nTaps; k++)
    {
      // A spiral of golden angles, its points equally spread over the disc.
      const float r = sqrt((k + 0.5) / nTaps);
      const float a = fTurn + 2.3999632 * k;
      const int2 q = p + (int2)round(r * fRadius * float2(cos(a), sin(a)));
      if (any(q < 0) || any(q >= (int2)g_AO.vSize.xy))
        continue;
      const float4 h = g_Textures[g_AO.nTextures.w].Load(int3(q, 0));
      if (h.w <= 0)
        continue;
      const float w = exp(-2 * r * r) * saturate(1 - abs(dot(PixelPos(q + 0.5, h.w) - vPos, vN)) * fPlane);
      vSum += w * float4(h.rgb, 1);
    }
    vFactor = vSum.rgb / vSum.w;
  }
  g_Factor[p] = float4(max(vFactor, 0), 1);
}
#endif
