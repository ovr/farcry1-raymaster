// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrStats.hlsl
//	Description: The HDR statistics (HdrStats.cpp: d3d12_DisplayHDR,
//	d3d12_HDRLog), added up into the words of a buffer (Constants.h,
//	XR_HDR_STAT_*), a sample every XR_HDR_STRIDE pixels across and down;
//	each group adds up its samples in its own words first.
//	  CSHistogram: the scene's light (before the exposure, as the meter
//	    takes it) by log2 of its luminance (as it is and by the meter's
//	    centre weight) and of its brightest channel; exposed, how much of
//	    it is in the tone map's shoulder and at the game's white or above;
//	    the EV and the meter's mean of the frame.
//	  CSOutput: the HDR output's nits, as the output pass (Hdr.hlsl,
//	    HDR_OUTPUT) gives them before PQ: of the brightest channel their
//	    sum, the most, how many are above paper white and at the peak.
//
//	Bindings (D3D12Compute.h): b0 SHdrStatsConstants; CSHistogram: t0 the
//	scene, t1 the adapted EV (1 x 1), t2 the meter's last level (1 x 1);
//	CSOutput: t0 the frame target, t1 the highlight layer; u0 the words
//	(a raw buffer).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "HdrShared.hlsli"

cbuffer cbStats : register(b0)
{
  SHdrStatsConstants g_Stats;
};

Texture2D<float4> g_Source : register(t0);
Texture2D<float4> g_Source1 : register(t1);
Texture2D<float4> g_Source2 : register(t2);
RWByteAddressBuffer g_Words : register(u0);

#define GROUP 16                    //!< threads a side of a group, a sample each
#define THREADS (GROUP * GROUP)

groupshared uint gs_Words[XR_HDR_STAT_SCENE_WORDS];

//! The thread's pixel; false outside the target.
bool SamplePixel(uint2 vThread, out int2 vPixel)
{
  vPixel = (int2)(vThread * XR_HDR_STRIDE);
  return all(vPixel < (int2)g_Stats.vSize.xy);
}

[numthreads(GROUP, GROUP, 1)]
void CSHistogram(uint2 vThread : SV_DispatchThreadID, uint2 vGroup : SV_GroupID, uint nIndex : SV_GroupIndex)
{
  for (uint i = nIndex; i < XR_HDR_STAT_SCENE_WORDS; i += THREADS)
    gs_Words[i] = 0;
  GroupMemoryBarrierWithGroupSync();
  const float fEV = g_Stats.vParams.x + (g_Stats.vParams.y > 0 ? g_Source1.Load(int3(0, 0, 0)).r : 0);
  int2 vPixel;
  if (SamplePixel(vThread, vPixel))
  {
    const float3 vLight = Decode(g_Source.Load(int3(vPixel, 0)).rgb);
    const float L = Luminance(vLight), M = Max3(vLight);
    const uint nBin = HdrHistogramBin(L);
    const uint nWeight = (uint)round(MeterWeight((vPixel + 0.5) * g_Stats.vSize.zw, g_Stats.vSize.zw) * XR_HDR_WEIGHT_ONE);
    InterlockedAdd(gs_Words[XR_HDR_STAT_LUMA + nBin], 1);
    InterlockedAdd(gs_Words[XR_HDR_STAT_LUMA_W + nBin], nWeight);
    InterlockedAdd(gs_Words[XR_HDR_STAT_MAX + HdrHistogramBin(M)], 1);
    InterlockedAdd(gs_Words[XR_HDR_STAT_SAMPLES], 1);
    InterlockedAdd(gs_Words[XR_HDR_STAT_WEIGHT], nWeight);
    const float fExposed = M * exp2(fEV);
    if (fExposed > g_Stats.vParams.z)
      InterlockedAdd(gs_Words[XR_HDR_STAT_SHOULDER], 1);
    if (fExposed >= 1)
      InterlockedAdd(gs_Words[XR_HDR_STAT_WHITE], 1);
  }
  GroupMemoryBarrierWithGroupSync();
  for (uint n = nIndex; n < XR_HDR_STAT_SCENE_WORDS; n += THREADS)
  {
    if (gs_Words[n])
      g_Words.InterlockedAdd(n * 4, gs_Words[n]);
  }
  if (nIndex == 0 && all(vGroup == 0))
  {
    float fMeanLog = 0;
    if (g_Stats.vParams.w > 0)
    {
      const float2 vMeter = g_Source2.Load(int3(0, 0, 0)).rg;
      fMeanLog = vMeter.y > 0 ? vMeter.x / vMeter.y : 0;
    }
    g_Words.Store(XR_HDR_STAT_EV * 4, asuint(fEV));
    g_Words.Store(XR_HDR_STAT_METER * 4, asuint(fMeanLog));
  }
}

groupshared uint gs_Out[5];         //!< the samples, their nits (x 1/4), the most (asuint), above paper white, at the peak

[numthreads(GROUP, GROUP, 1)]
void CSOutput(uint2 vThread : SV_DispatchThreadID, uint nIndex : SV_GroupIndex)
{
  if (nIndex < 5)
    gs_Out[nIndex] = 0;
  GroupMemoryBarrierWithGroupSync();
  int2 vPixel;
  if (SamplePixel(vThread, vPixel))
  {
    const float fPaper = g_Stats.vParams.x, fPeak = g_Stats.vParams.y;
    const float3 f = g_Source.Load(int3(vPixel, 0)).rgb;
    bool bScene;
    const float3 vRatio = LayerRatio(f, g_Source1.Load(int3(vPixel, 0)), bScene);
    const float fNits = Max3(min(Decode(f) * vRatio * fPaper, fPeak));
    InterlockedAdd(gs_Out[0], 1);
    InterlockedAdd(gs_Out[1], (uint)round(fNits * 4));
    InterlockedMax(gs_Out[2], asuint(fNits));
    if (fNits > fPaper * 1.02)
      InterlockedAdd(gs_Out[3], 1);
    if (fNits >= fPeak * 0.95)
      InterlockedAdd(gs_Out[4], 1);
  }
  GroupMemoryBarrierWithGroupSync();
  if (nIndex == 0 && gs_Out[0])
  {
    // A group's sum in XR_HDR_NITS_UNIT: at most 256 samples of 10000 nits, so that the buffer's
    // word holds a frame's.
    g_Words.InterlockedAdd(XR_HDR_STAT_OUT_PIXELS * 4, gs_Out[0]);
    g_Words.InterlockedAdd(XR_HDR_STAT_OUT_SUM * 4, (uint)round(gs_Out[1] / (4.0 * XR_HDR_NITS_UNIT)));
    g_Words.InterlockedMax(XR_HDR_STAT_OUT_MAX * 4, gs_Out[2]);
    g_Words.InterlockedAdd(XR_HDR_STAT_OUT_PAPER * 4, gs_Out[3]);
    g_Words.InterlockedAdd(XR_HDR_STAT_OUT_PEAK * 4, gs_Out[4]);
  }
}
