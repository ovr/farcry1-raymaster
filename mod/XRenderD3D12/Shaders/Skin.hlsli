// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Skin.hlsli
//	Description: The characters' skin (r_Skin: their faces' and heads'
//	textures, PbrCompanion.h's IsSkin). The game's model templates give it
//	the highlight of every material, saturate(R.V)^32 (a glossy plastic's:
//	the lobe of the perceptual roughness 0.35) times the texture's alpha,
//	and lambert's light, lit up to the line where the light passes it and
//	dark beyond: a doll's face of plastic, under the paths' soft light most
//	of all. Skin reflects little (4 % of the light head on is too much: 2.8
//	%, F0 0.028), with a broad lobe (its oil smoother where the texture's
//	alpha, the artists' gloss, is high: the lips, the nose, the eyes' wet
//	rims), more towards grazing views (Fresnel); and its light scatters
//	under the surface, red the farthest, so that it reaches past that line
//	a little and reddens it.
//	  SkinSpecular: pi * N.L * D * V * F (GGX's distribution D, Smith's
//	    height correlated visibility V, Schlick's Fresnel F of SKIN_F0) of
//	    alpha^2 (alpha = r^2 of the perceptual roughness r): the light a
//	    lamp's highlight adds in the game's colour, whose lambert's light of
//	    a texture is pi times the light it reflects (the caller multiplies
//	    by the light's scale, the draw's 2, and by SkinSpecularShare);
//	  SkinRoughness: r between the matte skin's and the oily skin's by the
//	    gloss (the texture's alpha, at SKIN_GLOSS_OILY fully oily), on to
//	    SKIN_ROUGH_WET at the gloss 1 (the eyes' rims, wet);
//	  SkinSpecularShare: the share of the highlight the gloss leaves: all
//	    of it from the bare skin's gloss (SKIN_GLOSS_BARE) up, less where
//	    the artists painted less (the stubble, the brows);
//	  SkinWrap: N.L of each colour wrapped round the line, ((N.L + w) / (1 +
//	    w))^(1 + w), w of each colour (red the most): 1 where N.L is 1,
//	    about N.L in the middle (red a few % more), lifted next to the line
//	    and a little light past it down to -w, red the farthest.
//	Stands alone (the unit tests' TestHeaders.hlsl).
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_SKIN_HLSLI
#define XR_SKIN_HLSLI

//! The share of the light skin reflects head on (its refractive index about 1.4).
static const float SKIN_F0 = 0.028;
//! The least N.V the visibility takes (seen edge on its formula divides by 0).
static const float SKIN_NV_LEAST = 1e-3;
//! The gloss (the texture's alpha) at which the skin is as smooth as the oily skin's roughness (Jack's lips, nose tip
//! and the strands of his hair about it).
static const float SKIN_GLOSS_OILY = 0.3;
//! The perceptual roughness at the gloss 1 (wet: the eyes' rims), not narrower (a lobe that would sparkle).
static const float SKIN_ROUGH_WET = 0.25;
//! The bare skin's gloss (Jack's face 0.086): its highlight whole; below it (the stubble, the brows) less.
static const float SKIN_GLOSS_BARE = 0.08;
//! The gloss of skin whose template has none in its texture's alpha (not a gloss alpha template): the bare skin's.
static const float SKIN_GLOSS_PLAIN = 0.1;
//! How far each colour's light reaches past the line where N.L is 0 (red the farthest: it scatters the most under it).
//! Three times as far made Jack's face orange under Training's cave lamps, which light it at low N.L.
static const float3 SKIN_WRAP = float3(0.12, 0.04, 0.02);

//! The perceptual roughness of skin whose gloss (the texture's alpha) is fGloss: fMatte (no gloss) to fOily (at
//! SKIN_GLOSS_OILY), on to SKIN_ROUGH_WET at 1 (not rougher than fOily).
float SkinRoughness(float fGloss, float fMatte, float fOily)
{
  const float g = saturate(fGloss);
  if (g <= SKIN_GLOSS_OILY)
    return lerp(fMatte, fOily, g / SKIN_GLOSS_OILY);
  return lerp(fOily, min(SKIN_ROUGH_WET, fOily), (g - SKIN_GLOSS_OILY) / (1 - SKIN_GLOSS_OILY));
}

//! The share of the skin's highlight at the gloss fGloss: 1 from the bare skin's (SKIN_GLOSS_BARE) up, less below.
float SkinSpecularShare(float fGloss)
{
  return saturate(fGloss / SKIN_GLOSS_BARE);
}

//! A light's highlight on skin of alpha^2 fAlpha2 (alpha = r^2 of the perceptual roughness r): pi * N.L * D * V * F,
//! vN the normal, vToLight and vToEye unit vectors from the surface; 0 where the light is behind it.
float SkinSpecular(float3 vN, float3 vToLight, float3 vToEye, float fAlpha2)
{
  const float fNL = dot(vN, vToLight);
  if (fNL <= 0)
    return 0;
  const float fNV = max(dot(vN, vToEye), SKIN_NV_LEAST);
  const float3 vH = normalize(vToLight + vToEye);
  const float fNH = saturate(dot(vN, vH));
  const float fVH = saturate(dot(vToEye, vH));
  const float a2 = clamp(fAlpha2, 1e-6, 1);
  const float d = fNH * fNH * (a2 - 1) + 1;
  const float fD = a2 / (3.14159265 * d * d);
  const float fV = 0.5 / (fNL * sqrt(fNV * fNV * (1 - a2) + a2) + fNV * sqrt(fNL * fNL * (1 - a2) + a2));
  const float f = 1 - fVH;
  const float f2 = f * f;
  const float fF = SKIN_F0 + (1 - SKIN_F0) * (f2 * f2 * f);
  return 3.14159265 * fNL * fD * fV * fF;
}

//! The light of each colour at N.L fNL on skin, w its scatter fScatter times SKIN_WRAP: ((N.L + w) / (1 + w))^(1 + w)
//! (0: lambert's, saturate(fNL)).
float3 SkinWrap(float fNL, float fScatter)
{
  const float3 w = SKIN_WRAP * max(fScatter, 0);
  return pow(saturate((fNL + w) / (1 + w)), 1 + w);
}

#endif // XR_SKIN_HLSLI
