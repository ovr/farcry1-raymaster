// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PondTests.cpp
//	Description: Unit tests of the ponds' ripple (d3d12_WaterPondRipple,
//	PondRipple.h): the original's height maps' slopes in the wave
//	texture's form (their sign and axes, the RMS over all frames, the
//	square in alpha, the tile's wrap, flat heights), and the sequence's
//	frame and drift by the time as the original's.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "PondRipple.h"

#include <cmath>
#include <vector>

namespace
{
  constexpr double PI = 3.14159265358979323846;
  double Decode(uint8_t b) { return (b - 128) / 32.0; }
}

// Why: the water reads red as dH/du (u along the world's x) and green as dH/dv, both 32 for the RMS about 128 like the
// wave texture's, and alpha their square for the variance the mip levels smooth out: a height rising along x must give
// red above 128 there and green 128, the slopes' RMS along an axis 1 over all frames together, alpha the square.
XR_TEST(Pond_RippleSlopes)
{
  constexpr int N = PondRipple::SIZE, F = 2;
  std::vector<float> H((size_t)N * N * F);
  for (int f = 0; f < F; f++)
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
        H[((size_t)f * N + y) * N + x] = (float)(100 + 40 * std::sin(2 * PI * 2 * x / N + f));
  const std::vector<uint8_t> P = PondRipple::MakeSlopes(H, N, F);
  CHECK(P.size() == (size_t)N * N * F * 4);
  double fSq = 0, fHeightSq = 0;
  int nSign = 0, nAlpha = 0;
  for (int f = 0; f < F; f++)
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        const uint8_t *p = &P[(((size_t)f * N + y) * N + x) * 4];
        const double dx = Decode(p[2]), dy = Decode(p[1]);
        CHECK_NEAR(dy, 0, 0);
        const double fDeriv = std::cos(2 * PI * 2 * x / N + f);
        if (std::abs(fDeriv) > 0.2 && (dx > 0) == (fDeriv > 0))
          nSign++;
        else if (std::abs(fDeriv) > 0.2)
          nSign -= 1000;
        if (std::abs(p[3] - (dx * dx + dy * dy) * 255 / PondRipple::SLOPE_SQ_RANGE) <= 0.5)
          nAlpha++;
        fSq += 0.5 * (dx * dx + dy * dy);
        fHeightSq += Decode(p[0]) * Decode(p[0]);
      }
  CHECK(nSign > 0);
  CHECK(nAlpha == N * N * F);
  CHECK_NEAR(std::sqrt(fSq / (N * N * F)), 1, 0.02);
  CHECK_NEAR(std::sqrt(fHeightSq / (N * N * F)), 1, 0.02);
}

// Why: the tile repeats every 8 m, so the slopes at its edges take their neighbours across the wrap: a wave whole on the
// tile has the same slope at its first and last rows as a sine's derivative there (no seam where the tiles meet).
XR_TEST(Pond_RippleWrap)
{
  constexpr int N = PondRipple::SIZE;
  std::vector<float> H((size_t)N * N);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
      H[(size_t)y * N + x] = (float)std::sin(2 * PI * y / N);
  const std::vector<uint8_t> P = PondRipple::MakeSlopes(H, N, 1);
  // dH/dv = cos(2 pi v / N): largest at v 0 (the wrap), as at v N / 2 with the other sign.
  const double fTop = Decode(P[1]), fBottom = Decode(P[((size_t)(N - 1) * N) * 4 + 1]), fMiddle = Decode(P[((size_t)(N / 2) * N) * 4 + 1]);
  CHECK(fTop > 1.3);
  CHECK_NEAR(fTop, -fMiddle, 1 / 32.0);
  CHECK_NEAR(fTop, fBottom, 1 / 16.0);
  for (int x = 0; x < N; x++)
    CHECK_NEAR(Decode(P[(size_t)x * 4 + 2]), 0, 0);
}

// Why: still water's heights have no slope: flat (128) and no variance, not a division by zero.
XR_TEST(Pond_RippleFlat)
{
  constexpr int N = PondRipple::SIZE;
  const std::vector<float> H((size_t)N * N * 3, 77.0f);
  const std::vector<uint8_t> P = PondRipple::MakeSlopes(H, N, 3);
  bool bFlat = true;
  for (size_t i = 0; i < P.size(); i += 4)
    bFlat = bFlat && P[i] == 128 && P[i + 1] == 128 && P[i + 2] == 128 && P[i + 3] == 0;
  CHECK(bFlat);
}

// Why: the original shows frame (int)(t / 0.05) modulo 32 and drifts the tile by 0.06 and 0.12 of it a second: the
// frame's whole part must be that frame (the shader blends towards the next by the rest), looping after 1.6 s; the drift
// a tile's fraction.
XR_TEST(Pond_RippleFrame)
{
  using namespace PondRipple;
  CHECK_NEAR(Frame(0), 0, 0);
  CHECK_NEAR(Frame(0.075), 1.5, 1e-9);
  CHECK_NEAR(Frame(-1), 0, 0);
  int nSame = 0;
  for (int i = 0; i < 4000; i++)
  {
    const double t = 0.0123 + i * 0.01;
    const double f = Frame(t);
    CHECK(f >= 0 && f < FRAMES);
    if ((int)std::floor(f) == (int)(t / FRAME_TIME) % FRAMES)
      nSame++;
  }
  CHECK(nSame == 4000);
  CHECK_NEAR(Frame(FRAMES * FRAME_TIME + 0.025), 0.5, 1e-9);
  CHECK_NEAR(Drift(10, 0), 0.6, 1e-12);
  CHECK_NEAR(Drift(10, 1), 0.2, 1e-12);
  for (double t : { 0.0, 3.3, 1234.5, 86400.0 })
    for (int a = 0; a < 2; a++)
      CHECK(Drift(t, a) >= 0 && Drift(t, a) < 1);
}
