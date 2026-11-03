// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Taa.hlsl
//	Description: TAA's kernels (d3d12_AA taa, UpscalerTaa.cpp; the method
//	is Taa.hlsli's), a thread a pixel of the display's size:
//	  CSDilate   t0 the depth (R32_FLOAT), t1 the motion vectors
//	             (R16G16_FLOAT); u0 each pixel's motion, the nearest of its
//	             3 x 3's (R16G16_FLOAT)
//	  CSResolve  t0 the frame's colours (LDR, or with TAA_HDR the HDR
//	             light), t1 CSDilate's motion, t2 the history (the frame
//	             before's, R16G16B16A16_FLOAT: rgb the colour, a the
//	             frames / TAA_FRAMES), t3 with TAA_EXPOSURE the light's
//	             exposure (R32_FLOAT, 1 x 1); u0 the new history, u1 with
//	             TAA_OUTPUT the picture (its debug view with TAA_DEBUG)
//	  CSSharpen  t0 the new history, t1 as CSResolve's t3; u0 the picture,
//	             the history sharpened by vParams.y (TaaSharpen)
//	Bindings (D3D12Compute.h): b0 STaaConstants, s1 linear clamp.
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbTaa : register(b0)
{
  STaaConstants g_Taa;
};

SamplerState g_LinearClamp : register(s1);

#if defined(XR_TAA_DILATE)
Texture2D<float> g_Depth : register(t0);
Texture2D<float2> g_Velocity : register(t1);
RWTexture2D<float2> g_Motion : register(u0);
#elif defined(XR_TAA_RESOLVE)
Texture2D<float4> g_Color : register(t0);
Texture2D<float2> g_MotionIn : register(t1);
Texture2D<float4> g_History : register(t2);
Texture2D<float> g_Exposure : register(t3);
RWTexture2D<float4> g_NewHistory : register(u0);
RWTexture2D<float4> g_Output : register(u1);
#else
Texture2D<float4> g_Resolved : register(t0);
Texture2D<float> g_Exposure : register(t1);
RWTexture2D<float4> g_Output : register(u0);
#endif

int2 TaaClamp(int2 p)
{
  return clamp(p, int2(0, 0), (int2)g_Taa.vSize.xy - 1);
}

#if defined(XR_TAA_DILATE)
float TaaLoadDepth(int2 p) { return g_Depth.Load(int3(TaaClamp(p), 0)); }
float2 TaaLoadVelocity(int2 p) { return g_Velocity.Load(int3(TaaClamp(p), 0)); }
#else
float TaaLoadDepth(int2 p) { return 0; }
float2 TaaLoadVelocity(int2 p) { return 0; }
#endif
#if defined(XR_TAA_RESOLVE)
float3 TaaLoadColor(int2 p) { return g_Color.Load(int3(TaaClamp(p), 0)).rgb; }
float2 TaaLoadMotion(int2 p) { return g_MotionIn.Load(int3(TaaClamp(p), 0)); }
float4 TaaSampleHistory(float2 uv) { return g_History.SampleLevel(g_LinearClamp, uv, 0); }
#else
float3 TaaLoadColor(int2 p) { return 0; }
float2 TaaLoadMotion(int2 p) { return 0; }
float4 TaaSampleHistory(float2 uv) { return 0; }
#endif

#include "Taa.hlsli"

//! The exposure TaaToWorking takes: 0 for LDR colours.
float TaaExposure()
{
  if (!(g_Taa.vFlags.x & TAA_HDR))
    return 0;
#if defined(XR_TAA_DILATE)
  return 0;
#else
  return (g_Taa.vFlags.x & TAA_EXPOSURE) ? max(g_Exposure.Load(int3(0, 0, 0)), 1e-8) : g_Taa.vParams.z;
#endif
}

[numthreads(XR_TAA_GROUP, XR_TAA_GROUP, 1)]
void CSMain(uint2 vPixel : SV_DispatchThreadID)
{
  if (any(vPixel >= (uint2)g_Taa.vSize.xy))
    return;
  const int2 p = (int2)vPixel;
#if defined(XR_TAA_DILATE)
  g_Motion[p] = TaaDilate(p);
#elif defined(XR_TAA_RESOLVE)
  const float fExposure = TaaExposure();
  const STaaPixel r = TaaResolve(p, g_Taa.vSize, fExposure, g_Taa.vParams.x, g_Taa.vParams.w, (g_Taa.vFlags.x & TAA_RESET) != 0);
  g_NewHistory[p] = float4(r.vColor, r.fFrames);
  if (g_Taa.vFlags.x & TAA_OUTPUT)
    g_Output[p] = float4((g_Taa.vFlags.x & TAA_DEBUG) ? TaaDebug(r, fExposure) : r.vColor, 1);
#else
  const float fExposure = TaaExposure();
  const float3 c = TaaToWorking(g_Resolved.Load(int3(p, 0)).rgb, fExposure);
  const float3 n = TaaToWorking(g_Resolved.Load(int3(TaaClamp(p + int2(0, -1)), 0)).rgb, fExposure);
  const float3 s = TaaToWorking(g_Resolved.Load(int3(TaaClamp(p + int2(0, 1)), 0)).rgb, fExposure);
  const float3 w = TaaToWorking(g_Resolved.Load(int3(TaaClamp(p + int2(-1, 0)), 0)).rgb, fExposure);
  const float3 e = TaaToWorking(g_Resolved.Load(int3(TaaClamp(p + int2(1, 0)), 0)).rgb, fExposure);
  g_Output[p] = float4(TaaFromWorking(TaaSharpen(c, n, s, w, e, g_Taa.vParams.y), fExposure), 1);
#endif
}
