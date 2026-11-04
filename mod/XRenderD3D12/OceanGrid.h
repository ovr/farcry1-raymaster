// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanGrid.h
//	Description: The ocean's grid (OceanGrid.cpp; d3d12_WaterGeometry; the
//	unit tests), on the CPU without the engine. The game draws the ocean as
//	one disc around the camera at the water level (its vertices in the
//	world, rings of segments about 10 degrees wide), the terrain hiding it
//	on land; in its place a polar grid around the camera of the same
//	extent, whose vertices Water.hlsl's vertex program raises to the waves'
//	height (Shaders/OceanGrid.hlsli).
//
//	The rings: XR_GRID_SPACING apart out to INNER, where XR_GRID_SEGMENTS
//	segments are that wide, then each a segment's width further than the
//	one before (the cells square: from above the water as wide on the
//	screen at every distance) out to XR_GRID_DISPLACED, then FAR_GROWTH
//	times as far each (nothing raised there, the water's extent alone) out
//	to OUTER. The vertex program pulls the vertices beyond the game's disc
//	onto its edge, so that the grid covers what the disc did.
//
//	The disc itself is read from its vertices each frame (ReadDisc): its
//	centre, its level, its rings and each ring's vertex alpha by segment
//	(the game's fade of the water towards the shore and with the distance),
//	into a table (DiscTable) the vertex program interpolates between them
//	(Shaders/OceanGrid.hlsli's GridDiscAlpha).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_OCEANGRID_H
#define XRENDERD3D12_OCEANGRID_H

#include "Shaders/Constants.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace OceanGrid
{
  //! The angle of a segment.
  constexpr double SEGMENT = 2 * 3.14159265358979323846 / XR_GRID_SEGMENTS;
  //! The radius where the segments are XR_GRID_SPACING wide (10.2 m).
  constexpr double INNER = XR_GRID_SPACING / SEGMENT;
  //! Beyond XR_GRID_DISPLACED each ring this much further out than the one before (as Shaders/OceanGrid.hlsli's
  //! GRID_FAR_GROWTH), out to OUTER (m: beyond any far plane).
  constexpr double FAR_GROWTH = 1.25;
  constexpr double OUTER = 20000;

  //! A vertex: as the game's disc's (VERTEX_FORMAT_P3F_COL4UB), at its place across the water from the grid's centre (m)
  //! on the rings of Rings(), z the index of its ring (the vertex program takes its radius from the frame's, FrameRings),
  //! the disc's colour.
  struct SVertex
  {
    float x, y, z;
    uint32_t nColor;
  };

  //! The rings' radii (m), the first 0: the centre.
  inline std::vector<double> Rings()
  {
    std::vector<double> R = { 0 };
    const int nInner = (int)std::floor(INNER / XR_GRID_SPACING);
    for (int i = 1; i <= nInner; i++)
      R.push_back(i * XR_GRID_SPACING);
    double r = R.back();
    while (r < XR_GRID_DISPLACED)
      R.push_back(r *= 1 + SEGMENT);
    while (r < OUTER)
      R.push_back(r *= FAR_GROWTH);
    return R;
  }

  //! The grid's spacing at the radius r (m), the larger of its rings' and its segments' (as Shaders/OceanGrid.hlsli's
  //! GridSpacing).
  inline double Spacing(double r)
  {
    return r < XR_GRID_DISPLACED ? std::max((double)XR_GRID_SPACING, r * SEGMENT) : r * (FAR_GROWTH - 1);
  }

  //! A frame's rings seen from fHeight (m) over (or under) the level, fPixelsPerRadian pixels a radian across the
  //! screen's height: each Rings()'s of its index or further out, so that a cell's rise on the screen over a ring's
  //! spacing is at least XR_GRID_PIXELS pixels where the cells turned towards the camera rise most, the flat water's H / r
  //! and GRID_FACING of the waves' slope fSlope (the RMS along an axis of those the grid carries far off: the long
  //! cascade's, as drawn): a ring at r at least XR_GRID_PIXELS r^2 / (P (H + GRID_FACING s r)) further than the one before.
  //! Square cells far off rose a pixel or less on the screen from a few metres over the sea (30 H / r pixels at
  //! 3840 x 2160): in each 2 x 2 pixels several thin triangles, each the water's program over all four. Out to OUTER (the
  //! vertex program pulls those beyond the disc onto its edge; the last rings there when the rest reach it sooner).
  constexpr double GRID_FACING = 0.4;
  inline void FrameRings(double fHeight, double fSlope, double fPixelsPerRadian, std::vector<float> &R, double fPixels = XR_GRID_PIXELS)
  {
    const std::vector<double> B = Rings();
    const double h = std::max(std::fabs(fHeight), 0.1), s = GRID_FACING * std::max(fSlope, 0.0);
    const double c = fPixelsPerRadian > 0 ? fPixels / fPixelsPerRadian : 0;
    R.resize(B.size());
    double r = 0;
    for (size_t k = 0; k < B.size(); k++)
    {
      if (k > 0)
        r = std::min(std::max(B[k], r + c * r * r / (h + s * r)), OUTER);
      R[k] = (float)r;
    }
  }

  //! The vertices (the centre, then each ring's XR_GRID_SEGMENTS from +x counterclockwise) and a list of triangles: a fan
  //! around the centre, two a cell between the rings.
  inline void Build(uint32_t nColor, std::vector<SVertex> &V, std::vector<uint32_t> &I)
  {
    const std::vector<double> R = Rings();
    constexpr uint32_t S = XR_GRID_SEGMENTS;
    V.clear();
    I.clear();
    V.reserve(1 + (R.size() - 1) * S);
    I.reserve(3 * S * (2 * (R.size() - 1) - 1));
    V.push_back({ 0, 0, 0, nColor });
    for (size_t k = 1; k < R.size(); k++)
      for (uint32_t s = 0; s < S; s++)
        V.push_back({ (float)(R[k] * std::cos(s * SEGMENT)), (float)(R[k] * std::sin(s * SEGMENT)), (float)k, nColor });
    const auto At = [](size_t k, uint32_t s) { return (uint32_t)(1 + (k - 1) * S + s % S); };
    for (uint32_t s = 0; s < S; s++)
      I.insert(I.end(), { 0u, At(1, s), At(1, s + 1) });
    for (size_t k = 1; k + 1 < R.size(); k++)
      for (uint32_t s = 0; s < S; s++)
        I.insert(I.end(), { At(k, s), At(k + 1, s), At(k + 1, s + 1), At(k, s), At(k + 1, s + 1), At(k, s + 1) });
  }

  //! The game's disc as the grid takes it: its centre and level, its rings' radii (the first 0, the centre) and each ring's
  //! vertex alpha at XR_GRID_DISC_SEGMENTS segments from +x counterclockwise (the game's fade of the water towards the shore
  //! and with the distance and the camera's height, which Water.hlsl's program weighs its reflection by).
  struct SDisc
  {
    double fX = 0, fY = 0, fZ = 0;
    int nRings = 0;
    float fRadius[XR_GRID_DISC_RINGS] = {};
    float fAlpha[XR_GRID_DISC_RINGS][XR_GRID_DISC_SEGMENTS] = {};
  };

  //! The alpha of a vertex colour (VERTEX_FORMAT_P3F_COL4UB's: its top byte).
  inline float ColorAlpha(uint32_t nColor)
  {
    return (float)(nColor >> 24) / 255.0f;
  }

  //! The disc of the n vertices V into D: rings of vertices XR_GRID_DISC_SEGMENTS segments apart around a centre, all at
  //! one level, in any order (the game's a strip, some of them repeated: their colours alike). Its centre the middle of
  //! their box (the outer ring has vertices at 0, 90, 180 and 270 degrees); a ring all the vertices within 1 % (and 1 cm)
  //! of the radius of its nearest (the centre's within 1 cm of it, at 0); a segment without a vertex the alpha of the
  //! nearest one with one in its ring. False
  //! where they are no such disc (a vertex between two segments, above or below the others, too many rings).
  inline bool ReadDisc(const SVertex *V, size_t n, SDisc &D)
  {
    constexpr int S = XR_GRID_DISC_SEGMENTS;
    D = SDisc();
    if (!V || n < 2 * (size_t)S)
      return false;
    double x0 = V[0].x, x1 = x0, y0 = V[0].y, y1 = y0;
    for (size_t i = 0; i < n; i++)
    {
      if (V[i].z != V[0].z)
        return false;
      x0 = std::min(x0, (double)V[i].x);
      x1 = std::max(x1, (double)V[i].x);
      y0 = std::min(y0, (double)V[i].y);
      y1 = std::max(y1, (double)V[i].y);
    }
    D.fX = 0.5 * (x0 + x1);
    D.fY = 0.5 * (y0 + y1);
    D.fZ = V[0].z;
    std::vector<double> R(n);
    for (size_t i = 0; i < n; i++)
      R[i] = std::hypot(V[i].x - D.fX, V[i].y - D.fY);
    const auto Near = [](double a, double b) { return std::fabs(a - b) <= 0.01 * std::max(a, b) + 0.01; };
    std::vector<double> Sorted = R, Rings;
    std::sort(Sorted.begin(), Sorted.end());
    for (double r : Sorted)
      if (Rings.empty() || !Near(Rings.back(), r))
        Rings.push_back(r);
    if (Rings.size() < 2 || Rings.size() > (size_t)XR_GRID_DISC_RINGS)
      return false;
    float fSum[XR_GRID_DISC_RINGS][S] = {};
    int nCount[XR_GRID_DISC_RINGS][S] = {};
    for (size_t i = 0; i < n; i++)
    {
      size_t k = 0;
      for (size_t j = 1; j < Rings.size(); j++)
        if (std::fabs(R[i] - Rings[j]) < std::fabs(R[i] - Rings[k]))
          k = j;
      const float a = ColorAlpha(V[i].nColor);
      if (k == 0 && R[i] < 0.01)
      {
        for (int s = 0; s < S; s++)
          fSum[0][s] += a, nCount[0][s]++;
        continue;
      }
      const double u = std::atan2(V[i].y - D.fY, V[i].x - D.fX) / (2 * 3.14159265358979323846) * S;
      const double s = std::round(u);
      if (std::fabs(u - s) > 0.1)
        return false;
      const int nSegment = (((int)s % S) + S) % S;
      fSum[k][nSegment] += a;
      nCount[k][nSegment]++;
    }
    D.nRings = (int)Rings.size();
    for (int k = 0; k < D.nRings; k++)
    {
      D.fRadius[k] = k == 0 && Rings[0] < 0.01 ? 0.0f : (float)Rings[k];
      for (int s = 0; s < S; s++)
      {
        // The nearest segment of the ring with a vertex (each ring has some: the rings are its vertices' radii).
        int nFrom = -1;
        for (int d = 0; d <= S / 2 && nFrom < 0; d++)
          for (int e : { s + d, s - d })
            if (nFrom < 0 && nCount[k][((e % S) + S) % S] > 0)
              nFrom = ((e % S) + S) % S;
        D.fAlpha[k][s] = nFrom >= 0 ? fSum[k][nFrom] / nCount[k][nFrom] : 0.0f;
      }
    }
    return true;
  }

  //! The words of D's table (Shaders/OceanGrid.hlsli's GridDiscAlpha): its rings' radii, then each ring's
  //! XR_GRID_DISC_SEGMENTS alphas.
  inline size_t DiscTableSize(const SDisc &D)
  {
    return (size_t)D.nRings * (1 + XR_GRID_DISC_SEGMENTS);
  }

  inline void DiscTable(const SDisc &D, float *pTable)
  {
    for (int k = 0; k < D.nRings; k++)
    {
      pTable[k] = D.fRadius[k];
      std::copy(D.fAlpha[k], D.fAlpha[k] + XR_GRID_DISC_SEGMENTS, pTable + D.nRings + (size_t)k * XR_GRID_DISC_SEGMENTS);
    }
  }

  //! The deferred ocean's table of D's alpha (Shaders/OceanGrid.hlsli's GridDiscAlphaTable: a pixel's alpha from four of
  //! its words, where GridDiscAlpha looks for its ring among all of the disc's first): XR_GRID_ALPHA_ROWS rows at the
  //! radii XR_GRID_ALPHA_R0 (2^(j / AlphaScale) - 1), the last at the disc's last ring (AlphaScale rows a doubling of 1 +
  //! r / XR_GRID_ALPHA_R0: 22.6 out to 1244.5 m, 3 % apart far off, 1.5 cm at the centre), each
  //! XR_GRID_DISC_SEGMENTS + 1 alphas from +x counterclockwise, GridDiscAlpha's at the segments (the first again at the
  //! end: a segment and the next one load). Between them bilinear, as the forward grid's between its vertices (where the
  //! alpha bends at a ring between two rows, within a quarter of its slope's change times their spacing).
  inline double AlphaScale(const SDisc &D)
  {
    const double R = D.nRings > 0 ? D.fRadius[D.nRings - 1] : 0.0;
    return (XR_GRID_ALPHA_ROWS - 1) / std::log2(1 + std::max(R, 1e-3) / XR_GRID_ALPHA_R0);
  }

  inline size_t AlphaTableSize()
  {
    return (size_t)XR_GRID_ALPHA_ROWS * (XR_GRID_DISC_SEGMENTS + 1);
  }

  inline void AlphaTable(const SDisc &D, float *pTable)
  {
    constexpr int S = XR_GRID_DISC_SEGMENTS;
    const double fScale = AlphaScale(D);
    for (int j = 0; j < XR_GRID_ALPHA_ROWS; j++)
    {
      // GridDiscAlpha's rings about the radius: the last with a radius not over it (not the last ring) and the next.
      const double r = j + 1 < XR_GRID_ALPHA_ROWS ? XR_GRID_ALPHA_R0 * (std::exp2(j / fScale) - 1) : (double)D.fRadius[D.nRings - 1];
      int k0 = 0;
      for (int k = 1; k + 1 < D.nRings; k++)
        k0 = D.fRadius[k] <= r ? k : k0;
      const int k1 = std::min(k0 + 1, D.nRings - 1);
      const double t = std::clamp((r - D.fRadius[k0]) / std::max((double)D.fRadius[k1] - D.fRadius[k0], 1e-6), 0.0, 1.0);
      float *pRow = pTable + (size_t)j * (S + 1);
      for (int s = 0; s <= S; s++)
        pRow[s] = (float)(D.fAlpha[k0][s % S] + (D.fAlpha[k1][s % S] - D.fAlpha[k0][s % S]) * t);
    }
  }
}

#endif
