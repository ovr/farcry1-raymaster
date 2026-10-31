// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrLight.hlsli
//	Description: The game's colours as light and back, for the HDR
//	targets (r_HDRRendering). Without bindings: the programs (Common.hlsli)
//	and the kernels (Bloom.hlsl) include it.
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

#endif
