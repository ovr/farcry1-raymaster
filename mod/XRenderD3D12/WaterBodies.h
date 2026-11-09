// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterBodies.h
//	Description: The level's water bodies by its terrain's heights: the
//	open sea and the ponds and lakes the terrain cuts off from it
//	(d3d12_WaterFoamPonds; OceanGrid.cpp's EnsureGridTerrain keeps the
//	share in the second channel of the ocean grid's terrain texture,
//	Water.hlsl's GridOpenShare reads it).
//
//	The engine's water is one plane at the level's water level over the
//	whole level: a pond is a hollow of the terrain under that level, and
//	the ocean's waves, its whitecaps, its shore's foam and the swell's
//	surf drew in it as on the sea's beaches. Swamp's ponds, a few
//	decimetres deep everywhere, were white with the shore's foam from
//	bank to bank; the original draws them as still grey water. Wind
//	waves grow with the fetch: over a pond of a hundred metres they are
//	centimetres, and no swell reaches it.
//
//	A height below the level is water. Water joined to the map's edge
//	through water (its eight neighbours; past the edge lies the open
//	sea) is the sea, share 1; the rest, cut off by the terrain, is a
//	pond, share 0. The terrain above the level takes the share of its
//	nearest water (1 without any), so that between the texels the share
//	changes only on the land between two bodies, never along a shore.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <vector>

namespace WaterBodies
{
  struct SStats
  {
    int nSea = 0;     //!< heights of the open sea
    int nPond = 0;    //!< heights of the ponds and lakes cut off from it
    int nBodies = 0;  //!< the ponds and lakes
    int nLargest = 0; //!< the heights of the largest of them
  };

  //! The share of the open sea (Share, n x n) at the heights H (n x n, row y at H[y * n]) of a level whose water lies at
  //! fLevel: 1 on the sea, 0 on a pond, the nearest water's on the land.
  inline SStats OpenShare(const float *H, int n, float fLevel, std::vector<float> &Share)
  {
    enum : uint8_t { LAND, POND, SEA, SEEN };
    const size_t N = (size_t)n * n;
    std::vector<uint8_t> Kind(N, LAND);
    for (size_t i = 0; i < N; i++)
      if (H[i] < fLevel)
        Kind[i] = POND;
    std::vector<int> Q;
    Q.reserve(N);
    const auto Neighbours = [n](int i, auto &&f)
    {
      const int x = i % n, y = i / n;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
          if ((dx || dy) && x + dx >= 0 && x + dx < n && y + dy >= 0 && y + dy < n)
            f((y + dy) * n + x + dx);
    };
    // The sea: from the water on the map's edge.
    for (int k = 0; k < n; k++)
      for (const int i : { k, (n - 1) * n + k, k * n, k * n + n - 1 })
        if (Kind[i] == POND)
        {
          Kind[i] = SEA;
          Q.push_back(i);
        }
    for (size_t q = 0; q < Q.size(); q++)
      Neighbours(Q[q], [&](int j) { if (Kind[j] == POND) { Kind[j] = SEA; Q.push_back(j); } });
    SStats s;
    s.nSea = (int)Q.size();
    // The ponds: what water is left, body by body.
    for (size_t i = 0; i < N; i++)
      if (Kind[i] == POND)
      {
        Q.clear();
        Kind[i] = SEEN;
        Q.push_back((int)i);
        for (size_t q = 0; q < Q.size(); q++)
          Neighbours(Q[q], [&](int j) { if (Kind[j] == POND) { Kind[j] = SEEN; Q.push_back(j); } });
        s.nBodies++;
        s.nPond += (int)Q.size();
        s.nLargest = s.nLargest > (int)Q.size() ? s.nLargest : (int)Q.size();
      }
    // The land: its nearest water's share (breadth first from all of it).
    Share.assign(N, 1.0f);
    std::vector<uint8_t> Done(N, 0);
    Q.clear();
    for (size_t i = 0; i < N; i++)
      if (Kind[i] != LAND)
      {
        Share[i] = Kind[i] == SEA ? 1.0f : 0.0f;
        Done[i] = 1;
        Q.push_back((int)i);
      }
    for (size_t q = 0; q < Q.size(); q++)
    {
      const float f = Share[Q[q]];
      Neighbours(Q[q], [&](int j) { if (!Done[j]) { Done[j] = 1; Share[j] = f; Q.push_back(j); } });
    }
    return s;
  }
}
