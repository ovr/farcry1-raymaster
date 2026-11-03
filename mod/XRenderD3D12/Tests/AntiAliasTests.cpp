// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AntiAliasTests.cpp
//	Description: Unit tests of the anti-aliasing without an upscaler on
//	the GPU (d3d12_AA, PostAA.cpp): FXAA (Shaders/Fxaa.hlsli) on small
//	pictures of edges drawn without anti-aliasing (a pixel white where its
//	centre is on the white side), against the exact coverage of each pixel
//	by the white side.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_Fxaa.h"

#include <cmath>
#include <functional>
#include <vector>

using namespace XrTest;

namespace
{
  struct SRgba
  {
    float r, g, b, a;
  };

  //! A picture of w x h pixels, row by row.
  struct SPicture
  {
    int w = 0, h = 0;
    std::vector<SRgba> Px;
    SRgba &At(int x, int y) { return Px[(size_t)y * w + x]; }
    const SRgba &At(int x, int y) const { return Px[(size_t)y * w + x]; }
  };

  //! What FXAA gave a pixel (CSFxaa).
  struct SFxaaPixel
  {
    SRgba Color;
    float fBlend;
    int nStepX, nStepY;
    bool bEdge;
  };

  std::vector<SFxaaPixel> Run(const SPicture &In, int nQuality)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_Fxaa);
    const size_t n = In.Px.size();
    d.Test.nTest[0] = (uint32)n;
    d.Test.nTest[1] = (uint32)In.w;
    d.Test.nTest[2] = (uint32)In.h;
    d.Test.nTest[3] = (uint32)nQuality;
    d.nGroups[0] = Groups64(n);
    for (const SRgba &p : In.Px)
      Push(d.In, p.r, p.g, p.b, p.a);
    d.nOut = 2 * n;
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SFxaaPixel> R(n);
    for (size_t i = 0; i < n; i++)
    {
      R[i].Color = { At(Out, i, 0), At(Out, i, 1), At(Out, i, 2), At(Out, i, 3) };
      R[i].fBlend = At(Out, n + i, 0);
      R[i].nStepX = (int)At(Out, n + i, 1);
      R[i].nStepY = (int)At(Out, n + i, 2);
      R[i].bEdge = At(Out, n + i, 3) > 0.5f;
    }
    return R;
  }

  //! The share of the pixel (x, y) (the square x..x+1, y..y+1) on the white side (bWhite of a point), by 32 x 32 samples.
  float Coverage(int x, int y, const std::function<bool(double, double)> &bWhite)
  {
    int n = 0;
    for (int j = 0; j < 32; j++)
      for (int i = 0; i < 32; i++)
        n += bWhite(x + (i + 0.5) / 32, y + (j + 0.5) / 32) ? 1 : 0;
    return n / 1024.0f;
  }

  //! An edge drawn without anti-aliasing (white where the pixel's centre is white, else black; alpha 1) and the exact
  //! coverage of each pixel.
  struct SEdge
  {
    SPicture Aliased;
    std::vector<float> Exact;
  };

  SEdge Edge(int w, int h, const std::function<bool(double, double)> &bWhite)
  {
    SEdge e;
    e.Aliased.w = w;
    e.Aliased.h = h;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        const float v = bWhite(x + 0.5, y + 0.5) ? 1.0f : 0.0f;
        e.Aliased.Px.push_back({ v, v, v, 1 });
        e.Exact.push_back(Coverage(x, y, bWhite));
      }
    return e;
  }

  //! The mean difference of a picture's red from the exact coverage over the pixels away from the picture's borders (the
  //! loads clamp there).
  double Error(const std::vector<float> &Exact, int w, int h, const std::function<float(size_t)> &Red)
  {
    double fSum = 0;
    int n = 0;
    for (int y = 2; y < h - 2; y++)
      for (int x = 2; x < w - 2; x++)
      {
        const size_t i = (size_t)y * w + x;
        fSum += std::fabs(Red(i) - Exact[i]);
        n++;
      }
    return fSum / n;
  }

  double AliasedError(const SEdge &e)
  {
    return Error(e.Exact, e.Aliased.w, e.Aliased.h, [&](size_t i) { return e.Aliased.Px[i].r; });
  }

  double FxaaError(const SEdge &e, const std::vector<SFxaaPixel> &Out)
  {
    return Error(e.Exact, e.Aliased.w, e.Aliased.h, [&](size_t i) { return Out[i].Color.r; });
  }

  //! A nearly horizontal edge: white below the line y = y0 + x / nRun (steps nRun pixels long without anti-aliasing).
  SEdge ShallowEdge(int w, int h, double y0, double nRun)
  {
    return Edge(w, h, [=](double x, double y) { return y > y0 + x / nRun; });
  }
}

//! A picture without edges stays as it is, bit for bit, at every quality: a grey with a faint pattern (a texture's grain
//! below the edge thresholds). Why: FXAA must not soften what is not an edge, and AntiAlias.hlsl does not write the
//! pixels it keeps, so the back buffer keeps them exactly.
XR_TEST(Fxaa_FlatPictureStays)
{
  SPicture In;
  In.w = 32;
  In.h = 16;
  for (int y = 0; y < In.h; y++)
    for (int x = 0; x < In.w; x++)
    {
      const float v = 0.4f + 0.008f * (float)((x * 7 + y * 13) % 5) / 4;
      In.Px.push_back({ v, v * 0.9f, v * 0.8f, 1 });
    }
  for (int q = 0; q <= 3; q++)
  {
    const std::vector<SFxaaPixel> Out = Run(In, q);
    for (size_t i = 0; i < In.Px.size(); i++)
    {
      CHECK(!Out[i].bEdge);
      CHECK(Out[i].Color.r == In.Px[i].r && Out[i].Color.g == In.Px[i].g && Out[i].Color.b == In.Px[i].b && Out[i].Color.a == In.Px[i].a);
    }
  }
}

//! A nearly horizontal edge (steps 8 pixels long) comes out much nearer the exact coverage than drawn without
//! anti-aliasing, at every quality: the steps' pixels get the grey between, the blend moves them up or down (across the
//! edge), only pixels next to the edge change, and nothing leaves 0..1. Why: this is what FXAA is for, the jagged
//! silhouettes of the scene (roofs, the horizon's hills, wires).
XR_TEST(Fxaa_ShallowStaircase)
{
  const SEdge e = ShallowEdge(64, 24, 8.3, 8);
  const double fAliased = AliasedError(e);
  for (int q = 0; q <= 3; q++)
  {
    const std::vector<SFxaaPixel> Out = Run(e.Aliased, q);
    const double fFxaa = FxaaError(e, Out);
    CHECK_MSG(fFxaa < 0.6 * fAliased, "quality " + std::to_string(q) + ": " + std::to_string(fFxaa) + " against " + std::to_string(fAliased));
    for (int y = 0; y < e.Aliased.h; y++)
      for (int x = 0; x < e.Aliased.w; x++)
      {
        const SFxaaPixel &p = Out[(size_t)y * e.Aliased.w + x];
        CHECK(p.Color.r >= 0 && p.Color.r <= 1);
        if (p.Color.r != e.Aliased.At(x, y).r)
        {
          // next to the line, blended across it
          CHECK(std::fabs(y + 0.5 - (8.3 + (x + 0.5) / 8)) < 1.6);
          CHECK(p.nStepX == 0 && std::abs(p.nStepY) == 1);
        }
      }
  }
}

//! The same edge turned by a right angle (nearly vertical, steps 8 pixels high): the blend goes left or right. Why: the
//! direction of the edge decides which neighbour a pixel blends with; the wrong one would smear along the edge.
XR_TEST(Fxaa_SteepStaircase)
{
  const SEdge e = Edge(24, 64, [](double x, double y) { return x > 8.3 + y / 8; });
  const double fAliased = AliasedError(e);
  const std::vector<SFxaaPixel> Out = Run(e.Aliased, 2);
  const double fFxaa = FxaaError(e, Out);
  CHECK_MSG(fFxaa < 0.6 * fAliased, std::to_string(fFxaa) + " against " + std::to_string(fAliased));
  for (size_t i = 0; i < Out.size(); i++)
  {
    if (Out[i].Color.r != e.Aliased.Px[i].r)
      CHECK(Out[i].nStepY == 0 && std::abs(Out[i].nStepX) == 1);
  }
}

//! An edge along the pixels' grid (a column black, the next white) stays sharp: no pixel changes by more than 0.1. Why:
//! such an edge is not aliased; softening it (the HUD-like straight lines of buildings) would only blur the picture.
XR_TEST(Fxaa_StraightEdgeStaysSharp)
{
  const SEdge e = Edge(32, 16, [](double x, double) { return x > 16; });
  for (int q = 0; q <= 3; q++)
  {
    const std::vector<SFxaaPixel> Out = Run(e.Aliased, q);
    for (size_t i = 0; i < Out.size(); i++)
      CHECK_MSG(std::fabs(Out[i].Color.r - e.Aliased.Px[i].r) <= 0.1, "quality " + std::to_string(q) + ", pixel " + std::to_string(i));
  }
}

//! The alpha of every pixel stays, those blended too. Why: the HUD masks through the back buffer's alpha (and the
//! motion blur keeps it); FXAA changes the colour alone.
XR_TEST(Fxaa_AlphaKept)
{
  SEdge e = ShallowEdge(64, 24, 8.3, 8);
  for (size_t i = 0; i < e.Aliased.Px.size(); i++)
    e.Aliased.Px[i].a = 0.25f + (float)(i % 61) / 128;
  const std::vector<SFxaaPixel> Out = Run(e.Aliased, 3);
  int nBlended = 0;
  for (size_t i = 0; i < Out.size(); i++)
  {
    CHECK(Out[i].Color.a == e.Aliased.Px[i].a);
    nBlended += Out[i].Color.r != e.Aliased.Px[i].r ? 1 : 0;
  }
  CHECK(nBlended > 0);
}

//! Long steps (32 pixels): the higher qualities follow the edge farther and come nearer the exact coverage than the
//! lowest. Why: d3d12_AAQuality's steps along the edge are what it buys; at the lowest the search stops before the ends
//! of a long step and the step's middle stays jagged.
XR_TEST(Fxaa_QualityFollowsLongEdges)
{
  const SEdge e = ShallowEdge(128, 16, 6.3, 32);
  const double fLow = FxaaError(e, Run(e.Aliased, 0)), fUltra = FxaaError(e, Run(e.Aliased, 3));
  CHECK_MSG(fUltra < fLow, std::to_string(fUltra) + " against " + std::to_string(fLow));
  CHECK(fUltra < 0.7 * AliasedError(e));
}
