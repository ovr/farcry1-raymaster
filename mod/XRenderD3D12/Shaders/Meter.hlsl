// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Meter.hlsl
//	Description: The meter's first level (Hdr.cpp), as Hdr.hlsl's
//	HDR_LUMINANCE draws it with 4 x 4 taps a texel: a thread a tap, the
//	threads of a texel's taps side by side (so a read of the group's taps
//	is of texels next to each other), then the light of its taps added up
//	in the program's order (HdrShared.hlsli does the rest).
//
//	Bindings (D3D12Compute.h): b0 SMeterConstants, t0 the scene; u0 the
//	level (D3D12_HDR_FORMAT).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "HdrShared.hlsli"

cbuffer cbMeter : register(b0)
{
  SMeterConstants g_Meter;
};

Texture2D<float4> g_Scene : register(t0);
RWTexture2D<float4> g_Level : register(u0);
SamplerState g_SamplerLinearClamp : register(s1);

#define TAPS 4                    //!< taps a side of a texel (Hdr.cpp's METER_TAPS)
#define TEXELS 2                  //!< texels a side of a group
#define SIDE (TAPS * TEXELS)      //!< threads a side of a group

groupshared float3 gs_Light[SIDE * SIDE];

[numthreads(SIDE, SIDE, 1)]
void CSMain(uint2 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  const uint2 vTexel = vGroup * TEXELS + vThread / TAPS;
  const int2 vTap = (int2)(vThread % TAPS);
  const float2 vTexelUV = g_Meter.vSize.zw;
  const float2 vUV = (float2(vTexel) + 0.5) * vTexelUV;
  const int n = (int)g_Meter.vTaps.x;
  gs_Light[vThread.y * SIDE + vThread.x] =
    Decode(g_Scene.SampleLevel(g_SamplerLinearClamp, MeterTapUV(vUV, vTexelUV, vTap.x, vTap.y, n), 0).rgb);
  GroupMemoryBarrierWithGroupSync();
  if (any(vTap != 0) || any(vTexel >= (uint2)g_Meter.vSize.xy))
    return;
  float3 vSum = 0;
  [unroll]
  for (int y = 0; y < TAPS; y++)
  {
    [unroll]
    for (int x = 0; x < TAPS; x++)
      vSum += gs_Light[(vThread.y + y) * SIDE + vThread.x + x];
  }
  g_Level[vTexel] = MeterTexel(vSum, n, vUV, vTexelUV);
}
