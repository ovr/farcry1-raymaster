// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrShared.hlsli
//	Description: The maths of the HDR passes that both the program
//	(Hdr.hlsl) and the kernels (Meter.hlsl, Bloom.hlsl) do: the light of
//	the scene (through the volumetric light also in TAA's resolve), the
//	meter's first level (HDR_LUMINANCE), a level of the bloom
//	from its 13 taps (HDR_BLOOM_DOWN), the bin of the statistics' histogram
//	(HdrStats.hlsl). Without bindings.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_HDRSHARED_HLSLI
#define XRENDERD3D12_HDRSHARED_HLSLI

#include "Constants.h"
#include "HdrLight.hlsli"

static const float3 LUMINANCE = float3(0.2126, 0.7152, 0.0722);   //!< of linear light (Rec. 709)
static const float HALF_MAX = 65000;

//! A colour of the scene as light (HdrDecode): never negative, never more than a half float holds,
//! 0 for NaN.
float3 Decode(float3 c)
{
  c = min(max(c, 0), HALF_MAX);
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

float Luminance(float3 vLight)
{
  return dot(vLight, LUMINANCE);
}

//! HDR_VOLUMETRIC: the light vLight of the scene at vPixel through the volumetric light (Hdr.hlsl's Volumetric; TAA's resolve
//! with the scene's light): fDepth the depth buffer's there, p and q SetVolumetricConstants's mParams0[0] and mParams0[1].xy,
//! vToUV the froxels' uv of a pixel.
float3 VolumetricThrough(Texture3D Froxels, SamplerState Linear, float2 vPixel, float3 vLight, float fDepth, float4 p, float2 q, float2 vToUV)
{
  float d = q.x / (fDepth + p.w);
  // A froxel's integrated light is at its far end: half a slice after its centre.
  float w = saturate(log2(max(d, 1e-3) / p.x) * p.y) - p.z;
  float4 v = Froxels.SampleLevel(Linear, float3(vPixel * vToUV, w), 0);
  return q.y > 0 ? v.rgb : vLight * v.a + v.rgb;
}

float Max3(float3 v)
{
  return max(v.r, max(v.g, v.b));
}

//! The meter's first level: the uv of tap (x, y) of the n x n bilinear ones over its texel at vUV
//! (vTexel the texel's size in uv).
float2 MeterTapUV(float2 vUV, float2 vTexel, int x, int y, int n)
{
  return vUV + ((float2(x, y) + 0.5) / n - 0.5) * vTexel;
}

//! The meter's weight of the screen at vUV (vTexel: the size of a texel or pixel there in uv): the
//! middle of the screen counts most, by the distance from it in half heights.
float MeterWeight(float2 vUV, float2 vTexel)
{
  float2 d = (vUV - 0.5) * float2(vTexel.y / vTexel.x, 1) * 2;
  return exp(-0.75 * dot(d, d));
}

//! Its texel at vUV from the sum of its taps' light: (w * log2 L, w), L the mean luminance
//! (clamped to 2^-12 .. 2^8), w the weight of the texel (MeterWeight).
float4 MeterTexel(float3 vSum, int n, float2 vUV, float2 vTexel)
{
  float L = clamp(Luminance(vSum / (n * n)), exp2(-12.0), exp2(8.0));
  float w = MeterWeight(vUV, vTexel);
  return float4(w * log2(L), w, 0, 0);
}

//! The bin of the HDR statistics' histogram (HdrStats.hlsl) of a light: by its log2 from
//! XR_HDR_HIST_MIN to XR_HDR_HIST_MAX in XR_HDR_HIST_BINS steps, the light outside in the first or the
//! last bin (0 in the first).
uint HdrHistogramBin(float fLight)
{
  float x = (log2(max(fLight, 1e-30)) - XR_HDR_HIST_MIN) * (XR_HDR_HIST_BINS / (float)(XR_HDR_HIST_MAX - XR_HDR_HIST_MIN));
  return (uint)clamp(floor(x), 0, XR_HDR_HIST_BINS - 1);
}

//! A texel of the next level from the 13 taps of a 6 x 6 footprint of the level before around it,
//! in rows: a b c at -2, d e at -1, f g h at 0, i j at 1, k l m at 2 (in its texels; a f k and d i
//! at -2 and -1 across, b g l at 0, e j at 1, c h m at 2): a box of 4 x 4 and four of 2 x 2. With
//! bPrefilter the taps are the scene's light, exposed: each box weighted by 1 / (1 + its luminance)
//! against single bright pixels, then what is above fThreshold (soft over fKnee).
float3 BloomDownTaps(float3 a, float3 b, float3 c, float3 d, float3 e, float3 f, float3 g, float3 h, float3 i, float3 j, float3 k,
                     float3 l, float3 m, bool bPrefilter, float fThreshold, float fKnee)
{
  // The inner box counts half, the four outer ones an eighth each.
  float3 vBox[5] = { (d + e + i + j) / 4, (a + b + f + g) / 4, (b + c + g + h) / 4, (f + g + k + l) / 4, (g + h + l + m) / 4 };
  float fWeight[5] = { 0.5, 0.125, 0.125, 0.125, 0.125 };
  float3 vSum = 0;
  float fSum = 0;
  [unroll]
  for (int n = 0; n < 5; n++)
  {
    float w = fWeight[n] * (bPrefilter ? 1 / (1 + Luminance(vBox[n])) : 1);
    vSum += vBox[n] * w;
    fSum += w;
  }
  float3 vLight = vSum / fSum;
  if (bPrefilter)
  {
    // What is above the threshold, with a soft knee (quadratic) below it.
    fKnee = max(fKnee, 1e-4);
    float fBright = Max3(vLight);
    float fSoft = clamp(fBright - fThreshold + fKnee, 0, 2 * fKnee);
    fSoft = fSoft * fSoft / (4 * fKnee);
    vLight *= max(fSoft, fBright - fThreshold) / max(fBright, 1e-4);
  }
  return vLight;
}

//! Whether adding n to a word whose value was nWas (InterlockedAdd's original value) wrapped it around: the carry of a sum kept in
//! two words (XR_HDR_STAT_OUT_SUM and XR_HDR_STAT_OUT_SUM_HIGH; HdrOutNitsSum on the CPU).
bool HdrSumCarries(uint nWas, uint n)
{
  return nWas + n < nWas;
}

#endif
