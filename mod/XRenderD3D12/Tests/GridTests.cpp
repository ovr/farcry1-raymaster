// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: GridTests.cpp
//	Description: Unit tests of the ocean's grid (d3d12_WaterGeometry): its
//	rings and triangles (OceanGrid.h), the game's disc read from its
//	vertices (ReadDisc, bit for bit the read with the radii sorted and
//	atan2; SegmentOf) and its alpha between them (DiscTable and
//	CDiscCache, which reads it again only when a byte of them changed;
//	OceanGrid.hlsli's GridDiscAlpha; the deferred ocean's by its table,
//	AlphaTable and GridDiscAlphaTable), how much of the waves' height a vertex
//	takes (OceanGrid.hlsli: by the depth under the level, the bodies in the
//	water, the camera, the distance), how much of the scene behind a crest
//	its water hides (GridHiddenShare) and the wakes' heights the grid reads
//	(Wake.hlsl's heights kernel as Wake.cpp runs it).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"
#include "OceanGrid.h"
#include "WakeBodies.h"

#include "Test_Grid.h"
#include "Test_GridDisc.h"
#include "Test_WakeRows.h"
#include "Test_WakeColumns.h"
#include "Test_WakeRowsInverse.h"
#include "Test_WakeView.h"
#include "Test_WakeHeight.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  using namespace OceanGrid;
  constexpr double PI = 3.14159265358979323846;
  constexpr int DS = XR_GRID_DISC_SEGMENTS;

  //! A disc as the game draws the ocean: its rings (m), a strip between each two of them from 90 degrees clockwise, each
  //! ring's 36 segments and the first again, at the world's (118.37, 797.61) at the level 16.2.
  const double DISC_X = 118.37, DISC_Y = 797.61;
  const float DISC_Z = 16.2f;
  const std::vector<double> GAME_RINGS = { 0, 0.5, 1.4, 2.94, 5.49, 9.6, 16.1, 26.3, 42.2, 66.4, 103.3, 159.1, 243, 368.5, 555.6, 833.4, 1244.5 };

  //! The alpha of ring k's vertex at segment s (from +x counterclockwise), the centre's one alike at every segment.
  uint32_t GameColor(size_t k, int s)
  {
    const uint32_t a = k == 0 ? 200u : (uint32_t)((k * 37 + s * 23 + 11) % 256);
    return (a << 24) | 0x00808080u;
  }

  std::vector<SVertex> GameDisc(const std::vector<double> &R, double fTurn = 0)
  {
    std::vector<SVertex> V;
    for (size_t k = 0; k + 1 < R.size(); k++)
      for (int j = 0; j <= DS; j++)
        for (size_t r : { k, k + 1 })
        {
          const int s = (((9 - j) % DS) + DS) % DS;
          const double a = (90.0 - 10.0 * j) * PI / 180 + fTurn;
          V.push_back({ (float)(DISC_X + R[r] * std::cos(a)), (float)(DISC_Y + R[r] * std::sin(a)), DISC_Z, GameColor(r, s) });
        }
    return V;
  }

  //! GridDiscAlpha's interpolation in double: bilinear in the ring (the last one with a radius not over r and the next)
  //! and the segment.
  double DiscReference(const SDisc &D, double x, double y)
  {
    const double r = std::hypot(x, y);
    int k0 = 0;
    while (k0 + 2 < D.nRings && D.fRadius[k0 + 1] <= r)
      k0++;
    const int k1 = k0 + 1;
    const double t = std::clamp((r - D.fRadius[k0]) / std::max((double)D.fRadius[k1] - D.fRadius[k0], 1e-6), 0.0, 1.0);
    double u = std::atan2(y, x) / (2 * PI) * DS;
    u = u < 0 ? u + DS : u;
    const int s0 = std::min((int)u, DS - 1), s1 = (s0 + 1) % DS;
    const double f = std::clamp(u - s0, 0.0, 1.0);
    const double a0 = D.fAlpha[k0][s0] + (D.fAlpha[k0][s1] - D.fAlpha[k0][s0]) * f;
    const double a1 = D.fAlpha[k1][s0] + (D.fAlpha[k1][s1] - D.fAlpha[k1][s0]) * f;
    return a0 + (a1 - a0) * t;
  }

  //! GridDiscAlphaTable's interpolation in double: bilinear in the row (log2(1 + r / XR_GRID_ALPHA_R0) times fScale) and
  //! the segment of the table T (AlphaTable).
  double TableReference(const std::vector<float> &T, double fScale, double x, double y)
  {
    const double v = std::min(std::log2(1 + std::hypot(x, y) / XR_GRID_ALPHA_R0) * fScale, XR_GRID_ALPHA_ROWS - 1.0);
    const int j = std::min((int)v, XR_GRID_ALPHA_ROWS - 2);
    double u = std::atan2(y, x) / (2 * PI) * DS;
    u = u < 0 ? u + DS : u;
    const int s = std::min((int)u, DS - 1);
    const double f = std::clamp(u - s, 0.0, 1.0), t = std::clamp(v - j, 0.0, 1.0);
    const float *p = &T[(size_t)j * (DS + 1) + s];
    const double a0 = p[0] + (p[1] - p[0]) * f, a1 = p[DS + 1] + (p[DS + 2] - p[DS + 1]) * f;
    return a0 + (a1 - a0) * t;
  }

  double Smoothstep(double a, double b, double x)
  {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
  }

  //! GridBodyShare in double: none in the ellipse, all GRID_BODY_MARGIN (1 m) beyond it by the first order distance.
  double BodyReference(const WakeBodies::SBody &b, double x, double y)
  {
    const double dx = x - b.fX, dy = y - b.fY;
    const double qx = (dx * b.fCos + dy * b.fSin) / b.fHalfA, qy = (dy * b.fCos - dx * b.fSin) / b.fHalfB;
    const double f = qx * qx + qy * qy - 1;
    return f <= 0 ? 0 : Smoothstep(0, 1, f / (2 * std::hypot(qx / b.fHalfA, qy / b.fHalfB)));
  }

  //! An item of the grid's kernel (CSGrid).
  struct SGridItem
  {
    float r = 0, fTexel = 1, h = 0, fDepth = 1e4f;
    WakeBodies::SBody Body;
    float px = 100, py = 100, fCameraH = 10;
    float z = -10, fSurface = 0, fLevel = 0;
  };
  struct SGridOut
  {
    float fSpacing, fLod, fShore, fBody, fCamera, fCap, fFar, fHidden;
  };

  std::vector<SGridOut> RunGrid(const std::vector<SGridItem> &Items)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_Grid);
    for (const SGridItem &it : Items)
    {
      const WakeBodies::SBody &b = it.Body;
      Push(d.In, it.r, it.fTexel, it.h, it.fDepth);
      Push(d.In, b.fX, b.fY, b.fCos, b.fSin);
      Push(d.In, b.fHalfA, b.fHalfB, it.px, it.py);
      Push(d.In, it.fCameraH, it.z, it.fSurface, it.fLevel);
    }
    d.Test.nTest[0] = (uint32)Items.size();
    d.nGroups[0] = Groups64(Items.size());
    d.nOut = 2 * Items.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SGridOut> o(Items.size());
    for (size_t i = 0; i < Items.size(); i++)
      o[i] = { At(Out, 2 * i, 0), At(Out, 2 * i, 1), At(Out, 2 * i, 2), At(Out, 2 * i, 3), At(Out, 2 * i + 1, 0), At(Out, 2 * i + 1, 1),
               At(Out, 2 * i + 1, 2), At(Out, 2 * i + 1, 3) };
    return o;
  }

  WakeBodies::SBody Body(float x, float y, float fHalfA, float fHalfB, float fYaw)
  {
    WakeBodies::SBody b;
    b.fX = x;
    b.fY = y;
    b.fHalfA = fHalfA;
    b.fHalfB = fHalfB;
    b.fCos = std::cos(fYaw);
    b.fSin = std::sin(fYaw);
    return b;
  }
}

// Why: the grid stands in for the game's disc out to its edge, its vertices as close as the waves the cascades and the
// wakes carry need near the camera (XR_GRID_SPACING, 12.5 cm: the wakes' cells) and no closer than a pixel needs further
// out (square cells: from above the water as wide on the screen at every distance), then sparse where nothing is raised;
// each ring no further from the one before than the spacing the vertex program picks the mip levels by (GridSpacing:
// the same as the CPU's), its triangles all facing up and covering the outer ring's polygon once (no hole, none folded
// over another), as many as its rings and segments make.
XR_TEST(Grid_Mesh)
{
  const std::vector<double> R = Rings();
  constexpr size_t S = XR_GRID_SEGMENTS;
  CHECK(R.size() > 3 && R[0] == 0);
  const int nInner = (int)std::floor(INNER / XR_GRID_SPACING);
  CHECK(nInner == 81);
  int nWrong = 0;
  for (size_t k = 1; k < R.size(); k++)
  {
    const double d = R[k] - R[k - 1];
    if ((int)k <= nInner)
      nWrong += std::fabs(d - XR_GRID_SPACING) > 1e-9;
    else if (R[k - 1] < XR_GRID_DISPLACED)
      nWrong += std::fabs(d / (R[k - 1] * SEGMENT) - 1) > 1e-9;
    else
      nWrong += std::fabs(R[k] / R[k - 1] - FAR_GROWTH) > 1e-9;
    nWrong += d > Spacing(R[k - 1]) * (1 + 1e-9);
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " rings not where they belong");
  CHECK(R.back() >= OUTER && R[R.size() - 2] < OUTER);

  std::vector<SVertex> V;
  std::vector<uint32_t> I;
  Build(0x80ffffffu, V, I);
  CHECK(V.size() == 1 + (R.size() - 1) * S);
  CHECK(I.size() == 3 * S * (2 * (R.size() - 1) - 1));
  int nOff = 0;
  for (size_t k = 1; k < R.size(); k++)
    for (size_t s = 0; s < S; s += 37)
    {
      const SVertex &v = V[1 + (k - 1) * S + s];
      nOff += std::fabs(std::hypot(v.x, v.y) - R[k]) > 1e-6 * R[k] + 1e-6 || std::fabs(std::atan2(v.y, v.x) - std::remainder(s * SEGMENT, 2 * PI)) > 1e-5 ||
              v.z != (float)k || v.nColor != 0x80ffffffu;
    }
  CHECK(nOff == 0);
  double fArea = 0;
  int nBad = 0;
  for (size_t t = 0; t + 2 < I.size(); t += 3)
  {
    if (I[t] >= V.size() || I[t + 1] >= V.size() || I[t + 2] >= V.size())
    {
      nBad++;
      continue;
    }
    const SVertex &a = V[I[t]], &b = V[I[t + 1]], &c = V[I[t + 2]];
    const double fCross = ((double)b.x - a.x) * ((double)c.y - a.y) - ((double)b.y - a.y) * ((double)c.x - a.x);
    nBad += !(fCross > 0);
    fArea += 0.5 * fCross;
  }
  CHECK_MSG(nBad == 0, std::to_string(nBad) + " triangles off the vertices or not facing up");
  CHECK_NEAR(fArea / (0.5 * S * R.back() * R.back() * std::sin(SEGMENT)), 1, 1e-5);

  // The vertex program's spacing.
  std::vector<SGridItem> Items;
  for (double r : { 0.0, 0.05, 5.0, 10.0, 10.2, 10.5, 50.0, 250.0, 499.0, 501.0, 1000.0, 5000.0 })
  {
    SGridItem it;
    it.r = (float)r;
    Items.push_back(it);
  }
  const std::vector<SGridOut> Out = RunGrid(Items);
  for (size_t i = 0; i < Items.size(); i++)
    CHECK_NEAR(Out[i].fSpacing, Spacing(Items[i].r), 1e-5 * Spacing(Items[i].r));
}

// Why: square cells far off rise less than a pixel on the screen from a few metres over the sea, several thin triangles
// in each 2 x 2 pixels, the water's program run over all four for each (Carrier's lagoon at 3840 x 2160: the water 0.4 ms
// slower with the grid). A frame's rings are each as far as Rings()'s of its index or further, so that a cell rises at
// least XR_GRID_PIXELS where the waves turn towards the camera: none further off than that needs (a camera high over the
// sea, a steep sea: the square cells), out to the far plane's OUTER in fewer rings from near the water; the vertex
// program reads them (GridRing) as the CPU made them, the spacing to the next ring its mip levels'.
XR_TEST(Grid_FrameRings)
{
  const std::vector<double> B = Rings();
  constexpr double P = 1407;  // 3840 x 2160 at 75 degrees across the height
  std::vector<float> R;
  FrameRings(2, 0.02, P, R, 0);
  int nOff = 0;
  for (size_t k = 0; k < B.size(); k++)
    nOff += R[k] != (float)std::min(B[k], OUTER);
  CHECK_MSG(nOff == 0, std::to_string(nOff) + " rings not Rings()'s with no least rise");

  struct SCase
  {
    double h, s;
  };
  for (const SCase &c : { SCase{ 2, 0.02 }, SCase{ 0.05, 0.0 }, SCase{ 1.3, 0.08 }, SCase{ -3, 0.02 }, SCase{ 100, 0.02 } })
  {
    FrameRings(c.h, c.s, P, R);
    CHECK(R.size() == B.size() && R[0] == 0 && R.back() == (float)OUTER);
    const double h = std::max(std::fabs(c.h), 0.1);
    int nWrong = 0, nLow = 0;
    for (size_t k = 1; k < R.size(); k++)
    {
      nWrong += R[k] < R[k - 1] || R[k] < (float)std::min(B[k], OUTER) * (1 - 1e-6f);
      // Each cell's rise where the waves face the camera at least the least (but at the far end).
      const double r = R[k - 1], d = (double)R[k] - R[k - 1];
      if (r > 0 && R[k] < (float)OUTER)
        nLow += d * (h + GRID_FACING * c.s * r) / (r * r) * P < XR_GRID_PIXELS * (1 - 1e-4);
    }
    CHECK_MSG(nWrong == 0 && nLow == 0, "h " + std::to_string(c.h) + ": " + std::to_string(nWrong) + " rings out of order, " +
                                            std::to_string(nLow) + " cells lower than the least");
  }
  // From 2 m over a calm sea the disc's edge (1244.5 m) in under two thirds of the rings; from 100 m the rings within
  // 200 m Rings()'s.
  const auto Within = [](const std::vector<float> &v, double r) { return std::count_if(v.begin(), v.end(), [r](float x) { return x < r; }); };
  FrameRings(2, 0.02, P, R);
  std::vector<float> Base;
  for (double b : B)
    Base.push_back((float)b);
  CHECK(Within(R, 1244.5) < 2 * Within(Base, 1244.5) / 3);
  FrameRings(100, 0.02, P, R);
  for (size_t k = 0; k < B.size() && B[k] < 200; k++)
    nOff += R[k] != (float)B[k];
  CHECK(nOff == 0);

  // GridRing: the CPU's radius and the spacing to the next ring (none from the last).
  FrameRings(2, 0.02, P, R);
  const std::vector<SVertex> V = GameDisc(GAME_RINGS);
  SDisc D;
  CHECK(ReadDisc(V.data(), V.size(), D));
  std::vector<float> Table(DiscTableSize(D));
  DiscTable(D, Table.data());
  Table.insert(Table.end(), R.begin(), R.end());
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_GridDisc);
  for (float f : Table)
    d.In.push_back(Bits(f));
  while (d.In.size() % 4)
    d.In.push_back(0);
  const uint32 nFirst = (uint32)(d.In.size() / 4);
  std::vector<uint32_t> Ks;
  for (uint32_t k = 0; k < R.size(); k += 7)
    Ks.push_back(k);
  Ks.push_back((uint32_t)R.size() - 1);
  for (uint32_t k : Ks)
    Push(d.In, 10.0f, 0.0f, (float)k, 0.0f);
  d.Test.nTest[0] = (uint32)Ks.size();
  d.Test.nTest[1] = (uint32)D.nRings;
  d.Test.nTest[2] = nFirst;
  d.Test.nTest[3] = (uint32)R.size();
  d.nGroups[0] = Groups64(Ks.size());
  d.nOut = Ks.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  nOff = 0;
  for (size_t i = 0; i < Ks.size(); i++)
  {
    const uint32_t k = Ks[i];
    const float fNext = k + 1 < R.size() ? R[k + 1] : R[k];
    nOff += At(Out, i, 1) != R[k] || std::fabs(At(Out, i, 2) - (fNext - R[k])) > 1e-6f * fNext;
  }
  CHECK_MSG(nOff == 0, std::to_string(nOff) + " rings off on the GPU");
}

// Why: a vertex takes each texture of the waves at the mip level whose texel is as wide as the grid's spacing there (no
// wave shorter than two cells of the grid left: those alias into a crawl of the vertices); level 0 where the grid is finer
// than the texture.
XR_TEST(Grid_Lod)
{
  const float Texels[] = { (float)(XR_OCEAN_TILE0 / XR_OCEAN_SIZE), (float)(XR_OCEAN_TILE1 / XR_OCEAN_SIZE), (float)(XR_OCEAN_TILE2 / XR_OCEAN_SIZE),
                           (float)XR_WAKE_CELL };
  std::vector<SGridItem> Items;
  for (float fTexel : Texels)
    for (double r : { 0.0, 5.0, 12.0, 30.0, 100.0, 400.0 })
    {
      SGridItem it;
      it.r = (float)r;
      it.fTexel = fTexel;
      Items.push_back(it);
    }
  const std::vector<SGridOut> Out = RunGrid(Items);
  for (size_t i = 0; i < Items.size(); i++)
  {
    const double fSpacing = Spacing(Items[i].r);
    CHECK_NEAR(Out[i].fLod, std::max(std::log2(fSpacing / Items[i].fTexel), 0.0), 1e-4);
    // At that level the texel is as wide as the spacing (or the texture's own where it is coarser).
    CHECK_NEAR(Items[i].fTexel * std::exp2(Out[i].fLod), std::max(fSpacing, (double)Items[i].fTexel), 1e-4 * fSpacing);
  }
}

// Why: in shallow water a wave cannot stand higher than the depth lets it (it breaks at a height of McCowan's 0.78 of the
// depth: a crest half of that over the level, a trough as far under it), and on the shore (no water under the level) the
// grid lies at the level as the game's disc did, where the terrain hides it; in deep water the waves are their own height.
XR_TEST(Grid_Shore)
{
  std::vector<SGridItem> Items;
  const float Depths[] = { -2.0f, 0.0f, 0.05f, 0.3f, 1.0f, 3.0f, 10.0f, 1e4f };
  std::vector<float> Heights;
  for (int i = -40; i <= 40; i++)
    Heights.push_back(0.1f * i);
  for (float fDepth : Depths)
    for (float h : Heights)
    {
      SGridItem it;
      it.h = h;
      it.fDepth = fDepth;
      Items.push_back(it);
    }
  const std::vector<SGridOut> Out = RunGrid(Items);
  int nOver = 0, nShore = 0, nSign = 0, nDeep = 0, nOrder = 0;
  for (size_t i = 0; i < Items.size(); i++)
  {
    const float h = Items[i].h, fDepth = Items[i].fDepth, g = Out[i].fShore;
    if (fDepth <= 0)
    {
      nShore += g != 0;
      continue;
    }
    nOver += std::fabs(g) >= 0.39f * fDepth || std::fabs(g) > std::fabs(h) * (1 + 1e-5f);
    nSign += h * g < 0;
    if (std::fabs(h) <= 0.039f * fDepth)
      nDeep += std::fabs(g - h) > 0.005f * std::fabs(h) + 1e-7f;
    if (i > 0 && Items[i - 1].fDepth == fDepth)
      nOrder += g < Out[i - 1].fShore - 1e-6f * 0.39f * fDepth;
  }
  CHECK(nOver == 0);
  CHECK(nShore == 0);
  CHECK(nSign == 0);
  CHECK(nDeep == 0);
  CHECK(nOrder == 0);
}

// Why: the game floats a boat on the level, so the ocean's waves would run through its hull: under a body's box (its
// ellipse, turned with its heading as the wakes' bodies are, WakeBodies.h) none of them, all of them again GRID_BODY_MARGIN
// (1 m) beyond it.
XR_TEST(Grid_Bodies)
{
  CRandom Random(293);
  std::vector<SGridItem> Items;
  for (int i = 0; i < 1024; i++)
  {
    SGridItem it;
    it.Body = Body(Random.Range(-5, 5), Random.Range(-5, 5), Random.Range(0.6f, 4), Random.Range(0.6f, 2), Random.Range(-3.1f, 3.1f));
    it.px = it.Body.fX + Random.Range(-6, 6);
    it.py = it.Body.fY + Random.Range(-6, 6);
    Items.push_back(it);
  }
  // A boat 6 x 2.4 m turned 30 degrees: along its keel 2.9 m from its middle none of the waves, 1.5 m across it (0.3 m
  // beyond its side) some, 3 m across (1.8 m beyond) all of them.
  const WakeBodies::SBody Boat = Body(10, 20, 3, 1.2f, 30 * (float)PI / 180);
  const float c = Boat.fCos, s = Boat.fSin;
  const float Points[][2] = { { 10 + 2.9f * c, 20 + 2.9f * s }, { 10 - 1.5f * s, 20 + 1.5f * c }, { 10 - 3.0f * s, 20 + 3.0f * c } };
  for (const auto &p : Points)
  {
    SGridItem it;
    it.Body = Boat;
    it.px = p[0];
    it.py = p[1];
    Items.push_back(it);
  }
  const std::vector<SGridOut> Out = RunGrid(Items);
  int nInside = 0, nWrong = 0;
  for (size_t i = 0; i + 3 < Items.size(); i++)
  {
    const SGridItem &it = Items[i];
    const WakeBodies::SBody &b = it.Body;
    const double dx = it.px - b.fX, dy = it.py - b.fY;
    const double u = (dx * b.fCos + dy * b.fSin) / b.fHalfA, v = (dy * b.fCos - dx * b.fSin) / b.fHalfB;
    const bool bInside = WakeBodies::Inside(b, it.px, it.py);
    nInside += bInside;
    // None of the waves just where the wakes' ellipse is (but on its very edge, where float and double may differ).
    if (std::fabs(u * u + v * v - 1) > 1e-4)
      nWrong += (Out[i].fBody == 0) != bInside;
    nWrong += std::fabs(Out[i].fBody - BodyReference(b, it.px, it.py)) > 1e-4;
  }
  CHECK(nInside > 20);
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " points with the wrong share");
  const size_t n = Items.size();
  CHECK(Out[n - 3].fBody == 0);
  CHECK(Out[n - 2].fBody > 0.05f && Out[n - 2].fBody < 0.95f);
  CHECK_NEAR(Out[n - 1].fBody, 1, 1e-6);
}

// Why: the game takes the camera as under the water by the level alone, so a crest over a camera at the water (or a
// trough under one just below it) would cut the view in two: around such a camera the surface is flat out to 2 m, the
// waves all there from 8 m on; and a crest never rises closer than 0.3 m (more than the near plane) under a camera over
// the water there, the limit rising 0.5 m a metre further out (from a boat in a high sea the crests a few metres off do
// stand over the eye), a trough as far over one under it; what is far under the limit keeps its height. Far off (beyond
// half of XR_GRID_DISPLACED) the waves fade out: the rings there are too sparse for them.
XR_TEST(Grid_Camera)
{
  std::vector<SGridItem> Items;
  const float Cameras[] = { 0.0f, 0.3f, 0.5f, 1.0f, 1.3f, 2.0f, 5.0f, -0.5f, -2.0f };
  const float Radii[] = { 0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 12.0f, 20.0f, 250.0f, 300.0f, 400.0f, 500.0f, 800.0f };
  for (float fH : Cameras)
    for (float r : Radii)
      for (int i = -50; i <= 50; i++)
      {
        SGridItem it;
        it.fCameraH = fH;
        it.r = r;
        it.h = 0.1f * i;
        Items.push_back(it);
      }
  const std::vector<SGridOut> Out = RunGrid(Items);
  int nShare = 0, nCap = 0, nKept = 0, nFar = 0;
  for (size_t i = 0; i < Items.size(); i++)
  {
    const SGridItem &it = Items[i];
    const float fH = it.fCameraH, r = it.r, h = it.h;
    const SGridOut &o = Out[i];
    // The share: none within 2 m of a camera within 0.75 m of the level, all from 8 m on or 1.5 m over or under it.
    if (std::fabs(fH) <= 0.75f && r <= 2)
      nShare += o.fCamera != 0;
    if (r >= 8 || std::fabs(fH) >= 1.5f)
      nShare += o.fCamera != 1;
    // The cap: on the camera's side of the level under the limit, the other side as it was.
    const float m = std::max(std::fabs(fH) - 0.3f, 0.0f) + 0.5f * r;
    const bool bCameraSide = fH >= 0 ? h > 0 : h < 0;
    if (!bCameraSide)
      nCap += o.fCap != h;
    else
    {
      nCap += m > 0 ? std::fabs(o.fCap) >= m || o.fCap * h <= 0 : o.fCap != 0;
      if (std::fabs(h) <= 0.1f * m)
        nKept += std::fabs(o.fCap - h) > 0.006f * std::fabs(h);
    }
    // The distance's share.
    if (r <= 0.5f * XR_GRID_DISPLACED)
      nFar += o.fFar != 1;
    else if (r >= XR_GRID_DISPLACED)
      nFar += o.fFar != 0;
    else
      nFar += !(o.fFar > 0 && o.fFar < 1);
    // Less further out (the item a radius before: the same camera and height).
    if (i >= 101 && Items[i - 101].fCameraH == fH && Items[i - 101].r < r)
      nFar += o.fFar > Out[i - 101].fFar;
  }
  CHECK(nShare == 0);
  CHECK(nCap == 0);
  CHECK(nKept == 0);
  CHECK(nFar == 0);

  // A storm's crest 1.8 m high 12 m from a camera 1.3 m over the water stands over its eye.
  SGridItem it;
  it.fCameraH = 1.3f;
  it.r = 12;
  it.h = 1.8f;
  CHECK(RunGrid({ it })[0].fCap > 1.7f);
}

// Why: the view's ray refracted into a wave goes down into the water (at least 41 degrees under the surface), under the
// trough beyond the crest, not on to what stands over the water behind it; drawn as seen through the water down to its
// height, a boat, the shore and the sky behind the storm's crests showed through them as through glass (Fort's Zodiac
// behind a crest 1.3 m high from the side, its sides above the level not attenuated at all). So the grid's water hides
// the scene behind a crest from the depth of the crest's height under the level on, all of it 0.5 m higher; none of the
// scene under the flat disc or a trough (the frames before), none of a shallow floor (a crest at most 0.39 of the depth).
XR_TEST(Grid_Hidden)
{
  std::vector<SGridItem> Items;
  const float Levels[] = { 0.0f, 12.3f, -5.0f };
  const float Surfaces[] = { -1.5f, -0.2f, 0.0f, 0.1f, 0.4f, 1.0f, 2.5f };
  for (float fLevel : Levels)
    for (float s : Surfaces)
      for (int i = -60; i <= 60; i++)
      {
        SGridItem it;
        it.fLevel = fLevel;
        it.fSurface = fLevel + s;
        it.z = fLevel + 0.05f * i;
        Items.push_back(it);
      }
  // Shallow floors under the highest crests GridShore leaves (0.39 of the depth), the sky and a deep floor.
  const float Depths[] = { 0.02f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f };
  const size_t nShallow = Items.size();
  for (float fDepth : Depths)
  {
    SGridItem it;
    it.fLevel = 12.3f;
    it.fSurface = it.fLevel + 0.39f * fDepth;
    it.z = it.fLevel - fDepth;
    Items.push_back(it);
  }
  SGridItem Sky;
  Sky.fLevel = 12.3f;
  Sky.fSurface = 12.6f;
  Sky.z = 5000;
  Items.push_back(Sky);
  SGridItem Hull = Sky;
  Hull.fSurface = Hull.fLevel + 1.0f;
  Hull.z = Hull.fLevel - 0.3f;
  Items.push_back(Hull);
  const std::vector<SGridOut> Out = RunGrid(Items);
  int nWrong = 0, nFlat = 0, nOrder = 0;
  for (size_t i = 0; i < nShallow; i++)
  {
    const SGridItem &it = Items[i];
    const double fCrest = std::max((double)it.fSurface - it.fLevel, 0.0);
    const double fRef = std::clamp(((double)it.z - it.fLevel + fCrest) / 0.5, 0.0, 1.0);
    nWrong += std::fabs(Out[i].fHidden - fRef) > 2e-5;
    // Under the flat disc or a trough (the surface not over the level), none of the scene under the level.
    if (it.fSurface <= it.fLevel && it.z <= it.fLevel)
      nFlat += Out[i].fHidden != 0;
    // More of a higher scene behind the same surface.
    if (i > 0 && Items[i - 1].fSurface == it.fSurface && Items[i - 1].fLevel == it.fLevel)
      nOrder += Out[i].fHidden < Out[i - 1].fHidden;
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " shares off the reference");
  CHECK(nFlat == 0);
  CHECK(nOrder == 0);
  for (size_t i = nShallow; i < nShallow + std::size(Depths); i++)
    CHECK(Out[i].fHidden == 0);
  CHECK(Out[Items.size() - 2].fHidden == 1);
  // A boat's hull 0.3 m under the level behind a crest 1 m high: hidden.
  CHECK(Out[Items.size() - 1].fHidden == 1);
}

// Why: the game fades the ocean's disc towards the shore and with the distance by its vertices' alpha (Water.hlsl weighs
// the reflection by it); the grid has none of the disc's vertices, so the disc is read from them each frame: its centre,
// its rings and each ring's alpha by segment, whatever order the strip gives them in and however often a vertex repeats
// (a segment without a vertex its nearest's alpha), nothing where the vertices make no such disc. Between them the vertex
// program interpolates as the disc's triangles did along their edges: linearly between the rings, between the segments,
// across 360 degrees too, the last ring's beyond it.
XR_TEST(Grid_Disc)
{
  const std::vector<SVertex> V = GameDisc(GAME_RINGS);
  SDisc D;
  CHECK(ReadDisc(V.data(), V.size(), D));
  CHECK(D.nRings == (int)GAME_RINGS.size());
  CHECK_NEAR(D.fX, DISC_X, 1e-3);
  CHECK_NEAR(D.fY, DISC_Y, 1e-3);
  CHECK(D.fZ == DISC_Z);
  int nWrong = 0;
  for (int k = 0; k < D.nRings && k < (int)GAME_RINGS.size(); k++)
  {
    nWrong += std::fabs(D.fRadius[k] - GAME_RINGS[k]) > 1e-3;
    for (int s = 0; s < DS; s++)
      nWrong += std::fabs(D.fAlpha[k][s] - ColorAlpha(GameColor(k, s))) > 1e-6f;
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " radii or alphas wrong");

  // The table.
  std::vector<float> Table(DiscTableSize(D));
  CHECK(Table.size() == GAME_RINGS.size() * (1 + DS));
  DiscTable(D, Table.data());
  CHECK(Table[0] == 0 && Table[1] == D.fRadius[1] && Table[D.nRings - 1] == D.fRadius[D.nRings - 1]);
  CHECK(Table[D.nRings + 3 * DS + 7] == D.fAlpha[3][7]);

  // No disc: turned by half a segment's width, a vertex off the level, more rings than XR_GRID_DISC_RINGS.
  SDisc No;
  const std::vector<SVertex> Turned = GameDisc(GAME_RINGS, 5 * PI / 180);
  CHECK(!ReadDisc(Turned.data(), Turned.size(), No));
  std::vector<SVertex> Off = V;
  Off[100].z += 0.01f;
  CHECK(!ReadDisc(Off.data(), Off.size(), No));
  std::vector<double> Many;
  for (int k = 0; k <= XR_GRID_DISC_RINGS; k++)
    Many.push_back(k);
  const std::vector<SVertex> TooMany = GameDisc(Many);
  CHECK(!ReadDisc(TooMany.data(), TooMany.size(), No));
  // A segment without a vertex: the next one's alpha.
  std::vector<SVertex> Gap;
  for (const SVertex &v : V)
  {
    const double dx = v.x - DISC_X, dy = v.y - DISC_Y;
    const int s = (int)std::lround(std::atan2(dy, dx) / (2 * PI) * DS + DS) % DS;
    if (!(std::fabs(std::hypot(dx, dy) - GAME_RINGS[5]) < 0.01 && s == 3))
      Gap.push_back(v);
  }
  SDisc G;
  CHECK(Gap.size() < V.size() && ReadDisc(Gap.data(), Gap.size(), G));
  CHECK(G.fAlpha[5][3] == D.fAlpha[5][4] && G.fAlpha[5][2] == D.fAlpha[5][2] && G.fAlpha[6][3] == D.fAlpha[6][3]);

  // GridDiscAlpha: at the vertices, at random points and beyond the last ring, across +x.
  struct SPoint
  {
    double x, y, fWant;
  };
  std::vector<SPoint> Points;
  for (int k = 1; k < D.nRings; k++)
    for (int s = 0; s < DS; s++)
    {
      const double a = s * 2 * PI / DS, r = D.fRadius[k];
      Points.push_back({ r * std::cos(a), r * std::sin(a), D.fAlpha[k][s] });
    }
  CRandom Random(2930);
  for (int i = 0; i < 2048; i++)
  {
    const double r = Random.Range(0, 1400), a = Random.Range(-3.14159f, 3.14159f);
    Points.push_back({ r * std::cos(a), r * std::sin(a), DiscReference(D, r * std::cos(a), r * std::sin(a)) });
  }
  for (double a : { -1e-5, 1e-5 })
    Points.push_back({ 300 * std::cos(a), 300 * std::sin(a), DiscReference(D, 300 * std::cos(a), 300 * std::sin(a)) });
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_GridDisc);
  for (float f : Table)
    d.In.push_back(Bits(f));
  while (d.In.size() % 4)
    d.In.push_back(0);
  const uint32 nFirst = (uint32)(d.In.size() / 4);
  for (const SPoint &p : Points)
    Push(d.In, (float)p.x, (float)p.y, 0, 0);
  d.Test.nTest[0] = (uint32)Points.size();
  d.Test.nTest[1] = (uint32)D.nRings;
  d.Test.nTest[2] = nFirst;
  d.nGroups[0] = Groups64(Points.size());
  d.nOut = Points.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  int nOff = 0;
  for (size_t i = 0; i < Points.size(); i++)
    nOff += std::fabs(At(Out, i, 0) - Points[i].fWant) > 1e-4;
  CHECK_MSG(nOff == 0, std::to_string(nOff) + " of " + std::to_string(Points.size()) + " points off");
  // The seam at +x: the alphas just below and just above 0 degrees alike.
  const size_t n = Points.size();
  CHECK_NEAR(At(Out, n - 2, 0), At(Out, n - 1, 0), 1e-3);
}

// Why: the deferred ocean shades each pixel where the grid's vertices were, so the disc's alpha is a pixel's: GridDiscAlpha's
// search among the disc's rings cost it 0.04-0.12 ms at 4K. A table of it made each frame by radius (rows a geometric
// series of 1 + r / XR_GRID_ALPHA_R0, the last at the disc's last ring) and segment gives a pixel's from two loads. Its rows
// are GridDiscAlpha's at their radii, the first segment again at the end; between them bilinear as the GPU takes it, across
// 360 degrees too, the last row's beyond the disc; off the rows within the bend of the disc's alphas at a ring between two
// rows (the test's change by up to 1 from a ring to the next: within 0.05, the forward grid's linear between its vertices
// alike).
XR_TEST(Grid_AlphaTable)
{
  const std::vector<SVertex> V = GameDisc(GAME_RINGS);
  SDisc D;
  CHECK(ReadDisc(V.data(), V.size(), D));
  std::vector<float> Table(DiscTableSize(D)), Alpha(AlphaTableSize());
  DiscTable(D, Table.data());
  AlphaTable(D, Alpha.data());
  const double fScale = AlphaScale(D), R = D.fRadius[D.nRings - 1];
  CHECK_NEAR(std::log2(1 + R / XR_GRID_ALPHA_R0) * fScale, XR_GRID_ALPHA_ROWS - 1, 1e-9);
  int nWrong = 0;
  for (int j = 0; j < XR_GRID_ALPHA_ROWS; j++)
    for (int s = 0; s <= DS; s++)
    {
      const double r = XR_GRID_ALPHA_R0 * (std::exp2(j / fScale) - 1), a = s * 2 * PI / DS;
      nWrong += std::fabs(Alpha[(size_t)j * (DS + 1) + s] - DiscReference(D, r * std::cos(a), r * std::sin(a))) > 1e-5;
    }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " of the table's alphas off GridDiscAlpha's at their rows");

  // On the GPU: at random points, beyond the disc, across +x.
  struct SPoint
  {
    double x, y;
  };
  std::vector<SPoint> Points;
  CRandom Random(2932);
  for (int i = 0; i < 4096; i++)
  {
    const double r = Random.Range(0, 1400), a = Random.Range(-3.14159f, 3.14159f);
    Points.push_back({ r * std::cos(a), r * std::sin(a) });
  }
  for (double a : { -1e-5, 1e-5 })
    Points.push_back({ 300 * std::cos(a), 300 * std::sin(a) });
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_GridDisc);
  for (float f : Table)
    d.In.push_back(Bits(f));
  for (float f : Alpha)
    d.In.push_back(Bits(f));
  while (d.In.size() % 4)
    d.In.push_back(0);
  const uint32 nFirst = (uint32)(d.In.size() / 4);
  for (const SPoint &p : Points)
    Push(d.In, (float)p.x, (float)p.y, 0, 0);
  d.Test.nTest[0] = (uint32)Points.size();
  d.Test.nTest[1] = (uint32)D.nRings;
  d.Test.nTest[2] = nFirst;
  d.Test.vTest[0] = (float)fScale;
  d.Test.vTest[1] = (float)Table.size();
  d.nGroups[0] = Groups64(Points.size());
  d.nOut = Points.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  int nOff = 0;
  double fMost = 0, fSum = 0;
  for (size_t i = 0; i < Points.size(); i++)
  {
    const double a = At(Out, i, 3);
    nOff += std::fabs(a - TableReference(Alpha, fScale, Points[i].x, Points[i].y)) > 1e-4;
    const double e = std::fabs(a - DiscReference(D, Points[i].x, Points[i].y));
    fMost = std::max(fMost, e);
    fSum += e;
  }
  CHECK_MSG(nOff == 0, std::to_string(nOff) + " of " + std::to_string(Points.size()) + " points off the table's");
  CHECK_MSG(fMost < 0.05 && fSum / Points.size() < 0.005,
            "off GridDiscAlpha's by " + std::to_string(fMost) + " at most, " + std::to_string(fSum / Points.size()) + " on the mean");
  const size_t n = Points.size();
  CHECK_NEAR(At(Out, n - 2, 3), At(Out, n - 1, 3), 1e-3);
  // Beyond the disc the last ring's.
  CHECK_NEAR(TableReference(Alpha, fScale, 1300, 0), D.fAlpha[D.nRings - 1][0], 1e-6);
}

// Why: the grid raises its vertices by the wakes' height too, from a texture of its own the wakes' kernel writes after
// the view (the window's height, its mip levels the means of 2 x 2 texels: the grid takes the level of its spacing),
// in half floats (a wave of a boat 0.3 m high to within a millimetre, a swimmer's 6 cm to a tenth of one).
XR_TEST(Grid_WakeHeights)
{
  constexpr int N = XR_WAKE_SIZE;
  std::vector<float> Start((size_t)N * N * 2);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
    {
      const double X = x * XR_WAKE_CELL, Y = y * XR_WAKE_CELL;
      Start[((size_t)y * N + x) * 2] = (float)(0.3 * std::cos(2 * PI * (3 * X + Y) / (N * XR_WAKE_CELL)) +
                                               0.06 * std::sin(2 * PI * (40 * X - 25 * Y) / (N * XR_WAKE_CELL)));
      Start[((size_t)y * N + x) * 2 + 1] = 0.1f;
    }
  SWakeFrame f;
  const int Origin[2] = { 0, 0 };
  WakeBodies::StepConstants(f.View, Origin, Origin, false, 0.02f, false, nullptr, 0);
  SWakeKernels k = { XR_BYTECODE(Test_WakeRows), XR_BYTECODE(Test_WakeColumns), XR_BYTECODE(Test_WakeRowsInverse), XR_BYTECODE(Test_WakeView) };
  k.Height = XR_BYTECODE(Test_WakeHeight);
  const SWakeOutput o = Gpu().RunWake(Start, { f }, k);
  int nWrong = 0;
  for (int m = 0; m < XR_WAKE_MIPS; m++)
  {
    const int w = N >> m, b = 1 << m;
    if (o.Heights[m].size() != (size_t)w * w)
    {
      nWrong++;
      continue;
    }
    for (int y = 0; y < w; y++)
      for (int x = 0; x < w; x++)
      {
        double fMean = 0;
        for (int j = 0; j < b; j++)
          for (int i = 0; i < b; i++)
            fMean += Start[((size_t)(y * b + j) * N + x * b + i) * 2];
        fMean /= b * b;
        nWrong += std::fabs(o.Heights[m][(size_t)y * w + x] - fMean) > 1.5e-3 * std::fabs(fMean) + 1e-6;
      }
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " texels off");
  // The window itself as it was (a view, no step).
  CHECK(o.State == Start);
}

// The disc is read once a frame from the game's vertices, which change only when its ring moves: the cache gives back the
// disc and both tables as a fresh read would, bit for bit, reads again for one changed byte, and nothing for no disc.
XR_TEST(Grid_DiscCache)
{
  const auto Same = [](const CDiscCache &C, const SDisc &D)
  {
    std::vector<float> Table(DiscTableSize(D)), Alpha(AlphaTableSize());
    DiscTable(D, Table.data());
    AlphaTable(D, Alpha.data());
    return C.GetTable() == Table && C.GetAlphaTable() == Alpha;
  };
  // Field by field (the struct may have padding).
  const auto Equal = [](const SDisc &a, const SDisc &b)
  {
    return a.fX == b.fX && a.fY == b.fY && a.fZ == b.fZ && a.nRings == b.nRings && memcmp(a.fRadius, b.fRadius, sizeof(a.fRadius)) == 0 &&
           memcmp(a.fAlpha, b.fAlpha, sizeof(a.fAlpha)) == 0;
  };
  CDiscCache C;
  const std::vector<SVertex> V = GameDisc(GAME_RINGS);
  SDisc D;
  CHECK(ReadDisc(V.data(), V.size(), D));
  const SDisc *p = C.Read(V.data(), V.size());
  CHECK(p && C.GetReads() == 1 && Equal(*p, D) && Same(C, D));
  // The same bytes elsewhere: not read again.
  const std::vector<SVertex> Copy = V;
  CHECK(C.Read(Copy.data(), Copy.size()) == p && C.GetReads() == 1 && Same(C, D));
  // One vertex's alpha a step lower: read again, as fresh.
  std::vector<SVertex> Changed = V;
  Changed[40].nColor -= 1u << 24;
  SDisc E;
  CHECK(ReadDisc(Changed.data(), Changed.size(), E) && !Equal(D, E));
  p = C.Read(Changed.data(), Changed.size());
  CHECK(p && C.GetReads() == 2 && Equal(*p, E) && Same(C, E));
  // No disc (turned by half a segment), none read, then the disc again; no vertices.
  const std::vector<SVertex> Turned = GameDisc(GAME_RINGS, 5 * PI / 180);
  CHECK(!C.Read(Turned.data(), Turned.size()) && C.GetReads() == 3 && C.GetTable().empty() && C.GetAlphaTable().empty());
  CHECK(!C.Read(Turned.data(), Turned.size()) && C.GetReads() == 3);
  p = C.Read(V.data(), V.size());
  CHECK(p && C.GetReads() == 4 && Equal(*p, D) && Same(C, D));
  CHECK(!C.Read(nullptr, V.size()) && C.GetReads() == 5);
}

namespace
{
  //! ReadDisc as it was before it found the rings without sorting the radii and the segments by a cheaper angle (the
  //! reference its disc must be bit for bit).
  bool ReadDiscSorted(const SVertex *V, size_t n, SDisc &D)
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
      const double u = std::atan2(V[i].y - D.fY, V[i].x - D.fX) / (2 * PI) * S;
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
}

// Why: the disc is read again each time the game's ring moves (most frames of a flight), so ReadDisc finds its rings
// without sorting the radii and its segments by a cheaper angle than atan2 where that decides; its disc must stay the one
// the sorted radii and atan2 give, bit for bit, whatever the vertices' order, rings near each other, turns near a tenth.
XR_TEST(Grid_DiscReadExact)
{
  const auto Equal = [](bool a, const SDisc &A, bool b, const SDisc &B)
  {
    return a == b && A.fX == B.fX && A.fY == B.fY && A.fZ == B.fZ && A.nRings == B.nRings &&
           memcmp(A.fRadius, B.fRadius, sizeof(A.fRadius)) == 0 && memcmp(A.fAlpha, B.fAlpha, sizeof(A.fAlpha)) == 0;
  };
  CRandom Random(4417);
  const double fTenth = 0.1 * 2 * PI / DS;
  const double Turns[] = { 0, 0.3, fTenth, -fTenth, fTenth * (1 + 1e-9), fTenth * (1 - 1e-9), 0.9 * fTenth, 1.1 * fTenth, 5 * fTenth };
  int nWrong = 0, nDiscs = 0;
  std::vector<double> Radii;
  for (int t = 0; t < 600; t++)
  {
    // Every 4th the radii moved by up to 1.5 % (rings near the next one's), every 9th a ring 1.01 times the one before,
    // every 5th the vertices in another order, every 11th some of them moved by 2 mm.
    std::vector<double> Rings = GAME_RINGS;
    if (t % 4 == 1)
      for (double &r : Rings)
        r *= 1 + (Random.Unit() * 2 - 1) * 0.015;
    if (t % 9 == 2)
      Rings.insert(Rings.begin() + 8, Rings[8] * 1.0101);
    if (t % 13 == 5)
      for (int k = (int)Rings.size(); k <= XR_GRID_DISC_RINGS + 1 - t % 2; k++)
        Rings.push_back(Rings.back() * 1.2);
    std::vector<SVertex> V = GameDisc(Rings, t % 3 == 0 ? Turns[(t / 3) % 9] : 0);
    if (t % 5 == 3)
      for (size_t i = V.size() - 1; i > 0; i--)
        std::swap(V[i], V[Random.Next() % (i + 1)]);
    if (t % 11 == 4)
      for (SVertex &v : V)
        v.x += (float)((int)(Random.Next() % 3) - 1) * 0.002f;
    // Every 7th an odd number of vertices (NextRing's last one alone); every other one with a buffer of the read before.
    if (t % 7 == 6)
      V.erase(V.begin() + (ptrdiff_t)(Random.Next() % V.size()));
    SDisc A, B;
    const bool a = ReadDisc(V.data(), V.size(), A, t % 2 ? &Radii : nullptr), b = ReadDiscSorted(V.data(), V.size(), B);
    nWrong += !Equal(a, A, b, B);
    nDiscs += a;
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " of 600 discs not the sorted read's");
  CHECK(nDiscs > 300 && nDiscs < 600);

  // SegmentOf against atan2: random directions, ones a tenth of a segment off one (a little more or less), the axes.
  int nOff = 0;
  for (int i = 0; i < 2000000; i++)
  {
    double a = (Random.Unit() * 2 - 1) * PI;
    if (i % 4 == 1)
      a = std::round(a / (2 * PI / DS)) * (2 * PI / DS) + (double)((int)(Random.Next() % 3) - 1) * fTenth * (1 + (Random.Unit() * 2 - 1) * 1e-3);
    const double r = 0.01 + Random.Unit() * 2000.0;
    double x = r * std::cos(a), y = r * std::sin(a);
    if (i % 1000 == 7)
    {
      x = (double)(Random.Next() % 3) - 1;
      y = i % 2000 == 7 ? -0.0 : (double)(Random.Next() % 3) - 1;
    }
    int s1 = -1, s2 = -1;
    const bool b1 = SegmentOf(x, y, s1);
    const double u = std::atan2(y, x) / (2 * PI) * DS, s = std::round(u);
    const bool b2 = !(std::fabs(u - s) > 0.1);
    if (b2)
      s2 = (((int)s % DS) + DS) % DS;
    nOff += b1 != b2 || (b1 && s1 != s2);
  }
  CHECK_MSG(nOff == 0, std::to_string(nOff) + " of 2000000 directions not atan2's segment");
}
