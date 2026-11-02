// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FrameAverage.hlsl
//	Description: The HDR output's frame average limit
//	(d3d12_HDRFrameAverage, HdrOutput.cpp, HdrFrameAverage.hlsli), before
//	the output pass (Hdr.hlsl, HDR_OUTPUT) of the same frame:
//	  CSSum: every vStride.x pixels across and down, the brightest channel
//	    of the SDR colour's light and what the highlight layer adds to it
//	    (the output's light, as the output pass gives it, less the SDR
//	    colour's), in paper whites, added up into the words of a buffer
//	    (Constants.h, XR_FALL_*); each group adds up its samples first.
//	  CSLimit: one thread: the frame's averages from the words (cleared for
//	    the next frame), the factor of the highlights that keeps the frame
//	    at the limit, stepped from the last frame's (kept in the words) into
//	    a 1 x 1 target, which the output pass and the HDR statistics take.
//
//	Bindings (D3D12Compute.h): b0 SFrameAverageConstants, t0 the frame
//	target, t1 the highlight layer; u0 the words (a raw buffer), u1 the
//	target (R32G32B32A32_FLOAT: x the factor, y the SDR picture's average,
//	z what the highlights add, w the factor this frame alone asks for).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "HdrShared.hlsli"
#include "HdrFrameAverage.hlsli"

cbuffer cbFrameAverage : register(b0)
{
  SFrameAverageConstants g_Fall;
};

Texture2D<float4> g_Frame : register(t0);
Texture2D<float4> g_Layer : register(t1);
RWByteAddressBuffer g_Words : register(u0);
RWTexture2D<float4> g_Target : register(u1);

#define GROUP 16                    //!< threads a side of a group, a sample each

groupshared uint gs_Sum[3];         //!< the samples, the SDR picture's light, what the highlights add (x XR_FALL_UNIT)

[numthreads(GROUP, GROUP, 1)]
void CSSum(uint2 vThread : SV_DispatchThreadID, uint nIndex : SV_GroupIndex)
{
  if (nIndex < 3)
    gs_Sum[nIndex] = 0;
  GroupMemoryBarrierWithGroupSync();
  const uint nStride = max(g_Fall.vStride.x, 1);
  const int2 vPixel = (int2)(vThread * nStride + nStride / 2);
  if (all(vPixel < (int2)g_Fall.vSize.xy))
  {
    const float3 f = g_Frame.Load(int3(vPixel, 0)).rgb;
    bool bScene;
    const float3 vRatio = LayerRatio(f, g_Layer.Load(int3(vPixel, 0)), bScene);
    const float3 vLight = Decode(f);
    const float fSdr = Max3(vLight);
    const float fOut = Max3(min(vLight * vRatio, g_Fall.vParams.y / g_Fall.vParams.x));
    InterlockedAdd(gs_Sum[0], 1);
    InterlockedAdd(gs_Sum[1], (uint)round(saturate(fSdr) * XR_FALL_UNIT));
    InterlockedAdd(gs_Sum[2], (uint)round(clamp(fOut - fSdr, 0, XR_FALL_MOST) * XR_FALL_UNIT));
  }
  GroupMemoryBarrierWithGroupSync();
  if (nIndex == 0 && gs_Sum[0])
  {
    g_Words.InterlockedAdd(XR_FALL_SAMPLES * 4, gs_Sum[0]);
    g_Words.InterlockedAdd(XR_FALL_SDR * 4, gs_Sum[1]);
    g_Words.InterlockedAdd(XR_FALL_HIGHLIGHTS * 4, gs_Sum[2]);
  }
}

[numthreads(1, 1, 1)]
void CSLimit()
{
  const uint n = g_Words.Load(XR_FALL_SAMPLES * 4);
  const float fScale = n ? 1.0 / ((float)XR_FALL_UNIT * n) : 0;
  const float fSdr = g_Words.Load(XR_FALL_SDR * 4) * fScale, fHighlights = g_Words.Load(XR_FALL_HIGHLIGHTS * 4) * fScale;
  g_Words.Store(XR_FALL_SAMPLES * 4, 0);
  g_Words.Store(XR_FALL_SDR * 4, 0);
  g_Words.Store(XR_FALL_HIGHLIGHTS * 4, 0);
  const float fTarget = FrameAverageFactor(fSdr, fHighlights, g_Fall.vParams.z);
  const float fFactor = FrameAverageStep(asfloat(g_Words.Load(XR_FALL_LAST * 4)), fTarget, g_Fall.vSteps.x, g_Fall.vSteps.y,
                                         g_Fall.vParams.w > 0);
  g_Words.Store(XR_FALL_LAST * 4, asuint(fFactor));
  g_Target[uint2(0, 0)] = float4(fFactor, fSdr, fHighlights, fTarget);
}
