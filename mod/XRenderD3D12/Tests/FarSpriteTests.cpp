// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FarSpriteTests.cpp
//	Description: Unit tests of the far vegetation sprites' pictures: the
//	directions they show (FarSpriteAngles.h, on the CPU), the original's
//	choice of one and d3d12_SpriteBlend's two, and DF_TEX0_ARRAY's
//	coordinates, blend, dissolve and noise as the shaders read them
//	(Shaders/ArrayCoords.hlsli on the GPU, TestHeaders.hlsl).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "FarSpriteAngles.h"

#include "Test_ArrayCoords.h"
#include "Test_BlendPictures.h"
#include "Test_PictureNoise.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XrTest;
using namespace FarSpriteAngles;

namespace
{
  //! a - b brought into -180 to 180 degrees.
  double AngleDiff(double a, double b)
  {
    double d = std::fmod(a - b, 360.0);
    if (d > 180)
      d -= 360;
    if (d < -180)
      d += 360;
    return d;
  }

  //! The weights of the 24 pictures in a blend.
  std::vector<double> Weights(const SBlend &b)
  {
    std::vector<double> w(COUNT, 0.0);
    w[b.n0] += 1 - b.fW;
    w[b.n1] += b.fW;
    return w;
  }
}

// Why: the original shows the picture of the 15 degrees the direction to the sprite falls in (the angle 165 - 15
// floor(direction / 15) of MakeSprite's, brought into 90 to 435), and the bake must draw each picture from the middle of
// its 15 degrees, else the trees show a picture of the wrong side.
XR_TEST(FarSprite_SliceOfDirection)
{
  CHECK(SliceOfDirection(0) == 5);
  CHECK(SliceOfDirection(14.99f) == 5);
  CHECK(SliceOfDirection(15) == 4);
  CHECK(SliceOfDirection(75) == 0);
  CHECK(SliceOfDirection(90) == 23);
  CHECK(SliceOfDirection(359.99f) == 6);
  for (int n = 0; n < COUNT; n++)
  {
    CHECK(SliceOfAngle(90.0f + STEP * n) == n);
    CHECK(SliceOfDirection(ViewDirection(n)) == n);
    CHECK(SliceOfDirection(ViewDirection(n) + 7.4f) == n);
    CHECK(SliceOfDirection(ViewDirection(n) - 7.4f) == n);
    CHECK(ViewDirection(n) >= 0 && ViewDirection(n) < 360);
  }
  CHECK(SliceOfAngle(450) == 0);
  CHECK(SliceOfAngle(75) == 23);
}

// Why: the far trees must end where the original's do, their distance times the engine's factor against the view
// distances; by the distance alone River's went on to 400 m, fogged trees on the cliff tops round the sky in a gap of the
// far mountains, a light block the original does not have.
XR_TEST(FarSprite_SizeFade)
{
  // The original's sprites on River (traced, film river-temple frame 220): the view distance 500 m, the engine's factor
  // 1.31667, the last quads at 303.8 m; a bush with a view distance of its own, 247.8 m, 176.8 m away at 0.485 of its size.
  const float z = 1.31667f, E = 0.8f * 500 / z;
  CHECK(std::fabs(E - 303.8f) < 0.05f);
  CHECK(SizeFade(100, 1000, 500, z) == 1.0f);
  CHECK(SizeFade(E * 7 / 8 - 0.01f, 1000, 500, z) == 1.0f);
  CHECK(std::fabs(SizeFade(E * 15 / 16, 1000, 500, z) - 0.5f) < 1e-4f);
  CHECK(SizeFade(E, 1000, 500, z) <= 0.0f);
  CHECK(SizeFade(350, 1000, 500, z) <= 0.0f);
  CHECK(std::fabs(SizeFade(176.8f, 247.8f, 500, z) - 0.485f) < 0.005f);
  // The call's distance set to 400 (XRENDERTRACED_SPRITE_VIEWDIST): the original's end 243.1 m; the factor set to 2: 200 m.
  CHECK(SizeFade(243.0f, 1000, 400, z) > 0 && SizeFade(243.2f, 1000, 400, z) <= 0);
  CHECK(SizeFade(199.9f, 1000, 500, 2) > 0 && SizeFade(200.1f, 1000, 500, 2) <= 0);
  // The factor 1 (d3d12_SpriteDistFactor 0): the end at 400 m, as before.
  CHECK(SizeFade(399.9f, 1000, 500, 1) > 0 && SizeFade(400.1f, 1000, 500, 1) <= 0);
  CHECK(SizeFade(10, 0, 500, z) == 0.0f);
}

// Why: d3d12_SpriteBlend's two pictures must be the two nearest the direction, weighed by how near each is: a picture
// alone at its own direction, half of each where the original changes from one to the other, and the one that weighs
// more always the original's, so that the blend shows what the original does, only without its jumps.
XR_TEST(FarSprite_BlendOfDirection)
{
  for (int i = 0; i < 7200; i++)
  {
    const float d = i * 0.05f;
    const SBlend b = BlendOfDirection(d);
    CHECK(b.n0 >= 0 && b.n0 < COUNT);
    CHECK(b.n1 == (b.n0 + 1) % COUNT);
    CHECK(b.fW >= 0 && b.fW < 1);
    CHECK_NEAR(AngleDiff(ViewDirection(b.n0) - STEP * b.fW, d), 0, 1e-3);
    if (std::fabs(b.fW - 0.5f) > 1e-3f)
      CHECK((b.fW < 0.5f ? b.n0 : b.n1) == SliceOfDirection(d));
  }
  for (int n = 0; n < COUNT; n++)
  {
    const SBlend b = BlendOfDirection(ViewDirection(n));
    CHECK(Weights(b)[n] > 1 - 1e-4);
    const SBlend e = BlendOfDirection((float)(15 * ((n + 5) % COUNT)));
    CHECK_NEAR(e.fW, 0.5, 1e-4);
  }
}

// Why: the weights of the pictures must change smoothly with the direction, across 0 / 360 degrees as well, else the
// trees jump again where they change.
XR_TEST(FarSprite_BlendContinuous)
{
  const double fStep = 0.01;
  std::vector<double> Prev = Weights(BlendOfDirection(350.0f));
  for (int i = 1; i <= 2000; i++)
  {
    const float d = (float)std::fmod(350.0 + i * fStep, 360.0);
    const std::vector<double> W = Weights(BlendOfDirection(d));
    double fMax = 0;
    for (int n = 0; n < COUNT; n++)
      fMax = std::max(fMax, std::fabs(W[n] - Prev[n]));
    CHECK(fMax < fStep / STEP * 1.05 + 1e-5);
    Prev = W;
  }
}

// Why: d3d12_SpriteBlend is the part of the 15 degrees the two pictures are blended over, about where the original changes
// from one to the other: outside it the original's picture alone (its frame as the original's), inside it the weight
// going from 0 to 1 without a jump, at the whole 15 degrees (1) the blend of the nearest two as it is.
XR_TEST(FarSprite_Band)
{
  for (int i = 0; i < 7200; i++)
  {
    const float d = i * 0.05f;
    const SBlend b = BlendOfDirection(d);
    const SBlend Full = BandBlend(b, 1);
    CHECK(Full.n0 == b.n0 && Full.n1 == b.n1 && Full.fW == b.fW);
    const SBlend s = BandBlend(b, 0.2f);
    const double w = (b.fW - 0.5) / 0.2 + 0.5;
    if (s.n0 == s.n1)
    {
      // Outside the band: the original's picture alone.
      CHECK(s.fW == 0);
      CHECK(w <= 1e-4 || w >= 1 - 1e-4);
      CHECK(s.n0 == (w < 0.5 ? b.n0 : b.n1));
      CHECK(s.n0 == SliceOfDirection(d));
    }
    else
    {
      CHECK(s.n0 == b.n0 && s.n1 == b.n1);
      CHECK(w > -1e-4 && w < 1 + 1e-4);
      CHECK_NEAR(s.fW, w, 1e-5);
    }
  }
  // Continuous across the band's ends and across 0 / 360 degrees.
  const double fStep = 0.01;
  std::vector<double> Prev = Weights(BandBlend(BlendOfDirection(350.0f), 0.2f));
  for (int i = 1; i <= 2000; i++)
  {
    const float d = (float)std::fmod(350.0 + i * fStep, 360.0);
    const std::vector<double> W = Weights(BandBlend(BlendOfDirection(d), 0.2f));
    double fMax = 0;
    for (int n = 0; n < COUNT; n++)
      fMax = std::max(fMax, std::fabs(W[n] - Prev[n]));
    CHECK(fMax < fStep / STEP / 0.2 * 1.05 + 1e-4);
    Prev = W;
  }
  // 0 and below: the original's choice; above 1 as 1.
  CHECK(BandBlend(BlendOfDirection(20), 0).n0 == SliceOfDirection(20));
  CHECK(BandBlend(BlendOfDirection(20), 0).fW == 0);
  CHECK(BandBlend(BlendOfDirection(20), -1).n0 == SliceOfDirection(20));
  CHECK(BandBlend(BlendOfDirection(20), 2).fW == BlendOfDirection(20).fW);
}

// Why: the weight reaches the shaders through the quad's v (FarSprites.cpp): 256 steps from 0 to 1 as an alpha's, the
// ends exact, a weight out of 0 to 1 at the nearest end, and the picture's own v kept.
XR_TEST(FarSprite_BlendV)
{
  CHECK(BlendStep(0) == 1);
  CHECK(BlendStep(1) == 256);
  CHECK(BlendStep(0.5f) == 129);
  CHECK(BlendStep(-1) == 1);
  CHECK(BlendStep(2) == 256);
  CHECK_NEAR(BlendV(0, 0), 2.5, 0);
  CHECK_NEAR(BlendV(1, 1), 513.5, 0);
}

// Why: the shaders must read the slice, the picture's coordinates and the blend back as FarSprites.cpp writes them: a
// single slice's coordinates (v 0 to 1: the other DF_TEX0_ARRAY draws, the sun's cascades' pictures) as they are, the
// slice after the last the first, the weight within half a step; and a pixel's neighbours outside the quad (the
// derivatives' helper pixels, v' a little below 0 or above 1) the same slices and weight.
XR_TEST(FarSprite_ArrayCoords)
{
  struct SCase
  {
    int n;
    float u, v, fW;   // fW < 0: one slice
  };
  std::vector<SCase> Cases;
  for (int n : { 0, 5, 22, 23 })
    for (float u : { 0.0f, 0.25f, 1.0f })
      for (float v : { -0.2f, 0.0f, 0.5f, 1.0f, 1.2f })
        for (float fW : { -1.0f, 0.0f, 0.3f, 0.5f, 0.999f, 1.0f })
          Cases.push_back({ n, u, v, fW });
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_ArrayCoords);
  d.Test.nTest[0] = (uint32)Cases.size();
  d.nGroups[0] = Groups64(Cases.size());
  for (const SCase &c : Cases)
    Push(d.In, 2.0f * c.n + c.u, c.fW < 0 ? c.v : BlendV(c.v, c.fW), (float)COUNT, 0);
  d.nOut = 2 * Cases.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SCase &c = Cases[i];
    CHECK_NEAR(At(Out, 2 * i, 0), c.u, 1e-5);
    CHECK_NEAR(At(Out, 2 * i, 1), c.v, 1e-4);
    CHECK_NEAR(At(Out, 2 * i, 2), c.n, 0);
    CHECK_NEAR(At(Out, 2 * i + 1, 0), c.u, 1e-5);
    CHECK_NEAR(At(Out, 2 * i + 1, 1), c.v, 1e-4);
    CHECK_NEAR(At(Out, 2 * i + 1, 2), c.fW < 0 ? c.n : (c.n + 1) % COUNT, 0);
    if (c.fW < 0)
      CHECK_NEAR(At(Out, 2 * i, 3), 0, 0);
    else
      CHECK_NEAR(At(Out, 2 * i, 3), c.fW, 0.5 / 255 + 1e-6);
  }
}

// Why: a blend must show what covers: the colour of a picture's empty texels (whatever the bake left there) never
// shows, the covered parts blend, and at weight 0 it is the slice itself, bit for bit (d3d12_SpriteBlend 0 and the
// cascades' pictures as before). The dissolve takes each picture's alpha as it is, b's where the noise is below the
// weight, so that the alpha test keeps as many of the pixels only one picture covers as its weight says.
XR_TEST(FarSprite_BlendPictures)
{
  struct SCase
  {
    float a[4], b[4], fW, fNoise;
    float Blend[4], Dissolve[4];
  };
  const SCase Cases[] = {
    { { 0.2f, 0.4f, 0.6f, 0.7f }, { 0.9f, 0.1f, 0.3f, 0.4f }, 0, 0, { 0.2f, 0.4f, 0.6f, 0.7f }, { 0.2f, 0.4f, 0.6f, 0.7f } },
    { { 0.2f, 0.4f, 0.6f, 0 }, { 0.9f, 0.1f, 0.3f, 1 }, 0, 0, { 0.2f, 0.4f, 0.6f, 0 }, { 0.2f, 0.4f, 0.6f, 0 } },
    { { 1, 0, 0, 1 }, { 0, 1, 0, 0 }, 0.5f, 0.6f, { 1, 0, 0, 0.5f }, { 1, 0, 0, 1 } },
    { { 1, 0, 0, 1 }, { 0, 1, 0, 0 }, 0.5f, 0.3f, { 1, 0, 0, 0.5f }, { 1, 0, 0, 0 } },
    { { 1, 0, 0, 1 }, { 0, 0, 1, 1 }, 0.25f, 0.1f, { 0.75f, 0, 0.25f, 1 }, { 0.75f, 0, 0.25f, 1 } },
    { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, 0.5f, 0.5f, { 0.5f, 0.5f, 0, 0 }, { 0.5f, 0.5f, 0, 0 } },
    { { 1, 1, 1, 1 }, { 0.2f, 0.3f, 0.4f, 0.5f }, 1, 0.99f, { 0.2f, 0.3f, 0.4f, 0.5f }, { 0.2f, 0.3f, 0.4f, 0.5f } },
  };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_BlendPictures);
  d.Test.nTest[0] = (uint32)std::size(Cases);
  d.nGroups[0] = 1;
  for (const SCase &c : Cases)
  {
    Push(d.In, c.a[0], c.a[1], c.a[2], c.a[3]);
    Push(d.In, c.b[0], c.b[1], c.b[2], c.b[3]);
    Push(d.In, c.fW, c.fNoise, 0, 0);
  }
  d.nOut = 2 * std::size(Cases);
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < std::size(Cases); i++)
    for (int k = 0; k < 4; k++)
    {
      // Weight 0: the slice's texel as it is.
      const double fEps = Cases[i].fW == 0 ? 0 : 1e-6;
      CHECK_NEAR(At(Out, 2 * i, k), Cases[i].Blend[k], fEps);
      CHECK_NEAR(At(Out, 2 * i + 1, k), Cases[i].Dissolve[k], fEps);
    }
}

// Why: the dissolve keeps as many pixels as the weight says only if the noise spreads its values evenly: over a picture
// and over every few pixels (a tree far away is a few pixels wide), and, the frames' numbers moving it along (a temporal
// anti-aliasing gathers them), over the frames at each pixel.
XR_TEST(FarSprite_PictureNoise)
{
  const int nSize = 64, nFrames = 64;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_PictureNoise);
  const size_t n = (size_t)nSize * nSize * nFrames;
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  for (int f = 0; f < nFrames; f++)
    for (int y = 0; y < nSize; y++)
      for (int x = 0; x < nSize; x++)
        Push(d.In, x + 0.5f, y + 0.5f, (float)f, 0);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  auto Noise = [&](int x, int y, int f) { return At(Out, ((size_t)f * nSize + y) * nSize + x, 0); };
  for (size_t i = 0; i < n; i++)
  {
    CHECK(At(Out, i, 0) >= 0 && At(Out, i, 0) < 1);
  }
  for (const float fW : { 0.1f, 0.25f, 0.5f, 0.75f, 0.9f })
  {
    // The picture of frame 0, and each block of 8 x 8 pixels.
    int nAll = 0;
    double fBlockWorst = 0;
    for (int by = 0; by < nSize; by += 8)
      for (int bx = 0; bx < nSize; bx += 8)
      {
        int nBlock = 0;
        for (int y = by; y < by + 8; y++)
          for (int x = bx; x < bx + 8; x++)
            nBlock += Noise(x, y, 0) < fW;
        nAll += nBlock;
        fBlockWorst = std::max(fBlockWorst, std::fabs(nBlock / 64.0 - fW));
      }
    CHECK_NEAR(nAll / double(nSize * nSize), fW, 0.005);
    CHECK(fBlockWorst < 0.06);
    // Each pixel over the frames.
    double fPixelWorst = 0;
    for (int y = 0; y < nSize; y++)
      for (int x = 0; x < nSize; x++)
      {
        int nKept = 0;
        for (int f = 0; f < nFrames; f++)
          nKept += Noise(x, y, f) < fW;
        fPixelWorst = std::max(fPixelWorst, std::fabs(nKept / double(nFrames) - fW));
      }
    CHECK(fPixelWorst < 0.07);
  }
  // The pixel's position, not where in it: the same noise across a pixel.
  SDispatch e = d;
  e.In.clear();
  e.Test.nTest[0] = 2;
  e.nGroups[0] = 1;
  e.nOut = 2;
  Push(e.In, 10.01f, 20.01f, 0, 0);
  Push(e.In, 10.99f, 20.99f, 0, 0);
  const std::vector<uint32> Out2 = Gpu().Run(e);
  CHECK_NEAR(At(Out2, 0, 0), At(Out2, 1, 0), 0);
  CHECK_NEAR(At(Out2, 0, 0), Noise(10, 20, 0), 0);
}
