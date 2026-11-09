// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayBehindTests.cpp
//	Description: Unit tests of why a frame gathers what is behind, above
//	and below the camera for its scene for rays (RayBehind.h,
//	UpdateRayBehind): the path tracer, the volumetric light's sun by rays
//	without a sun, the lamps' shadows by rays (d3d12_RTLightBehind).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "RayBehind.h"

using namespace RayBehind;

XR_TEST(RayBehind_Lamps)
{
  // Training's opening cut scene with the lamps' shadows by rays and no sun's rays: gathered for the lamps; with
  // d3d12_RTLightBehind 0 not at all, as before.
  CHECK(Reasons(false, false, false, true, true) == LAMPS);
  CHECK(Reasons(false, false, false, true, false) == 0);
  // No draw traced rays towards its lamps the frame before: none for them.
  CHECK(Reasons(false, false, false, false, true) == 0);
  // Outdoors in the sun the lamps still want it (what shades them is not towards the sun).
  CHECK(Reasons(false, false, true, true, true) == LAMPS);
}

XR_TEST(RayBehind_PathsAndVolumetric)
{
  // The path tracer always; the volumetric light's sun by rays only where the frame has no sun.
  CHECK(Reasons(true, false, true, false, false) == PATHS);
  CHECK(Reasons(false, true, false, false, false) == VOLUMETRIC);
  CHECK(Reasons(false, true, true, false, false) == 0);
  CHECK(Reasons(true, true, false, true, true) == (PATHS | VOLUMETRIC | LAMPS));
  CHECK(Reasons(false, false, false, false, false) == 0);
}
