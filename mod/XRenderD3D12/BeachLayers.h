// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: BeachLayers.h
//	Description: The layers of the beach (eDC_WaterBeach, SetupBeachDraw
//	in Water.cpp): the foam running up the sand and the wet sand under it,
//	as the original's shore shaders lay them. A level's mission names its
//	shore shader (Shaders Shore): TerrainWaterBeach (Training, Carrier,
//	Fort, Pier, ...), drawn with its technique with the refraction (three
//	waves of textures/wave1, then the wet sand of water2), or
//	TerrainWaterBeachResearch (Steam, Research, Bunker, Dam, Rebellion,
//	Archive), which has only the technique without it: two waves of wave2
//	(wave1's picture, its alpha 0.4 of wave1's), water1's faint detail
//	(u halved, not clamped), the wet sand (d3d12_BeachResearch; before it
//	the second was not drawn at all). The unit tests (BeachTests.cpp) hold
//	the passes to that.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_BEACHLAYERS_H
#define XRENDERD3D12_BEACHLAYERS_H

#include <string_view>

namespace BeachLayers
{
  constexpr int WAVES = 3;   //!< the waves of foam drawn at a time; pass WAVES is the wet sand

  enum class ETexture
  {
    Wave1,    //!< textures/wave1: TerrainWaterBeach's waves
    Wave2,    //!< textures/wave2: TerrainWaterBeachResearch's
    Water1,   //!< textures/water1: its detail layer
    Water2,   //!< textures/water2: the wet sand of both
  };

  struct SLayer
  {
    ETexture eTexture;
    float fU;      //!< u times this (the strip's u runs 0 to 1 and back between its cross sections)
    bool bClamp;   //!< the texture clamped (the waves), else wrapped
  };

  //! Whether the shore shader of this lower case name is TerrainWaterBeachResearch's fainter beach.
  inline bool IsFaint(std::string_view Name) { return Name == "terrainwaterbeachresearch"; }

  //! The layer of pass nPass (0 to WAVES) of the beach, bFaint: TerrainWaterBeachResearch's.
  inline SLayer Layer(int nPass, bool bFaint)
  {
    if (nPass >= WAVES)
      return { ETexture::Water2, 1.25f, false };
    if (!bFaint)
      return { ETexture::Wave1, 1.0f, true };
    if (nPass == WAVES - 1)
      return { ETexture::Water1, 0.5f, false };
    return { ETexture::Wave2, 1.0f, true };
  }
}

#endif
