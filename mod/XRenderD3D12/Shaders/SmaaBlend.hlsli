// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SmaaBlend.hlsli
//	Description: The third pass of SMAA 1x (d3d12_AA smaa, PostAA.cpp;
//	AntiAlias.hlsl's AA_SMAA): the neighbourhood blending of
//	SMAANeighborhoodBlendingPS (third_party/smaa's SMAA.hlsl) from the
//	blending weights of Smaa.hlsli's SmaaWeights. For a pixel:
//	  - the weights of its four sides: its right neighbour's alpha, its
//	    lower neighbour's green, its own blue (left) and red (up);
//	  - none above 1e-5: the pixel stays;
//	  - else the pair (left and right, or up and down) with the larger
//	    weight, each side's weight w the share of that neighbour in the
//	    blend of the pixel with it, the two blends weighted by w / the
//	    pair's sum.
//	SMAA's bilinear taps at the fractions w are exact loads here, in
//	linear light as SMAA asks for this pass (the sRGB read and write that
//	the UNORM back buffer and its copy have not): the colours decoded from
//	sRGB, mixed, encoded again. The alpha stays (the HUD masks through the
//	back buffer's).
//	The includer defines, before it includes this:
//	  float4 SmaaLoadColor(int2 p)    the picture's pixel p (gamma encoded),
//	                                  clamped to the picture
//	  float4 SmaaLoadWeights(int2 p)  SmaaWeights of the pixel p, clamped
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_SMAA_BLEND_HLSLI
#define XRENDERD3D12_SMAA_BLEND_HLSLI

//! What SMAA does with a pixel: bBlend false leaves it as it is; else the neighbours vStep[i] away have the shares
//! fShare[i] of its colour (in linear light), the pixel itself the rest.
struct SSmaaBlend
{
  int2 vStep[2];
  float fShare[2];
  bool bBlend;
};

//! The sRGB transfer function and its inverse (IEC 61966-2-1), of one channel.
float SmaaChannelToLinear(float c)
{
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float SmaaChannelToGamma(float c)
{
  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055;
}

float3 SmaaToLinear(float3 c)
{
  return float3(SmaaChannelToLinear(c.r), SmaaChannelToLinear(c.g), SmaaChannelToLinear(c.b));
}

float3 SmaaToGamma(float3 c)
{
  return float3(SmaaChannelToGamma(c.r), SmaaChannelToGamma(c.g), SmaaChannelToGamma(c.b));
}

//! SMAA's blend of the pixel p.
SSmaaBlend SmaaBlend(int2 p)
{
  SSmaaBlend r;
  float4 a;
  a.x = SmaaLoadWeights(p + int2(1, 0)).a;
  a.y = SmaaLoadWeights(p + int2(0, 1)).g;
  a.wz = SmaaLoadWeights(p).xz;
  r.bBlend = dot(a, float4(1, 1, 1, 1)) >= 1e-5;
  const bool bHorizontal = max(a.x, a.z) > max(a.y, a.w);
  // The fractions towards the two neighbours (right and left, or down and up) and the blends' weights.
  const float2 vFraction = bHorizontal ? a.xz : a.yw;
  const float fSum = vFraction.x + vFraction.y;
  const float2 vWeight = fSum > 0 ? vFraction / fSum : float2(0, 0);
  r.vStep[0] = bHorizontal ? int2(1, 0) : int2(0, 1);
  r.vStep[1] = -r.vStep[0];
  r.fShare[0] = r.bBlend ? vWeight.x * vFraction.x : 0;
  r.fShare[1] = r.bBlend ? vWeight.y * vFraction.y : 0;
  return r;
}

//! The colour of the pixel p blended as b says (b.bBlend: the pixel's own otherwise), its alpha kept.
float4 SmaaBlendColor(int2 p, SSmaaBlend b)
{
  const float4 c = SmaaLoadColor(p);
  if (!b.bBlend)
    return c;
  const float3 vLinear = SmaaToLinear(c.rgb) * (1 - b.fShare[0] - b.fShare[1]) + SmaaToLinear(SmaaLoadColor(p + b.vStep[0]).rgb) * b.fShare[0] +
                         SmaaToLinear(SmaaLoadColor(p + b.vStep[1]).rgb) * b.fShare[1];
  return float4(saturate(SmaaToGamma(vLinear)), c.a);
}

#endif
