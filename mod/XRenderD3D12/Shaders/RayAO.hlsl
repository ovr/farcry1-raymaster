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
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"

ConstantBuffer<SRayAOConstants> g_AO : register(b2);
RWTexture2D<float4> g_Out : register(u0);
RWTexture2D<unorm float> g_Visibility : register(u1);
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
//! the ray cone's width at the origin (the alpha tests' mip levels).
float AOHit(RayDesc Ray, uint nMask, float fConeWidth)
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
      const SRaySurface S = RaySurface(Query.CandidateInstanceID(), Query.CandidatePrimitiveIndex(), Query.CandidateTriangleBarycentrics(),
        Query.CandidateObjectToWorld3x4(), Ray.Direction, fConeWidth, true);
      bHit = RayAlphaPasses(Query.CandidateInstanceID(), S, Ray.Direction);
    }
    if (bHit)
      Query.CommitNonOpaqueTriangleHit();
  }
  return Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? max(Query.CommittedRayT(), 1e-3) : 0;
}

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
  const float t = AOHit(Ray, g_AO.nFlags.w & 0xff, fPixel);
  g_Out[q] = float4(t > 0 ? saturate((t / g_AO.vParams.x - (1 - AO_FALLOFF)) / AO_FALLOFF) : 1, 0, d, 0);
}

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
