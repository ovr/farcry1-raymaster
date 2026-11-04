// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Wake.hlsl
//	Description: The wakes' kernels (Wake.cpp's UpdateWakes; Wake.hlsli's
//	math): the waves of the bodies moving through the ocean's surface in a
//	window of XR_WAKE_SIZE x XR_WAKE_SIZE cells around the camera.
//	A step:
//	  XR_WAKE_ROWS      a group a row: the window's texels (the cells new to
//	                    it cleared), the bodies' displacement added to the
//	                    height, the sponge at the edge, the FFT along x
//	  XR_WAKE_COLUMNS   a group a column: the FFT along y, each wave number
//	                    turned and damped over the step, the inverse FFT
//	                    along y
//	  XR_WAKE_ROWS_INVERSE a group a row: the inverse FFT along x, the
//	                    window's texels
//	then once a frame
//	  XR_WAKE_VIEW      16 x 16 texels a group: the slopes of the height, the
//	                    foam (the frame before's fading and spreading, the
//	                    bodies' and the breaking waves' new), the view the
//	                    water reads into level 0 and the means of 2 x 2 into
//	                    levels 1 to 3
//	  XR_WAKE_HEIGHT    with the ocean's grid (d3d12_WaterGeometry): 16 x 16
//	                    texels a group, the height into level 0 of the
//	                    heights the grid reads, the means into levels 1 to 3
//
//	Bindings (D3D12Compute.h): b0 SWakeConstants;
//	  ROWS          t0 the window (RG32F: the header's q); u0 the rows
//	                (RG32F)
//	  COLUMNS       t0 the rows; u0 the columns (RG32F)
//	  ROWS_INVERSE  t0 the columns; u0 the window
//	  VIEW          t0 the window, t1 the foam of the frame before (R32F, the
//	                other of two taking turns); u0 the foam, u1..u4 levels
//	                0..3 of the view (RGBA16F: the slopes dh/dx, dh/dy, their
//	                square, the foam's share white)
//	  HEIGHT        t0 the window; u0..u3 levels 0..3 of the heights (R16F, m)
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbWake : register(b0)
{
  SWakeConstants g_Wake;
};

#include "Wake.hlsli"

#if defined(XR_WAKE_ROWS) || defined(XR_WAKE_COLUMNS) || defined(XR_WAKE_ROWS_INVERSE)

Texture2D<float2> g_In : register(t0);
RWTexture2D<float2> g_Out : register(u0);

//! Whether texel p's cell was not in the window of the step before (the cells the window came to as it moved).
bool WakeNew(uint2 p)
{
  const int2 c = int2(g_Wake.vWindow.xy), b = int2(g_Wake.vWindow.zw);
  return g_Wake.nCounts.y != 0 && (WakeCell(p.x, c.x) != WakeCell(p.x, b.x) || WakeCell(p.y, c.y) != WakeCell(p.y, b.y));
}

[numthreads(XR_WAKE_SIZE / 2, 1, 1)]
void CSMain(uint j : SV_GroupThreadID, uint3 vGroup : SV_GroupID)
{
  // vGroup.y the row (the column); thread j the elements j and j + N / 2.
  const uint r = vGroup.y;
  [unroll]
  for (uint e = 0; e < 2; e++)
  {
    const uint i = j + e * (WAKE_N / 2);
#ifdef XR_WAKE_COLUMNS
    const uint2 p = uint2(r, i);
#else
    const uint2 p = uint2(i, r);
#endif
    float2 q = g_In[p];
#ifdef XR_WAKE_ROWS
    if (WakeNew(p))
      q = 0;
    // The bodies' displacement: the depth each frees where it was, less the depth it takes where it is.
    const int2 c = int2(g_Wake.vWindow.xy);
    const float2 X = float2(WakeOffset(p.x, c.x), WakeOffset(p.y, c.y));
    for (uint n = 0; n < g_Wake.nCounts.x; n++)
    {
      const float4 vBefore = g_Wake.vBodies[4 * n], vShapeBefore = g_Wake.vBodies[4 * n + 1];
      const float4 vAfter = g_Wake.vBodies[4 * n + 2], vShapeAfter = g_Wake.vBodies[4 * n + 3];
      if (min(length(X - vBefore.xy), length(X - vAfter.xy)) < vShapeBefore.w)
        q.x += WakeDepth(X, vBefore, vShapeBefore) - WakeDepth(X, vAfter, vShapeAfter);
    }
    q *= WakeSponge(WakeEdgeCells(p, c), g_Wake.vSponge.x, g_Wake.vStep.w);
#endif
    gs_Wake[0][i] = q;
  }
  GroupMemoryBarrierWithGroupSync();
#ifdef XR_WAKE_ROWS
  const uint nSrc = WakeFFT(j, -1, 0);
#elif defined(XR_WAKE_COLUMNS)
  uint nSrc = WakeFFT(j, -1, 0);
  [unroll]
  for (uint e1 = 0; e1 < 2; e1++)
  {
    const uint i = j + e1 * (WAKE_N / 2);
    gs_Wake[nSrc][i] = WakeMul(gs_Wake[nSrc][i], WakeTurn(WakeWaveVector(uint2(r, i)), g_Wake.vStep.x, g_Wake.vStep.y, g_Wake.vStep.z));
  }
  GroupMemoryBarrierWithGroupSync();
  nSrc = WakeFFT(j, 1, nSrc);
#else
  const uint nSrc = WakeFFT(j, 1, 0);
#endif
  [unroll]
  for (uint e2 = 0; e2 < 2; e2++)
  {
    const uint i = j + e2 * (WAKE_N / 2);
#ifdef XR_WAKE_COLUMNS
    g_Out[uint2(r, i)] = gs_Wake[nSrc][i];
#elif defined(XR_WAKE_ROWS)
    g_Out[uint2(i, r)] = gs_Wake[nSrc][i];
#else
    // The forward and the inverse FFTs along both axes: N^2 times the window.
    g_Out[uint2(i, r)] = gs_Wake[nSrc][i] * (1.0 / (WAKE_N * WAKE_N));
#endif
  }
}

#elif defined(XR_WAKE_VIEW)

Texture2D<float2> g_State : register(t0);
Texture2D<float> g_FoamBefore : register(t1);
RWTexture2D<float> g_Foam : register(u0);
RWTexture2D<float4> g_Level0 : register(u1);
RWTexture2D<float4> g_Level1 : register(u2);
RWTexture2D<float4> g_Level2 : register(u3);
RWTexture2D<float4> g_Level3 : register(u4);

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
  const uint2 p = vGroup.xy * 16 + vThread;
  const uint m = WAKE_N - 1;
  // The slopes by central differences (the window wraps; at its edge the sponge left no waves).
  const float2 s = float2(g_State[uint2((p.x + 1) & m, p.y)].x - g_State[uint2((p.x - 1) & m, p.y)].x,
                          g_State[uint2(p.x, (p.y + 1) & m)].x - g_State[uint2(p.x, (p.y - 1) & m)].x) * (0.5 / XR_WAKE_CELL);

  // The foam: none in the cells new to the window and in its two outer cells (the texels there take turns with the far
  // side's); the frame before's fading and spreading, the bodies' (moving faster than WAKE_FOAM_SPEED, where they are at the
  // frame's end) and the breaking waves' new (but under a body).
  const int2 c = int2(g_Wake.vWindow.xy), b = int2(g_Wake.vWindow.zw);
  const bool bNew = g_Wake.nCounts.y != 0 && (WakeCell(p.x, c.x) != WakeCell(p.x, b.x) || WakeCell(p.y, c.y) != WakeCell(p.y, b.y));
  float fBefore = 0, fAround = 0;
  if (!bNew && g_Wake.vSponge.y > 0)
  {
    fBefore = g_FoamBefore[p];
    fAround = g_FoamBefore[uint2((p.x + 1) & m, p.y)] + g_FoamBefore[uint2((p.x - 1) & m, p.y)] + g_FoamBefore[uint2(p.x, (p.y + 1) & m)] +
              g_FoamBefore[uint2(p.x, (p.y - 1) & m)];
  }
  const float2 X = float2(WakeOffset(p.x, c.x), WakeOffset(p.y, c.y));
  float fBreaking = saturate((length(s) - g_Wake.vBreak.x) * g_Wake.vBreak.y) * g_Wake.vBreak.z, fMade = 0;
  for (uint n = 0; n < g_Wake.nCounts.x; n++)
  {
    const float4 vAfter = g_Wake.vBodies[4 * n + 2], vShapeAfter = g_Wake.vBodies[4 * n + 3];
    if (length(X - vAfter.xy) < g_Wake.vBodies[4 * n + 1].w)
    {
      // Under a body the slopes are its own hollow's sides, no wave breaking.
      const float fShape = WakeShape(X, vAfter, vShapeAfter);
      if (fShape > 0)
        fBreaking = 0;
      fMade = max(fMade, saturate(vShapeAfter.w * g_Wake.vSponge.w) * g_Wake.vBreak.z * fShape);
    }
  }
  float fFoam = WakeFoam(fBefore, fAround, g_Wake.vSponge.y, g_Wake.vSponge.z, max(fMade, fBreaking));
  if (WakeEdgeCells(p, c) < 2)
    fFoam = 0;
  g_Foam[p] = fFoam;

  float4 v = float4(s, dot(s, s), fFoam);
  g_Level0[p] = v;
  gs_A[vThread.y * 16 + vThread.x] = v;
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 8))
  {
    v = Mean(vThread, 16, true);
    g_Level1[vGroup.xy * 8 + vThread] = v;
    gs_B[vThread.y * 8 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 4))
  {
    v = Mean(vThread, 8, false);
    g_Level2[vGroup.xy * 4 + vThread] = v;
    gs_A[vThread.y * 4 + vThread.x] = v;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 2))
    g_Level3[vGroup.xy * 2 + vThread] = Mean(vThread, 4, true);
}

#elif defined(XR_WAKE_HEIGHT)

// The waves' heights for the ocean's grid (d3d12_WaterGeometry; Water.hlsl's WakeGridHeight): the window's, each mip level
// the mean of the one before's 2 x 2 texels.
Texture2D<float2> g_State : register(t0);
RWTexture2D<float> g_Level0 : register(u0);
RWTexture2D<float> g_Level1 : register(u1);
RWTexture2D<float> g_Level2 : register(u2);
RWTexture2D<float> g_Level3 : register(u3);

groupshared float gs_H[16 * 16];
groupshared float gs_G[8 * 8];

//! The mean of the 2 x 2 texels of a level of n x n at the thread's place in the next.
float MeanHeight(uint2 v, uint n, bool bH)
{
  const uint i = 2 * v.y * n + 2 * v.x;
  return bH ? 0.25 * (gs_H[i] + gs_H[i + 1] + gs_H[i + n] + gs_H[i + n + 1]) : 0.25 * (gs_G[i] + gs_G[i + 1] + gs_G[i + n] + gs_G[i + n + 1]);
}

[numthreads(16, 16, 1)]
void CSMain(uint3 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  const uint2 p = vGroup.xy * 16 + vThread;
  float h = g_State[p].x;
  g_Level0[p] = h;
  gs_H[vThread.y * 16 + vThread.x] = h;
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 8))
  {
    h = MeanHeight(vThread, 16, true);
    g_Level1[vGroup.xy * 8 + vThread] = h;
    gs_G[vThread.y * 8 + vThread.x] = h;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 4))
  {
    h = MeanHeight(vThread, 8, false);
    g_Level2[vGroup.xy * 4 + vThread] = h;
    gs_H[vThread.y * 4 + vThread.x] = h;
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 2))
    g_Level3[vGroup.xy * 2 + vThread] = MeanHeight(vThread, 4, true);
}

#endif
