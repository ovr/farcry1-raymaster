// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanSpectrum.h
//	Description: The FFT ocean's sea on the CPU (OceanFFT.cpp; the unit
//	tests): the wind sea's spectrum, how its waves run, which of the three
//	cascades holds each, and the cascades' starting amplitudes (Tessendorf,
//	"Simulating Ocean Water"), in double. Without the renderer's types.
//
//	  - JONSWAP's spectrum of a sea limited by its fetch (Hasselmann et al.
//	    1973): S(w) = alpha g^2 / w^5 exp(-5/4 (wp / w)^4) gamma^r, r =
//	    exp(-(w - wp)^2 / (2 sigma^2 wp^2)), alpha = 0.076 (U^2 / (F g))^0.22,
//	    wp = 22 (g^2 / (U F))^(1/3), gamma 3.3, sigma 0.07 below the peak and
//	    0.09 above; U the wind (d3d12_WaterWind), F = FETCH;
//	  - spread over directions by Mitsuyasu's cos^2s(theta / 2) about the
//	    wind (WIND), s = s_p (w / wp)^5 below the peak, s_p (w / wp)^-2.5
//	    above, s_p = 11.5 (U / c_p)^-2.5: the long waves along the wind, the
//	    ripple in every direction, some of it against it;
//	  - deep water's dispersion with the capillary term, w^2 = g k (1 + k^2 /
//	    K_CAPILLARY^2), w taken to the nearest multiple of 2 pi / PERIOD (the
//	    sea repeats every PERIOD seconds, so that the phase w t stays exact
//	    in a float: the GPU takes 2 pi frac(n tau), tau the time's fraction of
//	    the period);
//	  - three tiles of SIZE x SIZE texels, TILE metres a side (250, 40.5 and
//	    6.9: no two of them nearly share a multiple under 900 m, so their sum
//	    does not repeat where the camera could see it); each wave is in
//	    exactly one of them (CascadeOf): the long cascade the waves longer than
//	    4 m, the middle one those down to 0.7 m, the short one those down to 4
//	    of its texels (0.11 m), so that a cascade's longest wave runs about 10
//	    times or more across its tile and its shortest over 4 texels or more.
//	    Over the k^-3 tail of the spectrum each cascade holds about a third of
//	    the slopes' variance (most of the height's in the long one);
//	  - h0(k) = (xi_r + i xi_i) sqrt(S(k) dk^2) / 2, the xi gaussians of a
//	    hash of the cascade and the texel (Box-Muller of PCG's): the same sea
//	    every run. The height is h(x, t) = sum H(k, t) e^(i k.x) with H(k, t) =
//	    h0(k) e^(-i w t) + conj(h0(-k)) e^(i w t): a wave of h0(k) runs along
//	    k, and the variance of h is sum S(k) dk^2.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_OCEANSPECTRUM_H
#define XRENDERD3D12_OCEANSPECTRUM_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace OceanSpectrum
{
  constexpr int SIZE = 256;                       //!< texels a side of a cascade (XR_OCEAN_SIZE)
  constexpr int CASCADES = 3;                     //!< XR_OCEAN_CASCADES
  //! Metres a side (XR_OCEAN_TILE0..2): about 250, 40 and 7, no two of them nearly sharing a multiple under 900 m (40
  //! and 7 would every 280 m: the two short cascades' sum would repeat there, Ocean_NoRepeat).
  constexpr double TILE[CASCADES] = { 250, 40.5, 6.9 };
  //! The shortest wave of each cascade (m); the next one's longest. The last: 4 of its texels.
  constexpr double SHORTEST[CASCADES] = { 4.0, 0.7, 4 * 6.9 / SIZE };
  constexpr double G = 9.81;
  constexpr double PI = 3.14159265358979323846;
  constexpr double FETCH = 50000;                 //!< m: the distance the wind blows over the sea
  constexpr double GAMMA = 3.3;                   //!< JONSWAP's peak enhancement
  constexpr double K_CAPILLARY = 370;             //!< rad / m: where surface tension matches gravity (1.7 cm)
  constexpr double PERIOD = 1024;                 //!< s: the sea repeats
  constexpr double WIND[2] = { 0.94, 0.34 };      //!< the wind's direction (the old wave texture's main layer's)
  //! Cox and Munk's variance of the slopes of a clean sea (both axes together): 0.003 + 0.00512 U.
  constexpr double COX_MUNK[2] = { 0.003, 0.00512 };
  //! The least variance of the drawn slopes finer than the short cascade (the sun's glint; OceanFFT.cpp applies it
  //! after the bump amounts' factor): a sharper glint would glitter in single pixels from frame to frame.
  constexpr double LEAST_FINE_VARIANCE = 0.001;
  //! Monahan and O'Muircheartaigh's (1980) share of the open sea white with whitecaps at the wind U (m/s, 10 m up):
  //! 3.84e-6 U^3.41 (0.03 % at 3.5 m/s, 0.3 % at 7, 1 % at 10, 3.9 % at 15).
  constexpr double MONAHAN[2] = { 3.84e-6, 3.41 };
  //! The whitecaps' share of each cascade's crests: the long waves' and the middle ones' (the short cascade's ripple
  //! breaks without trapping air: no foam).
  constexpr double WHITECAP_SHARE[CASCADES] = { 0.5, 0.5, 0 };
  //! The share of the whitecaps' coverage breaking at a time (the rest the foam they leave, fading over FOAM_LIFE),
  //! fitted by Ocean_WhitecapCoverage (the coverage Monahan's within 8 % at 7, 10 and 15 m/s).
  constexpr double WHITECAP_ACTIVE = 0.25;
  //! s: the whitecaps' foam fades as exp(-t / FOAM_LIFE) once its crest has passed.
  constexpr double FOAM_LIFE = 4;
  //! The width of the determinant's ramp over which a breaking crest turns white, in the RMS of its spread about 1.
  constexpr double FOAM_RAMP = 0.25;
  //! m^2/s: how fast the foam of each cascade's whitecaps spreads over the sea as it fades (a diffusion: its patches
  //! grow, sqrt(2 FOAM_SPREAD t) about the crest that left them, 2 m after FOAM_LIFE of the long cascade's, the middle
  //! cascade's smaller breakers' as much in their texels, 0.3 m), the coverage kept.
  constexpr double FOAM_SPREAD[CASCADES] = { 0.5, 0.5 * (TILE[1] / TILE[0]) * (TILE[1] / TILE[0]), 0 };
  //! The most of a texel's foam spread to each of its neighbours in a step (the diffusion's FTCS is stable below 0.25).
  constexpr double FOAM_MOST_SPREAD = 0.2;
  //! The share of a texel's foam of cascade c spread to each of its neighbours over fSeconds.
  inline double FoamSpreadShare(int c, double fSeconds)
  {
    const double fTexel = TILE[c] / SIZE;
    return std::min(FOAM_SPREAD[c] * std::max(fSeconds, 0.0) / (fTexel * fTexel), FOAM_MOST_SPREAD);
  }
  //! s: the foam's kernels run at most this often (no more work at a higher frame rate; between, the water reads the
  //! last view).
  constexpr double FOAM_STEP = 1.0 / 120;
  //! The caustics' cascades (XR_CAUSTICS_CASCADES: the middle and the short one, from XR_CAUSTICS_FIRST) and the first of
  //! each one's four depths (m), each of the others twice the one before (WaterCaustics.hlsli's CausticsAtDepth): the short
  //! waves (0.1-0.7 m) focus the sun at 1-8 m of depth at the drawn steepness, the middle ones (0.7-4 m) at 7-50 m.
  constexpr int CAUSTICS_CASCADES = 2, CAUSTICS_FIRST = 1;
  constexpr double CAUSTICS_DEPTH[CAUSTICS_CASCADES] = { 2.0, 0.5 };

  //! The share of the sea white with whitecaps at the wind U (m/s), Monahan's.
  inline double WhitecapCoverage(double U)
  {
    return MONAHAN[0] * std::pow(std::max(U, 0.0), MONAHAN[1]);
  }

  //! m/s: the wind below which the sea has no whitecaps (d3d12_WaterWhitecapWind's default): the onset of whitecapping,
  //! Beaufort's 3 ("crests begin to break, scattered whitecaps" from 3.4 m/s), the winds Monahan's law was fitted over
  //! starting about there (a calm sea's crests do not break; the game's sea is calm, its wind 3.5 m/s).
  constexpr double WHITECAP_ONSET = 4;

  //! The share of the sea white with whitecaps at the wind U (m/s) with none below the wind fOnset (0: Monahan's at
  //! any wind).
  inline double WhitecapCoverageFrom(double U, double fOnset)
  {
    return U < fOnset ? 0.0 : WhitecapCoverage(U);
  }

  //! The light (the Rec. 709 luminance of the engine's sun's colour) up to which the sun is a moon: Steam's night 0.28,
  //! Rebellion's under its storm 0.31; by day 0.46 (in Volcano's smoke) to 0.78.
  constexpr double MOON_LIGHT = 0.36;
  //! The light from which the sun is the day's.
  constexpr double DAY_LIGHT = 0.42;

  //! The share of the surf's foam shown under the sun's light fLuma (the luminance of its colour), fNight the share
  //! under a moon (d3d12_WaterSurfNight): fNight up to MOON_LIGHT, all from DAY_LIGHT, smoothstep between; all without
  //! a sun (fLuma 0: a scene indoors has none, and its water's light is not a moon's).
  inline double SurfLightShare(double fLuma, double fNight)
  {
    if (!(fLuma > 0))
      return 1;
    const double n = std::min(std::max(fNight, 0.0), 1.0);
    const double t = std::min(std::max((fLuma - MOON_LIGHT) / (DAY_LIGHT - MOON_LIGHT), 0.0), 1.0);
    return n + (1 - n) * t * t * (3 - 2 * t);
  }

  //! The standard normal distribution's quantile: the x below which the share p of it lies (bisection of erfc; for the
  //! few calls at a change of the wind).
  inline double NormalQuantile(double p)
  {
    double fLow = -40, fHigh = 40;
    for (int i = 0; i < 200; i++)
    {
      const double m = 0.5 * (fLow + fHigh);
      (0.5 * std::erfc(-m / std::sqrt(2.0)) < p ? fLow : fHigh) = m;
    }
    return 0.5 * (fLow + fHigh);
  }

  //! Deep water's angular frequency (rad / s) of a wave of wave number k (rad / m).
  inline double Omega(double k)
  {
    return std::sqrt(G * k * (1 + k * k / (K_CAPILLARY * K_CAPILLARY)));
  }

  //! d Omega / dk: its group velocity (m / s).
  inline double OmegaSlope(double k)
  {
    return G * (1 + 3 * k * k / (K_CAPILLARY * K_CAPILLARY)) / (2 * std::max(Omega(k), 1e-12));
  }

  //! Omega(k) as a whole number of steps of 2 pi / PERIOD (the nearest).
  inline int OmegaSteps(double k)
  {
    return (int)std::lround(Omega(k) * PERIOD / (2 * PI));
  }

  //! JONSWAP's peak (rad / s) and its alpha for the wind U (m / s).
  inline double PeakOmega(double U)
  {
    return 22 * std::cbrt(G * G / (U * FETCH));
  }
  inline double Alpha(double U)
  {
    return 0.076 * std::pow(U * U / (FETCH * G), 0.22);
  }

  //! JONSWAP's variance density per rad / s (m^2 s) at w, wind U; fGamma 1: Pierson and Moskowitz's shape.
  inline double Jonswap(double w, double U, double fGamma = GAMMA)
  {
    if (w <= 0)
      return 0;
    const double wp = PeakOmega(U);
    const double fSigma = w <= wp ? 0.07 : 0.09;
    const double r = std::exp(-(w - wp) * (w - wp) / (2 * fSigma * fSigma * wp * wp));
    return Alpha(U) * G * G / std::pow(w, 5) * std::exp(-1.25 * std::pow(wp / w, 4)) * std::pow(fGamma, r);
  }

  //! Mitsuyasu's spreading s at w, wind U.
  inline double SpreadPower(double w, double U)
  {
    const double wp = PeakOmega(U);
    const double sp = 11.5 * std::pow(U * wp / G, -2.5);
    return w < wp ? sp * std::pow(w / wp, 5) : sp * std::pow(w / wp, -2.5);
  }

  //! The share per radian of the waves of w running at the angle fTheta to the wind: cos^2s(theta / 2) normalised over
  //! the circle (its integral 2 sqrt(pi) Gamma(s + 1/2) / Gamma(s + 1)).
  inline double Spreading(double fTheta, double w, double U)
  {
    const double s = SpreadPower(w, U);
    const double q = std::exp(std::lgamma(s + 1) - std::lgamma(s + 0.5)) / (2 * std::sqrt(PI));
    return q * std::pow(std::fabs(std::cos(fTheta / 2)), 2 * s);
  }

  //! The variance density per (rad / m)^2 of the waves of wave vector (kx, ky), wind U: S(w) D(theta) (dw / dk) / k.
  inline double Spectrum(double kx, double ky, double U)
  {
    const double k = std::sqrt(kx * kx + ky * ky);
    if (k <= 0)
      return 0;
    const double w = Omega(k);
    const double fWind = std::atan2(WIND[1], WIND[0]);
    return Jonswap(w, U) * Spreading(std::atan2(ky, kx) - fWind, w, U) * OmegaSlope(k) / k;
  }

  //! The cascade that holds the waves of wave number k (rad / m), -1 none (longer than the long tile is not asked; shorter
  //! than the short cascade's 4 texels).
  inline int CascadeOf(double k)
  {
    if (k <= 0)
      return -1;
    for (int c = 0; c < CASCADES; c++)
      if (k < 2 * PI / SHORTEST[c])
        return c;
    return -1;
  }

  //! The wave vector (rad / m) of texel (x, y) of cascade c: its frequencies in the FFT's order (0..SIZE/2-1, then
  //! -SIZE/2..-1) times 2 pi / TILE.
  inline void WaveVector(int c, int x, int y, double &kx, double &ky)
  {
    const int mx = x < SIZE / 2 ? x : x - SIZE, my = y < SIZE / 2 ? y : y - SIZE;
    kx = 2 * PI * mx / TILE[c];
    ky = 2 * PI * my / TILE[c];
  }

  //! PCG's hash (O'Neill; Jarzynski and Olano's form).
  inline uint32_t Pcg(uint32_t v)
  {
    const uint32_t s = v * 747796405u + 2891336453u;
    const uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
  }

  //! Two independent gaussians of mean 0 and variance 1 for texel (x, y) of cascade c (Box-Muller).
  inline void Gaussians(int c, int x, int y, double &a, double &b)
  {
    const uint32_t nKey = Pcg((uint32_t)(c * SIZE * SIZE + y * SIZE + x) ^ 0x6F2A41C3u);
    const double u1 = (Pcg(nKey) + 0.5) / 4294967296.0, u2 = (Pcg(nKey ^ 0x9E3779B9u) + 0.5) / 4294967296.0;
    const double r = std::sqrt(-2 * std::log(u1));
    a = r * std::cos(2 * PI * u2);
    b = r * std::sin(2 * PI * u2);
  }

  //! A cascade of the sea: per texel (row by row) h0's real and imaginary parts and Omega's steps (OmegaSteps), 0 where the
  //! cascade holds no wave; and what its waves give on average: the variance of the height (m^2), of the slopes along x
  //! and along y.
  struct SCascade
  {
    std::vector<float> H0;          //!< SIZE * SIZE * 4 floats (RGBA32F: h0 re, im, steps, 0)
    double fHeightVar = 0;
    double fSlopeVarX = 0, fSlopeVarY = 0;
  };

  //! The sea of the wind U.
  struct SSea
  {
    double fWind = 0;
    SCascade Cascades[CASCADES];
    //! The slopes' variance the cascades hold together (both axes).
    double SlopeVar() const
    {
      double v = 0;
      for (const SCascade &c : Cascades)
        v += c.fSlopeVarX + c.fSlopeVarY;
      return v;
    }
    //! The RMS of the slopes along one axis (the mean of the two).
    double SlopeRMS() const
    {
      return std::sqrt(SlopeVar() / 2);
    }
    //! The weight of cascade c's height in the height the water's light through the crests reads (Water.hlsl's
    //! WaveNormal): its share of the slopes over its height's RMS, so that the cascades' heights together have RMS 1, each
    //! in the weight of its slopes (as the old wave texture's layers).
    double HeightWeight(int c) const
    {
      const SCascade &k = Cascades[c];
      const double fShare = (k.fSlopeVarX + k.fSlopeVarY) / std::max(SlopeVar(), 1e-30);
      return k.fHeightVar > 0 ? std::sqrt(fShare / k.fHeightVar) : 0;
    }
    //! The variance of the slopes finer than the short cascade (both axes): Cox and Munk's of the wind less what the
    //! cascades hold (none at a light wind, where the cascades hold more than Cox and Munk measured).
    double FineVar() const
    {
      return std::max(COX_MUNK[0] + COX_MUNK[1] * fWind - SlopeVar(), 0.0);
    }
    //! How far the determinant of cascade c's Jacobian with the displacement's factor fChop spreads about 1 (its RMS, to the
    //! first order): J = 1 + fChop div D, div D = -|k| H a gaussian of the variance of the cascade's slopes along both axes.
    double JacobianSigma(int c, double fChop) const
    {
      const SCascade &k = Cascades[c];
      return fChop * std::sqrt(k.fSlopeVarX + k.fSlopeVarY);
    }
    //! The first order of the determinant of cascade c's Jacobian (1 + fChop div D, OceanFFT.hlsli's OceanFoam) below which
    //! its crests break into whitecaps: the share of its surface below it is its share of the whitecaps breaking at a time
    //! (WhitecapCoverage times WHITECAP_SHARE times WHITECAP_ACTIVE); -1 (none) for a cascade without whitecaps.
    double WhitecapJacobian(int c, double fChop) const
    {
      const double fShare = WhitecapCoverage(fWind) * WHITECAP_SHARE[c] * WHITECAP_ACTIVE;
      const double s = JacobianSigma(c, fChop);
      return fShare > 0 && s > 0 ? 1 + s * NormalQuantile(fShare) : -1;
    }
    //! The sea's significant height (m): 4 times the RMS of the height of its cascades together.
    double SignificantHeight() const
    {
      return 4 * std::sqrt(Cascades[0].fHeightVar + Cascades[1].fHeightVar + Cascades[2].fHeightVar);
    }
    //! The speed (m / s) of the waves at the spectrum's peak: g over its frequency, deep water's phase speed.
    double PeakSpeed() const
    {
      return G / PeakOmega(fWind);
    }
  };

  //! The sea of the wind U (m / s, at least 0.5).
  inline void MakeSea(double U, SSea &Sea)
  {
    U = std::max(U, 0.5);
    Sea.fWind = U;
    for (int c = 0; c < CASCADES; c++)
    {
      SCascade &k = Sea.Cascades[c];
      k.H0.assign((size_t)SIZE * SIZE * 4, 0.0f);
      k.fHeightVar = k.fSlopeVarX = k.fSlopeVarY = 0;
      const double dk = 2 * PI / TILE[c], dk2 = dk * dk;
      for (int y = 0; y < SIZE; y++)
        for (int x = 0; x < SIZE; x++)
        {
          double kx, ky;
          WaveVector(c, x, y, kx, ky);
          const double fK = std::sqrt(kx * kx + ky * ky);
          // The Nyquist row and column (k and -k the same texel) are left out; CascadeOf leaves them anyway.
          if (x == SIZE / 2 || y == SIZE / 2 || CascadeOf(fK) != c)
            continue;
          const double P = Spectrum(kx, ky, U) * dk2;
          double a, b;
          Gaussians(c, x, y, a, b);
          const double fAmp = std::sqrt(P) / 2;
          float *pOut = &k.H0[((size_t)y * SIZE + x) * 4];
          pOut[0] = (float)(a * fAmp);
          pOut[1] = (float)(b * fAmp);
          pOut[2] = (float)OmegaSteps(fK);
          k.fHeightVar += P;
          k.fSlopeVarX += kx * kx * P;
          k.fSlopeVarY += ky * ky * P;
        }
    }
  }
}

#endif
