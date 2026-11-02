// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Exposure.hlsl
//	Description: The automatic exposure's meter by a histogram
//	(d3d12_ExposureMeter 1, Hdr.cpp):
//	  CSHistogram: the scene's light (before the exposure) every
//	    vStride.x pixels, added up into the words of a buffer (Constants.h,
//	    XR_EXPOSURE_*): by log2 of its luminance with the meter's centre
//	    weight, and where the depth buffer is at the far plane (the sky) by
//	    log2 of its brightest channel; each group adds up its samples in
//	    its own words first.
//	  CSTarget: one group: the words read (and cleared for the next frame),
//	    the target EV (HdrExposure.hlsli) into a 1 x 1 target, which the
//	    adaptation (Hdr.hlsl, HDR_ADAPT) takes as the old meter's.
//
//	Bindings (D3D12Compute.h): b0 SExposureConstants, t0 the scene, t1 the
//	depth buffer (with vParams1.y); u0 the words (a raw buffer), u1 the
//	target (R32G32B32A32_FLOAT: x the EV, y the window's mean log2
//	luminance, z the sky's ceiling EV, w the sky's share).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "HdrShared.hlsli"
#include "HdrExposure.hlsli"

cbuffer cbExposure : register(b0)
{
  SExposureConstants g_Exposure;
};

Texture2D<float4> g_Scene : register(t0);
Texture2D<float4> g_Depth : register(t1);
RWByteAddressBuffer g_Words : register(u0);
RWTexture2D<float4> g_Target : register(u1);

#define GROUP 16                    //!< threads a side of a group, a sample each
#define THREADS (GROUP * GROUP)

groupshared uint gs_Words[XR_EXPOSURE_WORDS];

[numthreads(GROUP, GROUP, 1)]
void CSHistogram(uint2 vThread : SV_DispatchThreadID, uint nIndex : SV_GroupIndex)
{
  for (uint i = nIndex; i < XR_EXPOSURE_WORDS; i += THREADS)
    gs_Words[i] = 0;
  GroupMemoryBarrierWithGroupSync();
  const uint nStride = max(g_Exposure.vStride.x, 1);
  const int2 vPixel = (int2)(vThread * nStride + nStride / 2);
  if (all(vPixel < (int2)g_Exposure.vSize.xy))
  {
    const float3 vLight = Decode(g_Scene.Load(int3(vPixel, 0)).rgb);
    const uint nWeight = (uint)round(MeterWeight((vPixel + 0.5) * g_Exposure.vSize.zw, g_Exposure.vSize.zw) * XR_HDR_WEIGHT_ONE);
    InterlockedAdd(gs_Words[XR_EXPOSURE_LUMA + HdrHistogramBin(Luminance(vLight))], nWeight);
    InterlockedAdd(gs_Words[XR_EXPOSURE_SAMPLES], 1);
    if (g_Exposure.vParams1.y > 0 && g_Depth.Load(int3(vPixel, 0)).r >= 1)
      InterlockedAdd(gs_Words[XR_EXPOSURE_SKY + HdrHistogramBin(Max3(vLight))], 1);
  }
  GroupMemoryBarrierWithGroupSync();
  for (uint n = nIndex; n < XR_EXPOSURE_WORDS; n += THREADS)
  {
    if (gs_Words[n])
      g_Words.InterlockedAdd(n * 4, gs_Words[n]);
  }
}

groupshared uint gs_Luma[XR_HDR_HIST_BINS];
groupshared uint gs_Sky[XR_HDR_HIST_BINS];
groupshared uint gs_Samples;

[numthreads(XR_HDR_HIST_BINS, 1, 1)]
void CSTarget(uint i : SV_GroupIndex)
{
  gs_Luma[i] = g_Words.Load((XR_EXPOSURE_LUMA + i) * 4);
  gs_Sky[i] = g_Words.Load((XR_EXPOSURE_SKY + i) * 4);
  if (i == 0)
    gs_Samples = g_Words.Load(XR_EXPOSURE_SAMPLES * 4);
  GroupMemoryBarrierWithGroupSync();
  // Cleared for the next frame's histogram.
  g_Words.Store((XR_EXPOSURE_LUMA + i) * 4, 0);
  g_Words.Store((XR_EXPOSURE_SKY + i) * 4, 0);
  if (i != 0)
    return;
  g_Words.Store(XR_EXPOSURE_SAMPLES * 4, 0);
  uint vLuma[XR_HDR_HIST_BINS], vSky[XR_HDR_HIST_BINS];
  for (int n = 0; n < XR_HDR_HIST_BINS; n++)
  {
    vLuma[n] = gs_Luma[n];
    vSky[n] = gs_Sky[n];
  }
  g_Target[uint2(0, 0)] = ExposureTarget(vLuma, vSky, gs_Samples, g_Exposure.vParams, g_Exposure.vParams1.x,
                                         float2(XR_EXPOSURE_SKY_FROM, XR_EXPOSURE_SKY_FULL));
}
