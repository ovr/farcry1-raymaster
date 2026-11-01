// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathNrd.hlsl
//	Description: The path tracer's kernels around NVIDIA NRD
//	(d3d12_PTDenoiser nrd, PathTrace.cpp, NrdHost.cpp), compiled only
//	when NRD is built in: its NRD.hlsli (third_party/nrd) packs what
//	REBLUR_DIFFUSE takes and unpacks what it gives. Shader Model 6.5, the
//	pass's constants of PathTrace.hlsl (SPathTraceConstants), a thread for
//	each pixel of the depth buffer and the G-buffer.
//	  CSPack: from CSTrace's paths (nTextures.y: rgb the light, w the view
//	    depth, 0 where the path tracer lights no surface), the hit
//	    distance of their first bounce (nTextures3.w) and the G-buffer's
//	    normal: u0 IN_DIFF_RADIANCE_HITDIST (the light, its hit distance
//	    normalized by vNrd.xyz), u1 IN_NORMAL_ROUGHNESS (the normal on the
//	    camera's side, roughness 1: the light is diffuse), u2 IN_VIEWZ
//	    (the view depth; vNrd.w where there is no surface, beyond NRD's
//	    denoising range), u3 IN_MV (the uv where the pixel's point was in
//	    the frame before, less its own: the scene's motion vectors,
//	    nTextures3.z, which have the characters' and the objects' own
//	    motion; without them the camera's alone, from mPrevViewProj).
//	  CSResolve: NRD's OUT_DIFF_RADIANCE_HITDIST (nTextures3.y) as a
//	    history of CSTemporal's (u0: rgb the light, w the view depth), which
//	    CSFilter takes as it is.
//
//////////////////////////////////////////////////////////////////////

#include "PathShade.hlsli"
#include "PathGBuffer.hlsli"
#include "NRD.hlsli"

ConstantBuffer<SPathTraceConstants> g_PT : register(b2);
#define RAY_VIEW g_PT
#include "RayView.hlsli"

//! The G-buffer's normal of the surface at vPos on the camera's side (the leaves' two sides), as PathTrace.hlsl's.
float3 PackNormal(int2 p, float3 vPos)
{
  const float3 vN = PathOctDecode(g_Textures[g_PT.nTextures2.x].Load(int3(p, 0)).rg);
  return dot(vN, g_PT.vCameraPos.xyz - vPos) < 0 ? -vN : vN;
}

RWTexture2D<float4> g_Radiance : register(u0);
RWTexture2D<float4> g_NormalRoughness : register(u1);
RWTexture2D<float> g_ViewZ : register(u2);
RWTexture2D<float2> g_Motion : register(u3);

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSPack(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float4 r = g_Textures[g_PT.nTextures.y].Load(int3(p, 0));
  if (r.w <= 0)
  {
    g_Radiance[p] = 0;
    g_NormalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(float3(0, 0, 1), 1, 0);
    g_ViewZ[p] = g_PT.vNrd.w;
    g_Motion[p] = 0;
    return;
  }
  const float d = r.w;
  const float3 vPos = PixelPos(p + 0.5, d);
  const float fHit = g_Textures[g_PT.nTextures3.w].Load(int3(p, 0)).r;
  g_Radiance[p] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(r.rgb, REBLUR_FrontEnd_GetNormHitDist(fHit, d, g_PT.vNrd.xyz, 1), true);
  g_NormalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(PackNormal(p, vPos), 1, 0);
  g_ViewZ[p] = d;
  float2 vMotion = 0;
  if (g_PT.nTextures3.z != XR_RAY_NONE)
    vMotion = g_Textures[g_PT.nTextures3.z].Load(int3(p, 0)).rg;
  else
  {
    // The camera's motion: the point's uv in the frame before less its own now, the jitter taken off.
    const float4 c = mul(float4(vPos, 1), g_PT.mPrevViewProj);
    if (c.w > 1e-3)
    {
      const float2 vPrev = float2(0.5 + 0.5 * c.x / c.w, 0.5 - 0.5 * c.y / c.w);
      const float2 vNow = (p + 0.5) * g_PT.vSize.zw - float2(0.5, -0.5) * g_PT.vJitter.xy;
      vMotion = vPrev - vNow;
    }
  }
  g_Motion[p] = vMotion;
}

[numthreads(XR_PT_GROUP, XR_PT_GROUP, 1)]
void CSResolve(uint3 vId : SV_DispatchThreadID)
{
  const int2 p = (int2)vId.xy;
  if (any(p >= (int2)g_PT.vSize.xy))
    return;
  const float d = g_Textures[g_PT.nTextures.y].Load(int3(p, 0)).w;
  if (d <= 0)
  {
    g_Radiance[p] = 0;
    return;
  }
  const float4 c = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(g_Textures[g_PT.nTextures3.y].Load(int3(p, 0)));
  g_Radiance[p] = float4(max(c.rgb, 0), d);
}
