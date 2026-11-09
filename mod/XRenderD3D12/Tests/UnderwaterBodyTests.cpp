// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: UnderwaterBodyTests.cpp
//	Description: Unit tests of the bodies the water looks past
//	(UnderwaterBody.h, d3d12_UnderwaterBodies): a character under the
//	ocean's level, the camera above it. Steam's fish under its level
//	42 m, their backs over it; a fish jumping; a swimmer's head out of
//	the water; a rock; the camera under the surface; the span of a
//	character's skinned vertices.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "UnderwaterBody.h"

using UnderwaterBody::LooksPast;
using UnderwaterBody::SSpan;
using UnderwaterBody::ZSpan;

XR_TEST(UnderwaterBody_LooksPast)
{
  // A fish (0.4 m) under Steam's sea (level 42), the camera on the shore.
  CHECK(LooksPast(true, SSpan{ 40.1f, 40.5f }, 42, 45));
  // Its back over the level under the waves' crests (Steam's 42.13, 42.26): its middle under it.
  CHECK(LooksPast(true, SSpan{ 41.4f, 42.13f }, 42, 45));
  CHECK(LooksPast(true, SSpan{ 41.6f, 42.26f }, 42, 45));
  // A fish jumping, its middle over the level: kept.
  CHECK(!LooksPast(true, SSpan{ 41.9f, 42.5f }, 42, 45));
  // A swimmer (1.8 m), the head out of the water: kept; a body sunk 2 m: past.
  CHECK(!LooksPast(true, SSpan{ 40.5f, 42.3f }, 42, 45));
  CHECK(!LooksPast(true, SSpan{ 40.2f, 41.97f }, 42, 45));
  CHECK(LooksPast(true, SSpan{ 38.2f, 40.0f }, 42, 45));
  // Not a character (a rock, a post, a plant): kept, the shore's foam round it.
  CHECK(!LooksPast(false, SSpan{ 40.1f, 40.5f }, 42, 45));
  // The camera under the surface: kept (the light in the water takes the depth of what is in it).
  CHECK(!LooksPast(true, SSpan{ 40.1f, 40.5f }, 42, 41));
  CHECK(!LooksPast(true, SSpan{ 40.1f, 40.5f }, 42, 42));
  // No span (no vertices): kept.
  CHECK(!LooksPast(true, SSpan{}, 42, 45));
}

XR_TEST(UnderwaterBody_ZSpan)
{
  // Two vertices of 32 bytes (position, normal, texture coordinates), a fish 1.2 m long at (10, 20, 40).
  struct V
  {
    float xyz[3], n[3], st[2];
  } Verts[2] = { { { 0, 0, -0.1f }, {}, {} }, { { 1.2f, 0, 0.3f }, {}, {} } };
  const float Moved[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 40, 1 };
  SSpan s = ZSpan(Verts, 2, sizeof(V), Moved);
  CHECK_NEAR(s.fBottom, 39.9f, 1e-5f);
  CHECK_NEAR(s.fTop, 40.3f, 1e-5f);
  // Turned nose up by 90 degrees about y (x goes to z): its tail end 1.2 m higher.
  const float Up[16] = { 0, 0, 1, 0, 0, 1, 0, 0, -1, 0, 0, 0, 10, 20, 40, 1 };
  s = ZSpan(Verts, 2, sizeof(V), Up);
  CHECK_NEAR(s.fBottom, 40.0f, 1e-5f);
  CHECK_NEAR(s.fTop, 41.2f, 1e-5f);
  // No vertices, too short a stride: none.
  CHECK(!(ZSpan(Verts, 0, sizeof(V), Moved).fBottom <= ZSpan(Verts, 0, sizeof(V), Moved).fTop));
  CHECK(!(ZSpan(nullptr, 2, sizeof(V), Moved).fBottom <= ZSpan(nullptr, 2, sizeof(V), Moved).fTop));
  CHECK(!(ZSpan(Verts, 2, 8, Moved).fBottom <= ZSpan(Verts, 2, 8, Moved).fTop));
}
