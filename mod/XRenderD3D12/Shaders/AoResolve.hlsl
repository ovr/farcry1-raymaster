// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AoResolve.hlsl
//	Description: The ambient occlusion as the factor the main view's
//	scene is multiplied by (AmbientOcclusion.cpp): the backend's
//	visibility to the power vParams.x, faded out with the fog as the
//	scene is (by the depth along the view axis), 1 where nothing wrote the
//	depth (the sky). With vParams.y (d3d12_AOApply 1) instead the light it
//	takes from the scene: the ambient light's part of the scene's colour
//	(t2, General.hlsl's XR_AMBIENT, fogged already) times 1 - the
//	visibility to the power vParams.x, 0 on the sky; with vParams.z also
//	the factor without the fog, for d3d12_AODebug 1.
//
//	Bindings (D3D12Compute.h): b0 SAoResolveConstants, t0 the visibility
//	(R8_UNORM, 1 nothing occluded), t1 the depth buffer (0 near, 1 far),
//	t2 the ambient light (R11G11B10_FLOAT); u0 the factor (R8G8B8A8_UNORM:
//	f, f, f, 1), u1 the light taken (R16G16B16A16_FLOAT: rgb, 0).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbResolve : register(b0)
{
  SAoResolveConstants g_Resolve;
};

Texture2D<float> g_Visibility : register(t0);
Texture2D<float> g_Depth : register(t1);
Texture2D<float3> g_Ambient : register(t2);
RWTexture2D<float4> g_Factor : register(u0);
RWTexture2D<float4> g_Taken : register(u1);

[numthreads(8, 8, 1)]
void CSMain(uint2 vPixel : SV_DispatchThreadID)
{
  if (any(vPixel >= (uint2)g_Resolve.vSize.xy))
    return;
  const float fDepth = g_Depth.Load(int3(vPixel, 0));
  if (g_Resolve.vParams.y != 0)
  {
    const float v = fDepth < 1 ? pow(saturate(g_Visibility.Load(int3(vPixel, 0))), g_Resolve.vParams.x) : 1;
    g_Taken[vPixel] = float4((1 - v) * g_Ambient.Load(int3(vPixel, 0)), 0);
    if (g_Resolve.vParams.z != 0)
      g_Factor[vPixel] = float4(v, v, v, 1);
    return;
  }
  float f = 1;
  if (fDepth < 1)
  {
    const float fViewDepth = g_Resolve.vDepthParams.y / (fDepth + g_Resolve.vDepthParams.x);
    const float fFog = g_Resolve.vFogParams.w != 0 ? saturate((g_Resolve.vFogParams.y - fViewDepth) * g_Resolve.vFogParams.z) : 1;
    f = lerp(1, pow(saturate(g_Visibility.Load(int3(vPixel, 0))), g_Resolve.vParams.x), fFog);
  }
  g_Factor[vPixel] = float4(f, f, f, 1);
}
