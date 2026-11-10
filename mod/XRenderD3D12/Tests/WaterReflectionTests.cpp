// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterReflectionTests.cpp
//	Description: Unit tests of when the water's reflection is drawn again
//	(r_WaterUpdateMinInterval, WaterReflection.h).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "WaterReflection.h"

#include <cmath>

namespace
{
  const float FACTOR = 0.01f, MIN_INTERVAL = 1.0f / 120, MAX_MOVE = 2;
  const float MIN_COS = cosf(5 * 3.14159265f / 180);

  //! Due for a camera that stood still fAge seconds fToWater metres from the water.
  bool Still(float fAge, float fToWater, float fMinInterval = MIN_INTERVAL)
  {
    return WaterReflection::IsDue(fAge, fToWater, 0, 1, FACTOR, fMinInterval, MAX_MOVE, MIN_COS);
  }
}

XR_TEST(WaterReflection_MinInterval)
{
  // At the water: every frame up to 120 FPS (the demos' step of 0.02 s too), every other one at 200 FPS.
  CHECK(Still(1.0f / 60, 0));
  CHECK(Still(0.02f, 0));
  CHECK(!Still(1.0f / 200, 0));
  CHECK(Still(2.0f / 200, 0));
  // Without the floor, the engine's rule: every frame at the water.
  CHECK(Still(1.0f / 200, 0, 0));
  CHECK(!Still(0, 0, 0));
}

XR_TEST(WaterReflection_EngineInterval)
{
  // Farther away the engine's interval is the longer one: 0.01 s per metre, at most 0.3 s.
  CHECK(!Still(0.09f, 10));
  CHECK(Still(0.11f, 10));
  CHECK(!Still(0.29f, 100));
  CHECK(Still(0.31f, 100));
  CHECK(!Still(0.29f, 1000));
}

XR_TEST(WaterReflection_CameraMoves)
{
  // A move or a turn draws it at once, however young it is.
  CHECK(WaterReflection::IsDue(0, 100, 2.1f, 1, FACTOR, MIN_INTERVAL, MAX_MOVE, MIN_COS));
  CHECK(!WaterReflection::IsDue(0, 100, 1.9f, 1, FACTOR, MIN_INTERVAL, MAX_MOVE, MIN_COS));
  CHECK(WaterReflection::IsDue(0, 100, 0, cosf(6 * 3.14159265f / 180), FACTOR, MIN_INTERVAL, MAX_MOVE, MIN_COS));
  CHECK(!WaterReflection::IsDue(0, 100, 0, cosf(4 * 3.14159265f / 180), FACTOR, MIN_INTERVAL, MAX_MOVE, MIN_COS));
}
