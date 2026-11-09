// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SpriteOrderTests.cpp
//	Description: Unit tests of the order of the scene's sprites
//	(SpriteOrder.h, d3d12_SpriteSort): back to front by the distance of
//	the centre of each one's vertices, those at the same distance as the
//	engine gave them; off, as the engine gave them. The case of Volcano's
//	lava: smoke and heat haze of two emitters given one emitter after the
//	other come out one after the other by their distance.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "SpriteOrder.h"

#include <cmath>
#include <vector>

namespace
{
  //! A camera facing quad of half side h about (x, y, z), its vertices 5 floats apart (position, then two more).
  std::vector<float> Quad(float x, float y, float z, float h)
  {
    return { x - h, y, z - h, 0, 0, x + h, y, z - h, 1, 0, x + h, y, z + h, 1, 1, x - h, y, z + h, 0, 1 };
  }
}

XR_TEST(SpriteOrder_CentreDistance)
{
  // The centre of a quad, not a corner; none without vertices.
  const std::vector<float> q = Quad(3, 4, 12, 2.5f);
  CHECK(std::fabs(SpriteOrder::CentreDistance2(q.data(), 4, 5, 0, 0, 0) - 169.0f) < 1e-3f);
  CHECK(std::fabs(SpriteOrder::CentreDistance2(q.data(), 4, 5, 3, 4, 12)) < 1e-6f);
  CHECK(SpriteOrder::CentreDistance2(q.data(), 0, 5, 1, 1, 1) == 0);
  CHECK(SpriteOrder::CentreDistance2(nullptr, 4, 5, 1, 1, 1) == 0);
  // A far one: the sum in doubles (Volcano's world positions are in the hundreds of metres).
  const std::vector<float> f = Quad(513.25f, 1433.5f, 63.25f, 4);
  CHECK(std::fabs(SpriteOrder::CentreDistance2(f.data(), 4, 5, 492.4f, 1378.0f, 63.25f) -
                  (20.85f * 20.85f + 55.5f * 55.5f)) < 0.05f);
}

XR_TEST(SpriteOrder_BackToFront)
{
  // Given emitter by emitter: haze A (60, 50 m), smoke A (55 m), haze B (92, 40 m), smoke B (45 m).
  const std::vector<float> Dist = { 3600, 2500, 3025, 8464, 1600, 2025 };
  std::vector<int> Order;
  SpriteOrder::BackToFront(Dist, true, Order);
  CHECK(Order == std::vector<int>({ 3, 0, 2, 1, 5, 4 }));
  // Off: as given.
  SpriteOrder::BackToFront(Dist, false, Order);
  CHECK(Order == std::vector<int>({ 0, 1, 2, 3, 4, 5 }));
  // The same distance: as given (a burst of new particles at one point keeps the engine's order).
  const std::vector<float> Same = { 4, 9, 4, 9, 4 };
  SpriteOrder::BackToFront(Same, true, Order);
  CHECK(Order == std::vector<int>({ 1, 3, 0, 2, 4 }));
  // None.
  SpriteOrder::BackToFront({}, true, Order);
  CHECK(Order.empty());
}
