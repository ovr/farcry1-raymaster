// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AntiAliasTests.cpp
//	Description: Unit tests of the anti-aliasing without an upscaler on
//	the GPU (d3d12_AA, PostAA.cpp): FXAA (Shaders/Fxaa.hlsli) and SMAA 1x
//	(Shaders/Smaa.hlsli with third_party/smaa, SmaaBlend.hlsli; its three
//	passes in turn, each one's output the next one's texture as the
//	renderer's targets store it) on small pictures of edges drawn without
//	anti-aliasing (a pixel white where its centre is on the white side),
//	against the exact coverage of each pixel by the white side.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_Fxaa.h"
#include "Test_SmaaEdges.h"
#include "Test_SmaaWeightsLow.h"
#include "Test_SmaaWeightsHigh.h"
#include "Test_SmaaWeightsUltra.h"
#include "Test_SmaaBlend.h"
// SMAA's precomputed textures, as PostAA.cpp uploads them.
#include "Textures/AreaTex.h"
#include "Textures/SearchTex.h"

#include <algorithm>

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

  //! v (0..1) as a UNORM byte, as a target stores it.
  uint8_t Unorm8(float v)
  {
    return (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255);
  }

  //! nChannels of each item of v as an 8-bit texture of w x h texels (R8, R8G8 or R8G8B8A8_UNORM).
  STestTexture Texture8(int w, int h, int nChannels, const std::function<float(size_t, int)> &v)
  {
    STestTexture t;
    t.nWidth = (uint32)w;
    t.nHeight = (uint32)h;
    t.Format = nChannels == 1 ? DXGI_FORMAT_R8_UNORM : nChannels == 2 ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    for (size_t i = 0; i < (size_t)w * h; i++)
      for (int c = 0; c < nChannels; c++)
        t.Data.push_back(Unorm8(v(i, c)));
    return t;
  }

  STestTexture PictureTexture(const SPicture &In)
  {
    return Texture8(In.w, In.h, 4, [&](size_t i, int c) { const SRgba &p = In.Px[i]; return c == 0 ? p.r : c == 1 ? p.g : c == 2 ? p.b : p.a; });
  }

  //! What SMAA's passes gave the pixels of a picture.
  struct SSmaa
  {
    std::vector<float> Edges;     //!< 2 a pixel: its left and top edges
    std::vector<float> Weights;   //!< 4 a pixel: its blending weights
    std::vector<SRgba> Color;     //!< as AntiAlias.hlsl leaves it
    std::vector<float> Shares;    //!< 2 a pixel: its neighbours' shares
    std::vector<bool> Blend;      //!< blended (else not written)
    std::vector<bool> Across;     //!< blended with the neighbours left and right (else up and down)
  };

  SDispatch SmaaDispatch(SBytecode Shader, const SPicture &In)
  {
    SDispatch d;
    d.Shader = Shader;
    const size_t n = In.Px.size();
    d.Test.nTest[0] = (uint32)n;
    d.Test.nTest[1] = (uint32)In.w;
    d.Test.nTest[2] = (uint32)In.h;
    d.nGroups[0] = Groups64(n);
    d.nOut = n;
    return d;
  }

  //! The blending pass alone, of the picture with the weights given (4 a pixel).
  void SmaaBlend(const SPicture &In, SSmaa &r)
  {
    const size_t n = In.Px.size();
    SDispatch d = SmaaDispatch(XR_BYTECODE(Test_SmaaBlend), In);
    d.Textures = { PictureTexture(In), Texture8(In.w, In.h, 4, [&](size_t i, int c) { return r.Weights[i * 4 + c]; }) };
    d.nOut = 2 * n;
    const std::vector<uint32> Out = Gpu().Run(d);
    r.Color.resize(n);
    r.Shares.resize(2 * n);
    r.Blend.resize(n);
    r.Across.resize(n);
    for (size_t i = 0; i < n; i++)
    {
      r.Color[i] = { At(Out, i, 0), At(Out, i, 1), At(Out, i, 2), At(Out, i, 3) };
      r.Shares[2 * i] = At(Out, n + i, 0);
      r.Shares[2 * i + 1] = At(Out, n + i, 1);
      r.Blend[i] = At(Out, n + i, 2) > 0.5f;
      r.Across[i] = At(Out, n + i, 3) > 0.5f;
    }
  }

  //! SMAA 1x of the picture: the edges (the high preset's threshold), the weights by the kernel Weights (a preset's),
  //! the blending.
  SSmaa Smaa(const SPicture &In, SBytecode Weights)
  {
    const size_t n = In.Px.size();
    SSmaa r;
    SDispatch d = SmaaDispatch(XR_BYTECODE(Test_SmaaEdges), In);
    d.Textures = { PictureTexture(In) };
    std::vector<uint32> Out = Gpu().Run(d);
    for (size_t i = 0; i < n; i++)
      r.Edges.insert(r.Edges.end(), { At(Out, i, 0), At(Out, i, 1) });
    d = SmaaDispatch(Weights, In);
    STestTexture Area, Search;
    Area.nWidth = AREATEX_WIDTH;
    Area.nHeight = AREATEX_HEIGHT;
    Area.Format = DXGI_FORMAT_R8G8_UNORM;
    Area.Data.assign(areaTexBytes, areaTexBytes + AREATEX_SIZE);
    Search.nWidth = SEARCHTEX_WIDTH;
    Search.nHeight = SEARCHTEX_HEIGHT;
    Search.Format = DXGI_FORMAT_R8_UNORM;
    Search.Data.assign(searchTexBytes, searchTexBytes + SEARCHTEX_SIZE);
    d.Textures = { Texture8(In.w, In.h, 2, [&](size_t i, int c) { return r.Edges[i * 2 + c]; }), Area, Search };
    Out = Gpu().Run(d);
    for (size_t i = 0; i < n; i++)
      r.Weights.insert(r.Weights.end(), { At(Out, i, 0), At(Out, i, 1), At(Out, i, 2), At(Out, i, 3) });
    SmaaBlend(In, r);
    return r;
  }

  //! The sRGB transfer function (IEC 61966-2-1) of a linear value, and its inverse.
  double ToGamma(double c)
  {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
  }

  double ToLinear(double c)
  {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  }

  //! SMAA blends in linear light: the light of its red (decoded from sRGB) against the exact coverage (the light of a
  //! pixel that much white).
  double SmaaError(const SEdge &e, const SSmaa &r)
  {
    return Error(e.Exact, e.Aliased.w, e.Aliased.h, [&](size_t i) { return (float)ToLinear(r.Color[i].r); });
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

//! A picture without edges keeps every pixel, at every preset: a grey with a faint pattern below SMAA's thresholds has
//! no edges, no blending weights and nothing blended. Why: SMAA must not soften what is not an edge; AntiAlias.hlsl
//! writes only the pixels it blends, so the back buffer keeps the rest bit for bit (and the edge kernel writes "no
//! edges" where SMAA's pixel shader would discard).
XR_TEST(Smaa_FlatPictureStays)
{
  SPicture In;
  In.w = 32;
  In.h = 16;
  for (int y = 0; y < In.h; y++)
    for (int x = 0; x < In.w; x++)
    {
      const float v = 0.4f + 0.02f * (float)((x * 7 + y * 13) % 5) / 4;
      In.Px.push_back({ v, v * 0.9f, v * 0.8f, 1 });
    }
  for (SBytecode Weights : { XR_BYTECODE(Test_SmaaWeightsLow), XR_BYTECODE(Test_SmaaWeightsHigh), XR_BYTECODE(Test_SmaaWeightsUltra) })
  {
    const SSmaa r = Smaa(In, Weights);
    for (size_t i = 0; i < In.Px.size(); i++)
    {
      CHECK(r.Edges[2 * i] == 0 && r.Edges[2 * i + 1] == 0);
      CHECK(!r.Blend[i]);
      CHECK(Unorm8(r.Color[i].r) == Unorm8(In.Px[i].r) && Unorm8(r.Color[i].g) == Unorm8(In.Px[i].g) &&
            Unorm8(r.Color[i].b) == Unorm8(In.Px[i].b));
    }
  }
}

//! The edges: a step of luma 0.2 down the picture (above the high preset's 0.1) is the left edge of the column right of
//! it and of no other pixel; a step of 0.05 is no edge. Why: the edges decide which pixels SMAA touches at all; the
//! threshold keeps the textures' grain out, and an edge is a pixel's own left or top side (what its weights blend
//! across).
XR_TEST(Smaa_EdgesAtTheThreshold)
{
  for (float fStep : { 0.2f, 0.05f })
  {
    SPicture In;
    In.w = 16;
    In.h = 8;
    for (int y = 0; y < In.h; y++)
      for (int x = 0; x < In.w; x++)
      {
        const float v = x < 8 ? 0.3f : 0.3f + fStep;
        In.Px.push_back({ v, v, v, 1 });
      }
    const SSmaa r = Smaa(In, XR_BYTECODE(Test_SmaaWeightsHigh));
    for (int y = 0; y < In.h; y++)
      for (int x = 0; x < In.w; x++)
      {
        const size_t i = (size_t)y * In.w + x;
        CHECK_MSG(r.Edges[2 * i] == (fStep > 0.1f && x == 8 ? 1.0f : 0.0f), "step " + std::to_string(fStep) + ", pixel " + std::to_string(i));
        CHECK(r.Edges[2 * i + 1] == 0);
      }
  }
}

//! A nearly horizontal edge (steps 8 pixels long) comes out much nearer the exact coverage than drawn without
//! anti-aliasing, at the low, high and ultra presets: only the pixels next to the edge change, blended up or down (across
//! it), and nothing leaves 0..1. Why: this is what SMAA is for, the jagged silhouettes of the scene; its weights come
//! from the area texture by the steps' lengths, which the search along the edge finds.
XR_TEST(Smaa_ShallowStaircase)
{
  const SEdge e = ShallowEdge(64, 24, 8.3, 8);
  const double fAliased = AliasedError(e);
  for (SBytecode Weights : { XR_BYTECODE(Test_SmaaWeightsLow), XR_BYTECODE(Test_SmaaWeightsHigh), XR_BYTECODE(Test_SmaaWeightsUltra) })
  {
    const SSmaa r = Smaa(e.Aliased, Weights);
    const double fSmaa = SmaaError(e, r);
    CHECK_MSG(fSmaa < 0.6 * fAliased, std::to_string(fSmaa) + " against " + std::to_string(fAliased));
    for (int y = 0; y < e.Aliased.h; y++)
      for (int x = 0; x < e.Aliased.w; x++)
      {
        const size_t i = (size_t)y * e.Aliased.w + x;
        CHECK(r.Color[i].r >= 0 && r.Color[i].r <= 1);
        if (r.Blend[i])
        {
          CHECK(std::fabs(y + 0.5 - (8.3 + (x + 0.5) / 8)) < 1.6);
          CHECK(!r.Across[i]);
        }
      }
  }
}

//! The same edge turned by a right angle (nearly vertical, steps 8 pixels high): the blend goes left or right. Why: the
//! pair of weights SMAA's blending picks decides which neighbours a pixel blends with; the wrong one would smear along
//! the edge.
XR_TEST(Smaa_SteepStaircase)
{
  const SEdge e = Edge(24, 64, [](double x, double y) { return x > 8.3 + y / 8; });
  const SSmaa r = Smaa(e.Aliased, XR_BYTECODE(Test_SmaaWeightsHigh));
  const double fSmaa = SmaaError(e, r), fAliased = AliasedError(e);
  CHECK_MSG(fSmaa < 0.6 * fAliased, std::to_string(fSmaa) + " against " + std::to_string(fAliased));
  int nBlended = 0;
  for (size_t i = 0; i < r.Blend.size(); i++)
  {
    if (r.Blend[i])
    {
      CHECK(r.Across[i]);
      nBlended++;
    }
  }
  CHECK(nBlended > 0);
}

//! An edge along the pixels' grid (a column black, the next white) stays sharp: no pixel changes by more than 0.1.
//! Why: such an edge is not aliased (no steps, so no area to blend); softening it would only blur the picture.
XR_TEST(Smaa_StraightEdgeStaysSharp)
{
  const SEdge e = Edge(32, 16, [](double x, double) { return x > 16; });
  for (SBytecode Weights : { XR_BYTECODE(Test_SmaaWeightsLow), XR_BYTECODE(Test_SmaaWeightsHigh), XR_BYTECODE(Test_SmaaWeightsUltra) })
  {
    const SSmaa r = Smaa(e.Aliased, Weights);
    for (size_t i = 0; i < r.Color.size(); i++)
      CHECK_MSG(std::fabs(r.Color[i].r - e.Aliased.Px[i].r) <= 0.1, "pixel " + std::to_string(i));
  }
}

//! The alpha of every pixel stays, those blended too. Why: the HUD masks through the back buffer's alpha; SMAA's blend
//! (whose bilinear taps would mix it) changes the colour alone here.
XR_TEST(Smaa_AlphaKept)
{
  SEdge e = ShallowEdge(64, 24, 8.3, 8);
  for (size_t i = 0; i < e.Aliased.Px.size(); i++)
    e.Aliased.Px[i].a = 0.25f + (float)(i % 61) / 128;
  const SSmaa r = Smaa(e.Aliased, XR_BYTECODE(Test_SmaaWeightsHigh));
  int nBlended = 0;
  for (size_t i = 0; i < r.Color.size(); i++)
  {
    CHECK(Unorm8(r.Color[i].a) == Unorm8(e.Aliased.Px[i].a));
    nBlended += r.Blend[i] ? 1 : 0;
  }
  CHECK(nBlended > 0);
}

//! The blending is in linear light: a black pixel whose right neighbour is white, with the weight 0.5 towards it, comes
//! out as half the light of white (sRGB 0.735), not as the gamma value 0.5; a weight on the pixel's own left side
//! (blue) blends it with the left neighbour. Why: SMAA asks for this pass's sRGB read and write (the UNORM back buffer
//! has neither); blended in gamma, the edges between dark and bright would come out too dark.
XR_TEST(Smaa_BlendsInLinearLight)
{
  SPicture In;
  In.w = 16;
  In.h = 4;
  for (int y = 0; y < In.h; y++)
    for (int x = 0; x < In.w; x++)
    {
      const float v = x < 8 ? 0.0f : 1.0f;
      In.Px.push_back({ v, v, v, 1 });
    }
  SSmaa r;
  r.Weights.assign(In.Px.size() * 4, 0.0f);
  // Row 1: the pixel (7, 1) by the alpha of its right neighbour (8, 1); row 2: the pixel (8, 2) by its own blue.
  r.Weights[((size_t)1 * In.w + 8) * 4 + 3] = 0.5f;
  r.Weights[((size_t)2 * In.w + 8) * 4 + 2] = 0.5f;
  SmaaBlend(In, r);
  const size_t a = (size_t)1 * In.w + 7, b = (size_t)2 * In.w + 8;
  CHECK(r.Blend[a] && r.Across[a] && r.Blend[b] && r.Across[b]);
  CHECK_MSG(std::fabs(r.Color[a].r - ToGamma(0.5)) < 0.003, std::to_string(r.Color[a].r));
  CHECK_MSG(std::fabs(r.Color[b].r - ToGamma(0.5)) < 0.003, std::to_string(r.Color[b].r));
  for (size_t i = 0; i < r.Blend.size(); i++)
    CHECK(r.Blend[i] == (i == a || i == b));
}

//! Long steps (32 pixels): the ultra preset (32 search steps) follows them to their ends and comes nearer the exact
//! coverage than the low one (4). Why: d3d12_AAQuality's search distance is what the presets buy; the low one stops
//! before the ends of a long step and leaves its middle jagged.
XR_TEST(Smaa_QualityFollowsLongEdges)
{
  const SEdge e = ShallowEdge(128, 16, 6.3, 32);
  const double fLow = SmaaError(e, Smaa(e.Aliased, XR_BYTECODE(Test_SmaaWeightsLow)));
  const double fUltra = SmaaError(e, Smaa(e.Aliased, XR_BYTECODE(Test_SmaaWeightsUltra)));
  CHECK_MSG(fUltra < fLow, std::to_string(fUltra) + " against " + std::to_string(fLow));
  CHECK(fUltra < 0.7 * AliasedError(e));
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
