// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RaySun.hlsl
//	Description: The sun's shadow by rays (d3d12_RTSunShadows, RaySun.cpp):
//	the mask of the sun's visibility at the main view's surfaces in the
//	depth buffer after the depth prepass, which the general program takes
//	in place of the sun's cascades where its pixel is the mask's surface
//	(XR_RAY_SUN, General.hlsl). Shader Model 6.5 (inline RayQuery), the
//	compute root signature with rays (D3D12Compute.h).
//
//	CSTrace: a ray for each cell of a grid over the view (a pixel, or 2 x 2
//	of them with the pixel traced another in each frame of 4: a quarter of
//	the rays, the history has them all), from the point of the cell's
//	pixel (from the depth and the view, the upscaler's jitter taken off),
//	its face's normal from the depths beside it (on each axis the side
//	nearer in depth: not across an edge), a little off it, towards a point
//	of the sun's disc (d3d12_RTSunAngle wide; another point for each pixel
//	and frame) among all the casters, and where what it finds is not a live
//	caster another among those (XR_RAY_LIVE: what casts on the light maps
//	and the terrain, whose light has the shadows of the rest, as the live
//	set of the cascades; none when the scene has none); each 0 where the
//	sun shows, else the distance to what hides it (alpha tested as
//	TraceScene); -1 where there is no surface (the sky) or it is beyond the
//	mask's range.
//	CSTemporal: at each pixel the rays of the cells round it on its surface
//	(the same view depth), the nearer the more (of a ray a pixel its own),
//	their visibility of each set blended into the history: where the point
//	was in the frame before (the camera's move), the history's texels there
//	whose depth is the point's, at most HISTORY_FRAMES of them, clamped to
//	the range of what this frame's rays within 2 pixels show; where the
//	history at the rays' own pixels was sure of the other way (a caster
//	that moves) it goes, to this frame's rays (a shadow that moved does not
//	stay behind, nor is its edge cut on the rays' grid). Without a ray on
//	its surface the history as it is.
//	CSFilter: the history spread over the soft edge of the shadow (where it
//	is neither 0 nor 1, or young), the half shadow as wide as the sun's
//	disc at the distance of the casters round the pixel: a spiral of taps
//	on the pixel's plane, the nearer the more;
//	into the mask with the pixel's view depth and how much the mask counts
//	(giving way to the cascades over the last part of its range).
//	With d3d12_RTSunDenoise 0 CSTemporal has no history and CSFilter takes
//	its rays as they are.
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"

ConstantBuffer<SRaySunConstants> g_Sun : register(b2);
RWTexture2D<float4> g_Out : register(u0);

//! The frames a history holds at most.
static const float HISTORY_FRAMES = 24;
//! CSFilter's taps and their widest radius (pixels).
static const uint FILTER_TAPS = 12;
static const float FILTER_MAX_RADIUS = 32;
//! How far apart two view depths of a surface may be, relative to them.
static const float SAME_DEPTH = 0.02;
//! A history of fewer frames is filtered also where it is all in the sun or in the shadow (its rays' noise).
static const float FILTER_YOUNG = 4;

//! The depth buffer at pixel p, clamped to the view.
float DepthAt(int2 p)
{
  p = clamp(p, 0, (int2)g_Sun.vSize.xy - 1);
  return g_Textures[g_Sun.nTextures.x].Load(int3(p, 0)).r;
}

//! The view depth of a value of the depth buffer.
float ViewDepthOf(float z)
{
  return g_Frame.vDepthParams.y / (z + g_Frame.vDepthParams.x);
}

//! The view depth at pixel p, 0 where the mask has no surface (the sky, beyond its range).
float SurfaceDepth(int2 p)
{
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  return z < 1 && d > 0 && d <= g_Sun.vParams.x ? d : 0;
}

//! The pixels a side of a cell of the rays' grid (1 or 2).
int TraceStep()
{
  return (int)g_Sun.nTrace.x;
}

//! The cells of the rays' grid across and down.
int2 RaysSize()
{
  return ((int2)g_Sun.vSize.xy + TraceStep() - 1) / TraceStep();
}

//! The pixel cell q's ray is from in this frame.
int2 RayPixel(int2 q)
{
  return q * TraceStep() + (int2)g_Sun.nTrace.yz;
}

//! CSTrace's ray of cell q (clamped to the grid): x, y its distances (all, live; x < 0: none), z the
//! view depth of its pixel.
float3 RaysAt(int2 q)
{
  q = clamp(q, 0, RaysSize() - 1);
  return g_Textures[g_Sun.nTextures.y].Load(int3(q, 0)).xyz;
}

//! The world position at vPixel (pixels, from the view's top left) and view depth d, the jitter taken off.
float3 PixelPos(float2 vPixel, float d)
{
  const float2 v = float2(vPixel.x * g_Sun.vSize.z * 2 - 1, 1 - vPixel.y * g_Sun.vSize.w * 2) - g_Sun.vJitter.xy;
  return g_Sun.vCameraPos.xyz + d * (g_Sun.vRayO.xyz + v.x * g_Sun.vRayX.xyz + v.y * g_Sun.vRayY.xyz);
}

//! The face's normal at pixel p (at vPos, view depth d), towards the camera.
float3 FaceNormal(int2 p, float3 vPos, float d)
{
  const float dl = ViewDepthOf(DepthAt(p + int2(-1, 0))), dr = ViewDepthOf(DepthAt(p + int2(1, 0)));
  const float du = ViewDepthOf(DepthAt(p + int2(0, -1))), dd = ViewDepthOf(DepthAt(p + int2(0, 1)));
  const float2 c = p + 0.5;
  const float3 vX = abs(dr - d) < abs(dl - d) ? PixelPos(c + float2(1, 0), dr) - vPos : vPos - PixelPos(c - float2(1, 0), dl);
  const float3 vY = abs(dd - d) < abs(du - d) ? PixelPos(c + float2(0, 1), dd) - vPos : vPos - PixelPos(c - float2(0, 1), du);
  const float3 vToCamera = g_Sun.vCameraPos.xyz - vPos;
  float3 n = cross(vX, vY);
  const float fLen = length(n);
  n = fLen > 1e-12 ? n / fLen : normalize(vToCamera);
  return dot(n, vToCamera) < 0 ? -n : n;
}

//! Interleaved gradient noise at pixel p, another pattern each frame.
float Noise(float2 p, uint nFrame)
{
  p += 5.588238 * (float)(nFrame & 63);
  return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

//! The distance along Ray to something among the instances of nMask that hides the sun, 0 where
//! nothing does; fConeWidth the ray cone's width at the origin (the alpha tests' mip levels); nKind
//! the kind of what hides it (InstanceContributionToHitGroupIndex: XR_RAY_LIVE for a live caster).
float SunHit(RayDesc Ray, uint nMask, float fConeWidth, out uint nKind)
{
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
      const float fCone = fConeWidth + Query.CandidateTriangleRayT() * 2 * g_Sun.vSun.w;
      const SRaySurface S = RaySurface(Query.CandidateInstanceID(), Query.CandidatePrimitiveIndex(), Query.CandidateTriangleBarycentrics(),
        Query.CandidateObjectToWorld3x4(), Ray.Direction, fCone, true);
      bHit = RayAlphaPasses(Query.CandidateInstanceID(), S, Ray.Direction);
    }
    if (bHit)
      Query.CommitNonOpaqueTriangleHit();
  }
  if (Query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
  {
    nKind = 0;
    return 0;
  }
  nKind = Query.CommittedInstanceContributionToHitGroupIndex();
  return max(Query.CommittedRayT(), 1e-3);
}

[numthreads(XR_RAY_SUN_GROUP, XR_RAY_SUN_GROUP, 1)]
void CSTrace(uint3 vId : SV_DispatchThreadID)
{
  const int2 q = (int2)vId.xy;
  if (any(q >= RaysSize()))
    return;
  const int2 p = RayPixel(q);
  const float d = all(p < (int2)g_Sun.vSize.xy) ? SurfaceDepth(p) : 0;
  if (d <= 0)
  {
    g_Out[q] = float4(-1, -1, 0, 0);
    return;
  }
  const float3 vPos = PixelPos(p + 0.5, d);
  const float3 vN = FaceNormal(p, vPos, d);
  const float fPixel = d * g_Sun.vParams.w;
  // A point of the sun's disc, uniform over it.
  const float u = Noise(p, g_Sun.nFlags.y), v = Noise(float2(p.y, p.x) + 23.0, g_Sun.nFlags.y + 7);
  const float2 vDisc = sqrt(u) * g_Sun.vSun.w * float2(cos(6.2831853 * v), sin(6.2831853 * v));
  RayDesc Ray;
  Ray.Origin = vPos + vN * (0.02 + 2 * fPixel);
  Ray.Direction = normalize(g_Sun.vSun.xyz + vDisc.x * g_Sun.vSunX.xyz + vDisc.y * g_Sun.vSunY.xyz);
  Ray.TMin = 0.01;
  Ray.TMax = g_Sun.vParams.z;
  // Where nothing hides the sun, no live caster does; where a live caster does, it hides it from both.
  uint nKind;
  const float fAll = SunHit(Ray, g_Sun.nFlags.w & 0xff, fPixel, nKind);
  float fLive = 0;
  if (fAll > 0)
    fLive = (nKind & XR_RAY_LIVE) ? fAll : (g_Sun.nFlags.w & 0x100) ? SunHit(Ray, XR_RAY_LIVE, fPixel, nKind) : 0;
  g_Out[q] = float4(fAll, fLive, d, 0);
}

//! The rays round a pixel (NearRays).
struct SNearRays
{
  float2 vMin, vMax;    //!< the range of the visibilities of those within 2 pixels of it (all, live)
  float2 vOwn;          //!< the visibilities of those on its surface, the nearer the more
  float fOwn;           //!< their weight (0: none)
  float2 vNearest;      //!< those of the nearest of any
  float fMoved;         //!< of the weight of those on its surface, the part where the shadow moved: their history
                        //!< at their pixels sure of the other way (0 to 1)
};

//! CSTemporal's group's rays: the cells within 2 (a ray a pixel) or 1 (2 x 2 pixels a ray) of its pixels',
//! a tile of 12 x 12 or 6 x 6 from a cell q00; with them the history at each one's pixel (x < 0: none).
groupshared float3 s_Rays[12 * 12];
groupshared float2 s_RaysBefore[12 * 12];

//! The history of the frame before at the point of vPixel (pixels, view depth d), where it was then: x y
//! the visibilities, z the frames in it (0: none there, another surface or off the view).
float3 HistoryAt(float2 vPixel, float d)
{
  float3 vHistory = 0;
  const float4 c = mul(float4(PixelPos(vPixel, d), 1), g_Sun.mPrevViewProj);
  if (c.w > 1e-3)
  {
    const float2 vNDC = c.xy / c.w + g_Sun.vJitter.zw;
    const float2 t = float2(0.5 + 0.5 * vNDC.x, 0.5 - 0.5 * vNDC.y) * g_Sun.vSize.xy - 0.5;
    const int2 b = (int2)floor(t);
    const float2 f = t - b;
    float4 vSum = 0;
    [unroll]
    for (int k = 0; k < 4; k++)
    {
      const int2 q = b + int2(k & 1, k >> 1);
      if (any(q < 0) || any(q >= (int2)g_Sun.vSize.xy))
        continue;
      const float4 h = g_Textures[g_Sun.nTextures.z].Load(int3(q, 0));
      const float w = ((k & 1) ? f.x : 1 - f.x) * ((k >> 1) ? f.y : 1 - f.y);
      if (h.z > 0 && abs(h.w - c.w) <= SAME_DEPTH * c.w + 0.02)
        vSum += w * float4(h.xyz, 1);
    }
    if (vSum.w > 1e-3)
      vHistory = vSum.xyz / vSum.w;
  }
  return vHistory;
}

//! The rays of the cells within nCells of pixel p's (view depth d), from s_Rays (its tile from cell q00);
//! nCells a constant (the loops unrolled). Those on its surface are weighed by their distance to it: of a
//! ray a pixel its own, of 2 x 2 pixels a ray a Gaussian over about a cell (no blocks of a cell where the
//! history is left out: a shadow that moves).
SNearRays NearRays(int2 p, float d, int nCells, int2 q00)
{
  const int nTile = XR_RAY_SUN_GROUP / (3 - nCells) + 2 * nCells;
  const int2 q0 = p / (3 - nCells);
  const float fFalloff = nCells == 2 ? 4 : 0.5;
  SNearRays Near;
  Near.vMin = 1;
  Near.vMax = 0;
  Near.vOwn = 0;
  Near.fOwn = 0;
  Near.vNearest = 1;
  Near.fMoved = 0;
  float fNearest = 1e30;
  [unroll]
  for (int y = -nCells; y <= nCells; y++)
  {
    [unroll]
    for (int x = -nCells; x <= nCells; x++)
    {
      const int2 q = q0 + int2(x, y);
      const int2 t = q - q00;
      const float3 r = s_Rays[t.y * nTile + t.x];
      if (r.x < 0)
        continue;
      const float2 vBefore = s_RaysBefore[t.y * nTile + t.x];
      const float2 v = float2(r.x == 0 ? 1 : 0, r.y == 0 ? 1 : 0);
      const float2 o = RayPixel(q) - p;
      // The range from the pixel's own window, not its cell's (the same for its 2 x 2 pixels: their
      // history kept or dropped together, a moving shadow's edge in blocks).
      if (all(abs(o) <= 2))
      {
        Near.vMin = min(Near.vMin, v);
        Near.vMax = max(Near.vMax, v);
      }
      const float fFar = dot(o, o);
      const float fDepth = abs(r.z - d) / d;
      if (fDepth <= SAME_DEPTH)
      {
        const float w = exp(-fFalloff * fFar);
        Near.vOwn += w * v;
        Near.fOwn += w;
        if (vBefore.x >= 0 && any(abs(v - vBefore) > 0.75))
          Near.fMoved += w;
      }
      if (fFar < fNearest)
      {
        fNearest = fFar;
        Near.vNearest = v;
      }
    }
  }
  if (Near.fOwn > 0)
  {
    Near.vOwn /= Near.fOwn;
    Near.fMoved /= Near.fOwn;
  }
  return Near;
}

[numthreads(XR_RAY_SUN_GROUP, XR_RAY_SUN_GROUP, 1)]
void CSTemporal(uint3 vId : SV_DispatchThreadID, uint3 vGroup : SV_GroupID, uint nThread : SV_GroupIndex)
{
  // The rays round the group's pixels, each loaded once.
  const int nCells = 3 - TraceStep();
  const int nTile = XR_RAY_SUN_GROUP / TraceStep() + 2 * nCells;
  const int2 q00 = (int2)(vGroup.xy * XR_RAY_SUN_GROUP) / TraceStep() - nCells;
  for (int i = (int)nThread; i < nTile * nTile; i += XR_RAY_SUN_GROUP * XR_RAY_SUN_GROUP)
  {
    const int2 q = clamp(q00 + int2(i % nTile, i / nTile), 0, RaysSize() - 1);
    const float3 r = RaysAt(q);
    float2 vBefore = -1;
    if (g_Sun.nFlags.x && r.x >= 0)
    {
      const float3 h = HistoryAt(RayPixel(q) + 0.5, r.z);
      if (h.z > 0)
        vBefore = h.xy;
    }
    s_Rays[i] = r;
    s_RaysBefore[i] = vBefore;
  }
  GroupMemoryBarrierWithGroupSync();
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_Sun.vSize.xy))
    return;
  // x, y the visibility of all and of the live casters, z the frames in it (0: no surface), w the view depth.
  const float d = SurfaceDepth(p);
  if (d <= 0)
  {
    g_Out[p] = float4(1, 1, 0, 0);
    return;
  }
  // The rays of the cells round the pixel (5 x 5 of a ray a pixel, 3 x 3 of 2 x 2 pixels): the range of
  // what they show, those on its surface weighed by how near, and the nearest of any.
  SNearRays Near;
  if (TraceStep() == 1)
    Near = NearRays(p, d, 2, q00);
  else
    Near = NearRays(p, d, 1, q00);
  const float3 vHistory = g_Sun.nFlags.x ? HistoryAt(p + 0.5, d) : 0;
  if (Near.fOwn <= 0)
  {
    // No ray on its surface this frame (2 x 2 pixels a ray): the history as it is, else the nearest ray.
    g_Out[p] = vHistory.z > 0 ? float4(vHistory, d) : float4(Near.vNearest, 1, d);
    return;
  }
  // Where the rays round it find the shadow moved (their history sure of the other way: a caster that
  // moves), the history goes, to this frame's rays (else it lags behind, and the range of the rays round
  // the pixel cuts its edge on their grid).
  const float fFrames = max(min(vHistory.z + 1, HISTORY_FRAMES) * saturate(1 - 2 * Near.fMoved), 1);
  g_Out[p] = float4(lerp(clamp(vHistory.xy, Near.vMin, Near.vMax), Near.vOwn, 1 / fFrames), fFrames, d);
}

[numthreads(XR_RAY_SUN_GROUP, XR_RAY_SUN_GROUP, 1)]
void CSFilter(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_Sun.vSize.xy))
    return;
  const float4 c = g_Textures[g_Sun.nTextures.w].Load(int3(p, 0));
  if (c.z <= 0)
  {
    g_Out[p] = 0;   // view depth 0: no pixel's
    return;
  }
  const float d = ViewDepthOf(DepthAt(p));
  float2 vVis = c.xy;
  // Only the soft edges (and a young history): where the pixel is all in the sun or all in the shadow of
  // each set the history is as its rays round it.
  const bool bEdge = (vVis.x > 0 && vVis.x < 1) || (vVis.y > 0 && vVis.y < 1) || c.z < FILTER_YOUNG;
  if (g_Sun.nFlags.z && bEdge)
  {
    // The distance of the casters round the pixel: the mean of the rays that hit (25 of them over 9 x 9
    // pixels: every other one of a ray a pixel, each of 2 x 2 pixels a ray).
    const int2 q0 = p / TraceStep();
    const int nStride = 3 - TraceStep();
    float2 vT = 0;
    [unroll]
    for (int y = -2; y <= 2; y++)
    {
      [unroll]
      for (int x = -2; x <= 2; x++)
      {
        const float r = RaysAt(q0 + int2(x, y) * nStride).x;
        if (r > 0)
          vT += float2(r, 1);
      }
    }
    const float fPixel = d * g_Sun.vParams.w;
    const float fRadius = vT.y > 0 ? min(vT.x / vT.y * g_Sun.vSun.w / fPixel, FILTER_MAX_RADIUS) : 0;
    if (fRadius > 0.5)
    {
      const float3 vPos = PixelPos(p + 0.5, d);
      const float3 vN = FaceNormal(p, vPos, d);
      const float fPlane = 1 / (0.05 + 2 * fPixel);
      const float fTurn = 6.2831853 * Noise(p, g_Sun.nFlags.y + 13);
      float3 vSum = float3(vVis, 1);
      [unroll]
      for (uint k = 0; k < FILTER_TAPS; k++)
      {
        // A spiral of golden angles, its points equally spread over the disc.
        const float r = sqrt((k + 0.5) / FILTER_TAPS);
        const float a = fTurn + 2.3999632 * k;
        const int2 q = p + (int2)round(r * fRadius * float2(cos(a), sin(a)));
        if (any(q < 0) || any(q >= (int2)g_Sun.vSize.xy))
          continue;
        const float4 h = g_Textures[g_Sun.nTextures.w].Load(int3(q, 0));
        if (h.z <= 0)
          continue;
        const float w = exp(-2 * r * r) * saturate(1 - abs(dot(PixelPos(q + 0.5, h.w) - vPos, vN)) * fPlane);
        vSum += w * float3(h.xy, 1);
      }
      vVis = vSum.xy / vSum.z;
    }
  }
  // How much the mask counts: all of it, giving way to the cascades over the end of its range.
  const float fWeight = saturate((g_Sun.vParams.x - d) / max(g_Sun.vParams.x - g_Sun.vParams.y, 1e-3));
  const uint n = (uint)round(saturate(vVis.x) * 255) | ((uint)round(saturate(vVis.y) * 255) << 8) | ((uint)round(fWeight * 255) << 16);
  g_Out[p] = float4(d, (float)n, 0, 0);
}
