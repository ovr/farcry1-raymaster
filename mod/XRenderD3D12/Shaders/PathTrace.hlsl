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
//	The rays leave a pixel's surface from its point on the ray scene's
//	(PixelSurface): the depth buffer's view depth is off by up to some
//	1e-3 of itself (Far Cry's world coordinates in single floats, a near
//	plane of 0.25 m; a few tenths of a metre at 300 m, under the surface
//	or over it, another way each frame), more than a ray's offset from
//	the surface: the terrain's sun ray (TerrainBakedShare) found the
//	terrain it left, streaks of shadow over the hills that came and went.
//	  CSTrace: a path from each pixel's surface. Its lamps' light by a ray
//	    (PathSampleLamp), then nPath.y bounces in directions of the
//	    hemisphere as the cosine spreads them: a hit gives its emission
//	    and its albedo times the sun's light and a lamp's there, the next
//	    bounce's light comes times its albedo; where a path finds nothing,
//	    the sky's light (vSky). The game's ambient light fills in: a ray
//	    that finds nothing nearer than vFill.y brings the ambient light of
//	    the point it leaves (the G-buffer's, then the hit's) times vFill.x,
//	    as the raster's ambient times its occlusion (where it finds nothing
//	    at all, the brighter of that and the sky). With vFill.z (where the
//	    frame has no sun) the pixel's light map adds what it lights beyond
//	    the lamps baked into it (PathLightmapFill, the G-buffer's
//	    nTextures4.x), times vFill.z. The sun's light on the
//	    pixel's own surface is not in it: the composite takes the
//	    visibility the raster found
//	    (the G-buffer's). With the radiance cache (nCache.x;
//	    PathCache.hlsli) the last bounce's hit takes all the light that
//	    comes to it from there (a cell across its face picked at random:
//	    noise, not edges), not by rays towards the sun and a lamp, where
//	    the cell has some. rgb the light, w the pixel's view depth (0: no
//	    surface the path tracer lights); u2 the hit distance of the first
//	    bounce (the rays' length where it finds nothing, 0 where the path
//	    ends at once), which NRD weighs (PathNrd.hlsl; a null view without
//	    it). With nCache.z rgb is the cache's light at the pixel's surface
//	    (d3d12_PTDebug 11).
//	  CSCacheUpdate: the radiance cache's samples, from a pixel of each
//	    nCache.w x nCache.w (another each frame): a path of CACHE_BOUNCES
//	    bounces at most from its surface as CSTrace's, the sun's and a
//	    lamp's light by rays at each point, the cache's at the end; back
//	    from there, all that comes to each of its points (the pixel's
//	    surface, the hits) is a sample of the cell there.
//	  CSCacheResolve: each entry of the cache: its samples of the frame
//	    blended into its light (vCache.z frames at most), the entry free
//	    again after PATH_CACHE_MAX_AGE frames without one.
//	  CSTemporal: blended into the history where the surface was in the
//	    frame before, vParams.z frames at most (the reference: all of them
//	    while the view stays).
//	  CSFilter: the history filtered on the pixel's surface (its plane and
//	    its normal), wider where it is young; rgb the composite's light, w
//	    the share of the light baked into the terrain's texture there
//	    (TerrainBakedShare; 1 elsewhere), which the composite takes out of
//	    its albedo (times SPathShade::vBakeLight).
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
#include "PathCache.hlsli"

ConstantBuffer<SPathTraceConstants> g_PT : register(b2);
RWTexture2D<float4> g_Out : register(u0);
RWTexture2D<unorm float> g_Frames : register(u1);
RWTexture2D<float> g_HitDistance : register(u2);
#define RAY_VIEW g_PT
#include "RayView.hlsli"
#include "PathCheckerboard.hlsli"

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
  P.vBakeLight = g_PT.vBakeLight.rgb;
  P.fLightGap = g_PT.vShade.y;
  P.fBakedFalloff = g_PT.vShade.z;
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

//! The G-buffer's normal (texel vGBuffer): on the side of its triangle the camera sees (the leaves' two sides), as
//! General.hlsl wrote it.
float3 PixelNormal(float4 vGBuffer)
{
  return PathOctDecode(vGBuffer.rg);
}

//! How far the depth buffer's view depth can be off, of itself (see the head of the file), and the least (m).
static const float PIXEL_DEPTH_ERROR = 2e-3;
static const float PIXEL_DEPTH_ERROR_MIN = 0.02;

//! The ray from the camera through the pixel's point vPos (from the depth buffer) within PIXEL_DEPTH_ERROR of its
//! distance, where the ray scene's surface the raster drew there is (TMax 0 at the camera).
RayDesc PixelSurfaceRay(float3 vPos)
{
  const float3 vRay = vPos - g_PT.vCameraPos.xyz;
  const float fLength = length(vRay);
  const float fError = PIXEL_DEPTH_ERROR_MIN + PIXEL_DEPTH_ERROR * fLength;
  RayDesc Ray;
  Ray.Origin = g_PT.vCameraPos.xyz;
  Ray.Direction = fLength > 0 ? vRay / fLength : float3(0, 0, 1);
  Ray.TMin = max(fLength - fError, 0.01);
  Ray.TMax = fLength > 0 ? fLength + fError : 0;
  return Ray;
}

//! The pixel's point vPos (from the depth buffer) and its face vFace on the ray scene's surface the raster drew there:
//! the nearest hit (alpha tested as TraceScene) among the instances of nMask of PixelSurfaceRay, and that triangle's
//! normal on the camera's side. The vegetation's (nMaterial, PathGBufferMaterial) as they are: its leaves bend in the
//! wind as the ray scene's do not. Where the ray finds nothing (a surface the ray scene leaves out), vPos and vFace as
//! they are too.
void PixelSurface(inout float3 vPos, inout float3 vFace, uint nMaterial, uint nMask)
{
  const RayDesc Ray = PixelSurfaceRay(vPos);
  if (nMaterial == XR_PT_MAT_VEGETATION + 1 || Ray.TMax <= 0)
    return;
  const SRayHit Hit = TraceScene(Ray, nMask, 0, g_PT.vParams.y, g_PT.nPath.z);
  if (!Hit.bHit)
    return;
  vPos = Ray.Origin + Ray.Direction * Hit.fT;
  vFace = RayFaceNormal(Hit.nInstance, Hit.nPrimitive, Hit.mObjectToWorld, Ray.Direction);
}

//! PixelSurface on the terrain alone (XR_RAY_TERRAIN), a terrain's pixel: the first of its triangles PixelSurfaceRay
//! finds (opaque; a height field has one surface so near the point).
void PixelTerrain(inout float3 vPos, inout float3 vFace)
{
  RayDesc Ray = PixelSurfaceRay(vPos);
  if (Ray.TMax <= 0)
    return;
  RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, XR_RAY_TERRAIN, Ray);
  Query.Proceed();
  if (Query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    return;
  vPos = Ray.Origin + Ray.Direction * Query.CommittedRayT();
  vFace = RayFaceNormal(Query.CommittedInstanceID(), Query.CommittedPrimitiveIndex(), Query.CommittedObjectToWorld3x4(), Ray.Direction);
}

//! The share of the light baked into the terrain's texture at a terrain pixel's point vPos (the depth buffer's; vN its
//! normal, vFace its face, d its view depth) as the composite takes it: TerrainBakedShare from its point on the ray
//! scene's terrain (PixelTerrain), the sun's ray over the terrain alone.
float PixelTerrainShare(float3 vPos, float3 vN, float3 vFace, float d)
{
  PixelTerrain(vPos, vFace);
  return TerrainBakedShare(vPos, vN, vFace, PathShadeParams(0), d * g_PT.vParams.y);
}

//! The face a path along vDir leaves a leaf by (the vegetation's pixels; vFace the side the camera sees): the leaf's
//! normal is the crown's (PathGBufferNormal), most of them up, so a path is often under the leaf's face; it goes on
//! through the leaf from its other side, as the raster lights both sides of a leaf by the crown's normal.
float3 PathLeafSide(float3 vFace, float3 vDir)
{
  return dot(vDir, vFace) < 0 ? -vFace : vFace;
}

//! A direction of the hemisphere about vN as the cosine spreads them, from two random numbers u, v of 0..1.
float3 CosineDirection(float3 vN, float u, float v)
{
  const float r = sqrt(u), a = 6.2831853 * v;
  const float3 vT = normalize(abs(vN.z) < 0.9 ? cross(vN, float3(0, 0, 1)) : cross(vN, float3(1, 0, 0)));
  const float3 vB = cross(vN, vT);
  return normalize(r * cos(a) * vT + r * sin(a) * vB + sqrt(saturate(1 - u)) * vN);
}

//! The radiance cache's light (PathCache.hlsli) where a path's last bounce ends at vPos, the face's normal vN: of a cell
//! picked across the face at random (vCache.w cells at most from vPos); bFound false (and 0) where it has none yet.
float3 PathCacheAt(float3 vPos, float3 vN, inout uint nSeed, out bool bFound)
{
  const float fSize = PathCacheCellSize(vPos, g_PT.vCache, g_PT.vCameraPos.xyz);
  const float3 vT = normalize(abs(vN.z) < 0.9 ? cross(vN, float3(0, 0, 1)) : cross(vN, float3(1, 0, 0)));
  const float3 vB = cross(vN, vT);
  const float u = PathRandom(nSeed) - 0.5, v = PathRandom(nSeed) - 0.5;
  const float3 vAt = vPos + (u * vT + v * vB) * (fSize * g_PT.vCache.w);
  uint nEntry;
  bFound = false;
  if (!PathCacheFind(PathCacheCellOf(vAt, vN, g_PT.vCache, g_PT.vCameraPos.xyz), nEntry))
    return 0;
  const float4 vLight = PathCacheLight(nEntry);
  bFound = vLight.w > 0;
  return bFound ? vLight.rgb : 0;
}

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSTrace(uint3 vId : SV_DispatchThreadID)
{
  int2 p = (int2)vId.xy;
  // The checkerboard: a pixel of each two of a row, the others from their neighbours (PathCheckerboard.hlsli).
  if (g_PT.nCheckerboard.x)
    p.x = 2 * p.x + (int)((vId.y ^ g_PT.nCheckerboard.y) & 1);
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 vGBuffer = GBufferNormalAt(p);
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  if (PathGBufferMaterial(vGBuffer) == 0 || z >= 1 || d <= 0)
  {
    g_Out[p] = 0;
    g_HitDistance[p] = 0;
    return;
  }
  float3 vPos = PixelPos(p + 0.5, d);
  float3 vFace = FaceNormal(p, vPos, d);
  PixelSurface(vPos, vFace, PathGBufferMaterial(vGBuffer), g_PT.nFlags.w & 0xff);
  const float3 vN = PixelNormal(vGBuffer);
  const float fPixel = d * g_PT.vParams.y;
  float fHitDistance = 0;
  // Each pixel's own numbers, others each frame.
  uint nSeed = Hash((uint)p.x | ((uint)p.y << 16)) ^ Hash(g_PT.nFlags.y * 0x9e3779b9u + 1);
  SPathShade P = PathShadeParams(XR_RAY_MAX_ALPHA_TESTS);
  // d3d12_PTDebug 11: the radiance cache's light at the pixel's surface.
  if (g_PT.nCache.z)
  {
    uint nEntry;
    const bool bFound = PathCacheFind(PathCacheCellOf(vPos, vN, g_PT.vCache, g_PT.vCameraPos.xyz), nEntry);
    g_Out[p] = float4(bFound ? PathCacheLight(nEntry).rgb : 0, d);
    g_HitDistance[p] = 0;
    return;
  }
  float3 vLight = PathSampleLamp(vPos, vN, vFace, P, nSeed, fPixel);
  // The game's ambient light the paths fill in with, at the point a ray leaves: the pixel's, then each hit's.
  float3 vFill = PathAmbientLight(g_Textures[g_PT.nTextures2.w].Load(int3(p, 0)).gba) * g_PT.vFill.x;

  // The bounces.
  P.nMaxTests = g_PT.nPath.z;
  float3 vThrough = 1;
  float3 vOrigin = vPos, vNormal = vN, vFaceNormal = vFace;
  float fWidth = fPixel;
  bool bLeaves = PathGBufferMaterial(vGBuffer) == XR_PT_MAT_VEGETATION + 1;
  [loop]
  for (uint b = 0; b < g_PT.nPath.y; b++)
  {
    const float u = PathRandom(nSeed), v = PathRandom(nSeed);
    const float3 vDir = CosineDirection(vNormal, u, v);
    if (bLeaves)
      vFaceNormal = PathLeafSide(vFaceNormal, vDir);
    // Under the face (a normal map's normal leaning over): the path ends.
    if (dot(vDir, vFaceNormal) <= 0)
      break;
    RayDesc Ray;
    Ray.Origin = vOrigin + vFaceNormal * (0.01 + fWidth);
    Ray.Direction = vDir;
    Ray.TMin = 0.005;
    Ray.TMax = g_PT.vParams.x;
    const SRayHit Hit = TraceScene(Ray, P.nMask, fWidth, PT_CONE_SPREAD, P.nMaxTests);
    if (b == 0)
      fHitDistance = Hit.bHit ? Hit.fT : Ray.TMax;
    if (!Hit.bHit)
    {
      vLight += vThrough * max(g_PT.vSky.rgb, vFill);
      break;
    }
    if (Hit.fT > g_PT.vFill.y)
      vLight += vThrough * vFill;
    const float fHitWidth = fWidth + Hit.fT * PT_CONE_SPREAD;
    const SPathSurface H = PathSurface(Hit, vDir, fHitWidth, P);
    // The last bounce: all the light that comes to its hit from the radiance cache (the sun's and the lamps' too, no
    // rays towards them), where its cell has some; else the sun's and a lamp's by rays.
    bool bCached = false;
    float3 vHitLight = 0;
    if (g_PT.nCache.x && b + 1 == g_PT.nPath.y)
      vHitLight = PathCacheAt(H.vPos, H.vNormal, nSeed, bCached);
    if (!bCached)
      vHitLight = PathSun(H.vPos, H.vNormal, H.vFaceNormal, P, fHitWidth) +
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
    bLeaves = false;
  }
  // In real time a sample's light at most: what a path rarely finds (a lamp by a bounce) would stay a blotch.
  const float fLuminance = PathLuminance(vLight);
  if (g_PT.vParams.w > 0 && fLuminance > g_PT.vParams.w)
    vLight *= g_PT.vParams.w / fLuminance;
  // d3d12_PTLightMaps, where the frame has no sun: what the pixel's light map lights it with beyond the lamps.
  if (g_PT.vFill.z > 0)
    vLight += g_PT.vFill.z * PathLightmapFill(g_Textures[g_PT.nTextures4.x].Load(int3(p, 0)).rgb,
                                              g_Textures[g_PT.nTextures2.w].Load(int3(p, 0)).gba, vPos, vN, P);
  g_Out[p] = float4(max(vLight, 0), d);
  g_HitDistance[p] = fHitDistance;
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
  float fHit;
  const float4 r = PathRaysAt(p, false, fHit);
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
  const float3 vN = PixelNormal(vGBuffer);
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
      const float fNormal = saturate(dot(PathNormalAt(q), vN));
      const float w = exp(-2 * r * r) * saturate(1 - abs(dot(vQ - vPos, vFace)) * fPlane) * pow(fNormal, 8);
      vSum += w * float4(h.rgb, 1);
    }
    vLight = vSum.rgb / vSum.w;
  }
  // The terrain: the share of the light its texture has baked in (the composite takes it times SPathShade::vBakeLight and
  // divides the albedo by that).
  float fBaked = 1;
  if (PathGBufferMaterial(vGBuffer) == XR_PT_MAT_TERRAIN + 1 && g_PT.vShade.x > 1)
    fBaked = PixelTerrainShare(vPos, vN, vFace, d);
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
  float3 vPos = PixelPos(p + 0.5, d);
  float3 vFace = FaceNormal(p, vPos, d);
  PixelSurface(vPos, vFace, PathGBufferMaterial(vGBuffer), g_PT.nFlags.w & 0xff);
  const float3 vN = PixelNormal(vGBuffer);
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
    vSpecular += vLamp * TraceLight(vPos, vFace, vToLight, fDistance, PathLampGap(L, P), P.nMask, d * g_PT.vParams.y, fHit, P.nMaxTests);
    nRays++;
  }
  g_Out[p] = float4(vColor * vSpecular, 0);
}

//! The bounces of a cache path at most (its points: the pixel's surface and its hits but the last).
static const uint CACHE_BOUNCES = 3;

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSCacheUpdate(uint3 vId : SV_DispatchThreadID)
{
  // A pixel of each nCache.w x nCache.w, another each frame.
  const uint nTile = g_PT.nCache.w;
  uint nSeed = PathCacheHash(vId.x | (vId.y << 16)) ^ PathCacheHash(g_PT.nFlags.y * 0x9e3779b9u + 7);
  const float fX = PathRandom(nSeed), fY = PathRandom(nSeed);
  const int2 p = (int2)(vId.xy * nTile + min((uint2)(float2(fX, fY) * nTile), nTile - 1));
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 vGBuffer = GBufferNormalAt(p);
  const float z = DepthAt(p);
  const float d = ViewDepthOf(z);
  if (PathGBufferMaterial(vGBuffer) == 0 || z >= 1 || d <= 0)
    return;
  float3 vPos = PixelPos(p + 0.5, d), vFace = FaceNormal(p, vPos, d);
  PixelSurface(vPos, vFace, PathGBufferMaterial(vGBuffer), g_PT.nFlags.w & 0xff);
  SPathShade P = PathShadeParams(g_PT.nPath.z);
  // The path's points (each one's entry, the light that comes straight to it from the sun and a lamp, what its ray's hit
  // gives back of its own, that hit's albedo), then from the last back all the light that comes to each.
  uint Entries[CACHE_BOUNCES];
  float3 Direct[CACHE_BOUNCES], Gives[CACHE_BOUNCES], Albedo[CACHE_BOUNCES];
  bool Valid[CACHE_BOUNCES];
  uint nPoints = 0;
  float3 vOrigin = vPos, vNormal = PixelNormal(vGBuffer), vFaceNormal = vFace;
  bool bLeaves = PathGBufferMaterial(vGBuffer) == XR_PT_MAT_VEGETATION + 1;
  float3 vFill = PathAmbientLight(g_Textures[g_PT.nTextures2.w].Load(int3(p, 0)).gba) * g_PT.vFill.x;
  float fWidth = d * g_PT.vParams.y;
  // The sun through a leaf (its ray from the leaf's side towards the sun), as the paths go through it.
  const float3 vSunFace = bLeaves ? PathLeafSide(vFaceNormal, g_Frame.vSunDir.xyz) : vFaceNormal;
  float3 vDirect = PathSun(vOrigin, vNormal, vSunFace, P, fWidth) + PathSampleLamp(vOrigin, vNormal, vFaceNormal, P, nSeed, fWidth);
  float3 vLast = 0;
  [loop]
  for (uint b = 0; b < CACHE_BOUNCES; b++)
  {
    const float u = PathRandom(nSeed), v = PathRandom(nSeed);
    const float3 vDir = CosineDirection(vNormal, u, v);
    if (bLeaves)
      vFaceNormal = PathLeafSide(vFaceNormal, vDir);
    // Under the face (a normal map's normal leaning over): the path ends at this point, the cache's light comes to it
    // (where it has none, what comes straight to it).
    if (dot(vDir, vFaceNormal) <= 0)
    {
      bool bFound;
      vLast = PathCacheAt(vOrigin, vNormal, nSeed, bFound);
      if (!bFound)
        vLast = vDirect;
      break;
    }
    Valid[b] = PathCacheInsert(PathCacheCellOf(vOrigin, vNormal, g_PT.vCache, g_PT.vCameraPos.xyz), Entries[b]);
    nPoints = b + 1;
    Direct[b] = vDirect;
    Gives[b] = 0;
    Albedo[b] = 0;
    RayDesc Ray;
    Ray.Origin = vOrigin + vFaceNormal * (0.01 + fWidth);
    Ray.Direction = vDir;
    Ray.TMin = 0.005;
    Ray.TMax = g_PT.vParams.x;
    const SRayHit Hit = TraceScene(Ray, P.nMask, fWidth, PT_CONE_SPREAD, P.nMaxTests);
    if (!Hit.bHit)
    {
      Gives[b] = max(g_PT.vSky.rgb, vFill);
      break;
    }
    const float fHitWidth = fWidth + Hit.fT * PT_CONE_SPREAD;
    const SPathSurface H = PathSurface(Hit, vDir, fHitWidth, P);
    Gives[b] = (Hit.fT > g_PT.vFill.y ? vFill : 0) + H.vEmission;
    Albedo[b] = H.vAlbedo;
    // The last hit: the cache's light comes to it, as to CSTrace's (where it has none, the sun's and a lamp's by rays).
    bool bFound = false;
    if (b + 1 == CACHE_BOUNCES)
      vLast = PathCacheAt(H.vPos, H.vNormal, nSeed, bFound);
    vDirect = bFound ? 0 : PathSun(H.vPos, H.vNormal, H.vFaceNormal, P, fHitWidth) +
                           PathSampleLamp(H.vPos, H.vNormal, H.vFaceNormal, P, nSeed, fHitWidth);
    if (b + 1 == CACHE_BOUNCES && !bFound)
      vLast = vDirect;
    vFill = H.vAmbient * g_PT.vFill.x;
    vOrigin = H.vPos;
    vNormal = H.vNormal;
    vFaceNormal = H.vFaceNormal;
    fWidth = fHitWidth;
    bLeaves = false;
  }
  // Back from the last point: what comes to point b is what comes straight to it, what its ray's hit gives back of its
  // own and that hit's albedo times what comes to the hit.
  float3 vIn = vLast;
  [loop]
  for (int i = (int)nPoints - 1; i >= 0; i--)
  {
    vIn = Direct[i] + Gives[i] + Albedo[i] * vIn;
    const float fLuminance = PathLuminance(vIn);
    const float3 vSample = g_PT.vParams.w > 0 && fLuminance > g_PT.vParams.w ? vIn * (g_PT.vParams.w / fLuminance) : vIn;
    if (Valid[i])
      PathCacheAdd(Entries[i], max(vSample, 0));
  }
}

[numthreads(256, 1, 1)]
void CSCacheResolve(uint3 vId : SV_DispatchThreadID)
{
  const uint e = vId.x;
  if (e >= PATH_CACHE_ENTRIES || g_PathCache.Load(e * 4) == 0)
    return;
  const uint nAt = PATH_CACHE_SAMPLES + e * 16;
  const uint4 s = g_PathCache.Load4(nAt);
  g_PathCache.Store4(nAt, uint4(0, 0, 0, 0));
  const uint2 n = g_PathCache.Load2(PATH_CACHE_LIGHT + e * 8);
  float3 vLight = float3(f16tof32(n.x), f16tof32(n.x >> 16), f16tof32(n.y));
  // Starting again: the light of the frames before is not taken.
  uint nFrames = g_PT.nCache.y ? 0 : (n.y >> 16) & 0xff, nAge = n.y >> 24;
  if (g_PT.nCache.y)
    vLight = 0;
  const uint nSamples = min(s.w, PATH_CACHE_MAX_SAMPLES);
  if (nSamples)
  {
    nFrames = min(nFrames + 1, (uint)g_PT.vCache.z);
    vLight = lerp(vLight, (float3)s.rgb / (PATH_CACHE_SCALE * nSamples), 1.0 / nFrames);
    nAge = 0;
  }
  else if (++nAge > PATH_CACHE_MAX_AGE)
  {
    // Not seen for long: the entry free again.
    g_PathCache.Store(e * 4, 0);
    g_PathCache.Store2(PATH_CACHE_LIGHT + e * 8, uint2(0, 0));
    return;
  }
  g_PathCache.Store2(PATH_CACHE_LIGHT + e * 8, uint2(f32tof16(vLight.r) | (f32tof16(vLight.g) << 16), f32tof16(vLight.b) | (nFrames << 16) | (nAge << 24)));
}
