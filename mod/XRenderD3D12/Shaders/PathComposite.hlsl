// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathComposite.hlsl
//	Description: The path tracer's light into the picture
//	(eD3D12P_PathComposite, d3d12_PathTracing, PathTrace.cpp): a triangle
//	over the scene's targets after the opaque list, added (ONE, ONE) to the
//	colour that has what the path tracer does not light and the fog
//	(General.hlsl, XR_PT). Where the G-buffer has a surface it lights:
//	  the fog's share * (HdrEncode(albedo * (sun + paths)) + highlights),
//	the albedo the G-buffer's (the game's colour as light, divided by the
//	light baked into the terrain's texture there: vParams1.rgb, the
//	terrain's SPathShade::vBakeLight or 1, times CSFilter's share), the sun its
//	light (vParams0.rgb) times N.L and the visibility the raster found
//	(the G-buffer's), the paths' light that of PathTrace.hlsl's CSFilter.
//	The leaves let half of the sun through from behind them. The
//	highlights (PathTrace.hlsl's CSSpecular) are the game's colour, added
//	to it as the raster adds them.
//	tex0 the albedo, tex1 the normal (PathGBuffer.hlsli), tex2 the sun's
//	visibility, tex3 the paths' light (w the terrain's share of its baked light), tex4
//	the highlights, all of the scene's size, as the target.
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#include "RayConstants.h"
#include "PathGBuffer.hlsli"

struct SCompositeOutput
{
  float4 vPos : SV_Position;
};

SCompositeOutput VSMain(SVertexInput In)
{
  SCompositeOutput Out;
  Out.vPos = float4(In.vPos.xy, 0, 1);
  return Out;
}

float4 PSMain(SCompositeOutput In) : SV_Target
{
  const int3 vAt = int3(In.vPos.xy, 0);
  const float4 vGBuffer = g_Textures[g_Draw.nTex1].Load(vAt);
  const uint nMaterial = PathGBufferMaterial(vGBuffer);
  if (nMaterial == 0)
    return 0;
  const float3 vAlbedo = g_Textures[g_Draw.nTex0].Load(vAt).rgb;
  const float fVisible = g_Textures[g_Draw.nTex2].Load(vAt).r;
  const float4 vPaths = g_Textures[g_Draw.nTex3].Load(vAt);
  const float fNL = dot(PathOctDecode(vGBuffer.rg), g_Frame.vSunDir.xyz);
  const float fSun = nMaterial == XR_PT_MAT_VEGETATION + 1 ? saturate(fNL) + 0.5 * saturate(-fNL) : saturate(fNL);
  const float3 vLight = g_Draw.vParams0.rgb * (fSun * fVisible) + vPaths.rgb;
  const float3 vBaked = nMaterial == XR_PT_MAT_TERRAIN + 1 ? g_Draw.vParams1.rgb * vPaths.w : vPaths.w;
  const float3 vLit = saturate(pow(saturate(vAlbedo), HDR_GAMMA) / max(vBaked, 0.01)) * vLight;
  const float3 vHighlights = g_Textures[g_Draw.nTex4].Load(vAt).rgb;
  return float4((float3(HdrEncode(vLit.r), HdrEncode(vLit.g), HdrEncode(vLit.b)) + vHighlights) * vGBuffer.b, 0);
}
