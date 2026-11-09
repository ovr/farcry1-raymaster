// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterBodiesTests.cpp
//	Description: Unit tests of the level's water bodies by its heights
//	(WaterBodies.h, d3d12_WaterFoamPonds): an island's sea and the pond
//	inside it, a pond joined to the sea by a channel (sea), the land's
//	share by its nearest water, a level without water, a level all
//	under it.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "WaterBodies.h"

#include <vector>

using namespace WaterBodies;

namespace
{
  //! An island of n x n heights: the sea (-5) outside the square [a, b), the land (+3) inside it.
  std::vector<float> Island(int n, int a, int b)
  {
    std::vector<float> H((size_t)n * n, -5.0f);
    for (int y = a; y < b; y++)
      for (int x = a; x < b; x++)
        H[(size_t)y * n + x] = 3.0f;
    return H;
  }
}

XR_TEST(WaterBodies_IslandPond)
{
  // A 40 x 40 map: the island 5..35, a pond 0.3 m deep at 15..25 inside it.
  const int n = 40;
  std::vector<float> H = Island(n, 5, 35);
  for (int y = 15; y < 25; y++)
    for (int x = 15; x < 25; x++)
      H[(size_t)y * n + x] = -0.3f;
  std::vector<float> S;
  const SStats s = OpenShare(H.data(), n, 0, S);
  CHECK(s.nBodies == 1);
  CHECK(s.nPond == 100);
  CHECK(s.nLargest == 100);
  CHECK(s.nSea == n * n - 30 * 30);
  CHECK(S[0] == 1);                    // the sea at the corner
  CHECK(S[20 * n + 20] == 0);          // the pond
  CHECK(S[20 * n + 13] == 0);          // its bank, 2 from it and 8 from the sea
  CHECK(S[20 * n + 6] == 1);           // the beach, 2 from the sea
  CHECK(S[15 * n + 15] == 0);          // the pond's corner: its share, not the sea's, along the shore
}

XR_TEST(WaterBodies_Channel)
{
  // The pond joined to the sea by a channel one height wide (diagonal steps too): the sea's.
  const int n = 40;
  std::vector<float> H = Island(n, 5, 35);
  for (int y = 15; y < 25; y++)
    for (int x = 15; x < 25; x++)
      H[(size_t)y * n + x] = -0.3f;
  for (int x = 5; x < 15; x++)
    H[(size_t)(20 + (x & 1)) * n + x] = -0.2f; // a zigzag: some steps only diagonal
  std::vector<float> S;
  const SStats s = OpenShare(H.data(), n, 0, S);
  CHECK(s.nBodies == 0);
  CHECK(s.nPond == 0);
  CHECK(S[20 * n + 20] == 1);
}

XR_TEST(WaterBodies_NoWater)
{
  const int n = 16;
  std::vector<float> H((size_t)n * n, 2.0f), S;
  const SStats s = OpenShare(H.data(), n, 0, S);
  CHECK(s.nSea == 0 && s.nPond == 0 && s.nBodies == 0);
  for (const float f : S)
    CHECK(f == 1);
}

XR_TEST(WaterBodies_AllSea)
{
  // All under the level, a hill in the middle: the sea everywhere, the hill too (its nearest water the sea).
  const int n = 16;
  std::vector<float> H((size_t)n * n, -1.0f), S;
  H[8 * n + 8] = 4.0f;
  const SStats s = OpenShare(H.data(), n, 0, S);
  CHECK(s.nSea == n * n - 1 && s.nBodies == 0);
  for (const float f : S)
    CHECK(f == 1);
}

XR_TEST(WaterBodies_TwoPonds)
{
  // Two ponds on the island, apart by land: two bodies, the land between them a pond's.
  const int n = 40;
  std::vector<float> H = Island(n, 5, 35), S;
  for (int y = 10; y < 14; y++)
    for (int x = 10; x < 14; x++)
      H[(size_t)y * n + x] = H[(size_t)(y + 12) * n + x + 12] = -0.5f;
  const SStats s = OpenShare(H.data(), n, 0, S);
  CHECK(s.nBodies == 2);
  CHECK(s.nPond == 32);
  CHECK(s.nLargest == 16);
  CHECK(S[17 * n + 17] == 0);
}
