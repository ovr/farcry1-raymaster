// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterLight.hlsli
//	Description: The sun's light on water surfaces (Water.hlsl): its
//	glint, the facets of the waves that mirror it. The slopes of the waves
//	about the pixel's normal are a gaussian whose variance is that of the
//	ripple finer than a pixel (LEAN, Olano and Baker: what the wave
//	texture's mip levels smoothed out of the normal) and a little more for
//	the ripple finer than the texture: the Beckmann distribution in the
//	slopes' space. Near the camera the normal's waves break the sun up into
//	sparks; far away the lost slopes widen them into one streak towards the
//	camera, the sun's path. And the sun's light through the thin crests of
//	the waves: lit from behind, the water of a crest scatters it forwards,
//	towards a camera that looks at the sun past it (turquoise: the red is
//	absorbed on the way). The glint only where the sky box shows a sun
//	towards it (d3d12_WaterGlintSky): none under a storm's cloud.
//	Without bindings: Water.hlsl and the unit tests
//	(Tests/Shaders/TestHeaders.hlsl) include it.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WATERLIGHT_HLSLI
#define XRENDERD3D12_WATERLIGHT_HLSLI

static const float WATER_LIGHT_PI = 3.14159265;

//! The density of the facets with normal H among the water's (per solid angle of H), its slopes
//! (dH/dx, dH/dy) a gaussian about those of the normal N, fVar the variance of the two together:
//! the Beckmann distribution in the slopes' space, exp(-|s - s0|^2 / fVar) / (pi fVar H.z^4) with s
//! = -H.xy / H.z the slope of the facet. Its integral times H.z over the hemisphere is 1. H and N
//! unit vectors, z up; 0 for H below the horizon.
float WaterSlopeDensity(float3 H, float3 N, float fVar)
{
  if (H.z <= 1e-4)
    return 0;
  const float2 d = N.xy / max(N.z, 1e-4) - H.xy / H.z;
  const float z2 = H.z * H.z;
  return exp(-dot(d, d) / fVar) / (WATER_LIGHT_PI * fVar * z2 * z2);
}

//! The share of the facets that the waves in front of them leave in sight of direction D (Smith's
//! for the Beckmann distribution of variance fVar of the slopes, Walter et al.'s rational fit):
//! from the angle to the water's plane (z), 1 looking down, 0 at the horizon.
float WaterMasking(float3 D, float fVar)
{
  const float c = D.z;
  if (c <= 0)
    return 0;
  const float a = c / (sqrt(fVar) * sqrt(max(1 - c * c, 1e-8)));
  return a < 1.6 ? (3.535 * a + 2.181 * a * a) / (1 + 2.276 * a + 2.577 * a * a) : 1;
}

//! Schlick's fresnel term of water (2 % looking straight down) at the cosine c.
float WaterFresnel(float c)
{
  const float m = 1 - saturate(c);
  const float m2 = m * m;
  return 0.02 + 0.98 * m2 * m2 * m;
}

//! The sun's glint on water whose normal is N (the waves the pixel resolves) and whose slopes about
//! it have the variance fVar (the ripple finer than a pixel and finer than the texture), V towards
//! the camera, L towards the sun, all unit vectors with z up: the light the facets mirror towards
//! the camera, a factor of the light of a white face turned to the sun (the game's diffuse light,
//! which a lambertian face of albedo 1 gets as E / pi): pi F D G / (4 V.z), the microfacet BRDF's
//! F D G / (4 L.z V.z) times the sun's irradiance pi L.z. The slopes are about the water's plane, so
//! its cosines are with z. 0 with the sun or the camera below the horizon.
float WaterGlint(float3 N, float3 V, float3 L, float fVar)
{
  if (L.z <= 0 || V.z <= 0)
    return 0;
  const float3 H = normalize(L + V);
  const float D = WaterSlopeDensity(H, N, fVar);
  const float G = WaterMasking(V, fVar) * WaterMasking(L, fVar);
  return WATER_LIGHT_PI * WaterFresnel(dot(V, H)) * D * G / (4 * V.z);
}

//! The light vLight of the glint, at most fCap in its brightest channel (the sun's flare in the HDR
//! targets, d3d12_HDRSun: what mirrors the sun is no brighter than the sun the game draws), its hue
//! kept.
float3 WaterCapLight(float3 vLight, float fCap)
{
  const float m = max(vLight.r, max(vLight.g, vLight.b));
  return m > fCap ? vLight * (fCap / m) : vLight;
}

//! The sky box's brightness towards the sun (the luma of its texels as the game draws them) up to which the sky shows
//! no sun there, and from which it shows it as a day sky does (d3d12_WaterGlintSky): a storm's cloud 0.08 (Rebellion),
//! a volcano's smoke 0.10 (Volcano), a night sky with its moon painted elsewhere 0.00 (Steam); 0.28 to 0.49 by day
//! (River, Pier, Training), 0.90 at dusk (Swamp).
static const float WATER_GLINT_SKY_DARK = 0.12;
static const float WATER_GLINT_SKY_LIT = 0.24;
//! The angle (radians, 5 degrees) of the four directions about the sun's that the sky box is looked up in with it.
static const float WATER_GLINT_SKY_CONE = 0.0873;

//! How much of the sun's glint the sky lets through (d3d12_WaterGlintSky): fLuma the sky box's brightness towards the
//! sun; none up to WATER_GLINT_SKY_DARK (no sun there: what the sun lights the water with stays, its mirror image does
//! not), all from WATER_GLINT_SKY_LIT, smoothly between.
float WaterGlintSkyShare(float fLuma)
{
  return smoothstep(WATER_GLINT_SKY_DARK, WATER_GLINT_SKY_LIT, fLuma);
}

//! The k-th (0 to 3) of the four directions WATER_GLINT_SKY_CONE about L (a unit vector, z up), a quarter turn apart.
float3 WaterGlintSkyDir(float3 L, uint k)
{
  const float3 T = normalize(cross(L, abs(L.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
  const float3 B = cross(L, T);
  float s, c;
  sincos(k * (WATER_LIGHT_PI / 2), s, c);
  return normalize(L * cos(WATER_GLINT_SKY_CONE) + (T * c + B * s) * sin(WATER_GLINT_SKY_CONE));
}

//! The brightness of the sun's light through a crest lit all the way through (WaterCrestLight 1)
//! against a white face turned to the sun: the water scatters it forwards, towards a camera that
//! looks at the sun past it. Fitted by eye: so that the crests show against the reflection of the
//! sky the water mirrors at a slant (Carrier by day, Bunker at sunset; 1 hardly shows).
static const float WATER_CREST_PEAK = 4;

//! The colour of the sun's light through a crest lit all the way through, as a share of the light
//! of a white face turned to the sun: WATER_CREST_PEAK times a turquoise (the red absorbed, the blue
//! scattered less forwards than the green), half of it in the hue of the level's deep water vDeep
//! (its brightest channel at the turquoise's), so that a lagoon and the open sea keep their own
//! colours.
float3 WaterCrestColor(float3 vDeep)
{
  static const float3 TURQUOISE = float3(0.10, 0.62, 0.50);
  const float m = max(vDeep.r, max(vDeep.g, vDeep.b));
  const float3 vHue = m > 1e-4 ? vDeep * (TURQUOISE.g / m) : TURQUOISE;
  return lerp(TURQUOISE, vHue, 0.5) * WATER_CREST_PEAK;
}

//! How much of the sun's light the thin crests of the waves let through towards the camera, 0 to 1
//! (times WaterCrestColor): N the normal, V towards the camera, L towards the sun (unit vectors, z
//! up), fHeight the height of the waves there (RMS 1), fSlopeRMS the RMS of their slopes along one
//! axis. The upper part of a wave (above the mean, all of it from twice the RMS), seen against the
//! sun (the forward scattering: the square of the cosine between the view's ray and the sun's),
//! more on its face turned away from the sun, which the light leaves after crossing the crest (half
//! on a level face, all of it from the waves' RMS slope away from the sun, none from it towards the
//! sun: the ripple's slopes hardly follow the long waves' heights, at a crest's top the slope is
//! level), seen at a slant (the thin edge: 1 - N.V); WATER_CREST_GAIN times that, at most 1 (a
//! crest at half the RMS whose back slopes by the RMS of 0.3, looking 10 degrees down towards a sun
//! 20 degrees up: 0.52). 0 with the sun below the horizon or overhead, or without waves.
static const float WATER_CREST_GAIN = 8;
float WaterCrestLight(float3 N, float3 V, float3 L, float fHeight, float fSlopeRMS)
{
  const float fSide = length(L.xy);
  if (L.z <= 0 || fSide < 1e-3 || fSlopeRMS <= 0)
    return 0;
  const float fCrest = smoothstep(0, 2, fHeight);
  const float c = saturate(dot(-V, L));
  const float fBack = saturate(0.5 + dot(N.xy, -L.xy / fSide) / (max(N.z, 1e-3) * 2 * fSlopeRMS));
  const float t = 1 - saturate(dot(N, V));
  return saturate(WATER_CREST_GAIN * fCrest * c * c * fBack * t);
}

#endif
