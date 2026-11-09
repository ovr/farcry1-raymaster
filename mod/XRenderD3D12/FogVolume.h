// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FogVolume.h
//	Description: The fog of a fog volume over a point under its layer
//	seen from above (General.hlsl's FogVolumeAlpha; this is its CPU
//	mirror for the tests), and the water volumes whose water takes the
//	material's colour (d3d12_WaterVolumeColor, Water.cpp's
//	SetupWaterDraw).
//
//	The engine gives each volume a layer (its top, fFogLayerZ) and a fog
//	distance d; the fog passes take a density of ln 100 / (2 d) per
//	metre, 99 % at twice d. Seen from above, the fog of a point fDepth
//	under the layer grows by a gate, from nothing at the layer to all of
//	it 5 m down (sooner the nearer the camera is to the layer), times the
//	fog of the point's depth along the view axis. A dense volume hides
//	its floor within centimetres of its surface, though: Training's
//	bunker pool (d 0.1 m, 23 per metre) showed its tiles through a fog
//	of 0.4 of the volume's colour a metre down, where the original's
//	water is murky. With d3d12_FogVolumeDepth the fog is at least that of
//	the point's depth under the layer, which every ray to it crosses (the
//	view's whole path under the layer would be more, but it would also
//	thicken the fog of the thin volumes far off at a slant, Swamp's,
//	which the gate matches to the original).
//
//	The original's WaterVolume and WaterVolumeBumpReflCM ("using difuse
//	component for color") draw the scene behind the water times the
//	material's diffuse colour (their refraction pass
//	CGRCIndoorWater_final, WaterColor = DiffuseColor; doubled in its HDR
//	mode, and the game ships with r_HDRRendering 0): the bunker pool's
//	(0.34, 0.36, 0.27) a murky green over its fog.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

namespace FogVolume
{
//! The fog passes' density per metre of a volume with the fog distance fMaxFogDist (m).
inline float Density(float fMaxFogDist)
{
  return logf(100.0f) / (2 * std::max(fMaxFogDist, 0.01f));
}

//! The fog from above (fCamera > 0) over a point fDepth under the layer, fViewDepth from the camera along the view axis;
//! bDepth: d3d12_FogVolumeDepth.
inline float Alpha(float fCamera, float fDepth, float fViewDepth, float fDensity, bool bDepth)
{
  if (fDepth <= 0)
    return 0;
  const float fGate = 1 - powf(1 - std::clamp(fDepth / 5, 0.0f, 1.0f), 3 + (1.6f - 3) * std::clamp(fCamera / 5, 0.0f, 1.0f));
  float a = fGate * (1 - expf(-fViewDepth * fDensity));
  if (bDepth)
    a = std::max(a, 1 - expf(-fDepth * fDensity));
  return a;
}

//! The water volume templates (lower case) whose water is the scene behind times the material's diffuse colour.
inline bool TintsWater(std::string_view Template)
{
  return Template.starts_with("watervolume");
}
} // namespace FogVolume
