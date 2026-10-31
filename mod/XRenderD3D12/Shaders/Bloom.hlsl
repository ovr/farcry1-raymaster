// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Bloom.hlsl
//	Description: A level of the bloom from the one before (Hdr.cpp), as
//	Hdr.hlsl's HDR_BLOOM_DOWN draws it, where the level before is twice
//	its size. The 13 taps of a texel are then at the centres and corners
//	of the target's texels: each is read (and with the prefilter decoded
//	and exposed) once into the group's memory for the 8 x 8 texels of the
//	group, at the uv of that point (HdrShared.hlsli does the rest).
//
//	Bindings (D3D12Compute.h): b0 SBloomConstants, t0 the level before (the
//	scene for the prefilter), t1 the EV (1 x 1 R32_FLOAT); u0 the level
//	(D3D12_HDR_FORMAT).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "HdrShared.hlsli"

cbuffer cbBloom : register(b0)
{
  SBloomConstants g_Bloom;
};

Texture2D<float4> g_Source : register(t0);
Texture2D<float> g_Exposure : register(t1);
RWTexture2D<float4> g_Level : register(u0);
SamplerState g_SamplerLinearClamp : register(s1);

#define TILE 8
#define CENTRES (TILE + 2)          //!< the texel centres from one before the group's to one after
#define CORNERS (TILE + 1)          //!< the corners from the group's first texel's to its last one's

groupshared float3 gs_Centres[CENTRES * CENTRES];
groupshared float3 gs_Corners[CORNERS * CORNERS];

float3 Tap(float2 vUV, bool bScene, float fExposure)
{
  const float3 c = g_Source.SampleLevel(g_SamplerLinearClamp, vUV, 0).rgb;
  return bScene ? Decode(c) * fExposure : c;
}

[numthreads(TILE, TILE, 1)]
void CSMain(uint2 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID, uint nThread : SV_GroupIndex)
{
  const bool bScene = g_Bloom.vFlags.x != 0;
  const float fExposure = bScene ? exp2(g_Bloom.vParams.x + (g_Bloom.vParams.y > 0 ? g_Exposure.Load(int3(0, 0, 0)) : 0)) : 1;
  const float2 r = g_Bloom.vSize.zw;
  const int2 vFirst = (int2)(vGroup * TILE);
  for (uint n = nThread; n < CENTRES * CENTRES; n += TILE * TILE)
    gs_Centres[n] = Tap((float2(vFirst + int2(n % CENTRES, n / CENTRES) - 1) + 0.5) * r, bScene, fExposure);
  for (uint n = nThread; n < CORNERS * CORNERS; n += TILE * TILE)
    gs_Corners[n] = Tap(float2(vFirst + int2(n % CORNERS, n / CORNERS)) * r, bScene, fExposure);
  GroupMemoryBarrierWithGroupSync();

  const uint2 vTexel = vGroup * TILE + vThread;
  if (any(vTexel >= (uint2)g_Bloom.vSize.xy))
    return;
  // The centres of the texels around (one before: index 0), the corners of the texel (its first: 0).
  const uint c = vThread.y * CENTRES + vThread.x, k = vThread.y * CORNERS + vThread.x;
  const float3 vLight = BloomDownTaps(
    gs_Centres[c], gs_Centres[c + 1], gs_Centres[c + 2],
    gs_Corners[k], gs_Corners[k + 1],
    gs_Centres[c + CENTRES], gs_Centres[c + CENTRES + 1], gs_Centres[c + CENTRES + 2],
    gs_Corners[k + CORNERS], gs_Corners[k + CORNERS + 1],
    gs_Centres[c + 2 * CENTRES], gs_Centres[c + 2 * CENTRES + 1], gs_Centres[c + 2 * CENTRES + 2],
    bScene, g_Bloom.vParams.z, g_Bloom.vParams.w);
  g_Level[vTexel] = float4(vLight, 1);
}
