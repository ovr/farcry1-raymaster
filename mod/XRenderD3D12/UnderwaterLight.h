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
//	objects under the water still work out in their program.
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

  //! The light the water of the fog volume's colour vFogVolume (the game's) and density fDensity scatters a metre at the
  //! surface: Underwater.hlsli's UnderClearSource of UnderDownLight, the sun's colour vSunColor (the game's: its light
  //! twice it), the sky's the fog's colour vSky (the game's), the sun's direction's height fSunZ (of a unit vector).
  inline void ClearSource(const float vFogVolume[3], float fDensity, const float vSunColor[3], const float vSky[3], float fSunZ,
                          float vSource[3])
  {
    const float vDeep[3] = { Decode(vFogVolume[0]), Decode(vFogVolume[1]), Decode(vFogVolume[2]) };
    float c[3];
    Attenuation(vDeep, fDensity, c);
    const float fCos = std::clamp(fSunZ, 0.0f, 1.0f), fWater = RefractedCos(fCos), fSun = SnellWindow(fWater) * fCos / fWater;
    float vMore[3];
    for (int i = 0; i < 3; i++)
      vMore[i] = std::max(c[i] - PURE_WATER[i] - PURE_SCATTERING[i], 0.0f);
    for (int i = 0; i < 3; i++)
    {
      const float fDown = (1 + CLEAR_UP) * 0.25f * (Decode(2 * vSunColor[i]) * fSun + SKY_IN * Decode(vSky[i]));
      vSource[i] = (PURE_SCATTERING[i] + std::min(vMore[1] * PARTICLE_SPECTRUM[i], vMore[i])) * fDown;
    }
  }
}

#endif
