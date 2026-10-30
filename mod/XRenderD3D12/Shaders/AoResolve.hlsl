// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AoResolve.hlsl
//	Description: The ambient occlusion as the factor the main view's
//	scene is multiplied by (AmbientOcclusion.cpp): the backend's
//	visibility to the power vParams.x, faded out with the fog as the
//	scene is (by the depth along the view axis), 1 where nothing wrote the
//	depth (the sky).
//
//	Bindings (D3D12Compute.h): b0 SAoResolveConstants, t0 the visibility
//	(R8_UNORM, 1 nothing occluded), t1 the depth buffer (0 near, 1 far),
//	u0 the factor (R8G8B8A8_UNORM: f, f, f, 1).
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbResolve : register(b0)
{
  SAoResolveConstants g_Resolve;
};

Texture2D<float> g_Visibility : register(t0);
Texture2D<float> g_Depth : register(t1);
RWTexture2D<float4> g_Factor : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint2 vPixel : SV_DispatchThreadID)
{
  if (any(vPixel >= (uint2)g_Resolve.vSize.xy))
    return;
  const float fDepth = g_Depth.Load(int3(vPixel, 0));
  float f = 1;
  if (fDepth < 1)
  {
    const float fViewDepth = g_Resolve.vDepthParams.y / (fDepth + g_Resolve.vDepthParams.x);
    const float fFog = g_Resolve.vFogParams.w != 0 ? saturate((g_Resolve.vFogParams.y - fViewDepth) * g_Resolve.vFogParams.z) : 1;
    f = lerp(1, pow(saturate(g_Visibility.Load(int3(vPixel, 0))), g_Resolve.vParams.x), fFog);
  }
  g_Factor[vPixel] = float4(f, f, f, 1);
}
