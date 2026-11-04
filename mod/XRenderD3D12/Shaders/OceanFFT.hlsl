// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanFFT.hlsl
//	Description: The FFT ocean's kernels (OceanFFT.cpp's UpdateOcean, a
//	frame; OceanFFT.hlsli's math), each over the three cascades at once
//	(the group's z):
//	  XR_OCEAN_ROWS     a group a row: the four spectra of the sea at the
//	                    frame's time from h0, their inverse FFTs along x
//	  XR_OCEAN_COLUMNS  a group a column: the inverse FFTs along y, the
//	                    fields
//	  XR_OCEAN_WAVES    16 x 16 texels a group: the waves' texels
//	                    (OceanWaves) into level 0 and the means of 2 x 2
//	                    into levels 1 to 3 and level 4 (a texture of its
//	                    own: the next kernel reads it)
//	  XR_OCEAN_MIPS     a group a cascade: level 4 and the means of 2 x 2
//	                    into levels 5 to 8 (1 x 1); the foam's view's too
//	  XR_OCEAN_FOAM     16 x 16 texels a group: the foam's state (OceanFoam:
//	                    the whitecaps, the displacement, the height;
//	                    d3d12_WaterFoam), a level
//	  XR_OCEAN_FOAM_VIEW as the waves, the state into the foam's view, the
//	                    texture the water reads
//
//	Bindings (D3D12Compute.h): b0 SOceanConstants;
//	  ROWS     t0 h0 (RGBA32F, a slice a cascade: re, im, the steps of
//	           Omega); u0 the rows (RGBA32F, two slices a cascade: the
//	           spectra 0 and 1, 2 and 3)
//	  COLUMNS  t0 the rows; u0 the fields (as the rows)
//	  WAVES    t0 the fields; u0..u3 levels 0..3 of the waves (RGBA16F, a
//	           slice a cascade), u4 level 4 (XR_OCEAN_LEVEL4 a side)
//	  MIPS     t0 level 4; u0..u4 levels 4..8 of the waves (of the view)
//	  FOAM     t0 the fields, t1 the frame before's state (the other of
//	           the two targets); u0 the state (RGBA32F, a slice a cascade:
//	           a half float's store may round towards 0, the foam's fading
//	           lost by it)
//	  FOAM_VIEW t0 the state; u0..u3 levels 0..3 of the view (RGBA16F),
//	           u4 its level 4
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbOcean : register(b0)
{
  SOceanConstants g_Ocean;
};

#if defined(XR_OCEAN_ROWS) || defined(XR_OCEAN_COLUMNS)

#include "OceanFFT.hlsli"

Texture2DArray<float4> g_In : register(t0);
RWTexture2DArray<float4> g_Out : register(u0);

[numthreads(XR_OCEAN_SIZE / 2, 1, 1)]
void CSMain(uint j : SV_GroupThreadID, uint3 vGroup : SV_GroupID)
{
  // vGroup.y the row (the column), z the cascade; thread j the elements j and j + N / 2.
  const uint c = vGroup.z;
  [unroll]
  for (uint e = 0; e < 2; e++)
  {
    const uint i = j + e * (OCEAN_N / 2);
#ifdef XR_OCEAN_ROWS
    const uint2 p = uint2(i, vGroup.y);
    float2 C[4];
    OceanSpectra(g_In[uint3(p, c)].xyz, g_In[uint3(OceanOpposite(p), c)].xyz, OceanWaveVector(p, g_Ocean.vCascades[c].x), g_Ocean.vParams.x, C);
    [unroll]
    for (uint f = 0; f < 4; f++)
      gs_Ocean[0][f][i] = C[f];
#else
    const uint2 p = uint2(vGroup.y, i);
    const float4 a = g_In[uint3(p, 2 * c)], b = g_In[uint3(p, 2 * c + 1)];
    gs_Ocean[0][0][i] = a.xy;
    gs_Ocean[0][1][i] = a.zw;
    gs_Ocean[0][2][i] = b.xy;
    gs_Ocean[0][3][i] = b.zw;
#endif
  }
  GroupMemoryBarrierWithGroupSync();
  const uint nSrc = OceanInverseFFT(j);
  [unroll]
  for (uint e2 = 0; e2 < 2; e2++)
  {
    const uint i = j + e2 * (OCEAN_N / 2);
#ifdef XR_OCEAN_ROWS
    const uint2 p = uint2(i, vGroup.y);
#else
    const uint2 p = uint2(vGroup.y, i);
#endif
    g_Out[uint3(p, 2 * c)] = float4(gs_Ocean[nSrc][0][i], gs_Ocean[nSrc][1][i]);
    g_Out[uint3(p, 2 * c + 1)] = float4(gs_Ocean[nSrc][2][i], gs_Ocean[nSrc][3][i]);
  }
}

#elif defined(XR_OCEAN_FOAM)

Texture2DArray<float4> g_Fields : register(t0);
#define OCEAN_FIELDS(c, s, p) g_Fields[uint3(p, 2 * (c) + (s))]
#include "OceanFFT.hlsli"
Texture2DArray<float4> g_FoamBefore : register(t1);
RWTexture2DArray<float4> g_State : register(u0);

[numthreads(16, 16, 1)]
void CSMain(uint3 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  const uint c = vGroup.z;
  const uint2 vTexel = vGroup.xy * 16 + vThread;
  // The frame before's foam (the other of the two targets) unread in the first frame: its texels are not made yet;
  // spread from the texel to its four neighbours (the tile wrapping).
  float fBefore = 0;
  if (g_Ocean.vFoam.x > 0)
  {
    const uint m = XR_OCEAN_SIZE - 1;
    const float fAround = g_FoamBefore[uint3((vTexel.x + 1) & m, vTexel.y, c)].x + g_FoamBefore[uint3((vTexel.x + m) & m, vTexel.y, c)].x +
                          g_FoamBefore[uint3(vTexel.x, (vTexel.y + 1) & m, c)].x + g_FoamBefore[uint3(vTexel.x, (vTexel.y + m) & m, c)].x;
    fBefore = FoamSpread(g_FoamBefore[uint3(vTexel, c)].x, fAround, g_Ocean.vFoamCascades[c].z);
  }
  g_State[uint3(vTexel, c)] = OceanFoam(c, vTexel, g_Ocean.vParams.y, g_Ocean.vParams.z, g_Ocean.vCascades[c].z, fBefore, g_Ocean.vFoam.x,
                                        g_Ocean.vFoamCascades[c].x, g_Ocean.vFoamCascades[c].y);
}

#elif defined(XR_OCEAN_WAVES) || defined(XR_OCEAN_FOAM_VIEW)

#ifdef XR_OCEAN_FOAM_VIEW
Texture2DArray<float4> g_State : register(t0);
#else
Texture2DArray<float4> g_Fields : register(t0);
#define OCEAN_FIELDS(c, s, p) g_Fields[uint3(p, 2 * (c) + (s))]
#include "OceanFFT.hlsli"
#endif

RWTexture2DArray<float4> g_Level0 : register(u0);
RWTexture2DArray<float4> g_Level1 : register(u1);
RWTexture2DArray<float4> g_Level2 : register(u2);
RWTexture2DArray<float4> g_Level3 : register(u3);
RWTexture2DArray<float4> g_Level4 : register(u4);

groupshared float4 gs_A[16 * 16];
groupshared float4 gs_B[8 * 8];

//! The mean of the 2 x 2 texels of a level of n x n at the thread's place in the next.
float4 Mean(uint2 v, uint n, bool bA)
{
  const uint i = 2 * v.y * n + 2 * v.x;
  return bA ? 0.25 * (gs_A[i] + gs_A[i + 1] + gs_A[i + n] + gs_A[i + n + 1])
            : 0.25 * (gs_B[i] + gs_B[i + 1] + gs_B[i + n] + gs_B[i + n + 1]);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  const uint c = vGroup.z;
  const uint2 vTexel = vGroup.xy * 16 + vThread;
#ifdef XR_OCEAN_FOAM_VIEW
  float4 v = g_State[uint3(vTexel, c)];
#else
  float4 v = OceanWaves(c, vTexel, g_Ocean.vParams.y, g_Ocean.vParams.z, g_Ocean.vCascades[c].z, g_Ocean.vCascades[c].y);
#endif
  g_Level0[uint3(vTexel, c)] = v;
  gs_A[vThread.y * 16 + vThread.x] = v;
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 8))
  {
    v = Mean(vThread, 16, true);
    g_Level1[uint3(vGroup.xy * 8 + vThread, c)] = v;
    gs_B[vThread.y * 8 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 4))
  {
    v = Mean(vThread, 8, false);
    g_Level2[uint3(vGroup.xy * 4 + vThread, c)] = v;
    gs_A[vThread.y * 4 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 2))
  {
    v = Mean(vThread, 4, true);
    g_Level3[uint3(vGroup.xy * 2 + vThread, c)] = v;
    gs_B[vThread.y * 2 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread == 0))
    g_Level4[uint3(vGroup.xy, c)] = Mean(vThread, 2, false);
}

#elif defined(XR_OCEAN_MIPS)

Texture2DArray<float4> g_Level4In : register(t0);
RWTexture2DArray<float4> g_Level4 : register(u0);
RWTexture2DArray<float4> g_Level5 : register(u1);
RWTexture2DArray<float4> g_Level6 : register(u2);
RWTexture2DArray<float4> g_Level7 : register(u3);
RWTexture2DArray<float4> g_Level8 : register(u4);

groupshared float4 gs_A[16 * 16];
groupshared float4 gs_B[8 * 8];

float4 Mean(uint2 v, uint n, bool bA)
{
  const uint i = 2 * v.y * n + 2 * v.x;
  return bA ? 0.25 * (gs_A[i] + gs_A[i + 1] + gs_A[i + n] + gs_A[i + n + 1])
            : 0.25 * (gs_B[i] + gs_B[i + 1] + gs_B[i + n] + gs_B[i + n + 1]);
}

[numthreads(XR_OCEAN_LEVEL4, XR_OCEAN_LEVEL4, 1)]
void CSMain(uint3 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  const uint c = vGroup.z;
  float4 v = g_Level4In[uint3(vThread, c)];
  g_Level4[uint3(vThread, c)] = v;
  gs_A[vThread.y * 16 + vThread.x] = v;
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 8))
  {
    v = Mean(vThread, 16, true);
    g_Level5[uint3(vThread, c)] = v;
    gs_B[vThread.y * 8 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 4))
  {
    v = Mean(vThread, 8, false);
    g_Level6[uint3(vThread, c)] = v;
    gs_A[vThread.y * 4 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 2))
  {
    v = Mean(vThread, 4, true);
    g_Level7[uint3(vThread, c)] = v;
    gs_B[vThread.y * 2 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread == 0))
    g_Level8[uint3(0, 0, c)] = Mean(vThread, 2, false);
}

#endif
