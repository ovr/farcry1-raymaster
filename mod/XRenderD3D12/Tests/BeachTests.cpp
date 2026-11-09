// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: BeachTests.cpp
//	Description: Unit tests of the beach's layers (BeachLayers.h,
//	d3d12_BeachResearch): TerrainWaterBeach's waves of wave1 and its wet
//	sand stay as they were; TerrainWaterBeachResearch (Steam's shore, not
//	drawn at all before) lays its own: wave2 twice, water1's detail, the
//	wet sand.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "BeachLayers.h"

using namespace BeachLayers;

XR_TEST(Beach_ShoreShaders)
{
  // The shore shaders the missions name (Shaders Shore), lower case as the shader manager has them.
  CHECK(IsFaint("terrainwaterbeachresearch"));
  CHECK(!IsFaint("terrainwaterbeach"));
  CHECK(!IsFaint("terrainwater_onlysky"));
  CHECK(!IsFaint(""));
}

XR_TEST(Beach_Layers)
{
  // TerrainWaterBeach (its technique with the refraction): three waves of wave1, clamped, u as it is; the wet sand.
  for (int n = 0; n < WAVES; n++)
  {
    const SLayer L = Layer(n, false);
    CHECK(L.eTexture == ETexture::Wave1 && L.bClamp && L.fU == 1.0f);
  }
  const SLayer Wet = Layer(WAVES, false);
  CHECK(Wet.eTexture == ETexture::Water2 && !Wet.bClamp && Wet.fU == 1.25f);

  // TerrainWaterBeachResearch: wave2 twice (clamped), water1 with u halved and wrapped, the same wet sand.
  CHECK(Layer(0, true).eTexture == ETexture::Wave2 && Layer(0, true).bClamp);
  CHECK(Layer(1, true).eTexture == ETexture::Wave2 && Layer(1, true).bClamp);
  const SLayer Detail = Layer(2, true);
  CHECK(Detail.eTexture == ETexture::Water1 && !Detail.bClamp && Detail.fU == 0.5f);
  const SLayer WetFaint = Layer(WAVES, true);
  CHECK(WetFaint.eTexture == Wet.eTexture && WetFaint.bClamp == Wet.bClamp && WetFaint.fU == Wet.fU);
}
