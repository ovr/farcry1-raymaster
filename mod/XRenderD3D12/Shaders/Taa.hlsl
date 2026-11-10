// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Taa.hlsl
//	Description: TAA's kernels (d3d12_AA taa, UpscalerTaa.cpp; the method
//	is Taa.hlsli's), a thread a pixel of the display's size:
//	  CSResolve  t0 the frame's colours (LDR, or with TAA_HDR the HDR
//	             light), t1 the depth (R32_FLOAT), t2 the motion vectors
//	             (R16G16_FLOAT; the pixel's that of the nearest of its
//	             3 x 3, TaaDilate), t3 the history (the frame before's,
//	             R16G16B16A16_FLOAT: rgb the colour, a the frames /
//	             TAA_FRAMES); u0 the new history, u1 with TAA_OUTPUT the
//	             picture (its debug view with TAA_DEBUG), u2 with
//	             TAA_EXPOSURE the light's exposure (R32_FLOAT, 1 x 1; a UAV,
//	             the SRVs' four slots taken)
//	  CSSharpen  t0 the new history, t1 the exposure as CSResolve's u2
//	             (an SRV); u0 the picture,
//	             the history sharpened by AMD FidelityFX CAS (TaaCas.hlsli)
//	             with the constants vCas0, vCas1 of its sharpness; its
//	             group's pixels and their border loaded into groupshared
//	             once, in CAS's space too (the bits of loads of each)
//	  CSResolve with XR_TAA_LIGHT (d3d12_AASceneLight): the colours the
//	             scene's light it makes itself, as HDR_SCENE_LIGHT would
//	             (Hdr.hlsl's SceneLight, its half floats), once a pixel of
//	             its group's 3 x 3s; its inputs from the texture table
//	             (vInputs, vVolumeInputs), the UAVs as CSResolve's
//	Bindings (D3D12Compute.h): b0 STaaConstants, s1 linear clamp; with
//	XR_TAA_LIGHT the compute root signature with rays, b2 STaaConstants.
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

#if defined(XR_TAA_LIGHT)
#include "HdrShared.hlsli"

cbuffer cbTaa : register(b2)
{
  STaaConstants g_Taa;
};
#else
cbuffer cbTaa : register(b0)
{
  STaaConstants g_Taa;
};
#endif

SamplerState g_LinearClamp : register(s1);

#if defined(XR_TAA_LIGHT)
Texture2D g_Textures[] : register(t0, space1);
Texture3D g_Volumes[] : register(t0, space6);
RWTexture2D<float4> g_NewHistory : register(u0);
RWTexture2D<float4> g_Output : register(u1);
RWTexture2D<float> g_ExposureIn : register(u2);
#elif defined(XR_TAA_RESOLVE)
Texture2D<float4> g_Color : register(t0);
Texture2D<float> g_Depth : register(t1);
Texture2D<float2> g_Velocity : register(t2);
Texture2D<float4> g_History : register(t3);
RWTexture2D<float4> g_NewHistory : register(u0);
RWTexture2D<float4> g_Output : register(u1);
RWTexture2D<float> g_ExposureIn : register(u2);
#else
Texture2D<float4> g_Resolved : register(t0);
Texture2D<float> g_Exposure : register(t1);
RWTexture2D<float4> g_Output : register(u0);
#endif

int2 TaaClamp(int2 p)
{
  return clamp(p, int2(0, 0), (int2)g_Taa.vSize.xy - 1);
}

#if defined(XR_TAA_LIGHT)
//! The group's pixels' light and their 3 x 3s' border's, from the group's first pixel less one on.
#define TAA_TILE (XR_TAA_GROUP + 2)
groupshared float3 s_TaaLight[TAA_TILE][TAA_TILE];
static int2 s_vTaaTile;

//! The scene's light at pixel q (in the picture) as HDR_SCENE_LIGHT's target holds it: Hdr.hlsl's SceneLight in half floats.
float3 TaaSceneLight(int2 q)
{
  const float2 vPixel = (float2)q + 0.5;
  float3 vLight = Decode(g_Textures[g_Taa.vInputs.x].Load(int3(q, 0)).rgb);
  if (g_Taa.vFlags.x & TAA_VOLUMETRIC)
  {
    const float fDepth = g_Textures[g_Taa.vVolumeInputs.x].Load(int3(q, 0)).r;
    vLight = VolumetricThrough(g_Volumes[g_Taa.vVolumeInputs.y], g_LinearClamp, vPixel, vLight, fDepth, g_Taa.vVolume0, g_Taa.vVolume1.xy,
                               g_Taa.vVolume1.zw);
  }
  return f16tof32(f32tof16(vLight));
}

float TaaLoadDepth(int2 p) { return g_Textures[g_Taa.vInputs.y].Load(int3(TaaClamp(p), 0)).r; }
float2 TaaLoadVelocity(int2 p) { return g_Textures[g_Taa.vInputs.z].Load(int3(TaaClamp(p), 0)).rg; }
float3 TaaLoadColor(int2 p)
{
  const int2 t = p - s_vTaaTile;
  return s_TaaLight[t.y][t.x];
}
float2 TaaDilate(int2 p);
float2 TaaLoadMotion(int2 p) { return TaaDilate(p); }
float4 TaaSampleHistory(float2 uv) { return g_Textures[g_Taa.vInputs.w].SampleLevel(g_LinearClamp, uv, 0); }
#elif defined(XR_TAA_RESOLVE)
float TaaLoadDepth(int2 p) { return g_Depth.Load(int3(TaaClamp(p), 0)); }
float2 TaaLoadVelocity(int2 p) { return g_Velocity.Load(int3(TaaClamp(p), 0)); }
float3 TaaLoadColor(int2 p) { return g_Color.Load(int3(TaaClamp(p), 0)).rgb; }
// The motion read only at the resolve's own pixel: TaaDilate's there, no kernel of its own.
float2 TaaDilate(int2 p);
float2 TaaLoadMotion(int2 p) { return TaaDilate(p); }
float4 TaaSampleHistory(float2 uv) { return g_History.SampleLevel(g_LinearClamp, uv, 0); }
#else
float TaaLoadDepth(int2 p) { return 0; }
float2 TaaLoadVelocity(int2 p) { return 0; }
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
#if defined(XR_TAA_RESOLVE)
  return (g_Taa.vFlags.x & TAA_EXPOSURE) ? max(g_ExposureIn[int2(0, 0)], 1e-8) : g_Taa.vParams.z;
#else
  return (g_Taa.vFlags.x & TAA_EXPOSURE) ? max(g_Exposure.Load(int3(0, 0, 0)), 1e-8) : g_Taa.vParams.z;
#endif
}

#if !defined(XR_TAA_RESOLVE)
//! The exposure of the picture CAS loads (TaaExposure, once a thread).
static float s_fCasExposure;

//! The group's pixels of the picture and their 3 x 3s' border's (clamped into it), from the group's first pixel less one on,
//! and each in CAS's space (TaaToCas): loaded and taken there once, not by each of the 3 x 3s around it.
#define CAS_TILE (XR_TAA_GROUP + 2)
groupshared float3 s_CasPicture[CAS_TILE][CAS_TILE];
groupshared float3 s_CasSpace[CAS_TILE][CAS_TILE];
static int2 s_vCasTile;

float3 TaaCasPicture(int2 p)
{
  const int2 t = p - s_vCasTile;
  return s_CasPicture[t.y][t.x];
}

float3 TaaCasLoad(int2 p)
{
  const int2 t = p - s_vCasTile;
  return s_CasSpace[t.y][t.x];
}

float TaaCasExposure()
{
  return s_fCasExposure;
}

#define TAA_CAS_LOAD 1
#include "TaaCas.hlsli"
#endif

[numthreads(XR_TAA_GROUP, XR_TAA_GROUP, 1)]
void CSMain(uint2 vPixel : SV_DispatchThreadID, uint2 vThread : SV_GroupThreadID, uint2 vGroup : SV_GroupID)
{
#if defined(XR_TAA_LIGHT)
  // The tile's light, a pixel once (clamped into the picture as TaaLoadColor's loads are).
  s_vTaaTile = (int2)(vGroup * XR_TAA_GROUP) - 1;
  for (uint i = vThread.y * XR_TAA_GROUP + vThread.x; i < TAA_TILE * TAA_TILE; i += XR_TAA_GROUP * XR_TAA_GROUP)
  {
    const int2 t = int2(i % TAA_TILE, i / TAA_TILE);
    s_TaaLight[t.y][t.x] = TaaSceneLight(TaaClamp(s_vTaaTile + t));
  }
  GroupMemoryBarrierWithGroupSync();
#elif !defined(XR_TAA_RESOLVE)
  s_fCasExposure = TaaExposure();
  s_vCasTile = (int2)(vGroup * XR_TAA_GROUP) - 1;
  for (uint i = vThread.y * XR_TAA_GROUP + vThread.x; i < CAS_TILE * CAS_TILE; i += XR_TAA_GROUP * XR_TAA_GROUP)
  {
    const int2 t = int2(i % CAS_TILE, i / CAS_TILE);
    const float3 c = g_Resolved.Load(int3(TaaClamp(s_vCasTile + t), 0)).rgb;
    s_CasPicture[t.y][t.x] = c;
    s_CasSpace[t.y][t.x] = TaaToCas(c, s_fCasExposure);
  }
  GroupMemoryBarrierWithGroupSync();
#endif
  if (any(vPixel >= (uint2)g_Taa.vSize.xy))
    return;
  const int2 p = (int2)vPixel;
#if defined(XR_TAA_RESOLVE)
  const float fExposure = TaaExposure();
  const STaaPixel r = TaaResolve(p, g_Taa.vSize, fExposure, g_Taa.vParams.x, g_Taa.vParams.w, (g_Taa.vFlags.x & TAA_RESET) != 0);
  g_NewHistory[p] = float4(r.vColor, r.fFrames);
  if (g_Taa.vFlags.x & TAA_OUTPUT)
    g_Output[p] = float4((g_Taa.vFlags.x & TAA_DEBUG) ? TaaDebug(r, fExposure) : r.vColor, 1);
#else
  g_Output[p] = float4(TaaCas(p, g_Taa.vCas0, g_Taa.vCas1), 1);
#endif
}
