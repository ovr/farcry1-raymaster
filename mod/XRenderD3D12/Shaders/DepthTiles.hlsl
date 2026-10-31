// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DepthTiles.hlsl
//	Description: The depth tiles of the water's depth copy (Water.cpp's
//	BuildDepthTiles): level 0 the least depth of each XR_DEPTH_TILE x
//	XR_DEPTH_TILE texels of the copy, each next level the least of 2 x 2
//	texels of the one before. A group makes 16 x 16 texels of its first
//	level and the four levels after (the threads' minima through group
//	shared memory): levels 0 to 4 from the copy, then (XR_TILES_UP)
//	levels 5 and 6 from level 4.
//
//	Bindings (D3D12Compute.h): b0 SDepthTileConstants; t0 the depth copy;
//	u0..u4 levels 0..4 (XR_TILES_UP: u0 level 4, read, u1 and u2 levels 5
//	and 6).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbTiles : register(b0)
{
  SDepthTileConstants g_Tiles;
};

SamplerState g_SamplerPointClamp : register(s0);

#ifndef XR_TILES_UP

Texture2D<float> g_Depth : register(t0);
RWTexture2D<float> g_Level0 : register(u0);
RWTexture2D<float> g_Level1 : register(u1);
RWTexture2D<float> g_Level2 : register(u2);
RWTexture2D<float> g_Level3 : register(u3);
RWTexture2D<float> g_Level4 : register(u4);

groupshared float gs_A[16 * 16];
groupshared float gs_B[8 * 8];

//! The least of the 2 x 2 texels of a level of n x n at the thread's place in the next.
float Least(uint2 v, uint n, bool bA)
{
  const uint i = 2 * v.y * n + 2 * v.x;
  return bA ? min(min(gs_A[i], gs_A[i + 1]), min(gs_A[i + n], gs_A[i + n + 1]))
            : min(min(gs_B[i], gs_B[i + 1]), min(gs_B[i + n], gs_B[i + n + 1]));
}

[numthreads(16, 16, 1)]
void CSMain(uint2 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  // Level 0: the copy's texels in gathers of 2 x 2 (at their shared corner; clamped at its edges),
  // all of them at once.
  const uint2 vTexel = vGroup * 16 + vThread;
  const int2 vFirst = (int2)vTexel * XR_DEPTH_TILE;
  float4 m = 1e30;
  [unroll]
  for (int y = 0; y < XR_DEPTH_TILE; y += 2)
  {
    [unroll]
    for (int x = 0; x < XR_DEPTH_TILE; x += 2)
      m = min(m, g_Depth.GatherRed(g_SamplerPointClamp, float2(vFirst + int2(x + 1, y + 1)) * g_Tiles.vSize.xy));
  }
  float d = min(min(m.x, m.y), min(m.z, m.w));
  g_Level0[vTexel] = d;
  gs_A[vThread.y * 16 + vThread.x] = d;
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 8))
  {
    d = Least(vThread, 16, true);
    g_Level1[vGroup * 8 + vThread] = d;
    gs_B[vThread.y * 8 + vThread.x] = d;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 4))
  {
    d = Least(vThread, 8, false);
    g_Level2[vGroup * 4 + vThread] = d;
    gs_A[vThread.y * 4 + vThread.x] = d;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 2))
  {
    d = Least(vThread, 4, true);
    g_Level3[vGroup * 2 + vThread] = d;
    gs_B[vThread.y * 2 + vThread.x] = d;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread == 0))
    g_Level4[vGroup] = Least(vThread, 2, false);
}

#else

RWTexture2D<float> g_Level4 : register(u0);
RWTexture2D<float> g_Level5 : register(u1);
RWTexture2D<float> g_Level6 : register(u2);

groupshared float gs_A[16 * 16];

[numthreads(16, 16, 1)]
void CSMain(uint2 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  // vSize.zw: the size of level 5 (even: level 6 is half of it).
  const uint2 vTexel = vGroup * 16 + vThread, vSize = (uint2)g_Tiles.vSize.zw;
  const bool bIn = all(vTexel < vSize);
  float d = 1e30;
  if (bIn)
  {
    const uint2 v = vTexel * 2;
    d = min(min(g_Level4[v], g_Level4[v + uint2(1, 0)]), min(g_Level4[v + uint2(0, 1)], g_Level4[v + uint2(1, 1)]));
    g_Level5[vTexel] = d;
  }
  gs_A[vThread.y * 16 + vThread.x] = d;
  GroupMemoryBarrierWithGroupSync();
  const uint2 vHalf = vGroup * 8 + vThread;
  if (all(vThread < 8) && all(vHalf < vSize / 2))
  {
    const uint i = 2 * vThread.y * 16 + 2 * vThread.x;
    g_Level6[vHalf] = min(min(gs_A[i], gs_A[i + 1]), min(gs_A[i + 16], gs_A[i + 17]));
  }
}

#endif
