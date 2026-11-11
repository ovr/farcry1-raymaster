// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FogVolumeTests.cpp
//	Description: Unit tests of a fog volume's fog from above and of the
//	water volumes tinted by their material (FogVolume.h,
//	d3d12_FogVolumeDepth, d3d12_WaterVolumeColor): the bunker pool's
//	dense volume hiding its floor seen through its water, the dry floor
//	under the layer with the gate alone (the cave of Training's first
//	cut scene), the gate alone where the depth's fog is less (Swamp's
//	thin volume seen from a few metres up), never less fog than the gate
//	nor more than the whole way's, the templates.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "FogVolume.h"

#include <algorithm>
#include <cmath>

using namespace FogVolume;

XR_TEST(FogVolume_DensePool)
{
  // Training's bunker pool: fog distance 0.1 m. Its floor 1 m down, 6 m off, the camera 1.5 m above the water.
  const float k = Density(0.1f);
  CHECK(std::fabs(k - logf(100.0f) / 0.2f) < 1e-4f);
  const float fGateOnly = Alpha(1.5f, 1, 5.8f, k);
  CHECK(fGateOnly > 0.3f && fGateOnly < 0.5f); // the floor's tiles showed through
  CHECK(Total(1.5f, 1, 5.8f, k, true) > 0.999f);
  // 10 cm down nine tenths fog.
  CHECK(Total(1.5f, 0.1f, 5.8f, k, true) > 0.89f);
  // The two passes together are the larger of the gate's fog and the depth's.
  for (float fDepth : { 0.01f, 0.05f, 0.1f, 0.3f, 1.0f })
    CHECK(std::fabs(Total(1.5f, fDepth, 5.8f, k, true) - std::max(Alpha(1.5f, fDepth, 5.8f, k), 1 - expf(-fDepth * k))) < 1e-5f);
}

XR_TEST(FogVolume_DryFloor)
{
  // The cave under Training's first cut scene: a volume of 0.1 m whose layer reaches over the dry floor around the
  // rock, seen from 9.2 m up. No ray to that floor crosses the water: the gate alone, most of the floor still shows.
  const float k = Density(0.1f);
  for (float fDepth : { 0.3f, 0.6f, 1.0f })
  {
    CHECK(Total(9.2f, fDepth, 9.5f + fDepth, k, false) == Alpha(9.2f, fDepth, 9.5f + fDepth, k));
    CHECK(Total(9.2f, fDepth, 9.5f + fDepth, k, false) < 0.5f);
  }
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
        CHECK(ThroughWater(Alpha(fCamera, fDepth, fViewDepth, k), fDepth, k) == 0);
      }
  // Over the layer nothing, either way.
  CHECK(Total(3, 0, 10, k, true) == 0 && Total(3, -1, 10, k, true) == 0);
}

XR_TEST(FogVolume_DepthBounds)
{
  // Through the water the fog is never less than the gate's, nor more than the fog of the whole way to the point.
  for (float fDist : { 0.1f, 1.0f, 30.0f })
  {
    const float k = Density(fDist);
    for (float fCamera : { 0.1f, 1.0f, 5.0f })
      for (float fDepth : { 0.05f, 1.0f, 3.0f, 8.0f })
        for (float fStretch : { 1.0f, 3.0f, 20.0f })
        {
          const float fViewDepth = (fCamera + fDepth) * fStretch;
          const float a0 = Alpha(fCamera, fDepth, fViewDepth, k), a1 = Total(fCamera, fDepth, fViewDepth, k, true);
          CHECK(a1 >= a0 - 1e-6f); // 1 - (1 - a) rounds
          CHECK(a1 <= 1 - expf(-fViewDepth * k) + 1e-6f);
        }
  }
}

// Training's bunker pool and the pool in its cave share a layer (20.46 m): each water takes the volume of the objects by
// it, not the ocean's nor a volume at another height.
XR_TEST(FogVolume_WaterPick)
{
  CWaterVolumePick Cave(455, 1060, 465, 1075, 20.47f);
  Cave.Add(2, 20.4643f, false, 314, 1003);   // the bunker's walls
  Cave.Add(1, 16, true, 458, 1066);          // the ocean
  Cave.Add(4, 31.44f, false, 460, 1068);     // another height
  Cave.Add(5, 20.4643f, false, 452, 1070);   // the cave's floor
  Cave.Add(3, 20.4643f, false, 330, 1010);
  CHECK(Cave.Best() == 5);
  CWaterVolumePick Bunker(305, 995, 325, 1012, 20.47f);
  Bunker.Add(5, 20.4643f, false, 452, 1070);
  Bunker.Add(2, 20.4643f, false, 312.25f, 1008.88f);
  Bunker.Add(0, 20.4643f, false, 312, 1000);
  CHECK(Bunker.Best() == 2);
  CWaterVolumePick None(0, 0, 1, 1, 20);
  None.Add(1, 20, true, 0, 0);
  CHECK(None.Best() == 0);
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
