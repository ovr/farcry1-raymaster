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
//	water is murky. With d3d12_FogVolumeDepth the floor seen through a
//	water volume's surface has at least the fog of its depth under it,
//	which the ray crosses: the water adds it (Water.hlsl, ThroughWater),
//	its colour lit by the brightness behind as the water's own scatter.
//	It was the fog pass's, in the volume's colour as given: the dark
//	cave pool round the rock of Training's first cut scene turned a flat
//	light blue sheet (and the layer may reach past the water). (The view's
//	whole path under the surface would be more, but it would also thicken
//	the fog of the thin volumes far off at a slant, Swamp's, which the
//	gate matches to the original.)
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

//! The fog pass's fog from above (fCamera > 0) over a point fDepth under the layer, fViewDepth from the camera along the
//! view axis (Common.hlsli's FogVolumeGate).
inline float Alpha(float fCamera, float fDepth, float fViewDepth, float fDensity)
{
  if (fDepth <= 0)
    return 0;
  const float fGate = 1 - powf(1 - std::clamp(fDepth / 5, 0.0f, 1.0f), 3 + (1.6f - 3) * std::clamp(fCamera / 5, 0.0f, 1.0f));
  return fGate * (1 - expf(-fViewDepth * fDensity));
}

//! What the water's surface lays over the scene behind it, where the fog pass laid fGated over a floor fDepth under the
//! surface: the two together the fog of that depth when it is more.
inline float ThroughWater(float fGated, float fDepth, float fDensity)
{
  const float fDeep = 1 - expf(-std::max(fDepth, 0.0f) * fDensity);
  return std::clamp((fDeep - fGated) / std::max(1 - fGated, 1e-4f), 0.0f, 1.0f);
}

//! The floor's fog seen through the water (bThroughWater) or on dry ground under the layer.
inline float Total(float fCamera, float fDepth, float fViewDepth, float fDensity, bool bThroughWater)
{
  const float a = Alpha(fCamera, fDepth, fViewDepth, fDensity);
  return bThroughWater ? 1 - (1 - a) * (1 - ThroughWater(a, fDepth, fDensity)) : a;
}

//! The fog volume a water volume's surface fills: the engine gives the surface none, and several pools may share a
//! layer's height (Training's bunker pool and its cave's). Of the volumes at the surface's height, the one with an
//! object of this frame nearest to the surface's box across (CScene::WaterFogVolume).
class CWaterVolumePick
{
public:
  CWaterVolumePick(float fMinX, float fMinY, float fMaxX, float fMaxY, float fSurfaceZ)
    : m_fMin{ fMinX, fMinY }, m_fMax{ fMaxX, fMaxY }, m_fSurfaceZ(fSurfaceZ)
  {
  }
  //! An object in the volume nFog (its layer fLayerZ) at x, y.
  void Add(int nFog, float fLayerZ, bool bCaustics, float x, float y)
  {
    if (nFog <= 0 || bCaustics || std::fabs(fLayerZ - m_fSurfaceZ) > 0.25f)
      return;
    const float dx = std::max({ m_fMin[0] - x, 0.0f, x - m_fMax[0] }), dy = std::max({ m_fMin[1] - y, 0.0f, y - m_fMax[1] });
    const float d = dx * dx + dy * dy;
    if (!m_nBest || d < m_fBest)
    {
      m_nBest = nFog;
      m_fBest = d;
    }
  }
  //! 0: none.
  int Best() const { return m_nBest; }

private:
  float m_fMin[2], m_fMax[2], m_fSurfaceZ;
  int m_nBest = 0;
  float m_fBest = 0;
};

//! The water volume templates (lower case) whose water is the scene behind times the material's diffuse colour.
inline bool TintsWater(std::string_view Template)
{
  return Template.starts_with("watervolume");
}
} // namespace FogVolume
