// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PbrSpecular.hlsli
//	Description: The highlights of the specular templates by the
//	roughness of the texture's RayMaster material map (r_PbrRoughness;
//	its red, PbrCompanion.h; General.hlsl's Specular). The templates
//	give every material the lobe saturate(R.V)^32, R the light reflected
//	about the normal; by the perceptual roughness r (alpha = r^2, the
//	Beckmann roughness) its power is n = (2 / alpha^2 - 2) / 4
//	(Blinn-Phong's power of N.H for that roughness, R.V's lobe about as
//	wide at a quarter of it), between PBR_SPEC_LEAST and PBR_SPEC_MOST,
//	and its height (n + 1) / (32 + 1) times the templates' (the lobe's
//	integral over the directions about R, 2 pi / (n + 1), the same as
//	the templates': as much light reflected, a broad lobe dimmer, a
//	narrow one brighter); the templates' 32 is the lobe of r = 0.35.
//	Where the normal changes across the pixel the lobe is wider:
//	alpha^2 + min(2 sigma^2 (|dN/dx|^2 + |dN/dy|^2), kappa)
//	(Tokuyoshi and Kaplanyan 2019, "Improved geometric specular
//	antialiasing"), no sparkle of the narrow lobes over bumps far off.
//	Stands alone (the unit tests' TestHeaders.hlsl).
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_PBR_SPECULAR_HLSLI
#define XR_PBR_SPECULAR_HLSLI

//! The templates' power of saturate(R.V) (General.hlsl's Specular).
static const float PBR_SPEC_TEMPLATE = 32;
//! The lobe's least and most power (r about 0.9 and 0.17 and beyond).
static const float PBR_SPEC_LEAST = 1;
static const float PBR_SPEC_MOST = 512;
//! The pixel's filter's variance (1 / (2 pi)) and the most the normal's change widens alpha^2 by (Tokuyoshi and
//! Kaplanyan's sigma^2 and kappa).
static const float PBR_SPEC_SIGMA2 = 0.15915494;
static const float PBR_SPEC_KAPPA = 0.18;

//! alpha^2 of the perceptual roughness fRough (0 to 1, clamped), widened by fNormalChange = |dN/dx|^2 + |dN/dy|^2 of
//! the shading normal across the pixel.
float PbrSpecAlpha2(float fRough, float fNormalChange)
{
  const float r = saturate(fRough);
  const float a = r * r;
  return saturate(a * a + min(2 * PBR_SPEC_SIGMA2 * max(fNormalChange, 0), PBR_SPEC_KAPPA));
}

//! The lobe of alpha^2: x its power of saturate(R.V), y its height against the templates' lobe.
float2 PbrSpecLobe(float fAlpha2)
{
  const float n = clamp((2 / max(fAlpha2, 1e-6) - 2) / 4, PBR_SPEC_LEAST, PBR_SPEC_MOST);
  return float2(n, (n + 1) / (PBR_SPEC_TEMPLATE + 1));
}

#endif // XR_PBR_SPECULAR_HLSLI
