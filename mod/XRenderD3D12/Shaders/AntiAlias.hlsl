// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AntiAlias.hlsl
//	Description: The anti-aliasing program (eD3D12P_AntiAlias, d3d12_AA,
//	PostAA.cpp): the passes over the finished picture, drawn into the back
//	buffer from its copy tex0 before the render mode, the screen effects
//	and the 2D. A triangle over the whole target, POSITION in clip space;
//	the mode in nFlags:
//	  AA_FXAA   FXAA (Fxaa.hlsli) of the copy tex0 at the quality
//	            vParams1.x: a pixel it leaves as it is is not written (the
//	            back buffer keeps it, bit for bit)
//	  AA_DEBUG  with it: every pixel, grey (half its luma) where there is
//	            no edge, towards red by the blend where there is one
//	vParams0: xy 1 / the target's size, zw its last pixel (x, y).
//	PSHighlight (SPipelineKey::nTarget 4, HdrOutput.cpp) writes the
//	highlight layer too: the ratio the layer's copy tex4 had for the copy's
//	colour (LayerRatio) of the pixel and of the neighbour it blends with,
//	blended as the colours, with the fingerprint of the colour written.
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"

struct SAaOutput
{
  float4 vPos : SV_Position;
};

SAaOutput VSMain(SVertexInput In)
{
  SAaOutput Out;
  Out.vPos = float4(In.vPos.xy, 0, 1);
  return Out;
}

int2 AaClamp(int2 p)
{
  return clamp(p, int2(0, 0), (int2)g_Draw.vParams0.zw);
}

float4 FxaaLoad(int2 p)
{
  return g_Textures[g_Draw.nTex0].Load(int3(AaClamp(p), 0));
}

#include "Fxaa.hlsli"

//! The debug view (AA_DEBUG) of a pixel with the luma l: grey without an edge, towards red by fBlend on one.
float4 AaDebug(float l, bool bEdge, float fBlend, float fAlpha)
{
  const float3 vGrey = (float3)(0.5 * l);
  return float4(bEdge ? lerp(vGrey, float3(1, 0, 0), saturate(2 * fBlend)) : vGrey, fAlpha);
}

float4 PSMain(SAaOutput In) : SV_Target
{
  const int2 p = (int2)In.vPos.xy;
  const SFxaaResult r = Fxaa(p, (uint)g_Draw.vParams1.x);
  if (HasFlag(AA_DEBUG))
    return AaDebug(FxaaLuma(FxaaLoad(p).rgb), r.bEdge, r.fBlend, r.vColor.a);
  if (!r.bEdge || r.fBlend <= 0)
    discard;
  return r.vColor;
}

SHighlightOutput PSHighlight(SAaOutput In)
{
  const int2 p = (int2)In.vPos.xy;
  SHighlightOutput Out;
  Out.vColor = PSMain(In);
  // The ratio of the pixel's light and of its neighbour's across the edge, blended as their colours.
  const SFxaaResult r = Fxaa(p, (uint)g_Draw.vParams1.x);
  bool bScene;
  const float3 vRatio = LayerRatio(FxaaLoad(p).rgb, g_Textures[g_Draw.nTex4].Load(int3(p, 0)), bScene);
  const int2 q = AaClamp(p + r.vStep);
  const float3 vRatioStep = LayerRatio(FxaaLoad(q).rgb, g_Textures[g_Draw.nTex4].Load(int3(q, 0)), bScene);
  Out.vHighlight = float4(HasFlag(AA_DEBUG) ? float3(1, 1, 1) : lerp(vRatio, vRatioStep, r.fBlend), HighlightFingerprint(Out.vColor.rgb));
  return Out;
}
