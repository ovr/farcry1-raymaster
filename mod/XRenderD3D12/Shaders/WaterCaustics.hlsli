// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterCaustics.hlsli
//	Description: The sea floor's caustics of the FFT ocean (General.hlsl's
//	DF_CAUSTICS with d3d12_WaterCaustics; the unit tests' kernels): the
//	light of a caustics cascade at the floor's depth from its texel (the
//	light at its four depths, OceanFFT.hlsl's XR_OCEAN_CAUSTICS), the blur
//	of the sun's disc, the fade beyond its last mip level and the factor of
//	the floor's light.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WATERCAUSTICS_HLSLI
#define XRENDERD3D12_WATERCAUSTICS_HLSLI

//! The sun's disc's width in the water (rad): 0.53 degrees over 1.333. A point of the floor d metres down takes its light
//! from a patch of the surface d times as wide: the caustics blurred by it.
static const float CAUSTICS_SUN_ANGLE = 0.00694;

//! A caustics cascade's light at depth d (m) from its texel v, the light at fFirst, 2, 4 and 8 times it: 1 at the surface,
//! towards the first linearly, between two by the log of the depth, below the last the last's.
float CausticsAtDepth(float4 v, float fFirst, float d)
{
  const float s = log2(max(d, 1e-4) / fFirst);
  if (s <= 0)
    return lerp(1, v.x, saturate(d / fFirst));
  return lerp(lerp(lerp(v.x, v.y, saturate(s)), v.z, saturate(s - 1)), v.w, saturate(s - 2));
}

//! A caustics cascade's share of its light's change from the mean where a pixel takes fTexels of its texels: all of it up
//! to its last mip level's texel (XR_CAUSTICS_MIPS), none from twice that (the hardware samples the last level beyond it).
float CausticsFar(float fTexels)
{
  return saturate(XR_CAUSTICS_MIPS - log2(max(fTexels, 1e-9)));
}

//! The factor of the floor's light at the caustics' light I (1 on the mean): the sun's share k of it moved (0 in its
//! shadow), at most fMost.
float CausticsFactor(float I, float k, float fMost)
{
  return clamp(1 + k * (I - 1), 0, fMost);
}

#endif
