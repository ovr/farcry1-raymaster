// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: UnderwaterLight.h
//	Description: The light the ocean's water scatters a metre at the
//	surface with d3d12_WaterUnderClear, on the CPU once a frame
//	(Underwater.cpp, Water.cpp; the unit tests), without the renderer's
//	types: the same for every pixel of the view under the water and of
//	its surface seen from below, so their shaders take it from their
//	constants. The same as Shaders/Underwater.hlsli's UnderClearSource of
//	UnderDownLight (WaterVolume.hlsli's VolumeAttenuation of the fog
//	volume's colour, HdrLight.hlsli's HdrDecode), which the blended
//	objects under the water still work out in their program. And the
//	eye's white balance under the water (d3d12_WaterUnderAdapt, Hdr.cpp's
//	resolve): the light at the camera's depth taken towards the light at
//	the surface, and its exposure there (d3d12_WaterUnderExposure).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_UNDERWATERLIGHT_H
#define XRENDERD3D12_UNDERWATERLIGHT_H

#include <algorithm>
#include <cmath>

namespace UnderwaterLight
{
  constexpr float ETA = 1.333f;                                             //!< WaterVolume.hlsli's VOLUME_ETA
  constexpr float MOST_RATIO = 64;                                          //!< VOLUME_MOST_RATIO
  constexpr float PURE_WATER[3] = { 0.2644f, 0.0565f, 0.0106f };            //!< VOLUME_PURE_WATER
  constexpr float PURE_SCATTERING[3] = { 0.00122f, 0.00191f, 0.00376f };    //!< Underwater.hlsli's UNDER_PURE_SCATTERING
  constexpr float PARTICLE_SPECTRUM[3] = { 550.0f / 610, 1, 550.0f / 470 }; //!< UNDER_PARTICLE_SPECTRUM
  constexpr float SKY_IN = 0.934f / 0.83f;                                  //!< UNDER_SKY_IN
  constexpr float CLEAR_UP = 1;                                             //!< UNDER_CLEAR_UP
  constexpr float EXPOSURE_DEPTH = 0.5f;  //!< the depth (m) over which the exposure under the water comes in (Exposure)
  constexpr float BALANCE_MOST = 4;       //!< the most one channel's white balance factor over another's (Balance)

  //! HdrLight.hlsli's HdrDecode: the light of a colour of the game (gamma 2.2, above 1 linear on).
  inline float Decode(float c)
  {
    return c > 1 ? 1 + 2.2f * (c - 1) : std::pow(std::max(c, 0.0f), 2.2f);
  }

  //! WaterVolume.hlsli's VolumeAttenuation of the linear colour v and the density fDensity.
  inline void Attenuation(const float v[3], float fDensity, float c[3])
  {
    const float Y = 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
    for (int i = 0; i < 3; i++)
      c[i] = std::max(Y > 0 ? fDensity * (Y / std::max(v[i], Y / MOST_RATIO)) : fDensity, PURE_WATER[i]);
  }

  //! WaterVolume.hlsli's VolumeRefractedCos.
  inline float RefractedCos(float fCos)
  {
    return std::sqrt(1 - std::clamp(1 - fCos * fCos, 0.0f, 1.0f) / (ETA * ETA));
  }

  //! WaterVolume.hlsli's SnellWindow.
  inline float SnellWindow(float fCos)
  {
    const float ci = std::clamp(fCos, 0.0f, 1.0f), s2 = ETA * ETA * (1 - ci * ci);
    if (s2 >= 1)
      return 0;
    const float ct = std::sqrt(1 - s2);
    const float rs = (ETA * ci - ct) / (ETA * ci + ct), rp = (ci - ETA * ct) / (ci + ETA * ct);
    return 1 - 0.5f * (rs * rs + rp * rp);
  }

  //! The attenuation of the water of the fog volume's colour vFogVolume (the game's) and density fDensity.
  inline void FogAttenuation(const float vFogVolume[3], float fDensity, float c[3])
  {
    const float vDeep[3] = { Decode(vFogVolume[0]), Decode(vFogVolume[1]), Decode(vFogVolume[2]) };
    Attenuation(vDeep, fDensity, c);
  }

  //! Underwater.hlsli's UnderDownLight: the light coming down just under the surface (and as much again back up), the sun's
  //! colour vSunColor (the game's: its light twice it), the sky's the fog's colour vSky (the game's), the sun's direction's
  //! height fSunZ (of a unit vector).
  inline void DownLight(const float vSunColor[3], const float vSky[3], float fSunZ, float vDown[3])
  {
    const float fCos = std::clamp(fSunZ, 0.0f, 1.0f), fWater = RefractedCos(fCos), fSun = SnellWindow(fWater) * fCos / fWater;
    for (int i = 0; i < 3; i++)
      vDown[i] = (1 + CLEAR_UP) * 0.25f * (Decode(2 * vSunColor[i]) * fSun + SKY_IN * Decode(vSky[i]));
  }

  //! The light the water of the fog volume's colour vFogVolume (the game's) and density fDensity scatters a metre at the
  //! surface: Underwater.hlsli's UnderClearSource of UnderDownLight (DownLight's arguments).
  inline void ClearSource(const float vFogVolume[3], float fDensity, const float vSunColor[3], const float vSky[3], float fSunZ,
                          float vSource[3])
  {
    float c[3], vDown[3], vMore[3];
    FogAttenuation(vFogVolume, fDensity, c);
    DownLight(vSunColor, vSky, fSunZ, vDown);
    for (int i = 0; i < 3; i++)
      vMore[i] = std::max(c[i] - PURE_WATER[i] - PURE_SCATTERING[i], 0.0f);
    for (int i = 0; i < 3; i++)
      vSource[i] = (PURE_SCATTERING[i] + std::min(vMore[1] * PARTICLE_SPECTRUM[i], vMore[i])) * vDown[i];
  }

  //! The eye's white balance at the depth fDepth (m) under the surface of the water of attenuation c, the light coming down
  //! just under the surface vDown (DownLight): each channel's factor vGain, the light there (vDown exp(-c fDepth)) taken
  //! towards vDown's colour by fDegree (0 none, 1 all of it: von Kries' factors vDown / that light, raised to fDegree),
  //! its luminance kept (the exposure as without). The factors at most BALANCE_MOST apart, the degree less where they would
  //! be more (deeper than 5.7 m under Fort's water: at 20 m red's would be 53 times, blue's 0.43; a lamp, a fire or the sky in
  //! Snell's window there red), as an eye's adaptation goes only so far: the deep water bluer again. Without the light (no
  //! sun, no sky) all 1.
  inline void Balance(const float c[3], const float vDown[3], float fDepth, float fDegree, float vGain[3])
  {
    const float Y[3] = { 0.2126f, 0.7152f, 0.0722f };
    const float z = std::max(fDepth, 0.0f);
    const float fSpread = (std::max({ c[0], c[1], c[2] }) - std::min({ c[0], c[1], c[2] })) * z;
    float d = std::clamp(fDegree, 0.0f, 1.0f);
    if (d * fSpread > std::log(BALANCE_MOST))
      d = std::log(BALANCE_MOST) / fSpread;
    float fBefore = 0, fAfter = 0;
    for (int i = 0; i < 3; i++)
    {
      const float fLight = vDown[i] * std::exp(-c[i] * z);
      vGain[i] = std::exp(d * c[i] * z);
      fBefore += Y[i] * fLight;
      fAfter += Y[i] * fLight * vGain[i];
    }
    const float k = fBefore > 0 && fAfter > 0 ? fBefore / fAfter : 0;
    for (int i = 0; i < 3; i++)
      vGain[i] = k > 0 ? vGain[i] * k : 1;
  }

  //! The eye's exposure under the water at the depth fDepth (m): fEV more than above it (2^fEV times the light), coming in
  //! over the first EXPOSURE_DEPTH under the surface (none at it: the view through the surface the same either side).
  inline float UnderExposure(float fEV, float fDepth)
  {
    return std::exp2(fEV * std::clamp(fDepth / EXPOSURE_DEPTH, 0.0f, 1.0f));
  }
}

#endif
