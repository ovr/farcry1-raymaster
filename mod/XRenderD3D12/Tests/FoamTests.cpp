// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FoamTests.cpp
//	Description: Unit tests of the sea's foam (d3d12_WaterFoam): Monahan's
//	whitecaps of the wind and the threshold of the crests' determinant
//	(OceanSpectrum.h), the pattern (FoamPattern.h: uniform, tiling, a lace
//	of several sizes), and the water's foam functions on the GPU
//	(Shaders/WaterFoam.hlsli, TestHeaders.hlsl): the shore's breaking
//	waves, the share of a pixel white, the pattern's two samples as one,
//	the whitecaps' modulation by the long waves, the breaker index's noise
//	and the foam's light. The foam's kernel is OceanTests.cpp's.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"
#include "OceanSpectrum.h"
#include "FoamPattern.h"

#include "Test_Foam.h"
#include "Test_FoamMix.h"
#include "Test_FoamLight.h"
#include "Test_FoamContact.h"
#include "Test_FoamSurf.h"
#include "Test_FoamClumps.h"
#include "Test_FoamShade.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace XrTest;

namespace
{
  //! The kernel's out[i] of each item: Test_Foam's of (depth, crest, Hs, index), (amount, u, width, blur), (a, b).
  struct SFoamItem
  {
    float fDepth, fCrest, fHs, fIndex;
    float fAmount, u, fWidth, fBlur;
    float a, b;
  };
  std::vector<uint32> RunFoam(const std::vector<SFoamItem> &Items)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_Foam);
    d.Test.nTest[0] = (uint32)Items.size();
    d.nGroups[0] = Groups64(Items.size());
    for (const SFoamItem &it : Items)
    {
      Push(d.In, it.fDepth, it.fCrest, it.fHs, it.fIndex);
      Push(d.In, it.fAmount, it.u, it.fWidth, it.fBlur);
      Push(d.In, it.a, it.b, 0, 0);
    }
    d.nOut = Items.size();
    return Gpu().Run(d);
  }

  //! Test_FoamMix's of (f0, f1, height, 1 / RMS), (n, p.x, p.y).
  struct SMixItem
  {
    float f0, f1, fHeight, fInvRMS;
    float n, px, py;
  };
  std::vector<uint32> RunMix(const std::vector<SMixItem> &Items)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_FoamMix);
    d.Test.nTest[0] = (uint32)Items.size();
    d.nGroups[0] = Groups64(Items.size());
    for (const SMixItem &it : Items)
    {
      Push(d.In, it.f0, it.f1, it.fHeight, it.fInvRMS);
      Push(d.In, it.n, it.px, it.py, 0);
    }
    d.nOut = Items.size();
    return Gpu().Run(d);
  }

  SFoamItem Shore(float fDepth, float fCrest, float fHs, float fIndex)
  {
    return { fDepth, fCrest, fHs, fIndex, 0, 0, 0.02f, 0, 0, 0 };
  }

  SFoamItem Cover(float fAmount, float u, float fWidth, float fBlur)
  {
    return { 1, 0, 0, 0.78f, fAmount, u, fWidth, fBlur, 0, 0 };
  }
}

// Why: the whitecaps' share of the sea is Monahan and O'Muircheartaigh's law of the wind, 3.84e-6 U^3.41: 0.03 % at
// 3.5 m/s (a few), 1 % at 10, 3.9 % at 15, none without wind.
XR_TEST(Foam_WhitecapLaw)
{
  using namespace OceanSpectrum;
  CHECK_NEAR(WhitecapCoverage(3.5) / 2.75e-4, 1, 0.01);
  CHECK_NEAR(WhitecapCoverage(10) / 9.87e-3, 1, 0.01);
  CHECK_NEAR(WhitecapCoverage(15) / 3.93e-2, 1, 0.01);
  CHECK_NEAR(WhitecapCoverage(0), 0, 0);
  double fBefore = 0;
  for (double U = 1; U <= 25; U += 1)
  {
    CHECK(WhitecapCoverage(U) > fBefore);
    fBefore = WhitecapCoverage(U);
  }
}

// Why: the crests' threshold is the normal quantile of the whitecaps' share (a few in ten thousand at a light wind):
// the share of the normal distribution below it must be the share, far into its tail.
XR_TEST(Foam_NormalQuantile)
{
  using namespace OceanSpectrum;
  for (double p : { 1e-7, 1e-5, 2.7e-4, 1e-3, 0.0123, 0.1, 0.3, 0.5, 0.8 })
  {
    const double z = NormalQuantile(p);
    CHECK_NEAR(0.5 * std::erfc(-z / std::sqrt(2.0)) / p, 1, 1e-9);
    CHECK_NEAR(NormalQuantile(1 - p), -z, 1e-9);
  }
  CHECK_NEAR(NormalQuantile(0.025), -1.959964, 1e-6);
  CHECK_NEAR(NormalQuantile(0.5), 0, 1e-12);
}

// Why: to its first order the determinant of a cascade's displacement is 1 + chop times its divergence, normal with the
// RMS of the cascade's slopes (the divergence of D = i k / |k| H is -|k| H); the threshold must leave below it the
// cascade's share of the whitecaps breaking at a time, the short cascade none.
XR_TEST(Foam_WhitecapThreshold)
{
  using namespace OceanSpectrum;
  for (double fWind : { 5.0, 10.0, 15.0 })
  {
    SSea Sea;
    MakeSea(fWind, Sea);
    for (int c = 0; c < CASCADES; c++)
    {
      const double fChop = 2.5, s = Sea.JacobianSigma(c, fChop), fJ = Sea.WhitecapJacobian(c, fChop);
      CHECK_NEAR(s, fChop * std::sqrt(Sea.Cascades[c].fSlopeVarX + Sea.Cascades[c].fSlopeVarY), 1e-12);
      if (WHITECAP_SHARE[c] <= 0)
      {
        CHECK_NEAR(fJ, -1, 0);
        continue;
      }
      const double fShare = WhitecapCoverage(fWind) * WHITECAP_SHARE[c] * WHITECAP_ACTIVE;
      CHECK_NEAR(0.5 * std::erfc(-((fJ - 1) / s) / std::sqrt(2.0)) / fShare, 1, 1e-6);
      CHECK(fJ < 1);
    }
    CHECK(Sea.SignificantHeight() > 0 && Sea.PeakSpeed() > 0);
  }
}

// Why: the foam is white where the pattern is above one less the amount, so the share white is the amount only if each
// value is as often as any other; the texture wraps, so its edges must join as its rows and columns inside do (no seam
// on the sea every 3.7 m); and it is a lace, near texels alike, far ones not.
XR_TEST(Foam_PatternUniform)
{
  const int n = FoamPattern::SIZE;
  const std::vector<uint8_t> P = FoamPattern::Make(n);
  CHECK(P.size() == (size_t)n * n);
  std::vector<int> Count(256, 0);
  for (uint8_t v : P)
    Count[v]++;
  const int fEach = n * n / 256;
  for (int c : Count)
    CHECK(std::abs(c - fEach) <= 1);
  auto At = [&](int x, int y) { return (double)P[(size_t)((y + n) % n) * n + (x + n) % n]; };
  // The mean step between a row and the next (and a column and the next): across the edges no larger than between the
  // rows inside (they differ by up to 1.4 times the mean where the cells' borders of all the octaves meet).
  double fRows = 0, fCols = 0, fEdgeRow = 0, fEdgeCol = 0;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++)
    {
      const double r = std::fabs(At(i, j) - At(i, j + 1)), c = std::fabs(At(j, i) - At(j + 1, i));
      fRows += r;
      fCols += c;
      if (j == n - 1)
      {
        fEdgeRow += r;
        fEdgeCol += c;
      }
    }
  fRows /= n;
  fCols /= n;
  CHECK_MSG(fEdgeRow < 1.5 * fRows && fEdgeCol < 1.5 * fCols, std::to_string(fEdgeRow / fRows) + ", " + std::to_string(fEdgeCol / fCols));
  // Smooth between neighbours (a texel is no sparkle), less alike further apart.
  auto Corr = [&](int dx, int dy) {
    double fMean = 0;
    for (uint8_t v : P)
      fMean += v;
    fMean /= P.size();
    double s = 0, v2 = 0;
    for (int y = 0; y < n; y += 3)
      for (int x = 0; x < n; x += 3)
      {
        s += (At(x, y) - fMean) * (At(x + dx, y + dy) - fMean);
        v2 += (At(x, y) - fMean) * (At(x, y) - fMean);
      }
    return s / v2;
  };
  CHECK(Corr(1, 0) > 0.8 && Corr(0, 1) > 0.8);
  CHECK(Corr(8, 0) < Corr(2, 0) && Corr(0, 8) < Corr(0, 2));
}

// Why: a wave breaks where the water is shallower than its height over the breaker index: all white at the water's
// edge, none beyond that depth, a quarter at half of it (the square of the share left: the foam gathers at the edge);
// the height twice the crest's, at least a quarter of the significant height (the rim the ripple keeps), at least 2 cm;
// a lower index (it breaks in deeper water) reaches further out.
XR_TEST(Foam_Shore)
{
  const std::vector<uint32> Out = RunFoam({
    Shore(0, 0.3f, 1, 0.78f),          // 0: the edge
    Shore(0.6f / 0.78f, 0.3f, 1, 0.78f), // 1: the breaking depth of a 0.6 m wave
    Shore(0.3f / 0.78f, 0.3f, 1, 0.78f), // 2: half of it
    Shore(0.1f, 0, 1, 0.78f),          // 3: a trough: a quarter of 1 m over 0.78, 0.32 m: 1 - 0.1 / 0.32
    Shore(0.01f, 0, 0, 0.78f),         // 4: no waves: 2 cm, half
    Shore(0.3f, 0.3f, 1, 0.55f),       // 5: a lower index: 1 - 0.3 / 1.09
    Shore(0.3f, 0.3f, 1, 1.0f),        // 6: a higher: 1 - 0.3 / 0.6
    Shore(5, 0.3f, 1, 0.78f),          // 7: deep
  });
  auto Sq = [](double s) { return s * s; };
  CHECK_NEAR(At(Out, 0, 0), 1, 1e-6);
  CHECK_NEAR(At(Out, 1, 0), 0, 1e-5);
  CHECK_NEAR(At(Out, 2, 0), 0.25, 1e-5);
  CHECK_NEAR(At(Out, 3, 0), Sq(1 - 0.1 / (0.25 / 0.78)), 1e-5);
  CHECK_NEAR(At(Out, 4, 0), 0.25, 1e-5);
  CHECK_NEAR(At(Out, 5, 0), Sq(1 - 0.3 / (0.6 / 0.55)), 1e-5);
  CHECK_NEAR(At(Out, 6, 0), 0.25, 1e-5);
  CHECK_NEAR(At(Out, 7, 0), 0, 0);
  CHECK(At(Out, 5, 0) > At(Out, 6, 0));
}

// Why: against a rock or a wall the waves run up and fall back, the foam floats there: all white at it (no path through
// the water to it), none beyond twice the waves' height along the view's path (the crest's twice, at least a quarter
// of the significant height, at least 2.5 cm), a quarter at half of it.
XR_TEST(Foam_Contact)
{
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FoamContact);
  const float Items[][3] = {
    { 0, 0.2f, 1 },        // 0: at the wall
    { 0.8f, 0.2f, 1 },     // 1: twice 0.4 m: the reach
    { 0.4f, 0.2f, 1 },     // 2: half of it
    { 0.25f, 0, 1 },       // 3: a trough: a quarter of 1 m twice, 0.5 m: (1 - 0.5)^2
    { 0.0125f, 0, 0 },     // 4: no waves: 2.5 cm, half: a quarter
    { 3, 0.2f, 1 },        // 5: far
  };
  for (const auto &it : Items)
    Push(d.In, it[0], it[1], it[2], 0);
  d.Test.nTest[0] = 6;
  d.nGroups[0] = 1;
  d.nOut = 6;
  const std::vector<uint32> Out = Gpu().Run(d);
  CHECK_NEAR(At(Out, 0, 0), 1, 1e-6);
  CHECK_NEAR(At(Out, 1, 0), 0, 1e-6);
  CHECK_NEAR(At(Out, 2, 0), 0.25, 1e-5);
  CHECK_NEAR(At(Out, 3, 0), 0.25, 1e-5);
  CHECK_NEAR(At(Out, 4, 0), 0.25, 1e-5);
  CHECK_NEAR(At(Out, 5, 0), 0, 0);
}

// Why: the swell breaks where the water gets shallower than its height over the breaker index and runs to the shore as
// bores: none further out, the breaking growing from there; at each depth one white roller a period, reaching a
// shallower place later (the fronts run shorewards, at the travel time's share sqrt(h / h_b) of FOAM_SURF_BORES
// periods); behind it the lace fading till the next, rising before a front without a jump; each bore its own strength.
XR_TEST(Foam_Surf)
{
  const float fBreak = 0.64f;
  const int nCycle = 512;
  const float Taus[3] = { 0.4f, 0.5f, 0.6f };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FoamSurf);
  for (float fTau : Taus)
    for (int i = 0; i < nCycle; i++)
      Push(d.In, fTau * fTau * fBreak, fBreak, (float)i / nCycle, 0);
  const size_t nFar = d.In.size() / 4;
  const float Far[3] = { 1.0f, 1.2f, 5.0f };
  for (float f : Far)
    for (int i = 0; i < 16; i++)
      Push(d.In, f * fBreak, fBreak, i / 16.0f, 0);
  Push(d.In, 0.1f, 0.0f, 0.25f, 0);  // no swell
  const size_t nEdge = d.In.size() / 4;
  for (int i = 0; i < 64; i++)
    Push(d.In, 0.999f * 0.999f * fBreak, fBreak, i / 64.0f, 0);
  const size_t nLace = d.In.size() / 4;
  // 0.3 of a period after bore k passed the middle of the way (x = k + 0.3).
  for (int k = 0; k < 8; k++)
    Push(d.In, 0.25f * fBreak, fBreak, k + 0.3f - XR_FOAM_SURF_BORES * 0.5f, 0);
  const size_t n = d.In.size() / 4;
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);

  for (size_t i = nFar; i < nEdge; i++)
    CHECK_NEAR(At(Out, i, 0), 0, 0);
  float fEdge = 0;
  for (size_t i = nEdge; i < nLace; i++)
    fEdge = std::max(fEdge, At(Out, i, 0));
  CHECK(fEdge < 0.02f);
  float fPeakAt[3] = {}, fJump = 0;
  for (int t = 0; t < 3; t++)
  {
    float fPeak = -1, fMean = 0;
    for (int i = 0; i < nCycle; i++)
    {
      const float v = At(Out, t * nCycle + i, 0), w = At(Out, t * nCycle + (i + 1) % nCycle, 0);
      fMean += v / nCycle;
      fJump = std::max(fJump, std::fabs(w - v));
      if (v > fPeak)
      {
        fPeak = v;
        fPeakAt[t] = (float)i / nCycle;
      }
    }
    // The front where the swell's phase plus the travel time's share is whole: at the cycle 1 - frac(bores tau).
    const float fExpect = 1 - (XR_FOAM_SURF_BORES * Taus[t] - std::floor(XR_FOAM_SURF_BORES * Taus[t]));
    CHECK(std::fabs(fPeakAt[t] - (fExpect - std::floor(fExpect))) < 0.02f);
    CHECK(fPeak >= 0.75f);
    CHECK(fMean > 0.15f && fMean < 0.6f);
  }
  // Seen at one moment the bores reach the shallower places later: the peak at tau 0.4 comes after 0.5's after 0.6's.
  CHECK(fPeakAt[2] < fPeakAt[1] && fPeakAt[1] < fPeakAt[0]);
  CHECK(fJump < 0.15f);
  float fLeast = 1, fMost = 0;
  for (size_t i = nLace; i < n; i++)
  {
    fLeast = std::min(fLeast, At(Out, i, 0));
    fMost = std::max(fMost, At(Out, i, 0));
  }
  CHECK(fLeast > 0.2f && fMost < 0.55f && fMost > 1.2f * fLeast);
}

// Why: a breaking wave's white is torn into clumps with water between them, not an even band: the surf's factor must be
// 1 on the mean over the shore (the foam's share kept), spread far either way (gaps and dense clumps) within
// FOAM_SURF_CLUMPS, metres across (alike 5 cm apart, unlike 3 m apart), the same while one bore passes a place (the
// clumps stay where it left them, from just before its front), another bore's another.
XR_TEST(Foam_SurfClumps)
{
  const float fBreak = 0.8f, fDepth = 0.2f;   // tau 0.5
  const int nGrid = 48;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FoamClumps);
  // Bore 3's place over a grid 0.37 m apart, at its front's share 0.1 and 0.6 behind it, just before it, bore 4's.
  const float fFront = 3 - XR_FOAM_SURF_BORES * 0.5f;
  const float Phases[4] = { fFront + 0.1f, fFront + 0.6f, fFront - 0.01f, fFront + 1.1f };
  for (float c : Phases)
    for (int y = 0; y < nGrid; y++)
      for (int x = 0; x < nGrid; x++)
      {
        Push(d.In, 0.37f * x + 5, 0.37f * y - 3, fDepth, fBreak);
        Push(d.In, c, 0, 0, 0);
      }
  const size_t nGrids = d.In.size() / 8;
  // Pairs 5 cm and 3 m apart.
  for (int i = 0; i < 256; i++)
    for (float dx : { 0.0f, 0.05f, 3.0f })
    {
      Push(d.In, 0.61f * (i % 16) + dx, 0.53f * (i / 16), fDepth, fBreak);
      Push(d.In, Phases[0], 0, 0, 0);
    }
  const size_t n = d.In.size() / 8;
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);

  const size_t m = (size_t)nGrid * nGrid;
  double fMean = 0, fVar = 0, fLeast = 10, fMost = 0, fSame = 0, fA = 0, fB = 0, fAB = 0;
  for (size_t i = 0; i < m; i++)
  {
    const double v = At(Out, i, 0);
    fMean += v / m;
    fLeast = std::min(fLeast, v);
    fMost = std::max(fMost, v);
    fSame = std::max(fSame, std::max(std::fabs(v - At(Out, m + i, 0)), std::fabs(v - At(Out, 2 * m + i, 0))));
  }
  for (size_t i = 0; i < m; i++)
  {
    const double a = At(Out, i, 0) - fMean, b = At(Out, 3 * m + i, 0) - fMean;
    fVar += a * a / m;
    fA += a * a;
    fB += b * b;
    fAB += a * b;
  }
  const double fCorrelation = fAB / std::sqrt(fA * fB);
  double fNear = 0, fFar = 0;
  for (size_t i = nGrids; i < n; i += 3)
  {
    fNear = std::max(fNear, (double)std::fabs(At(Out, i, 0) - At(Out, i + 1, 0)));
    fFar += std::fabs(At(Out, i, 0) - At(Out, i + 2, 0)) / 256;
  }
  std::printf("  clumps: mean %.3f, RMS %.3f, %.3f..%.3f; the next bore's correlation %.3f; 5 cm apart at most %.4f, 3 m %.3f "
              "on the mean\n", fMean, std::sqrt(fVar), fLeast, fMost, fCorrelation, fNear, fFar);
  CHECK(std::fabs(fMean - 1) < 0.08);
  CHECK(std::sqrt(fVar) > 0.2);
  CHECK(fLeast >= 1 - 0.9 - 1e-5 && fMost <= 1 + 0.9 + 1e-5);
  CHECK(fLeast < 0.6 && fMost > 1.4);
  CHECK_NEAR(fSame, 0, 0);
  CHECK(std::fabs(fCorrelation) < 0.3);
  CHECK(fNear < 0.15);
  CHECK(fFar > 0.2);
}

// Why: a wake's trail of even white is fog a few metres off (its lace finer than the pixel); its thin foam must be torn
// into clumps metres across (alike 5 cm apart, unlike 3 m apart) with gaps between them, its amount kept on the mean,
// within FOAM_SURF_CLUMPS either way, while the white water just behind a hull (an amount of 1) stays white all over and
// no foam makes none.
XR_TEST(Foam_WakeClumps)
{
  const float fThin = 0.3f;
  const int nGrid = 48;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FoamClumps);
  for (float a : { fThin, 1.0f, 0.0f })
    for (int y = 0; y < nGrid; y++)
      for (int x = 0; x < nGrid; x++)
      {
        Push(d.In, 0.37f * x - 7, 0.37f * y + 4, 0, 0);
        Push(d.In, 0, a, 0, 0);
      }
  const size_t m = (size_t)nGrid * nGrid;
  for (int i = 0; i < 256; i++)
    for (float dx : { 0.0f, 0.05f, 3.0f })
    {
      Push(d.In, 0.61f * (i % 16) + dx, 0.53f * (i / 16), 0, 0);
      Push(d.In, 0, fThin, 0, 0);
    }
  const size_t n = d.In.size() / 8;
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);

  double fMean = 0, fLeast = 10, fMost = 0, fFull = 0, fNone = 0;
  for (size_t i = 0; i < m; i++)
  {
    const double v = At(Out, i, 1) / fThin;
    fMean += v / m;
    fLeast = std::min(fLeast, v);
    fMost = std::max(fMost, v);
    fFull = std::max(fFull, (double)std::fabs(At(Out, m + i, 1) - 1));
    fNone = std::max(fNone, (double)std::fabs(At(Out, 2 * m + i, 1)));
  }
  double fNear = 0, fFar = 0;
  for (size_t i = 3 * m; i < n; i += 3)
  {
    fNear = std::max(fNear, (double)std::fabs(At(Out, i, 1) - At(Out, i + 1, 1)) / fThin);
    fFar += std::fabs(At(Out, i, 1) - At(Out, i + 2, 1)) / fThin / 256;
  }
  std::printf("  wake clumps of %.1f: factor mean %.3f, %.3f..%.3f; 5 cm apart at most %.4f, 3 m %.3f on the mean; of 1 at most "
              "%.1e off, of 0 %.1e\n", fThin, fMean, fLeast, fMost, fNear, fFar, fFull, fNone);
  CHECK(std::fabs(fMean - 1) < 0.08);
  CHECK(fLeast >= 1 - 0.9 - 1e-5 && fMost <= 1 + 0.9 + 1e-5);
  CHECK(fLeast < 0.6 && fMost > 1.4);
  // Its finest octave 17 cm across: 5 cm apart a ninth of the factor's span at most.
  CHECK(fNear < 0.25);
  CHECK(fFar > 0.2);
  CHECK(fFull < 1e-6);
  CHECK(fNone == 0);
}

// Why: a patch of foam is thicker in its middle and thins to its edge, so dense foam (all white) still shows its bubbles'
// clusters; the brightness over the white part of a pixel's pattern must be 1 on the mean (the foam's light kept), at
// most FOAM_SHADE either way, rising towards the patch's middle, and none where the pattern is finer than the pixel.
XR_TEST(Foam_Shade)
{
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FoamShade);
  const float Amounts[3] = { 0.1f, 0.5f, 1.0f };
  const int nU = 200;
  for (float a : Amounts)
    for (int i = 0; i < nU; i++)
      Push(d.In, 1 - a + a * (i + 0.5f) / nU, a, 0, 0);
  for (int i = 0; i < 16; i++)
    Push(d.In, i / 15.0f, 1, 1, 0);   // the pattern finer than the pixel
  const size_t n = d.In.size() / 4;
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (int k = 0; k < 3; k++)
  {
    double fMean = 0, fLeast = 10, fMost = 0;
    bool bRises = true;
    for (int i = 0; i < nU; i++)
    {
      const double v = At(Out, k * nU + i, 0);
      fMean += v / nU;
      fLeast = std::min(fLeast, v);
      fMost = std::max(fMost, v);
      if (i > 0 && v < At(Out, k * nU + i - 1, 0))
        bRises = false;
    }
    CHECK_NEAR(fMean, 1, 1e-3);
    CHECK(fLeast >= 0.75 - 1e-5 && fMost <= 1.25 + 1e-5 && fMost - fLeast > 0.45);
    CHECK(bRises);
  }
  for (size_t i = 3 * nU; i < n; i++)
    CHECK_NEAR(At(Out, i, 0), 1, 1e-6);
}

// Why: the amount is the share of the surface white, so over the pattern's uniform values the pixels' mean white share
// must be the amount, sharp or softened; none where there is no amount, all where it is 1; where the pattern is finer
// than the pixel the amount itself. Thin foam (a low amount) hides only some of the water under it.
XR_TEST(Foam_Cover)
{
  const int nU = 4096;
  std::vector<SFoamItem> Items;
  const float Amounts[] = { 0, 0.05f, 0.3f, 0.7f, 0.95f, 1 }, Widths[] = { 0.02f, 0.1f };
  for (float a : Amounts)
    for (float w : Widths)
      for (int i = 0; i < nU; i++)
        Items.push_back(Cover(a, (i + 0.5f) / nU, w, 0));
  for (float a : Amounts)
    Items.push_back(Cover(a, 0.9f, 0.02f, 1));
  const std::vector<uint32> Out = RunFoam(Items);
  size_t k = 0;
  for (float a : Amounts)
    for (float w : Widths)
    {
      double fMean = 0, fMost = 0;
      for (int i = 0; i < nU; i++, k++)
      {
        fMean += At(Out, k, 1);
        fMost = std::max(fMost, (double)At(Out, k, 1));
        CHECK_NEAR(At(Out, k, 2), At(Out, k, 1) * (0.45 + 0.55 * a), 1e-5);
      }
      fMean /= nU;
      CHECK_MSG(std::fabs(fMean - a) < 0.012, std::to_string(a) + " at " + std::to_string(w) + ": " + std::to_string(fMean));
      if (a == 0)
        CHECK_NEAR(fMost, 0, 0);
    }
  for (float a : Amounts)
    CHECK_NEAR(At(Out, k++, 1), a, 1e-6);
}

// Why: the pattern's two samples (two tiles of different sizes, one turned) hide each other's repetition; their mean
// is not uniform (a triangle), and the share white is the amount only of a uniform value: FoamUniform must map the
// mean of two independent uniform values back to uniform.
XR_TEST(Foam_Uniform)
{
  const int n = 96;
  std::vector<SFoamItem> Items;
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++)
    {
      SFoamItem it = Cover(0, 0, 0.02f, 0);
      it.a = (i + 0.5f) / n;
      it.b = (j + 0.5f) / n;
      Items.push_back(it);
    }
  const std::vector<uint32> Out = RunFoam(Items);
  for (double q : { 0.05, 0.2, 0.5, 0.8, 0.95 })
  {
    size_t nBelow = 0;
    for (size_t i = 0; i < Items.size(); i++)
      nBelow += At(Out, i, 3) < q;
    CHECK_NEAR((double)nBelow / Items.size(), q, 0.01);
  }
  for (size_t i = 0; i < Items.size(); i++)
    CHECK(At(Out, i, 3) >= 0 && At(Out, i, 3) <= 1);
}

// Why: the middle cascade's whitecaps break on the long waves' crests (Longuet-Higgins and Stewart's modulation): more
// where the long waves are high, none in their deep troughs, their share over the sea unchanged (the modulation's mean
// 1 over heights symmetric about the level); the breaker index's noise keeps McCowan's 0.78 on the mean, within
// 0.55-1.0; the noise is smooth and 0..1, its mean 0.5.
XR_TEST(Foam_WhitecapMix)
{
  std::vector<SMixItem> Items = {
    { 0.1f, 0.2f, 0, 2, 0.5f, 0, 0 },       // 0: at the level: f0 + f1
    { 0.1f, 0.2f, 0.5f, 2, 0, 0, 0 },       // 1: a crest of 1 RMS: f0 + 2 f1
    { 0.1f, 0.2f, -0.5f, 2, 1, 0, 0 },      // 2: a trough of 1 RMS: f0
    { 0.1f, 0.2f, 0.25f, 2, 2, 0, 0 },      // 3: half an RMS up: f0 + 1.5 f1
    { 0.8f, 0.5f, 0.5f, 2, -1, 0, 0 },      // 4: saturated
  };
  // The normal heights' mean modulation (z on a fine grid, weighted by the density).
  const int nZ = 801;
  for (int i = 0; i < nZ; i++)
  {
    const float z = -4 + 8.0f * i / (nZ - 1);
    Items.push_back({ 0, 0.1f, z, 1, 0.5f, 0, 0 });
  }
  // The noise along a line, 0.01 apart; then over 1500 knots for its mean.
  const size_t nNoise = Items.size(), nLine = 2000;
  for (int i = 0; i < (int)nLine; i++)
    Items.push_back({ 0, 0, 0, 1, 0.5f, -731.3f + 0.01f * i, 408.7f + 0.0037f * i });
  for (int i = 0; i < 6000; i++)
    Items.push_back({ 0, 0, 0, 1, 0.5f, 52.1f + 0.53f * (i % 75), -97.4f + 0.61f * (i / 75) });
  const std::vector<uint32> Out = RunMix(Items);
  CHECK_NEAR(At(Out, 0, 0), 0.3, 1e-6);
  CHECK_NEAR(At(Out, 1, 0), 0.5, 1e-6);
  CHECK_NEAR(At(Out, 2, 0), 0.1, 1e-6);
  CHECK_NEAR(At(Out, 3, 0), 0.4, 1e-6);
  CHECK_NEAR(At(Out, 4, 0), 1, 0);
  CHECK_NEAR(At(Out, 0, 1), 0.78, 1e-6);
  CHECK_NEAR(At(Out, 1, 1), 0.78 * 0.7, 1e-6);
  CHECK_NEAR(At(Out, 2, 1), 0.78 * 1.3, 1e-6);
  CHECK_NEAR(At(Out, 3, 1), 0.78 * 1.3, 1e-6);
  CHECK_NEAR(At(Out, 4, 1), 0.78 * 0.7, 1e-6);
  double fSum = 0, fWeight = 0;
  for (int i = 0; i < nZ; i++)
  {
    const double z = -4 + 8.0 * i / (nZ - 1), w = std::exp(-0.5 * z * z);
    fSum += w * At(Out, 5 + i, 0);
    fWeight += w;
  }
  CHECK_NEAR(fSum / fWeight, 0.1, 1e-3);
  double fMean = 0, fStep = 0;
  for (size_t i = nNoise; i < Items.size(); i++)
  {
    const float v = At(Out, i, 2);
    CHECK(v >= 0 && v <= 1);
    if (i >= nNoise + nLine)
      fMean += v;
    else if (i > nNoise)
      fStep = std::max(fStep, (double)std::fabs(v - At(Out, i - 1, 2)));
  }
  fMean /= (double)(Items.size() - nNoise - nLine);
  CHECK_NEAR(fMean, 0.5, 0.03);
  CHECK_MSG(fStep < 0.05, std::to_string(fStep));
}

// Why: the foam is a white diffuse layer (albedo 0.6) under the sun and the sky: facing the sun all of both, in its
// shadow the sky's alone, edge-on to it the wrap's share of the sun, turned away from it none of the sun.
XR_TEST(Foam_Light)
{
  const SVec3 Up = { 0, 0, 1 };
  const SVec3 Sun = { 1, 0.8f, 0.6f }, Sky = { 0.1f, 0.2f, 0.4f };
  const SVec3 Ls[] = { Up, { 1, 0, 0 }, Normalize(SVec3{ 1, 0, -0.5f }) };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FoamLight);
  const float Shadows[] = { 1, 0, 1, 1 };
  const SVec3 Dirs[] = { Ls[0], Ls[0], Ls[1], Ls[2] };
  for (int i = 0; i < 4; i++)
  {
    Push(d.In, Up, Shadows[i]);
    Push(d.In, Dirs[i], 0);
    Push(d.In, Sun, 0);
    Push(d.In, Sky, 0);
  }
  d.Test.nTest[0] = 4;
  d.nGroups[0] = 1;
  d.nOut = 4;
  const std::vector<uint32> Out = Gpu().Run(d);
  const float s[3] = { Sun.x, Sun.y, Sun.z }, k[3] = { Sky.x, Sky.y, Sky.z };
  for (int c = 0; c < 3; c++)
  {
    CHECK_NEAR(At(Out, 0, c), 0.6 * (s[c] + k[c]), 1e-6);
    CHECK_NEAR(At(Out, 1, c), 0.6 * k[c], 1e-6);
    CHECK_NEAR(At(Out, 2, c), 0.6 * (s[c] * 0.2 / 1.2 + k[c]), 1e-6);
    CHECK_NEAR(At(Out, 3, c), 0.6 * k[c], 1e-6);
  }
}
