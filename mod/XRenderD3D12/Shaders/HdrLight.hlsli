// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrLight.hlsli
//	Description: The game's colours as light and back, for the HDR
//	targets (r_HDRRendering), and the fingerprint of the HDR output's
//	highlight layer. Without bindings: the programs (Common.hlsli) and the
//	kernels (Bloom.hlsl, HdrStats.hlsl) include it.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_HDRLIGHT_HLSLI
#define XRENDERD3D12_HDRLIGHT_HLSLI

//! The light of a colour of the game with r_HDRRendering (Hdr.hlsl, Hdr.cpp): c^2.2 up to 1, the
//! display's range, and above it on along the curve's tangent at 1, 1 + 2.2 (c - 1). The game adds
//! and scales its colours in gamma space, so what goes above 1 there (additive sprites over each
//! other, its 2x and 4x light) is light added up, not a brighter display value: with c^2.2 ten
//! sprites of 1 over each other would be a light of 158, not 21, and the bloom of a fire a ball.
static const float HDR_GAMMA = 2.2;
float HdrDecode(float c)
{
  return c > 1 ? 1 + HDR_GAMMA * (c - 1) : pow(max(c, 0), HDR_GAMMA);
}

//! Its inverse: the colour of a light.
float HdrEncode(float fLight)
{
  return fLight > 1 ? 1 + (fLight - 1) / HDR_GAMMA : pow(max(fLight, 0), 1 / HDR_GAMMA);
}

//! The highlight layer of the HDR output (HdrOutput.cpp). A pass that puts the scene into the
//! 8-bit back buffer also writes the layer: rgb the light of the scene on an HDR display over the
//! light of its colour in the back buffer, per channel, and a the fingerprint of that colour as the
//! back buffer stores it (8 bits a channel). The output pass takes the layer's light where the
//! back buffer still holds that colour, i.e. where nothing was drawn over the scene since. One
//! channel one step off (rounding) still matches: 0.45 / 255 at most, within HIGHLIGHT_TOLERANCE
//! with the layer's half float.
static const float3 HIGHLIGHT_WEIGHTS = float3(0.3, 0.45, 0.25);
static const float HIGHLIGHT_TOLERANCE = 0.6 / 255;
float HighlightFingerprint(float3 c)
{
  return dot(round(saturate(c) * 255), HIGHLIGHT_WEIGHTS) / 255;
}

//! The output pass's ratio of the light of a pixel of the frame (its colour f) to the light of f: the
//! layer's (vLayer) where its fingerprint matches f (bScene), 1 where something was drawn over the
//! scene since.
float3 LayerRatio(float3 f, float4 vLayer, out bool bScene)
{
  bScene = abs(HighlightFingerprint(f) - vLayer.a) <= HIGHLIGHT_TOLERANCE;
  return bScene ? max(vLayer.rgb, 1) : 1;
}

#endif
