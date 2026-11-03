// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AntiAliasSmaa.hlsl
//	Description: The compute kernels of SMAA 1x's first two passes
//	(d3d12_AA smaa, PostAA.cpp; Smaa.hlsli), each compiled with the preset
//	of d3d12_AAQuality (SMAA_PRESET_LOW, _MEDIUM, _HIGH, _ULTRA,
//	Shaders.cmake):
//	  CSEdges    t0 the copy of the picture (D3D12_BACKBUFFER_FORMAT);
//	             u0 the edges (R8G8_UNORM), every pixel written
//	  CSWeights  t0 the edges, t1 the area texture (R8G8_UNORM),
//	             t2 the search texture (R8_UNORM); u0 the blending
//	             weights (R8G8B8A8_UNORM), every pixel written
//	b0: xy 1 / the picture's size, zw its size (SMAA_RT_METRICS).
//	Bindings: D3D12Compute.h (s0 point clamp, s1 linear clamp).
//
//////////////////////////////////////////////////////////////////////

cbuffer cbSmaa : register(b0)
{
  float4 g_SmaaMetrics;
};

Texture2D g_SmaaIn0 : register(t0);
Texture2D g_SmaaIn1 : register(t1);
Texture2D g_SmaaIn2 : register(t2);
SamplerState g_SmaaPoint : register(s0);
SamplerState g_SmaaLinear : register(s1);
RWTexture2D<float2> g_SmaaEdgesOut : register(u0);
RWTexture2D<float4> g_SmaaWeightsOut : register(u0);

#define SMAA_RT_METRICS g_SmaaMetrics
#define SMAA_LINEAR_SAMPLER g_SmaaLinear
#define SMAA_POINT_SAMPLER g_SmaaPoint
#include "Smaa.hlsli"

[numthreads(8, 8, 1)]
void CSEdges(uint2 vPixel : SV_DispatchThreadID)
{
  if (any(vPixel >= (uint2)g_SmaaMetrics.zw))
    return;
  g_SmaaEdgesOut[vPixel] = SmaaEdges(((float2)vPixel + 0.5) * g_SmaaMetrics.xy, g_SmaaIn0);
}

[numthreads(8, 8, 1)]
void CSWeights(uint2 vPixel : SV_DispatchThreadID)
{
  if (any(vPixel >= (uint2)g_SmaaMetrics.zw))
    return;
  g_SmaaWeightsOut[vPixel] = SmaaWeights(((float2)vPixel + 0.5) * g_SmaaMetrics.xy, g_SmaaIn0, g_SmaaIn1, g_SmaaIn2);
}
