// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FogVolumeTests.cpp
//	Description: Unit tests of a fog volume's fog from above and of the
//	water volumes tinted by their material (FogVolume.h,
//	d3d12_FogVolumeDepth, d3d12_WaterVolumeColor): the bunker pool's
//	dense volume hiding its floor near its surface, the gate alone where
//	the depth's fog is less (Swamp's thin volume seen from a few metres
//	up), never less fog than before nor more than the whole way's, the
//	templates.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "FogVolume.h"

#include <cmath>

using namespace FogVolume;

XR_TEST(FogVolume_DensePool)
{
  // Training's bunker pool: fog distance 0.1 m. Its floor 1 m down, 6 m off, the camera 1.5 m above the water.
  const float k = Density(0.1f);
  CHECK(std::fabs(k - logf(100.0f) / 0.2f) < 1e-4f);
  const float fGateOnly = Alpha(1.5f, 1, 5.8f, k, false);
  CHECK(fGateOnly > 0.3f && fGateOnly < 0.5f); // the floor's tiles showed through
  CHECK(Alpha(1.5f, 1, 5.8f, k, true) > 0.999f);
  // 10 cm down nine tenths fog.
  CHECK(Alpha(1.5f, 0.1f, 5.8f, k, true) > 0.89f);
}

XR_TEST(FogVolume_GateKept)
{
  // A thin volume (Swamp's: 35 m) seen from 3 m up or more: the gate's fog is the larger wherever the point may be (its
  // view depth at least the camera's height over it), the depth's changes nothing. (From 2 m up the depth's is the larger
  // for the points a few centimetres under the layer right below the camera, by a third of a level.)
  const float k = Density(35);
  for (float fCamera : { 3.0f, 5.0f, 20.0f })
    for (float fDepth : { 0.05f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f })
      for (float fStretch : { 1.0f, 2.0f, 10.0f, 50.0f })
      {
        const float fViewDepth = (fCamera + fDepth) * fStretch;
        CHECK(Alpha(fCamera, fDepth, fViewDepth, k, true) == Alpha(fCamera, fDepth, fViewDepth, k, false));
      }
  // Over the layer nothing, either way.
  CHECK(Alpha(3, 0, 10, k, true) == 0 && Alpha(3, -1, 10, k, true) == 0);
}

XR_TEST(FogVolume_DepthBounds)
{
  // With the depth the fog is never less than without it, nor more than the fog of the whole way to the point.
  for (float fDist : { 0.1f, 1.0f, 30.0f })
  {
    const float k = Density(fDist);
    for (float fCamera : { 0.1f, 1.0f, 5.0f })
      for (float fDepth : { 0.05f, 1.0f, 3.0f, 8.0f })
        for (float fStretch : { 1.0f, 3.0f, 20.0f })
        {
          const float fViewDepth = (fCamera + fDepth) * fStretch;
          const float a0 = Alpha(fCamera, fDepth, fViewDepth, k, false), a1 = Alpha(fCamera, fDepth, fViewDepth, k, true);
          CHECK(a1 >= a0);
          CHECK(a1 <= 1 - expf(-fViewDepth * k) + 1e-6f);
        }
  }
}

XR_TEST(FogVolume_TintTemplates)
{
  // The templates the materials name, lower case as the shader manager has them.
  CHECK(TintsWater("watervolume"));
  CHECK(TintsWater("watervolumebumpreflcm"));
  CHECK(!TintsWater("terrainlake"));
  CHECK(!TintsWater("terrainwater"));
  CHECK(!TintsWater("lavavolume"));
  CHECK(!TintsWater(""));
}
