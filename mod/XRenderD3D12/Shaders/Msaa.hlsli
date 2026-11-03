// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Msaa.hlsli
//	Description: MSAA's resolve of a pixel (d3d12_AA msaa2, msaa4, msaa8;
//	Msaa.cpp, Msaa.hlsl) from its samples, which the includer gives:
//	  float4 MsaaLoadColor(int2 p, uint s)  sample s of pixel p's colour
//	  float MsaaLoadDepth(int2 p, uint s)   the same of its depth
//	A pixel the edge covers in part should show the mean of what the
//	display shows of each sample (the render tests' reference: the view
//	drawn at twice the size, its 2 x 2 means of the display's light). The
//	colours are the game's, gamma encoded (HdrDecode their light):
//	  - LDR (the back buffer): the mean of the samples' light, encoded
//	    again;
//	  - HDR (the HDR target, its light above white too): each sample's
//	    light exposed and tone mapped by the ACES curve's fit per channel
//	    (K. Narkowicz's, the shape of d3d12_ToneMap 2's), their mean, then
//	    the curve and the exposure undone; between the darkest and the
//	    brightest sample per channel. A plain mean of the colours lets a
//	    bright sample take the pixel (on an edge against the sun or a lamp
//	    the steps stay), and B. Karis's 1 / (1 + max) weights, a stronger
//	    curve than the tone map's at the scene's exposure, darkened the
//	    edges against the sky (Training's islands: the edges' error 0.62
//	    of none's where the plain mean was 0.55).
//	The depth is the nearest sample's, so that what reads it per pixel
//	(the ambient occlusion, the sun's mask by rays, the water) sees the
//	edge's nearer surface.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_SHADERS_MSAA_HLSLI
#define XRENDERD3D12_SHADERS_MSAA_HLSLI

#include "HdrLight.hlsli"

float4 MsaaLoadColor(int2 p, uint s);
float MsaaLoadDepth(int2 p, uint s);

static const float MSAA_HALF_MAX = 65000;

//! The light of a colour of the game, per channel (HdrDecode; never negative, never more than a half float holds).
float3 MsaaDecode(float3 c)
{
  c = clamp(c, 0, MSAA_HALF_MAX);
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

float3 MsaaEncode(float3 v)
{
  return float3(HdrEncode(v.r), HdrEncode(v.g), HdrEncode(v.b));
}

//! The tone map of the resolve: the ACES curve's fit per channel, light (exposed) in, the display's light out (0 to
//! 2.51 / 2.43).
float3 MsaaToneMap(float3 x)
{
  return x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14);
}

//! Its inverse: the light of the display's light y, the root x >= 0 of (2.43 y - 2.51) x^2 + (0.59 y - 0.03) x + 0.14 y = 0,
//! in the form without the cancellation of the two terms at each side of 0.0508.
float3 MsaaToneMapInverse(float3 y)
{
  y = clamp(y, 0, 1.0329);
  const float3 a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, c = 0.14 * y;
  const float3 d = sqrt(max(b * b - 4 * a * c, 0));
  return select(b < 0, 2 * c / max(d - b, 1e-20), (b + d) / (-2 * a));
}

//! The colour of pixel p from its nSamples samples: bHdr the HDR target's (fExposure the factor of the light the
//! resolve exposes it by), else the back buffer's; the alpha the plain mean.
float4 MsaaResolveColor(int2 p, uint nSamples, bool bHdr, float fExposure)
{
  float3 vSum = 0, vMost = 0, vLeast = MSAA_HALF_MAX;
  float fAlpha = 0;
  for (uint s = 0; s < nSamples; s++)
  {
    const float4 c = MsaaLoadColor(p, s);
    const float3 v = MsaaDecode(c.rgb);
    vSum += bHdr ? MsaaToneMap(v * fExposure) : v;
    vMost = max(vMost, v);
    vLeast = min(vLeast, v);
    fAlpha += c.a;
  }
  const float n = max(nSamples, 1u);
  const float3 v = bHdr ? MsaaToneMapInverse(vSum / n) / fExposure : vSum / n;
  // The mean is between them; the clamp keeps the inverse's rounding near the curve's top (light far above white) off
  // the uniform pixels.
  return float4(MsaaEncode(clamp(v, vLeast, vMost)), fAlpha / n);
}

//! The depth of pixel p: the nearest of its nSamples samples (0 near, 1 far).
float MsaaResolveDepth(int2 p, uint nSamples)
{
  float fDepth = 1;
  for (uint s = 0; s < nSamples; s++)
    fDepth = min(fDepth, MsaaLoadDepth(p, s));
  return fDepth;
}

#endif
