// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CausticsTests.cpp
//	Description: Unit tests of the sea floor's caustics of the FFT ocean
//	(d3d12_WaterCaustics): the refracted sun's ray (OceanFFT.hlsli's
//	CausticsRay against Snell's law in double), the light at the floor's
//	depth from the four depths of a texel (WaterCaustics.hlsli), and the
//	kernel (OceanFFT.hlsl's XR_OCEAN_CAUSTICS as OceanFFT.cpp runs it,
//	GpuHarness.h's RunOcean): a level sea's light even, the light kept at
//	every depth, one wave's against its rays' density in double and the
//	focus under its crest, the mip levels' means, the game's sea's contrast.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"
#include "OceanSpectrum.h"

#include "Test_OceanRows.h"
#include "Test_OceanColumns.h"
#include "Test_OceanWaves.h"
#include "Test_OceanMips.h"
#include "Test_OceanCaustics.h"
#include "Test_CausticsRay.h"
#include "Test_CausticsDepth.h"
#include "Test_CausticsFar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  using namespace OceanSpectrum;
  constexpr int N = OceanSpectrum::SIZE, C = OceanSpectrum::CASCADES, CC = CAUSTICS_CASCADES;
  constexpr double ETA = 1 / 1.333;

  SOceanKernels Kernels()
  {
    SOceanKernels k = { XR_BYTECODE(Test_OceanRows), XR_BYTECODE(Test_OceanColumns), XR_BYTECODE(Test_OceanWaves), XR_BYTECODE(Test_OceanMips) };
    k.Caustics = XR_BYTECODE(Test_OceanCaustics);
    return k;
  }

  //! The sun at fElevation degrees, towards fAzimuth degrees from x.
  void Sun(double fElevation, double fAzimuth, double L[3])
  {
    const double e = fElevation * PI / 180, a = fAzimuth * PI / 180;
    L[0] = std::cos(e) * std::cos(a);
    L[1] = std::cos(e) * std::sin(a);
    L[2] = std::sin(e);
  }

  //! UpdateOcean's constants at tau (no displacement, the heights' weights 1), the sun L, the slopes times fSlopes, each
  //! caustics cascade's depths.
  SOceanConstants Constants(double fTau, const double L[3], double fSlopes)
  {
    SOceanConstants oc = {};
    oc.vParams[0] = (float)fTau;
    oc.vParams[2] = 0.1f;
    for (int c = 0; c < C; c++)
    {
      oc.vCascades[c][0] = (float)TILE[c];
      oc.vCascades[c][1] = 1;
      oc.vCascades[c][2] = (float)(TILE[c] / N);
    }
    for (int i = 0; i < 3; i++)
      oc.vSun[i] = (float)L[i];
    oc.vSun[3] = (float)fSlopes;
    for (int c = 0; c < CC; c++)
      for (int k = 0; k < 4; k++)
        oc.vCausticsDepths[c][k] = (float)(CAUSTICS_DEPTH[c] * (1 << k));
    return oc;
  }

  //! The caustics' texel (x, y) of level m, caustics cascade c: the light at its four depths.
  const float *Texel(const SOceanOutput &o, int m, int c, int x, int y)
  {
    const size_t w = (size_t)N >> m;
    return &o.Caustics[m][(((size_t)c * w + y) * w + x) * 4];
  }

  //! The sun's ray (L towards it) refracted at the surface of slopes (sx, sy), in double (Snell's law): its move per metre
  //! down; fLight the light let in for the area seen from above (N.L |(-s, 1)| / L.z).
  void Refract(double sx, double sy, const double L[3], double &mx, double &my, double &fLight)
  {
    const double fLen = std::sqrt(sx * sx + sy * sy + 1), n[3] = { -sx / fLen, -sy / fLen, 1 / fLen };
    const double fCos = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
    fLight = std::max(fCos * fLen / L[2], 0.0);
    const double k = 1 - ETA * ETA * (1 - fCos * fCos), b = ETA * fCos - std::sqrt(k);
    double T[3];
    for (int i = 0; i < 3; i++)
      T[i] = -ETA * L[i] + b * n[i];
    mx = T[0] / -T[2];
    my = T[1] / -T[2];
  }

  //! One wave a cos(k x) along x in cascade 2 (nBin cycles a tile): h0 as Ocean_Gerstner's.
  std::vector<float> OneWave(int nBin, double a)
  {
    std::vector<float> H0((size_t)C * N * N * 4, 0.0f);
    double kx, ky;
    WaveVector(2, nBin, 0, kx, ky);
    const float fSteps = (float)OmegaSteps(kx);
    float *p = &H0[(size_t)2 * N * N * 4];
    p[(size_t)nBin * 4 + 0] = (float)(a / 2);
    p[(size_t)nBin * 4 + 2] = fSteps;
    p[(size_t)(N - nBin) * 4 + 2] = fSteps;
    return H0;
  }

  //! The kernel's rays of a cell a side (OceanFFT.hlsl's CAUSTICS_RAYS).
  constexpr int RAYS = 4;

  //! The kernel's rays of a row of one wave along x in double (a row: no move across it, the rays of a cell across it as
  //! one of their sum): each cell between two texels' centres RAYS rays at the middles of its parts, their moves and
  //! light linear between the texels' rays' (Slope(x) at texel x), each moved d metres down at most 7 texels, its light
  //! over the two texels around where it lands (the row wrapping).
  template <class FSlope>
  std::vector<double> RowRays(FSlope Slope, double fTexel, double d, const double L[3])
  {
    std::vector<double> Out(N, 0.0), Move(N), Light(N);
    double mx0, my0, f0;
    Refract(0, 0, L, mx0, my0, f0);
    for (int x = 0; x < N; x++)
    {
      double mx, my;
      Refract(Slope(x), 0, L, mx, my, Light[x]);
      Move[x] = mx - mx0;
    }
    for (int q = 0; q < N; q++)
      for (int r = 0; r < RAYS; r++)
      {
        const double t = (r + 0.5) / RAYS, m = Move[q] * (1 - t) + Move[(q + 1) % N] * t;
        const double fLight = Light[q] * (1 - t) + Light[(q + 1) % N] * t;
        const double X = q + t + std::clamp(m * d / fTexel, -7.0, 7.0);
        const double f = std::floor(X), w = X - f;
        const int i = (((int)f % N) + N) % N;
        Out[i] += fLight * (1 - w) / RAYS;
        Out[(i + 1) % N] += fLight * w / RAYS;
      }
    return Out;
  }
}

// Why: the caustics are where the sun's rays go after the surface bends them, Snell's law at its normal (-s, 1): a level
// surface keeps the refracted sun's ray (no move against it), a slope moves the ray towards the higher side (a crest
// gathers them: (1 - 1/n) s per metre for an overhead sun and small slopes); a face turned to the sun lets in more of its
// light for the area seen from above (1 - s.L.xy / L.z), one turned away none.
XR_TEST(Caustics_Ray)
{
  struct SCase { double sx, sy, fElevation, fAzimuth; };
  const SCase Cases[] = { { 0, 0, 90, 0 }, { 0.01, 0, 90, 0 }, { 0.2, -0.1, 90, 0 }, { 0.05, 0.03, 40, 30 }, { -0.3, 0.2, 25, 200 },
                          { 0.1, 0.1, 60, 45 }, { 0.6, 0, 10, 0 }, { -0.6, 0, 10, 0 } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_CausticsRay);
  for (const SCase &c : Cases)
  {
    double L[3];
    Sun(c.fElevation, c.fAzimuth, L);
    Push(d.In, (float)c.sx, (float)c.sy, 0, 0);
    Push(d.In, (float)L[0], (float)L[1], (float)L[2], 0);
  }
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
  {
    const SCase &c = Cases[i];
    double L[3], mx, my, fLight;
    Sun(c.fElevation, c.fAzimuth, L);
    Refract(c.sx, c.sy, L, mx, my, fLight);
    CHECK_NEAR(At(Out, i, 2), fLight, 1e-5);
    CHECK_NEAR(fLight, std::max(1 - (c.sx * L[0] + c.sy * L[1]) / L[2], 0.0), 1e-9);
    if (fLight > 0)
    {
      CHECK_NEAR(At(Out, i, 0), mx, 1e-4 * (1 + std::fabs(mx)));
      CHECK_NEAR(At(Out, i, 1), my, 1e-4 * (1 + std::fabs(my)));
    }
  }
  // Overhead: none on a level surface, (1 - 1/n) s per metre for a small slope, towards the higher side.
  CHECK_NEAR(At(Out, 0, 0), 0, 1e-7);
  CHECK_NEAR(At(Out, 1, 0) / 0.01, 1 - ETA, 1e-3);
  // A face turned away from a low sun lets none in.
  CHECK(At(Out, 6, 2) == 0 || At(Out, 7, 2) == 0);
}

// Why: the floor's program has a texel's light at four depths (each twice the one before) and must give it at the floor's
// depth: 1 at the surface (no rays moved yet), towards the first depth linearly, exact at each, between two by the log of
// the depth (the pattern sharpens about as much from 1 to 2 m as from 2 to 4), below the last the last's; its factor of
// the floor's light 1 + k (I - 1), clamped to 0 and the most.
XR_TEST(Caustics_Depth)
{
  struct SCase { float v[4]; float fFirst, d, k, fMost; double fWant; };
  const float V[4] = { 1.5f, 0.4f, 3.0f, 2.0f };
  const SCase Cases[] = {
    { { V[0], V[1], V[2], V[3] }, 0.5f, 0.0f, 0, 4, 1.0 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, 0.25f, 0, 4, 1.25 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, 0.5f, 0, 4, 1.5 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, 1.0f, 0, 4, 0.4 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, 2.0f, 0, 4, 3.0 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, 4.0f, 0, 4, 2.0 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, 9.0f, 0, 4, 2.0 },
    { { V[0], V[1], V[2], V[3] }, 0.5f, (float)(0.5 * std::sqrt(2.0)), 0, 4, 0.5 * (1.5 + 0.4) },
    { { V[0], V[1], V[2], V[3] }, 2.0f, (float)(2.0 * std::pow(2.0, 2.25)), 0, 4, 3.0 + 0.25 * (2.0 - 3.0) },
  };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_CausticsDepth);
  for (const SCase &c : Cases)
  {
    Push(d.In, c.v[0], c.v[1], c.v[2], c.v[3]);
    Push(d.In, c.fFirst, c.d, c.k, c.fMost);
  }
  // The factor: k 0, I 1, the most, below 0.
  const float Factor[4][4] = { { 3, 0, 0, 4 }, { 1, 0.6f, 0, 4 }, { 9, 0.6f, 0, 4 }, { 0.2f, 1.5f, 0, 4 } };
  for (const auto &f : Factor)
  {
    // I = f[0] at the first depth (its x), k = f[1].
    Push(d.In, f[0], 0, 0, 0);
    Push(d.In, 1, 1, f[1], f[3]);
  }
  const size_t n = d.In.size() / 8;
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < std::size(Cases); i++)
    CHECK_MSG(std::fabs(At(Out, i, 0) - Cases[i].fWant) < 1e-5, std::to_string(i) + ": " + std::to_string(At(Out, i, 0)));
  const size_t f0 = std::size(Cases);
  CHECK_NEAR(At(Out, f0 + 0, 1), 1, 1e-6);                 // k 0: the floor's light as it was
  CHECK_NEAR(At(Out, f0 + 1, 1), 1, 1e-6);                 // I 1
  CHECK_NEAR(At(Out, f0 + 2, 1), 4, 1e-6);                 // 1 + 0.6 * 8 clamped to the most
  CHECK_NEAR(At(Out, f0 + 3, 1), 0, 1e-6);                 // 1 + 1.5 * -0.8 clamped to 0
}

// Why: the caustics keep XR_CAUSTICS_MIPS levels (the last 16 x 16 texels a cascade: no kernel for the levels below it,
// each costs a dispatch and two barriers). Where a pixel takes more than the last level's texel the hardware samples that
// level, its pattern finer than the pixel (it would shimmer): the cascade's change from the mean must be all there up to
// that texel, none from twice it, between by the log of the pixel's size.
XR_TEST(Caustics_Far)
{
  const double Cases[][2] = { { 0.3, 1 }, { 1, 1 }, { 16, 1 }, { 16 * std::sqrt(2.0), 0.5 }, { 32, 0 }, { 1000, 0 } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_CausticsFar);
  for (const auto &c : Cases)
    Push(d.In, (float)c[0], 0, 0, 0);
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
    CHECK_MSG(std::fabs(At(Out, i, 0) - Cases[i][1]) < 1e-5, std::to_string(Cases[i][0]) + ": " + std::to_string(At(Out, i, 0)));
  CHECK(XR_OCEAN_SIZE >> (XR_CAUSTICS_MIPS - 1) == 16);
}

// Why: a level sea bends no ray: its caustics must be the sun's light unchanged, 1 at every depth and mip level, overhead
// and slanting (the floor's program moves the pattern along the refracted sun's ray, the kernel only the rays' moves
// against it).
XR_TEST(Caustics_Level)
{
  for (double fElevation : { 90.0, 35.0 })
  {
    double L[3];
    Sun(fElevation, 20, L);
    const SOceanOutput o = Gpu().RunOcean(std::vector<float>((size_t)C * N * N * 4, 0.0f), Constants(0.1, L, 1), Kernels());
    double fWorst = 0;
    for (int m = 0; m < XR_CAUSTICS_MIPS; m++)
      for (float v : o.Caustics[m])
        fWorst = std::max(fWorst, std::fabs(v - 1.0));
    CHECK_MSG(fWorst < 1e-6, std::to_string(fElevation) + " degrees: " + std::to_string(fWorst));
  }
}

// Why: the rays carry the light the surface lets in, so the floor gets all of it back on the mean at every depth (the
// tiles wrap: the rays leaving a texel's group land in another's), however the waves focus it: the mean of each cascade
// at each depth 1, its contrast growing with the depth (the focus nearer), and the mip levels the means of the texels
// under them (the far floor's light 1, no shimmer).
XR_TEST(Caustics_Energy)
{
  SSea Sea;
  MakeSea(10, Sea);
  std::vector<float> H0;
  for (const SCascade &k : Sea.Cascades)
    H0.insert(H0.end(), k.H0.begin(), k.H0.end());
  double L[3];
  Sun(55, 70, L);
  const SOceanOutput o = Gpu().RunOcean(H0, Constants(0.3, L, 1), Kernels());
  for (int c = 0; c < CC; c++)
  {
    double fBefore = 0, fLargest = 0;
    for (int k = 0; k < 4; k++)
    {
      double fSum = 0, fSq = 0, fMost = 0;
      for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
        {
          const double v = Texel(o, 0, c, x, y)[k];
          fSum += v;
          fSq += v * v;
          fMost = std::max(fMost, v);
        }
      const double fMean = fSum / (N * N), fRms = std::sqrt(std::max(fSq / (N * N) - fMean * fMean, 0.0));
      std::printf("  caustics cascade %d at %.1f m: mean %.5f, RMS %.3f, most %.2f\n", c, CAUSTICS_DEPTH[c] * (1 << k), fMean, fRms, fMost);
      CHECK_MSG(std::fabs(fMean - 1) < 1e-4, std::to_string(fMean));
      // Towards the focus the contrast grows (beyond it a random sea's stays high and evens out slowly).
      if (k < 3)
        CHECK(fRms > fBefore);
      fBefore = fRms;
      fLargest = std::max(fLargest, fRms);
    }
    CHECK(fLargest > 0.2);
  }
  double fWorst = 0;
  for (int m = 1; m < XR_CAUSTICS_MIPS; m++)
  {
    const int w = N >> m, b = 1 << m;
    for (int c = 0; c < CC; c++)
      for (int y = 0; y < w; y += std::max(w / 8, 1))
        for (int x = 0; x < w; x += std::max(w / 8, 1))
          for (int k = 0; k < 4; k++)
          {
            double fMean = 0;
            for (int j = 0; j < b; j++)
              for (int i = 0; i < b; i++)
                fMean += Texel(o, 0, c, x * b + i, y * b + j)[k];
            fMean /= b * b;
            fWorst = std::max(fWorst, std::fabs(Texel(o, m, c, x, y)[k] - fMean) / fMean);
          }
  }
  std::printf("  the levels against the means of level 0: %.2e\n", fWorst);
  CHECK_MSG(fWorst < 3e-3, std::to_string(fWorst));
}

// Why: the game's sea is a light wind's (3.5 m/s) with the slopes drawn at d3d12_WaterSlope's 0.0446, under half the
// physical sea's: its caustics must still show on the shallow floor, the short cascade's pattern well lit by 2 m (its RMS
// at least 0.4 there) and focused by 4 m, the middle cascade's faint there (its waves are longer); the physical sea's
// (d3d12_WaterSlope 0) at least as strong at every depth up to the focus.
XR_TEST(Caustics_DefaultSea)
{
  SSea Sea;
  MakeSea(3.5, Sea);
  std::vector<float> H0;
  for (const SCascade &k : Sea.Cascades)
    H0.insert(H0.end(), k.H0.begin(), k.H0.end());
  double L[3];
  Sun(60, 70, L);
  double Rms[2][CC][4] = {};
  for (int p = 0; p < 2; p++)
  {
    const double fScale = p ? 1.0 : 0.0446 / Sea.SlopeRMS();
    const SOceanOutput o = Gpu().RunOcean(H0, Constants(0.3, L, fScale), Kernels());
    for (int c = 0; c < CC; c++)
      for (int k = 0; k < 4; k++)
      {
        double fSum = 0, fSq = 0, fMost = 0;
        for (int y = 0; y < N; y++)
          for (int x = 0; x < N; x++)
          {
            const double v = Texel(o, 0, c, x, y)[k];
            fSum += v;
            fSq += v * v;
            fMost = std::max(fMost, v);
          }
        const double fMean = fSum / (N * N);
        Rms[p][c][k] = std::sqrt(std::max(fSq / (N * N) - fMean * fMean, 0.0));
        std::printf("  %s (slopes times %.3f) cascade %d at %.1f m: RMS %.3f, most %.2f\n", p ? "the physical sea" : "the game's sea", fScale,
                    c, CAUSTICS_DEPTH[c] * (1 << k), Rms[p][c][k], fMost);
      }
  }
  CHECK_MSG(Rms[0][1][2] > 0.4, std::to_string(Rms[0][1][2]));
  CHECK_MSG(Rms[0][1][3] > Rms[0][1][2], std::to_string(Rms[0][1][3]));
  CHECK_MSG(Rms[0][0][1] < 0.3, std::to_string(Rms[0][0][1]));
  for (int c = 0; c < CC; c++)
    for (int k = 0; k < 3; k++)
      CHECK_MSG(Rms[1][c][k] >= Rms[0][c][k], std::to_string(c) + ", " + std::to_string(k));
}

// Why: one wave a cos(k x) is a row of lenses: under its crests the rays gather, under its troughs they spread, the
// light of a point of the floor its rays' density, 1 / |dX/dx| of the map X = x + d m(x) from the surface (m the ray's
// move per metre), focused at d = 1 / ((1 - 1/n) a k^2) under the crest. The kernel must be the rays it says (in double,
// within half floats), their light the density before the focus within 3 % (4 x 4 rays a cell, bilinear: 2 x 2 left 8.7 %), and at the
// focus a bright line under the crest; the same with a slanting sun (the faces towards it brighter).
XR_TEST(Caustics_Wave)
{
  const int nBin = 8;                       // 32 texels a wave in the short cascade
  double k, ky;
  WaveVector(2, nBin, 0, k, ky);
  const double fTexel = TILE[2] / N, fFocus = 4.0, a = 1 / ((1 - ETA) * fFocus * k * k);
  const std::vector<float> H0 = OneWave(nBin, a);
  for (double fElevation : { 90.0, 50.0 })
  {
    double L[3];
    Sun(fElevation, 0, L);
    const SOceanOutput o = Gpu().RunOcean(H0, Constants(0, L, 1), Kernels());
    // The wave's slopes at the texels as the waves' kernel made them (half floats).
    std::vector<double> S(N);
    for (int x = 0; x < N; x++)
      S[x] = -a * k * std::sin(k * x * fTexel);
    // The rays against double: before the focus within the half floats, at it (the half floats' slopes move a ray a few
    // thousandths of a texel, a texel under the crest takes some 20 of them) within 2 %.
    double fWorstRays = 0, fWorstFocus = 0, fWorstDensity = 0, fWorstRows = 0;
    for (int i = 0; i < 4; i++)
    {
      const double d = CAUSTICS_DEPTH[1] * (1 << i);
      const std::vector<double> Want = RowRays([&](int x) { return S[x]; }, fTexel, d, L);
      for (int x = 0; x < N; x++)
      {
        const double v = Texel(o, 0, 1, x, 0)[i];
        double &fWorst = d < fFocus ? fWorstRays : fWorstFocus;
        fWorst = std::max(fWorst, std::fabs(v - Want[x]) / Want[x]);
        fWorstRows = std::max(fWorstRows, (double)std::fabs(Texel(o, 0, 1, x, 101)[i] - v) / v);
        // Before the focus (overhead: d at most half of it; slanting the first two depths): the rays' density.
        if (d <= fFocus / 2 && (fElevation == 90 || i < 2))
        {
          // The point x0 of the surface whose ray lands at texel x (the map increasing before the focus).
          double mx0, my0, f0;
          Refract(0, 0, L, mx0, my0, f0);
          auto Map = [&](double x0, double &fLight) {
            const double s = -a * k * std::sin(k * x0 * fTexel);
            double mx, my;
            Refract(s, 0, L, mx, my, fLight);
            return x0 + (mx - mx0) * d / fTexel;
          };
          double x0 = x, fLight = 1;
          for (int n = 0; n < 50; n++)
          {
            const double h = 1e-4, f1 = Map(x0 + h, fLight), f2 = Map(x0 - h, fLight);
            x0 -= (Map(x0, fLight) - x) / ((f1 - f2) / (2 * h));
          }
          const double fSlope = (Map(x0 + 1e-4, fLight) - Map(x0 - 1e-4, fLight)) / 2e-4;
          Map(x0, fLight);
          fWorstDensity = std::max(fWorstDensity, std::fabs(v - fLight / fSlope) / (fLight / fSlope));
        }
      }
    }
    std::printf("  %g degrees: the rays against double %.2e (at the focus %.2e), the density before the focus %.3f, the rows %.2e\n",
                fElevation, fWorstRays, fWorstFocus, fWorstDensity, fWorstRows);
    CHECK_MSG(fWorstRays < 3e-3, std::to_string(fWorstRays));
    CHECK_MSG(fWorstFocus < 0.02, std::to_string(fWorstFocus));
    CHECK_MSG(fWorstDensity < 0.03, std::to_string(fWorstDensity));
    CHECK_MSG(fWorstRows < 1e-3, std::to_string(fWorstRows));
    if (fElevation == 90)
    {
      // At the focus: the brightest texel of each wave under its crest (x = 0 mod 32), much brighter than the mean.
      for (int w = 0; w < nBin; w++)
      {
        int nAt = 0;
        float fMost = 0;
        for (int x = 0; x < N / nBin; x++)
          if (Texel(o, 0, 1, w * (N / nBin) + x, 0)[3] > fMost)
            fMost = Texel(o, 0, 1, w * (N / nBin) + x, 0)[3], nAt = x;
        CHECK_MSG(nAt <= 1 || nAt >= N / nBin - 1, std::to_string(nAt));
        CHECK_MSG(fMost > 4, std::to_string(fMost));
      }
    }
  }
}
