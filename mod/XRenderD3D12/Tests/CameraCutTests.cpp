// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CameraCutTests.cpp
//	Description: Unit tests of the main view's camera cuts (d3d12_CutReset,
//	CameraCut.h): Training's opening cut scene's cuts are cuts, its camera's
//	moves between them and a player's are not.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "CameraCut.h"

#include <cmath>

namespace
{
  //! The direction of the game's camera angles (degrees: x up, z around the vertical) as the renderer's view matrix
  //! has it, of unit length.
  void Dir(float ax, float az, float d[3])
  {
    const float x = ax * 3.14159265f / 180, z = az * 3.14159265f / 180;
    d[0] = -sinf(z) * cosf(x);
    d[1] = cosf(z) * cosf(x);
    d[2] = sinf(x);
  }

  bool Cut(float x0, float y0, float z0, float ax0, float az0, float x, float y, float z, float ax, float az)
  {
    const float p0[3] = { x0, y0, z0 }, p[3] = { x, y, z };
    float d0[3], d[3];
    Dir(ax0, az0, d0);
    Dir(ax, az, d);
    return CameraCut::IsCut(p0, d0, p, d);
  }
}

XR_TEST(CameraCut_TrainingIntro)
{
  // Its three cuts (the frame log of the film's shot training-intro: frames 509, 1059 and 1148).
  CHECK(Cut(459.4f, 1068.3f, 28.9f, 82, 73, 298.8f, 990.2f, 20.7f, -60, 41));
  CHECK(Cut(299.9f, 989.5f, 20.8f, -3, 32, 301.7f, 988.0f, 20.6f, 12, -98));
  CHECK(Cut(301.7f, 988.0f, 20.6f, -12, -105, 298.2f, 988.4f, 22.8f, 0, 92));
  // Its camera between them: a few centimetres and less than a degree a frame.
  CHECK(!Cut(299.7f, 989.6f, 20.8f, -5, 34, 299.8f, 989.6f, 20.8f, -5, 33));
  CHECK(!Cut(301.7f, 988.0f, 20.6f, 11, -99, 301.7f, 988.0f, 20.6f, 9, -99));
}

XR_TEST(CameraCut_Limits)
{
  // A vehicle at 120 km/h at 15 frames a second (2.2 m), a fast mouse flick (30 degrees): no cut.
  CHECK(!Cut(0, 0, 10, 0, 0, 2.2f, 0, 10, 0, 0));
  CHECK(!Cut(0, 0, 10, 0, 0, 0, 0, 10, 0, 30));
  CHECK(!Cut(0, 0, 10, 0, 0, 0, 0, 10, 30, 0));
  // Past the step or the turn: a cut; a teleport across the level too.
  CHECK(Cut(0, 0, 10, 0, 0, 4.5f, 0, 10, 0, 0));
  CHECK(Cut(0, 0, 10, 0, 0, 0, 0, 10, 0, 50));
  CHECK(Cut(0, 0, 10, 0, 0, 0, 0, 10, -50, 0));
  CHECK(Cut(0, 0, 10, 0, 0, 900, 500, 40, 0, 0));
  // The same camera: no cut.
  CHECK(!Cut(1, 2, 3, 10, 20, 1, 2, 3, 10, 20));
}
