// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathCache.hlsli
//	Description: The path tracer's radiance cache (d3d12_PTCache,
//	PathTrace.cpp): a hash grid over the world of all the light that
//	comes to a point (the sun's and the lamps' straight to it, and by the
//	bounces), as PathTrace.hlsl's paths bring it: the mean, over the
//	directions of the hemisphere as the cosine spreads them, of what a
//	ray finds. A cell is a cube, vCache.x m near the camera and twice
//	as large each time the distance doubles past vCache.x / vCache.y,
//	times the axis its normal leans along most (6 of them: no light
//	through a thin wall from its other side).
//	  The buffer (RWByteAddressBuffer, u3): the keys (a uint an entry, 0
//	free), then the samples of the frame (4 uints an entry: rgb * 256
//	and their count), then the light (2 uints: rgb as halves, the frames
//	in it and the frames since its last sample; an entry without one for
//	PATH_CACHE_MAX_AGE frames is free again).
//	  CSCacheUpdate (PathTrace.hlsl) adds samples, CSCacheResolve blends
//	them in, CSTrace takes it for all the light at its last bounce's hit
//	(no rays towards the sun and a lamp there).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PATHCACHE_HLSLI
#define XRENDERD3D12_PATHCACHE_HLSLI

//! The entries (a power of 2), the slots a key may take after its own, the samples a frame adds to an entry at most.
static const uint PATH_CACHE_ENTRIES = XR_PT_CACHE_ENTRIES;
static const uint PATH_CACHE_PROBES = 8;
static const uint PATH_CACHE_MAX_SAMPLES = 255;
static const uint PATH_CACHE_MAX_AGE = 120;
//! The fixed point of the samples' sums.
static const float PATH_CACHE_SCALE = 256;
static const uint PATH_CACHE_SAMPLES = PATH_CACHE_ENTRIES * 4;
static const uint PATH_CACHE_LIGHT = PATH_CACHE_ENTRIES * 20;

RWByteAddressBuffer g_PathCache : register(u3);

//! A 32 bit hash.
uint PathCacheHash(uint x)
{
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

//! A cell: the slot its key starts at and the key (never 0).
struct SPathCacheCell
{
  uint nSlot;
  uint nKey;
};

//! The size of the cells at vPos (m): vCache.x at the camera, doubling each time the distance doubles beyond vCache.x / vCache.y.
float PathCacheCellSize(float3 vPos, float4 vCache, float3 vCamera)
{
  const float fLevel = clamp(floor(log2(max(distance(vPos, vCamera) * vCache.y / vCache.x, 1))), 0, 15);
  return vCache.x * exp2(fLevel);
}

//! The cell of the point vPos with the normal vN.
SPathCacheCell PathCacheCellOf(float3 vPos, float3 vN, float4 vCache, float3 vCamera)
{
  const float fSize = PathCacheCellSize(vPos, vCache, vCamera);
  const int3 c = (int3)floor(vPos / fSize);
  const uint nLevel = (uint)round(log2(fSize / vCache.x));
  const float3 a = abs(vN);
  const uint nAxis = a.x >= a.y && a.x >= a.z ? (vN.x < 0 ? 1u : 0u) : a.y >= a.z ? (vN.y < 0 ? 3u : 2u) : (vN.z < 0 ? 5u : 4u);
  const uint nLo = ((uint)c.x & 0xffff) | ((uint)c.y << 16);
  const uint nHi = ((uint)c.z & 0xffff) | (nLevel << 16) | (nAxis << 20);
  SPathCacheCell Cell;
  Cell.nSlot = PathCacheHash(nLo ^ PathCacheHash(nHi));
  Cell.nKey = PathCacheHash(nHi * 0x9e3779b9u ^ PathCacheHash(nLo + 0x632be5abu)) | 1;
  return Cell;
}

//! The entry of a cell, taken where it has none yet (false: none, the slots all taken by others).
bool PathCacheInsert(SPathCacheCell Cell, out uint nEntry)
{
  [loop]
  for (uint i = 0; i < PATH_CACHE_PROBES; i++)
  {
    nEntry = (Cell.nSlot + i) & (PATH_CACHE_ENTRIES - 1);
    uint nWas;
    g_PathCache.InterlockedCompareExchange(nEntry * 4, 0, Cell.nKey, nWas);
    if (nWas == 0 || nWas == Cell.nKey)
      return true;
  }
  nEntry = 0;
  return false;
}

//! The entry of a cell (false: none).
bool PathCacheFind(SPathCacheCell Cell, out uint nEntry)
{
  [loop]
  for (uint i = 0; i < PATH_CACHE_PROBES; i++)
  {
    nEntry = (Cell.nSlot + i) & (PATH_CACHE_ENTRIES - 1);
    const uint nKey = g_PathCache.Load(nEntry * 4);
    if (nKey == Cell.nKey)
      return true;
    if (nKey == 0)
      break;
  }
  nEntry = 0;
  return false;
}

//! An entry's light (rgb) and the frames in it (w; 0: none yet).
float4 PathCacheLight(uint nEntry)
{
  const uint2 n = g_PathCache.Load2(PATH_CACHE_LIGHT + nEntry * 8);
  return float4(f16tof32(n.x), f16tof32(n.x >> 16), f16tof32(n.y), (n.y >> 16) & 0xff);
}

//! A sample of the light brought to an entry's cell.
void PathCacheAdd(uint nEntry, float3 vLight)
{
  const uint nAt = PATH_CACHE_SAMPLES + nEntry * 16;
  uint nCount;
  g_PathCache.InterlockedAdd(nAt + 12, 1, nCount);
  if (nCount >= PATH_CACHE_MAX_SAMPLES)
    return;
  const uint3 n = (uint3)round(clamp(vLight, 0, 1024) * PATH_CACHE_SCALE);
  uint nIgnore;
  g_PathCache.InterlockedAdd(nAt, n.r, nIgnore);
  g_PathCache.InterlockedAdd(nAt + 4, n.g, nIgnore);
  g_PathCache.InterlockedAdd(nAt + 8, n.b, nIgnore);
}

#endif
