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
//	  AA_SMAA   SMAA 1x's neighbourhood blending (SmaaBlend.hlsli) of the
//	            copy tex0 by the blending weights tex1 (R8G8B8A8, its
//	            compute kernels' of AntiAliasSmaa.hlsl): a pixel without
//	            weights is not written; tex2 its edges (R8G8, the debug
//	            view's)
//	  AA_CMAA2  CMAA2's picture tex1 (its kernels of AntiAliasCmaa2.hlsl
//	            anti-alias a copy of tex0 in place): a pixel it left as it
//	            was is not written, nor one on the picture's border (CMAA2
//	            reads black outside the picture and blends its corners with
//	            it), the others keep tex0's alpha; tex2 its edges (R8_UNORM,
//	            4 bits a pixel, two pixels a texel; the debug view's)
//	  AA_DEBUG  with it: every pixel, grey (half its luma) where there is
//	            no edge, towards red by the blend where there is one
//	vParams0: xy 1 / the target's size, zw its last pixel (x, y).
//	PSHighlight (SPipelineKey::nTarget 4, HdrOutput.cpp) writes the
//	highlight layer too: the ratio the layer's copy tex4 had for the copy's
//	colour (LayerRatio) of the pixel and of the neighbours it blends with,
//	blended as the colours (CMAA2: the pixel's own, it names no neighbours),
//	with the fingerprint of the colour written.
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
  return clamp(p, int2(0, 0), (int2)g_DrawExt.vParams0.zw);
}

float4 FxaaLoad(int2 p)
{
  return g_Textures[g_Draw.nTex0].Load(int3(AaClamp(p), 0));
}

#include "Fxaa.hlsli"

float4 SmaaLoadColor(int2 p)
{
  return FxaaLoad(p);
}

float4 SmaaLoadWeights(int2 p)
{
  return g_Textures[g_Draw.nTex1].Load(int3(AaClamp(p), 0));
}

#include "SmaaBlend.hlsli"

//! The debug view (AA_DEBUG) of a pixel with the luma l: grey without an edge, towards red by fBlend on one.
float4 AaDebug(float l, bool bEdge, float fBlend, float fAlpha)
{
  const float3 vGrey = (float3)(0.5 * l);
  return float4(bEdge ? lerp(vGrey, float3(1, 0, 0), saturate(2 * fBlend)) : vGrey, fAlpha);
}

//! SMAA (AA_SMAA) of the pixel p: as PSMain.
float4 SmaaPixel(int2 p, SSmaaBlend b)
{
  if (HasFlag(AA_DEBUG))
  {
    const float4 c = SmaaLoadColor(p);
    const bool bEdge = b.bBlend || any(g_Textures[g_Draw.nTex2].Load(int3(p, 0)).rg > 0);
    return AaDebug(FxaaLuma(c.rgb), bEdge, b.fShare[0] + b.fShare[1], c.a);
  }
  if (!b.bBlend)
    discard;
  return SmaaBlendColor(p, b);
}

//! CMAA2 (AA_CMAA2) of the pixel p: its colour where it changed it, away from the picture's border; the debug view red
//! by the change against the range of the pixel's four neighbours.
float4 Cmaa2Pixel(int2 p)
{
  const float4 c = FxaaLoad(p);
  const float3 v = g_Textures[g_Draw.nTex1].Load(int3(p, 0)).rgb;
  // CMAA2's loads outside the picture read black: its border pixels stay as they were (it darkens the corners).
  const bool bBorder = any(p <= 0) || any(p >= (int2)g_DrawExt.vParams0.zw);
  const bool bChanged = !bBorder && any(v != c.rgb);
  if (HasFlag(AA_DEBUG))
  {
    const uint nEdges = ((uint)round(g_Textures[g_Draw.nTex2].Load(int3(p.x >> 1, p.y, 0)).r * 255) >> ((p.x & 1) * 4)) & 0xF;
    float3 vMin = c.rgb, vMax = c.rgb;
    const int2 Steps[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
    for (uint i = 0; i < 4; i++)
    {
      const float3 n = FxaaLoad(p + Steps[i]).rgb;
      vMin = min(vMin, n);
      vMax = max(vMax, n);
    }
    const float3 d = abs(v - c.rgb), r = vMax - vMin;
    return AaDebug(FxaaLuma(c.rgb), bChanged || nEdges != 0, max(max(d.r, d.g), d.b) / max(max(max(r.r, r.g), r.b), 1.0 / 255), c.a);
  }
  if (!bChanged)
    discard;
  return float4(v, c.a);
}

float4 PSMain(SAaOutput In) : SV_Target
{
  const int2 p = (int2)In.vPos.xy;
  if (HasFlag(AA_SMAA))
    return SmaaPixel(p, SmaaBlend(p));
  if (HasFlag(AA_CMAA2))
    return Cmaa2Pixel(p);
  const SFxaaResult r = Fxaa(p, (uint)g_DrawExt.vParams1.x);
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
  bool bScene;
  if (HasFlag(AA_SMAA))
  {
    // The ratios of the pixel's light and of its two neighbours', with their shares.
    const SSmaaBlend b = SmaaBlend(p);
    Out.vColor = SmaaPixel(p, b);
    float3 vRatio = LayerRatio(FxaaLoad(p).rgb, g_Textures[g_Draw.nTex4].Load(int3(p, 0)), bScene) * (1 - b.fShare[0] - b.fShare[1]);
    for (uint i = 0; i < 2; i++)
    {
      const int2 q = AaClamp(p + b.vStep[i]);
      vRatio += LayerRatio(FxaaLoad(q).rgb, g_Textures[g_Draw.nTex4].Load(int3(q, 0)), bScene) * b.fShare[i];
    }
    Out.vHighlight = float4(HasFlag(AA_DEBUG) ? float3(1, 1, 1) : vRatio, HighlightFingerprint(Out.vColor.rgb));
    return Out;
  }
  if (HasFlag(AA_CMAA2))
  {
    // The pixel's own ratio: CMAA2's picture names no neighbours.
    Out.vColor = Cmaa2Pixel(p);
    const float3 vRatio = LayerRatio(FxaaLoad(p).rgb, g_Textures[g_Draw.nTex4].Load(int3(p, 0)), bScene);
    Out.vHighlight = float4(HasFlag(AA_DEBUG) ? float3(1, 1, 1) : vRatio, HighlightFingerprint(Out.vColor.rgb));
    return Out;
  }
  Out.vColor = PSMain(In);
  // The ratio of the pixel's light and of its neighbour's across the edge, blended as their colours.
  const SFxaaResult r = Fxaa(p, (uint)g_DrawExt.vParams1.x);
  const float3 vRatio = LayerRatio(FxaaLoad(p).rgb, g_Textures[g_Draw.nTex4].Load(int3(p, 0)), bScene);
  const int2 q = AaClamp(p + r.vStep);
  const float3 vRatioStep = LayerRatio(FxaaLoad(q).rgb, g_Textures[g_Draw.nTex4].Load(int3(q, 0)), bScene);
  Out.vHighlight = float4(HasFlag(AA_DEBUG) ? float3(1, 1, 1) : lerp(vRatio, vRatioStep, r.fBlend), HighlightFingerprint(Out.vColor.rgb));
  return Out;
}
