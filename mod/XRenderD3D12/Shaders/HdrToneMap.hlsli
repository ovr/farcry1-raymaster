// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrToneMap.hlsli
//	Description: The neutral tone map of the HDR pipeline (d3d12_ToneMap
//	4; Hdr.hlsl's resolve and its highlight layer). Without bindings: the
//	programs and the unit tests (Tests/Shaders/TestHeaders.hlsl) include
//	it.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_HDRTONEMAP_HLSLI
#define XRENDERD3D12_HDRTONEMAP_HLSLI

#include "HdrLight.hlsli"

//! The neutral tone map: light in, display values out (0 .. P; P 1 for the SDR picture, the peak's
//! display value for the HDR output's layer). Up to the knee k (a display value, d3d12_ToneMapKnee)
//! the game's look: the colour of the light as it is (HdrEncode). Above it the brightest channel m
//! goes on along the soft shoulder's curve, k + d (1 - e^(-(m - k) / d)) with d = P - k (its slope
//! 1 at the knee, P far above; a grey comes out as with d3d12_ToneMap 1), and the colour is scaled
//! with it as a whole, so that its hue and saturation stay (the per channel shoulder of
//! d3d12_ToneMap 1 turns a bright blue sky white and cyan: its blue rolls off, the red and the
//! green do not). The light far above white goes over to that per channel shoulder, by
//! 1 - 1 / (1 + fDesat e^2) of the brightest channel's excess e = m - the new m: next to nothing
//! just above the knee (a sky pushed up by the exposure keeps its hue), most far above, where the
//! game itself clips each channel: the sun white, fire yellow with a white core (kept whole, its
//! orange went white in the middle and red around; towards white alone, a white ball). No toe:
//! below the knee nothing changes. (The shoulder of Khronos' PBR Neutral, P - d^2 / (m - k + d),
//! was tried: it reaches P too slowly, the sun of the HDR output 380 nits of a 455 nit peak.)
float3 NeutralToneMap(float3 vLight, float k, float P, float fDesat)
{
  float3 c = float3(HdrEncode(vLight.r), HdrEncode(vLight.g), HdrEncode(vLight.b));
  float m = max(c.r, max(c.g, c.b));
  if (m <= k)
    return c;
  float d = P - k;
  float n = k + d * (1 - exp(-(m - k) / d));
  float3 vChannels = lerp(k + d * (1 - exp(-(max(c, k) - k) / d)), c, step(c, k));
  float e = m - n;
  float g = 1 - 1 / (1 + max(fDesat, 0) * e * e);
  return lerp(c * (n / m), vChannels, g);
}

//! The HDR output's tone map (Hdr.hlsl's highlight layer): the display values of the light on an HDR display, P the
//! peak's display value (HdrEncode of the peak over paper white), k the knee, fDesat the desaturation (as NeutralToneMap's).
//! Up to the game's white (the brightest display value m at most 1) the SDR picture's (NeutralToneMap to 1), so that what the
//! game shows as white or below, the sky, the clouds, what the sun lights, is at paper white at most also where the
//! exposure lifted it into the shoulder; from the light fFull (d3d12_HDRHighlights, in the game's whites) up the neutral
//! tone map to P, the headroom for the highlights alone (the sun, fire, lamps, glints); between them the two blended by
//! smoothstep of m (smooth, never decreasing: both curves rise, the HDR one faster). fFull at most 1: the neutral tone map
//! to P throughout (from the knee, as up to step 257: a sky the exposure lifted above the knee went above paper white).
float3 HdrOutputToneMap(float3 vLight, float k, float P, float fDesat, float fFull)
{
  float3 h = NeutralToneMap(vLight, k, P, fDesat);
  if (fFull <= 1)
    return h;
  float3 s = NeutralToneMap(vLight, k, 1, fDesat);
  float m = HdrEncode(max(vLight.r, max(vLight.g, vLight.b)));
  return lerp(s, h, smoothstep(1, HdrEncode(fFull), m));
}

#endif
