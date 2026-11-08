// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SkyFogBand.h
//	Description: The fog over the lower part of the sky box
//	(d3d12_SkyFogBand), as the D3D9 renderer draws it after the box (its
//	draws, seen on Swamp and Volcano with the camera at several heights
//	and the fog's end changed by DemoSkyFog over a fixed camera): with the
//	frame's fog ending before 256 m, a band in the fog's colour on a ring
//	of radius 32 about the camera (the box's half size), opaque from 32
//	below its edge up to the edge and fading out over the 16 above. The
//	edge goes down by 1 / 6 per metre of the fog's end (from 64 m on; a
//	nearer end puts it where 64 m would) and by 1 / 35 per metre of the
//	camera's height over z = 0 (a camera below, as the mirrored one of the
//	water's reflection, as one at 0). The fog's start, the far plane, the
//	water level and the sky box's stretching and alpha do not move it.
//	Draw3D.cpp's DrawSkyItem draws it; Shaders/SkyFogBand.hlsli has the
//	same for the sky the water reflects; the unit tests (SkyTests.cpp)
//	hold both to the D3D9 renderer's values.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_SKYFOGBAND_H
#define XRENDERD3D12_SKYFOGBAND_H

#include <algorithm>

namespace SkyFogBand
{
  constexpr float RADIUS = 32;        //!< the ring's radius (the sky box's half size)
  constexpr float FADE = 16;          //!< the height over the edge it fades out over
  constexpr float BELOW = 32;         //!< how far it goes down from its edge (opaque)
  constexpr int SEGMENTS = 12;        //!< the ring's segments, of 30 degrees from the y axis on (towards -x)
  constexpr float FOG_END_MAX = 256;  //!< drawn with the frame's fog ending before this (m)
  constexpr float FOG_END_MIN = 64;   //!< a nearer fog's end puts the edge where this one does (m)

  //! Whether the band is drawn with the frame's fog ending at fFogEnd (m).
  inline bool Drawn(float fFogEnd)
  {
    return fFogEnd < FOG_END_MAX;
  }

  //! The z of its edge on the ring (the box's axes about the camera), the frame's fog ending at fFogEnd, the camera at
  //! height fCameraZ.
  inline float Edge(float fFogEnd, float fCameraZ)
  {
    return (160 - std::max(fFogEnd, FOG_END_MIN)) / 6 - std::max(fCameraZ, 0.0f) / 35;
  }
}

#endif
