// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanTests.cpp
//	Description: Unit tests of the FFT ocean (d3d12_WaterFFT): its sea on
//	the CPU (OceanSpectrum.h: JONSWAP's spectrum, the dispersion, the
//	cascades) and its pass on the GPU (the renderer's OceanFFT.hlsl kernels
//	as OceanFFT.cpp runs them, GpuHarness.h's RunOcean): the fields against
//	the inverse FFTs of their spectra in double, the waves' slopes and
//	their units, the speed the waves run at, the mip levels' means (LEAN),
//	no repetition of the cascades' sum and the same waves every run.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "OceanSpectrum.h"

#include "Test_OceanRows.h"
#include "Test_OceanColumns.h"
#include "Test_OceanWaves.h"
#include "Test_OceanMips.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

using namespace XrTest;

namespace
{
  using namespace OceanSpectrum;
  using cd = std::complex<double>;
  constexpr int N = OceanSpectrum::SIZE, C = OceanSpectrum::CASCADES;   // SIZE alone is also Windows' struct

  SOceanKernels Kernels()
  {
    return { XR_BYTECODE(Test_OceanRows), XR_BYTECODE(Test_OceanColumns), XR_BYTECODE(Test_OceanWaves), XR_BYTECODE(Test_OceanMips) };
  }

  //! The frame's constants as UpdateOcean sets them (the heights' weights given).
  SOceanConstants Constants(double fTau, double fChop, const double Weights[C])
  {
    SOceanConstants oc = {};
    oc.vParams[0] = (float)fTau;
    oc.vParams[1] = (float)fChop;
    oc.vParams[2] = 0.1f;
    for (int c = 0; c < C; c++)
    {
      oc.vCascades[c][0] = (float)TILE[c];
      oc.vCascades[c][1] = (float)Weights[c];
      oc.vCascades[c][2] = (float)(TILE[c] / N);
    }
    return oc;
  }

  SOceanConstants Constants(double fTau, double fChop, const SSea &Sea)
  {
    double w[C];
    for (int c = 0; c < C; c++)
      w[c] = Sea.HeightWeight(c);
    return Constants(fTau, fChop, w);
  }

  std::vector<float> H0Of(const SSea &Sea)
  {
    std::vector<float> H0;
    for (const SCascade &k : Sea.Cascades)
      H0.insert(H0.end(), k.H0.begin(), k.H0.end());
    return H0;
  }

  //! Field pair s (0: Dx, Dy, h, dDx/dy; 1: dh/dx, dh/dy, dDx/dx, dDy/dy) of cascade c at texel (x, y).
  const float *Field(const SOceanOutput &o, int c, int s, int x, int y)
  {
    return &o.Fields[((((size_t)(2 * c + s) * N) + y) * N + x) * 4];
  }

  //! The waves' texel (x, y) of level m, cascade c.
  const float *Wave(const SOceanOutput &o, int m, int c, int x, int y)
  {
    const size_t w = (size_t)N >> m;
    return &o.Waves[m][(((size_t)c * w + y) * w + x) * 4];
  }

  //! In place: a[x] = sum over k of a[k] e^(2 pi i k x / n), n a power of 2.
  void InverseFFT(cd *a, int n, int nStride)
  {
    for (int i = 1, j = 0; i < n; i++)
    {
      int b = n >> 1;
      for (; j & b; b >>= 1)
        j ^= b;
      j ^= b;
      if (i < j)
        std::swap(a[(size_t)i * nStride], a[(size_t)j * nStride]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
      const cd w = std::polar(1.0, 2 * PI / len);
      for (int i = 0; i < n; i += len)
      {
        cd t = 1;
        for (int k = 0; k < len / 2; k++, t *= w)
        {
          cd &u = a[(size_t)(i + k) * nStride], &v = a[(size_t)(i + k + len / 2) * nStride];
          const cd x = u, y = v * t;
          u = x + y;
          v = x - y;
        }
      }
    }
  }

  //! N x N (a[y * N + x]): rows, then columns.
  void InverseFFT2(std::vector<cd> &a)
  {
    for (int y = 0; y < N; y++)
      InverseFFT(&a[(size_t)y * N], N, 1);
    for (int x = 0; x < N; x++)
      InverseFFT(&a[x], N, N);
  }

  //! The spectrum H(k, tau) of a texel of cascade c (OceanFFT.hlsli's OceanSpectra, in double).
  cd Spectrum(const SCascade &k, int x, int y, double fTau)
  {
    const float *p = &k.H0[((size_t)y * N + x) * 4];
    const int ox = (N - x) & (N - 1), oy = (N - y) & (N - 1);
    const float *q = &k.H0[((size_t)oy * N + ox) * 4];
    const double fSteps = p[2] * fTau;
    const double fTheta = 2 * PI * (fSteps - std::floor(fSteps));
    return cd(p[0], p[1]) * std::polar(1.0, -fTheta) + std::conj(cd(q[0], q[1])) * std::polar(1.0, fTheta);
  }

  //! The eight real fields of cascade c in double: 0 Dx, 1 Dy, 2 h, 3 dDx/dy, 4 dh/dx, 5 dh/dy, 6 dDx/dx, 7 dDy/dy; and the
  //! largest imaginary part their inverse FFTs left (none for a real field).
  std::vector<std::vector<double>> Fields(const SCascade &k, int c, double fTau, double &fImag)
  {
    std::vector<std::vector<cd>> F(8, std::vector<cd>((size_t)N * N));
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        double kx, ky;
        WaveVector(c, x, y, kx, ky);
        const double fK = std::sqrt(kx * kx + ky * ky);
        const cd H = Spectrum(k, x, y, fTau), I(0, 1);
        if (fK <= 0)
          continue;
        const double nx = kx / fK, ny = ky / fK;
        const size_t i = (size_t)y * N + x;
        F[0][i] = I * nx * H;
        F[1][i] = I * ny * H;
        F[2][i] = H;
        F[3][i] = -kx * ny * H;
        F[4][i] = I * kx * H;
        F[5][i] = I * ky * H;
        F[6][i] = -kx * nx * H;
        F[7][i] = -ky * ny * H;
      }
    std::vector<std::vector<double>> Out(8, std::vector<double>((size_t)N * N));
    fImag = 0;
    for (int f = 0; f < 8; f++)
    {
      InverseFFT2(F[f]);
      for (size_t i = 0; i < F[f].size(); i++)
      {
        Out[f][i] = F[f][i].real();
        fImag = std::max(fImag, std::fabs(F[f][i].imag()));
      }
    }
    return Out;
  }

  //! The GPU's field f (Fields' numbering) of cascade c at texel i.
  float GpuField(const SOceanOutput &o, int c, int f, size_t i)
  {
    return Field(o, c, f / 4, (int)(i % N), (int)(i / N))[f % 4];
  }

  double Rms(const std::vector<double> &v)
  {
    double s = 0;
    for (double x : v)
      s += x * x;
    return std::sqrt(s / std::max<size_t>(v.size(), 1));
  }
}

// Why: the sea's heights are the integral of its spectrum and its peak sets the long waves' length; JONSWAP's shape
// with gamma 1 is Pierson and Moskowitz's, whose integral is alpha g^2 / (5 wp^4); the spreading over the directions
// must neither add nor lose any of it.
XR_TEST(Ocean_Jonswap)
{
  for (double U : { 3.0, 5.0, 10.0 })
  {
    const double wp = PeakOmega(U);
    double fBest = 0, wBest = 0;
    for (int i = 0; i < 20000; i++)
    {
      const double w = wp * (0.3 + 4.7 * i / 20000.0);
      if (Jonswap(w, U) > fBest)
      {
        fBest = Jonswap(w, U);
        wBest = w;
      }
    }
    CHECK_NEAR(wBest / wp, 1.0, 0.005);
    double m0 = 0;
    const double dw = 1e-4;
    for (double w = dw / 2; w < 60; w += dw)
      m0 += Jonswap(w, U, 1.0) * dw;
    CHECK_NEAR(m0 / (Alpha(U) * G * G / (5 * wp * wp * wp * wp)), 1.0, 0.002);
    for (double r : { 0.5, 1.0, 3.0 })
    {
      double fSum = 0;
      const int n = 20000;
      for (int i = 0; i < n; i++)
        fSum += Spreading(-PI + 2 * PI * (i + 0.5) / n, r * wp, U) * 2 * PI / n;
      CHECK_NEAR(fSum, 1.0, 1e-3);
    }
  }
}

// Why: the waves run at the speed of their dispersion (deep water's, with surface tension at the shortest), and
// Omega's whole steps of 2 pi / PERIOD, which keep the GPU's phase exact, must not change any wave's speed by much.
XR_TEST(Ocean_Dispersion)
{
  CHECK_NEAR(Omega(0.05) / std::sqrt(G * 0.05), 1.0, 1e-6);
  CHECK_NEAR(Omega(K_CAPILLARY) / std::sqrt(2 * G * K_CAPILLARY), 1.0, 1e-12);
  for (double k : { 0.03, 0.5, 5.0, 50.0 })
    CHECK_NEAR(OmegaSlope(k) / ((Omega(k * (1 + 1e-6)) - Omega(k * (1 - 1e-6))) / (2e-6 * k)), 1.0, 1e-6);
  double fWorst = 0;
  for (int c = 0; c < C; c++)
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        double kx, ky;
        WaveVector(c, x, y, kx, ky);
        const double k = std::sqrt(kx * kx + ky * ky);
        if (CascadeOf(k) == c)
          fWorst = std::max(fWorst, std::fabs(OmegaSteps(k) * 2 * PI / PERIOD / Omega(k) - 1));
      }
  CHECK(fWorst > 0 && fWorst < 0.01);
}

// Why: a wave in two cascades would be twice as high and one in none missing; each must be in exactly one, the bands
// next to each other, each cascade's longest wave several times across its tile; the sea the cascades hold must be the
// spectrum's (its heights' variance its integral) with the slopes shared between them.
XR_TEST(Ocean_Partition)
{
  for (int c = 0; c + 1 < C; c++)
    CHECK(TILE[c + 1] >= 9 * SHORTEST[c] && TILE[c + 1] < TILE[c]);
  CHECK_NEAR(SHORTEST[C - 1], 4 * TILE[C - 1] / N, 1e-12);
  SSea Sea;
  MakeSea(5, Sea);
  double fHeightVar = 0, fSlopeVar = 0;
  for (int c = 0; c < C; c++)
  {
    const SCascade &k = Sea.Cascades[c];
    int nBad = 0;
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        double kx, ky;
        WaveVector(c, x, y, kx, ky);
        const bool bIn = CascadeOf(std::sqrt(kx * kx + ky * ky)) == c && x != N / 2 && y != N / 2;
        const float *p = &k.H0[((size_t)y * N + x) * 4];
        if (bIn != (p[2] != 0))
          nBad++;
      }
    CHECK(nBad == 0);
    fHeightVar += k.fHeightVar;
    fSlopeVar += k.fSlopeVarX + k.fSlopeVarY;
  }
  for (int c = 0; c < C; c++)
  {
    const double fShare = (Sea.Cascades[c].fSlopeVarX + Sea.Cascades[c].fSlopeVarY) / fSlopeVar;
    CHECK(fShare > 0.2 && fShare < 0.5);
  }
  // The integral over the frequencies the cascades hold (from the long tile's longest wave to the short one's shortest).
  double m0 = 0;
  const double w0 = Omega(2 * PI / TILE[0]), w1 = Omega(2 * PI / SHORTEST[C - 1]), dw = 1e-4;
  for (double w = w0 + dw / 2; w < w1; w += dw)
    m0 += Jonswap(w, 5) * dw;
  CHECK_NEAR(fHeightVar / m0, 1.0, 0.03);
  CHECK_NEAR(Sea.SlopeVar(), fSlopeVar, 1e-12);
}

// Why: one wave of h0 must come out of the kernels as the cosine its spectrum says, at its phase at the time, with its
// displacement, slopes and their derivatives (each cascade in its own slices): the FFTs, their order of the frequencies
// and the packing of the eight fields (to 1e-4 of each: WARP's sines and cosines of the twiddles and of the phase are
// less exact than a GPU's, 5e-5 there).
XR_TEST(Ocean_FFT)
{
  std::vector<float> H0((size_t)C * N * N * 4, 0.0f);
  const int Bins[C][2] = { { 3, N - 2 }, { N - 5, 7 }, { 11, 4 } };
  const float Amp[C][2] = { { 0.02f, 0.01f }, { -0.004f, 0.003f }, { 0.0005f, -0.0007f } };
  const double fTau = 0.123;
  for (int c = 0; c < C; c++)
  {
    const int x = Bins[c][0], y = Bins[c][1], ox = (N - x) & (N - 1), oy = (N - y) & (N - 1);
    double kx, ky;
    WaveVector(c, x, y, kx, ky);
    const float fSteps = (float)OmegaSteps(std::sqrt(kx * kx + ky * ky));
    float *p = &H0[(((size_t)c * N + y) * N + x) * 4];
    p[0] = Amp[c][0];
    p[1] = Amp[c][1];
    p[2] = fSteps;
    H0[(((size_t)c * N + oy) * N + ox) * 4 + 2] = fSteps;
  }
  const double One[C] = { 1, 1, 1 };
  const SOceanOutput o = Gpu().RunOcean(H0, Constants(fTau, 0, One), Kernels());
  for (int c = 0; c < C; c++)
  {
    double kx, ky;
    WaveVector(c, Bins[c][0], Bins[c][1], kx, ky);
    const double fK = std::sqrt(kx * kx + ky * ky), nx = kx / fK, ny = ky / fK;
    const double fSteps = OmegaSteps(fK) * fTau, fTheta = 2 * PI * (fSteps - std::floor(fSteps));
    const double a = 2 * std::abs(cd(Amp[c][0], Amp[c][1])), fArg = std::arg(cd(Amp[c][0], Amp[c][1]));
    double fWorst = 0;
    for (int y = 0; y < N; y += 3)
      for (int x = 0; x < N; x += 5)
      {
        const double X = x * TILE[c] / N, Y = y * TILE[c] / N;
        const double fPhase = kx * X + ky * Y + fArg - fTheta, s = std::sin(fPhase), co = std::cos(fPhase);
        // Dx, Dy, h, dDx/dy; dh/dx, dh/dy, dDx/dx, dDy/dy, each over its scale.
        const double Want[8] = { -a * nx * s, -a * ny * s, a * co, -a * nx * ky * co, -a * kx * s, -a * ky * s, -a * nx * kx * co, -a * ny * ky * co };
        const double Scale[8] = { a, a, a, a * fK, a * fK, a * fK, a * fK, a * fK };
        for (int f = 0; f < 8; f++)
          fWorst = std::max(fWorst, std::fabs(Field(o, c, f / 4, x, y)[f % 4] - Want[f]) / Scale[f]);
      }
    CHECK_MSG(fWorst < 1e-4, std::to_string(fWorst));
  }
}

// Why: the four complex fields the kernels transform carry two real fields each; they separate only if each real
// field's spectrum is hermitian, which h0 and its opposite texel's steps make so. The GPU's fields of a whole sea
// must be the inverse FFTs of the eight spectra in double (at the time as the GPU has it, a float; its phases n tau to
// a float's precision, n up to some thousands in the short cascade: 1e-4 of the fields there).
XR_TEST(Ocean_Packing)
{
  SSea Sea;
  MakeSea(5, Sea);
  const double fTau = (double)0.37f;
  const SOceanOutput o = Gpu().RunOcean(H0Of(Sea), Constants(fTau, 0, Sea), Kernels());
  for (int c = 0; c < C; c++)
  {
    double fImag = 0;
    const std::vector<std::vector<double>> Want = Fields(Sea.Cascades[c], c, fTau, fImag);
    for (int f = 0; f < 8; f++)
    {
      const double fRms = Rms(Want[f]);
      double fErr = 0;
      for (size_t i = 0; i < Want[f].size(); i++)
      {
        const double d = GpuField(o, c, f, i) - Want[f][i];
        fErr += d * d;
      }
      fErr = std::sqrt(fErr / Want[f].size());
      CHECK(fRms > 0);
      CHECK_MSG(fErr < (c == C - 1 ? 2e-4 : 1e-4) * fRms, "cascade " + std::to_string(c) + " field " + std::to_string(f) + ": " + std::to_string(fErr / fRms));
      CHECK(fImag < 1e-9 * fRms);
    }
  }
}

// Why: the water's normal takes the waves' slopes in metres per metre, summed over the cascades; without the
// displacement they are the fields' slopes, the height in its weight and the squared slope with them, and over the
// tiles their RMS is the spectrum's (Water.hlsl's crests' light and the glint take it from the CPU's sea).
XR_TEST(Ocean_Slopes)
{
  SSea Sea;
  MakeSea(5, Sea);
  const SOceanOutput o = Gpu().RunOcean(H0Of(Sea), Constants(0.61, 0, Sea), Kernels());
  // Each channel within a step of a half float (2^-10 of the value, 2^-24 at the subnormals: a store may round towards
  // zero, as the RX 7900 XTX's does) and a little of the float arithmetic.
  double fWorst = 0, fVar = 0;
  for (int c = 0; c < C; c++)
  {
    const double w = Sea.HeightWeight(c);
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        const float *t = Wave(o, 0, c, x, y), *a = Field(o, c, 0, x, y), *b = Field(o, c, 1, x, y);
        const double s2 = (double)b[0] * b[0] + (double)b[1] * b[1];
        const double Want[4] = { b[0], b[1], a[2] * w, s2 };
        for (int i = 0; i < 4; i++)
          fWorst = std::max(fWorst, std::fabs(t[i] - Want[i]) / (std::fabs(Want[i]) * (std::ldexp(1.0, -10) + 1e-6) + std::ldexp(1.0, -24)));
        fVar += s2;
      }
  }
  CHECK(fWorst <= 1.0);
  CHECK_NEAR(std::sqrt(fVar / ((double)N * N) / 2) / Sea.SlopeRMS(), 1.0, 0.1);
}

// Why: the waves must run at their own speeds (the long ones faster), along their wave vectors, so that they change
// their shape as they go: two waves' phases after a second have moved by their Omega, a speed of Omega / k.
XR_TEST(Ocean_PhaseSpeed)
{
  std::vector<float> H0((size_t)C * N * N * 4, 0.0f);
  const int Bins[2][2] = { { 2, 0 }, { 0, 9 } };
  for (const auto &b : Bins)
  {
    double kx, ky;
    WaveVector(0, b[0], b[1], kx, ky);
    const float fSteps = (float)OmegaSteps(std::sqrt(kx * kx + ky * ky));
    float *p = &H0[((size_t)b[1] * N + b[0]) * 4];
    p[0] = 0.05f;
    p[2] = fSteps;
    H0[((size_t)((N - b[1]) & (N - 1)) * N + ((N - b[0]) & (N - 1))) * 4 + 2] = fSteps;
  }
  const double One[C] = { 1, 1, 1 }, fTau = 0.2, fDt = 1.0;
  const SOceanOutput o0 = Gpu().RunOcean(H0, Constants(fTau, 0, One), Kernels());
  const SOceanOutput o1 = Gpu().RunOcean(H0, Constants(fTau + fDt / PERIOD, 0, One), Kernels());
  for (const auto &b : Bins)
  {
    double kx, ky;
    WaveVector(0, b[0], b[1], kx, ky);
    const double fK = std::sqrt(kx * kx + ky * ky);
    // The wave's phase: the height against cos and sin of k.x.
    auto Phase = [&](const SOceanOutput &o) {
      cd z = 0;
      for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
          z += (double)Field(o, 0, 0, x, y)[2] * std::polar(1.0, -(kx * x + ky * y) * TILE[0] / N);
      return std::arg(z);
    };
    double d = Phase(o1) - Phase(o0);
    d -= 2 * PI * std::round(d / (2 * PI));
    // Its crests move along k: the phase falls by Omega dt.
    const double fWant = OmegaSteps(fK) * 2 * PI / PERIOD * fDt;
    CHECK_NEAR(-d, fWant, 1e-3);
    CHECK_NEAR(-d / fK / fDt / (Omega(fK) / fK), 1.0, 0.01);
  }
}

// Why: the water's LEAN takes the variance its waves lost over a pixel from the mip levels as the mean of the squared
// slope less the square of the mean slope; each level must hold the means of the level-0 texels under it.
XR_TEST(Ocean_Lean)
{
  SSea Sea;
  MakeSea(5, Sea);
  const SOceanOutput o = Gpu().RunOcean(H0Of(Sea), Constants(0.45, 0, Sea), Kernels());
  double fWorst = 0, fLeast = 0;
  for (int m = 1; m < XR_OCEAN_MIPS; m++)
  {
    const int w = N >> m, n = 1 << m, nStep = std::max(1, w / 16);
    for (int c = 0; c < C; c++)
      for (int y = 0; y < w; y += nStep)
        for (int x = 0; x < w; x += nStep)
        {
          double Mean[4] = {};
          for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++)
            {
              const float *t = Wave(o, 0, c, x * n + i, y * n + j);
              for (int e = 0; e < 4; e++)
                Mean[e] += t[e] / ((double)n * n);
            }
          const float *t = Wave(o, m, c, x, y);
          const double fScale = std::sqrt(Mean[3]) + 1e-3;
          for (int e = 0; e < 2; e++)
            fWorst = std::max(fWorst, std::fabs(t[e] - Mean[e]) / fScale);
          fWorst = std::max(fWorst, std::fabs(t[3] - Mean[3]) / (Mean[3] + 1e-6));
          fLeast = std::min(fLeast, (t[3] - (double)t[0] * t[0] - (double)t[1] * t[1]) / (Mean[3] + 1e-6));
        }
  }
  CHECK(fWorst < 5e-3);
  CHECK(fLeast > -5e-3);
}

// Why: the old wave texture's layers repeated every few metres; the cascades' sum must not: at no shift within
// 600 m along the axes and the diagonals may the summed slopes correlate much with themselves (one cascade alone
// does fully at its tile). The tiles' sizes are chosen so that no two of them nearly share a multiple there.
XR_TEST(Ocean_NoRepeat)
{
  SSea Sea;
  MakeSea(5, Sea);
  const SOceanOutput o = Gpu().RunOcean(H0Of(Sea), Constants(0.29, 0, Sea), Kernels());
  // Each cascade's slopes' autocorrelation on its grid (periodic), by FFT: |F|^2 back.
  std::vector<std::vector<double>> R(C);
  double fTotal = 0;
  for (int c = 0; c < C; c++)
  {
    R[c].assign((size_t)N * N, 0.0);
    for (int e = 0; e < 2; e++)
    {
      std::vector<cd> a((size_t)N * N);
      for (size_t i = 0; i < a.size(); i++)
        a[i] = Wave(o, 0, c, (int)(i % N), (int)(i / N))[e];
      InverseFFT2(a);   // the conjugate transform: the same |F|^2
      for (cd &z : a)
        z = std::norm(z);
      InverseFFT2(a);
      for (size_t i = 0; i < a.size(); i++)
        R[c][i] += a[i].real() / ((double)N * N * N * N);
    }
    fTotal += R[c][0];
  }
  auto Corr = [&](double sx, double sy, int nOnly) {
    double r = 0;
    for (int c = 0; c < C; c++)
    {
      if (nOnly >= 0 && c != nOnly)
        continue;
      double u = sx * N / TILE[c], v = sy * N / TILE[c];
      u -= N * std::floor(u / N);
      v -= N * std::floor(v / N);
      const int x0 = (int)u % N, y0 = (int)v % N, x1 = (x0 + 1) % N, y1 = (y0 + 1) % N;
      const double fx = u - std::floor(u), fy = v - std::floor(v);
      const auto At = [&](int x, int y) { return R[c][(size_t)y * N + x]; };
      r += (At(x0, y0) * (1 - fx) + At(x1, y0) * fx) * (1 - fy) + (At(x0, y1) * (1 - fx) + At(x1, y1) * fx) * fy;
    }
    return r / (nOnly >= 0 ? R[nOnly][0] : fTotal);
  };
  for (int c = 0; c < C; c++)
    CHECK_NEAR(Corr(TILE[c], 0, c), 1.0, 1e-6);
  const double Dirs[4][2] = { { 1, 0 }, { 0, 1 }, { 0.70710678, 0.70710678 }, { 0.70710678, -0.70710678 } };
  double fMost = 0, fAt = 0;
  for (const auto &d : Dirs)
    for (double s = 20; s < 600; s += 0.02)
    {
      const double r = Corr(s * d[0], s * d[1], -1);
      if (r > fMost)
      {
        fMost = r;
        fAt = s;
      }
    }
  CHECK_MSG(fMost < 0.6, "the summed slopes correlate by " + std::to_string(fMost) + " at a shift of " + std::to_string(fAt) + " m");
}

// Why: the demos' frames must be the same every run (the identity and media checks): the sea from its hash, the
// kernels without races.
XR_TEST(Ocean_Deterministic)
{
  SSea a, b;
  MakeSea(6, a);
  MakeSea(6, b);
  for (int c = 0; c < C; c++)
    CHECK(a.Cascades[c].H0 == b.Cascades[c].H0);
  const SOceanOutput o0 = Gpu().RunOcean(H0Of(a), Constants(0.77, 0, a), Kernels());
  const SOceanOutput o1 = Gpu().RunOcean(H0Of(a), Constants(0.77, 0, a), Kernels());
  CHECK(memcmp(o0.Fields.data(), o1.Fields.data(), o0.Fields.size() * 4) == 0);
  for (int m = 0; m < XR_OCEAN_MIPS; m++)
    CHECK(memcmp(o0.Waves[m].data(), o1.Waves[m].data(), o0.Waves[m].size() * 4) == 0);
}
