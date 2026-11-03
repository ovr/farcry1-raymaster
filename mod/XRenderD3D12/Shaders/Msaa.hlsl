// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Msaa.hlsl
//	Description: MSAA's resolve passes (d3d12_AA msaa2, msaa4, msaa8;
//	Msaa.cpp; the method is Msaa.hlsli's), a triangle over the main view:
//	  VSMain   the triangle, from SV_VertexID
//	  PSColor  t0 the multisampled colour (the HDR target's or the back
//	           buffer's format), t1 with MSAA_AUTO_EXPOSURE the adapted EV
//	           (1 x 1 R32_FLOAT, Hdr.cpp's): the pixel's colour
//	           (MsaaResolveColor; with MSAA_HDR the HDR target's, exposed
//	           by 2^(vExposure.x + the adapted EV)) into the main view's
//	           target
//	  PSDepth  t0 the multisampled depth buffer (R24_UNORM_X8_TYPELESS):
//	           SV_Depth the pixel's nearest (MsaaResolveDepth) into the
//	           main view's depth buffer
//	Bindings (Msaa.cpp's root signature): b0 SMsaaConstants (root
//	constants), t0, t1.
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbMsaa : register(b0)
{
  SMsaaConstants g_Msaa;
};

#if defined(XR_MSAA_DEPTH)
Texture2DMS<float> g_Depth : register(t0);
float4 MsaaLoadColor(int2 p, uint s) { return 0; }
float MsaaLoadDepth(int2 p, uint s) { return g_Depth.Load(p, s); }
#else
Texture2DMS<float4> g_Color : register(t0);
Texture2D<float> g_Exposure : register(t1);
float4 MsaaLoadColor(int2 p, uint s) { return g_Color.Load(p, s); }
float MsaaLoadDepth(int2 p, uint s) { return 1; }
#endif

#include "Msaa.hlsli"

float4 VSMain(uint nVertex : SV_VertexID) : SV_Position
{
  const float2 uv = float2((nVertex << 1) & 2, nVertex & 2);
  return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

#if defined(XR_MSAA_DEPTH)
float PSDepth(float4 vPos : SV_Position) : SV_Depth
{
  return MsaaResolveDepth(int2(vPos.xy), g_Msaa.vParams.x);
}
#else
float4 PSColor(float4 vPos : SV_Position) : SV_Target
{
  float fEV = g_Msaa.vExposure.x;
  if (g_Msaa.vParams.y & MSAA_AUTO_EXPOSURE)
    fEV += g_Exposure.Load(int3(0, 0, 0));
  return MsaaResolveColor(int2(vPos.xy), g_Msaa.vParams.x, (g_Msaa.vParams.y & MSAA_HDR) != 0, exp2(fEV));
}
#endif
