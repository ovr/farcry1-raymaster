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
//	  XR_OCEAN_CAUSTICS 32 x 32 texels a group (the group's z a caustics
//	                    cascade, XR_CAUSTICS_FIRST its waves'): the light of
//	                    the sun's rays refracted by its waves at four depths
//	                    (d3d12_WaterCaustics) into level 0, the means of 2 x 2
//	                    into levels 1 to 4 (2 x 2 a group; XR_CAUSTICS_MIPS)
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
//	  CAUSTICS t0 the waves (level 0); u0..u4 levels 0..4 of the caustics
//	           (RGBA16F, a slice a caustics cascade: the light at its depths,
//	           a texel's on the mean 1)
//
//	The caustics (the density of the refracted rays on a level floor, as
//	photons splatted from a height field): the sun's light a texel's area
//	of the surface lets in goes along its refracted ray; 4 x 4 rays a cell
//	between the texels' centres, each a 16th of the cell's light
//	(CausticsRay's: more on the faces turned to the sun), land at each depth
//	where the ray takes it, bilinearly over the four texels around (in
//	group shared memory, a fixed point of 2^-16 of a texel's light). The
//	tile wraps, so no light is lost: the mean is 1 at every depth, the
//	focus of a crest (convex) a bright line, the trough's spread dim.
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

#elif defined(XR_OCEAN_CAUSTICS)

#include "OceanFFT.hlsli"
Texture2DArray<float4> g_Waves : register(t0);
RWTexture2DArray<float4> g_Level0 : register(u0);
RWTexture2DArray<float4> g_Level1 : register(u1);
RWTexture2DArray<float4> g_Level2 : register(u2);
RWTexture2DArray<float4> g_Level3 : register(u3);
RWTexture2DArray<float4> g_Level4 : register(u4);

//! The group's texels a side (XR_CAUSTICS_TILE), a texel a thread (1024: 256 left the GPU half idle): the cells within the margin around them are a group's
//! too, so the larger the tile the fewer rays are cast more than once (32: 2.25 times as many cells as texels, 16: 4).
static const uint CAUSTICS_TILE = XR_CAUSTICS_TILE;
//! The cells (each between four texels' centres, a texel's area) whose rays may reach the group's texels, a side.
static const uint CAUSTICS_CELLS = CAUSTICS_TILE + 2 * XR_CAUSTICS_MARGIN;
//! The rays of a cell a side, at the middles of its n x n parts: where the waves spread the light, the rays land further
//! apart, and bilinearly over the texels a lattice of them n / s a texel (s the spread) leaves a ripple of about sinc^2(n /
//! s): 2 x 2 left 8.7 % where a wave spread them 1.5 times (Caustics_Wave), 4 x 4 1.1 %, none up to twice.
static const uint CAUSTICS_RAYS = 4;
//! A ray's light in the sums' fixed point: a cell's rays a texel's light on the mean, 2^16 (a level sea's exactly: the
//! bilinear weights at the parts' middles are multiples of 1 / (2 n)^2).
static const float CAUSTICS_RAY_UNIT = 65536.0 / (CAUSTICS_RAYS * CAUSTICS_RAYS);
//! The texels around the cells: their rays' moves at the last depth (in texels, half floats, two in a word; then the mip
//! levels 1 to 3, four floats a texel, from 0, 1024 and 1280) and the light they let in (half floats, two texels in a
//! word); the light the group's texels got at each depth (2^16 a texel's on the mean).
groupshared uint gs_Moves[(CAUSTICS_CELLS + 1) * (CAUSTICS_CELLS + 1)];
groupshared uint gs_Lit[((CAUSTICS_CELLS + 1) * (CAUSTICS_CELLS + 1) + 1) / 2];
groupshared uint gs_Light[4][CAUSTICS_TILE * CAUSTICS_TILE];

//! A ray's light fLight (in the fixed point) at depth k, at L in the group's texels (their centres at whole numbers),
//! over the four texels around it bilinearly; those outside the group's are another group's.
void CausticsAdd(uint k, float2 L, float fLight)
{
  const float2 f = floor(L), t = L - f;
  const int2 p = int2(f);
  const int n = CAUSTICS_TILE;
  if (any(p < -1) || any(p >= n))
    return;
  const float a = fLight * (1 - t.x), b = fLight * t.x;
  const bool bX0 = p.x >= 0, bX1 = p.x + 1 < n;
  const int i = p.y * n + p.x;
  if (p.y >= 0)
  {
    if (bX0)
      InterlockedAdd(gs_Light[k][i], (uint)round(a * (1 - t.y)));
    if (bX1)
      InterlockedAdd(gs_Light[k][i + 1], (uint)round(b * (1 - t.y)));
  }
  if (p.y + 1 < n)
  {
    if (bX0)
      InterlockedAdd(gs_Light[k][i + n], (uint)round(a * t.y));
    if (bX1)
      InterlockedAdd(gs_Light[k][i + n + 1], (uint)round(b * t.y));
  }
}

//! x as a half float, the nearer of the two around it (a GPU's f32tof16 and its stores of half floats may round towards
//! 0: the light's mean was 4e-4 low), and v as the half floats it stores.
uint CausticsHalf(float x)
{
  const uint h = f32tof16(x);
  return abs(f16tof32(h + 1) - x) < abs(f16tof32(h) - x) ? h + 1 : h;
}

float4 CausticsRound(float4 v)
{
  return float4(f16tof32(CausticsHalf(v.x)), f16tof32(CausticsHalf(v.y)), f16tof32(CausticsHalf(v.z)), f16tof32(CausticsHalf(v.w)));
}

//! The ray's move of texel i around the cells and the light it lets in.
float2 CausticsMove(uint i)
{
  const uint u = gs_Moves[i];
  return float2(f16tof32(u), f16tof32(u >> 16));
}

float CausticsLit(uint i)
{
  return f16tof32(gs_Lit[i >> 1] >> (16 * (i & 1)));
}

//! Level 0's texel at p of the group's (its depths' light).
float4 CausticsLight(uint2 p)
{
  const uint i = p.y * CAUSTICS_TILE + p.x;
  return float4(gs_Light[0][i], gs_Light[1][i], gs_Light[2][i], gs_Light[3][i]) / 65536;
}

//! A mip level's texel i in gs_Moves, and its store.
float4 CausticsLevel(uint i)
{
  return asfloat(uint4(gs_Moves[4 * i], gs_Moves[4 * i + 1], gs_Moves[4 * i + 2], gs_Moves[4 * i + 3]));
}

void CausticsStore(uint i, float4 v)
{
  [unroll]
  for (uint k = 0; k < 4; k++)
    gs_Moves[4 * i + k] = asuint(v[k]);
}

//! The mean of the 2 x 2 texels of a level of n x n (from nFrom in gs_Moves) at the thread's place in the next.
float4 CausticsMean(uint2 v, uint n, uint nFrom)
{
  const uint i = nFrom + 2 * v.y * n + 2 * v.x;
  return 0.25 * (CausticsLevel(i) + CausticsLevel(i + 1) + CausticsLevel(i + n) + CausticsLevel(i + n + 1));
}

[numthreads(CAUSTICS_TILE, CAUSTICS_TILE, 1)]
void CSMain(uint3 vGroup : SV_GroupID, uint2 vThread : SV_GroupThreadID)
{
  // Caustics cascade c of wave cascade w: each texel's refracted ray's move at the last depth less the level surface's
  // (the floor's program takes the pattern where the sun's ray crossed the surface) and the light it lets in, once (not
  // a ray's: its move is a smooth function of the slopes); 4 x 4 rays a cell, their moves and light bilinear between its
  // texels', from the cells within the margin of the group's texels, each moved at each depth by its share of the move,
  // its light into the texels around where it lands.
  const uint T = CAUSTICS_TILE * CAUSTICS_TILE;
  const uint c = vGroup.z, w = c + XR_CAUSTICS_FIRST, nThread = vThread.y * CAUSTICS_TILE + vThread.x, R = CAUSTICS_CELLS + 1;
  const uint m = XR_OCEAN_SIZE - 1;
  const int2 vOrigin = int2(vGroup.xy * CAUSTICS_TILE) - XR_CAUSTICS_MARGIN;
  const float3 vSun = g_Ocean.vSun.xyz;
  float fLevelLight;
  const float2 vLevel = CausticsRay(0, vSun, fLevelLight);
  const float4 vTexels = g_Ocean.vCausticsDepths[c] / g_Ocean.vCascades[w].z;   // a ray's move in texels per unit of it
  for (uint i2 = nThread; 2 * i2 < R * R; i2 += T)
  {
    uint nLit = 0;
    [unroll]
    for (uint h = 0; h < 2; h++)
    {
      const uint i = 2 * i2 + h;
      if (i < R * R)
      {
        float fLight;
        const float2 s = g_Waves[uint3(uint2(vOrigin + int2(i % R, i / R)) & m, w)].xy * g_Ocean.vSun.w;
        const float2 vMove = (CausticsRay(s, vSun, fLight) - vLevel) * vTexels.w;
        gs_Moves[i] = CausticsHalf(vMove.x) | (CausticsHalf(vMove.y) << 16);
        nLit |= CausticsHalf(fLight) << (16 * h);
      }
    }
    gs_Lit[i2] = nLit;
  }
  [unroll]
  for (uint k0 = 0; k0 < 4; k0++)
    gs_Light[k0][nThread] = 0;
  GroupMemoryBarrierWithGroupSync();
  const float4 vScale = vTexels / vTexels.w;
  for (uint s = nThread; s < CAUSTICS_CELLS * CAUSTICS_CELLS; s += T)
  {
    const uint2 q = uint2(s % CAUSTICS_CELLS, s / CAUSTICS_CELLS);
    const uint i00 = q.y * R + q.x, i10 = i00 + 1, i01 = i00 + R, i11 = i01 + 1;
    const float2 a = CausticsMove(i00), b = CausticsMove(i10), d = CausticsMove(i01), e = CausticsMove(i11);
    // A cell of the margin whose rays land outside the group's texels at every depth (the moves at most the last's): none.
    const float fReach = min(max(max(length(a), length(b)), max(length(d), length(e))), XR_CAUSTICS_MARGIN - 1) + 1;
    const float2 vCell = float2(q) - XR_CAUSTICS_MARGIN;
    if (any(vCell + 1 + fReach < 0) || any(vCell - fReach > CAUSTICS_TILE))
      continue;
    const float4 L = float4(CausticsLit(i00), CausticsLit(i10), CausticsLit(i01), CausticsLit(i11)) * CAUSTICS_RAY_UNIT;
    [loop]
    for (uint j = 0; j < CAUSTICS_RAYS * CAUSTICS_RAYS; j++)
    {
      const float2 t = (float2(j % CAUSTICS_RAYS, j / CAUSTICS_RAYS) + 0.5) / CAUSTICS_RAYS;
      const float2 vMove = lerp(lerp(a, b, t.x), lerp(d, e, t.x), t.y);
      const float fLight = lerp(lerp(L.x, L.y, t.x), lerp(L.z, L.w, t.x), t.y);
      const float2 vAt = float2(q) + t - XR_CAUSTICS_MARGIN;
      [unroll]
      for (uint k = 0; k < 4; k++)
        CausticsAdd(k, vAt + clamp(vMove * vScale[k], -(XR_CAUSTICS_MARGIN - 1), XR_CAUSTICS_MARGIN - 1), fLight);
    }
  }
  GroupMemoryBarrierWithGroupSync();
  // The texels into level 0; the means of 2 x 2 into levels 1 to 4 (through gs_Moves; 2 x 2 a group at the last).
  g_Level0[uint3(vGroup.xy * CAUSTICS_TILE + vThread, c)] = CausticsRound(CausticsLight(vThread));
  float4 v;
  if (all(vThread < 16))
  {
    const uint2 p = vThread * 2;
    v = 0.25 * (CausticsLight(p) + CausticsLight(p + uint2(1, 0)) + CausticsLight(p + uint2(0, 1)) + CausticsLight(p + 1));
    g_Level1[uint3(vGroup.xy * 16 + vThread, c)] = CausticsRound(v);
    CausticsStore(vThread.y * 16 + vThread.x, v);
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 8))
  {
    v = CausticsMean(vThread, 16, 0);
    g_Level2[uint3(vGroup.xy * 8 + vThread, c)] = CausticsRound(v);
    CausticsStore(256 + vThread.y * 8 + vThread.x, v);
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 4))
  {
    v = CausticsMean(vThread, 8, 256);
    g_Level3[uint3(vGroup.xy * 4 + vThread, c)] = CausticsRound(v);
    CausticsStore(320 + vThread.y * 4 + vThread.x, v);
  }
  GroupMemoryBarrierWithGroupSync();
  if (all(vThread < 2))
    g_Level4[uint3(vGroup.xy * 2 + vThread, c)] = CausticsRound(CausticsMean(vThread, 4, 320));
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
