// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: XeGtao.hlsl
//	Description: The passes of Intel's XeGTAO (third_party/xegtao) for
//	the ambient occlusion (AOXeGtao.cpp), with the bindings of the compute
//	root signature (D3D12Compute.h): b0 XeGTAO's GTAOConstants, s0 point
//	clamp. One pass per compile (Shaders.cmake), its entry CSMain:
//	  XR_GTAO_PREFILTER  the view space depth and its MIPs: t0 the depth
//	                     buffer (0 near, 1 far), u0..u4 the MIPs (R32_FLOAT);
//	                     8 x 8 threads per 16 x 16 pixels
//	  XR_GTAO_NORMALS    the view space normals from the depth buffer: t0
//	                     it, u0 the normals (R32_UINT, R11G11B10 packed)
//	  XR_GTAO_MAIN       the occlusion, XR_GTAO_SLICES slices of
//	                     XR_GTAO_STEPS steps (the quality): t0 the MIPs, t1
//	                     the normals; u0 the term (R8_UINT), u1 the edges
//	                     (R8_UNORM)
//	  XR_GTAO_DENOISE    a pass of the denoiser, XR_GTAO_LAST the last one
//	                     (into the output, the term's scale taken out): t0
//	                     the term, t1 the edges; u0 the term after it; two
//	                     pixels across per thread
//	Floats are 32-bit throughout (Far Cry sees farther than half floats
//	reach), the tuning comes from the constants, not from XeGTAO's
//	built-in defaults. The noise: XeGTAO's Hilbert curve index driving the
//	R2 sequence, moved by NoiseIndex every frame while an upscaler
//	gathers the frames (0 without one).
//
//////////////////////////////////////////////////////////////////////

#define XE_GTAO_FP32_DEPTHS
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0
#define XE_GTAO_USE_DEFAULT_CONSTANTS 0
#define VA_SATURATE saturate

#include "XeGTAO.h"

cbuffer cbGtao : register(b0)
{
  GTAOConstants g_Gtao;
};

#include "XeGTAO.hlsli"

SamplerState g_PointClamp : register(s0);

#if defined(XR_GTAO_PREFILTER)

Texture2D<float> g_Depth : register(t0);
RWTexture2D<float> g_Mip0 : register(u0);
RWTexture2D<float> g_Mip1 : register(u1);
RWTexture2D<float> g_Mip2 : register(u2);
RWTexture2D<float> g_Mip3 : register(u3);
RWTexture2D<float> g_Mip4 : register(u4);

[numthreads(8, 8, 1)]
void CSMain(uint2 vThread : SV_DispatchThreadID, uint2 vGroupThread : SV_GroupThreadID)
{
  XeGTAO_PrefilterDepths16x16(vThread, vGroupThread, g_Gtao, g_Depth, g_PointClamp, g_Mip0, g_Mip1, g_Mip2, g_Mip3, g_Mip4);
}

#elif defined(XR_GTAO_NORMALS)

Texture2D<float> g_Depth : register(t0);
RWTexture2D<uint> g_Normals : register(u0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSMain(uint2 vPixel : SV_DispatchThreadID)
{
  const float3 vNormal = XeGTAO_ComputeViewspaceNormal(vPixel, g_Gtao, g_Depth, g_PointClamp);
  g_Normals[vPixel] = XeGTAO_FLOAT3_to_R11G11B10_UNORM(saturate(vNormal * 0.5 + 0.5));
}

#elif defined(XR_GTAO_MAIN)

Texture2D<float> g_ViewDepth : register(t0);
Texture2D<uint> g_Normals : register(t1);
RWTexture2D<uint> g_Term : register(u0);
RWTexture2D<unorm float> g_Edges : register(u1);

//! The noise of a pixel in the frame NoiseIndex.
float2 Noise(uint2 vPixel)
{
  const uint nIndex = HilbertIndex(vPixel.x, vPixel.y) + 288 * (g_Gtao.NoiseIndex % 64);
  return frac(0.5 + nIndex * float2(0.75487766624669276005, 0.5698402909980532659114));
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSMain(uint2 vPixel : SV_DispatchThreadID)
{
  const float3 vNormal = normalize(XeGTAO_R11G11B10_UNORM_to_FLOAT3(g_Normals.Load(int3(vPixel, 0))) * 2 - 1);
  XeGTAO_MainPass(vPixel, XR_GTAO_SLICES, XR_GTAO_STEPS, Noise(vPixel), vNormal, g_Gtao, g_ViewDepth, g_PointClamp, g_Term, g_Edges);
}

#elif defined(XR_GTAO_DENOISE)

Texture2D<uint> g_Term : register(t0);
Texture2D<float> g_Edges : register(t1);
RWTexture2D<uint> g_Output : register(u0);

#ifdef XR_GTAO_LAST
static const bool s_bLast = true;
#else
static const bool s_bLast = false;
#endif

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSMain(uint2 vThread : SV_DispatchThreadID)
{
  XeGTAO_Denoise(vThread * uint2(2, 1), g_Gtao, g_Term, g_Edges, g_PointClamp, g_Output, s_bLast);
}

#endif
