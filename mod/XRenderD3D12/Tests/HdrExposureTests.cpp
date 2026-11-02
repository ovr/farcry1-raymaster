// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrExposureTests.cpp
//	Description: Unit tests of the automatic exposure's target from its
//	histogram (d3d12_ExposureMeter 1, Shaders/HdrExposure.hlsli) on the
//	GPU: its percentiles and window mean as the CPU's (HdrStatsMath.h), the
//	window leaving out a black border and the sun, the sky's ceiling: the
//	sky's bright part at the knee when the sky is a big part of the frame,
//	nothing when it is a window's worth, only ever lower.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Shaders/Constants.h"
#include "HdrStatsMath.h"
#include "Test_Exposure.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  //! A case: the luminance's bins (centre weighted) and the sky's.
  struct SHist
  {
    std::vector<uint32> Luma = std::vector<uint32>(XR_HDR_HIST_BINS), Sky = std::vector<uint32>(XR_HDR_HIST_BINS);
  };

  //! What the kernel gives for a case.
  struct SResult
  {
    float fTarget, fMean, fCeiling, fShare;      //!< ExposureTarget
    float fPercentile, fWindow, fSkyPercentile;  //!< the luminance's percentile, its window mean, the sky's percentile
  };

  //! The parameters of a run (the renderer's defaults; d3d12_ToneMapKnee 0.85).
  struct SParams
  {
    float fKey = -2.6f, fLow = 0.05f, fHigh = 0.95f, fHighlights = 0.9f;
    float fKneeLog2 = 2.2f * std::log2(0.85f), fFrom = XR_EXPOSURE_SKY_FROM, fFull = XR_EXPOSURE_SKY_FULL;
    float fPercentile = 0.5f;
    uint32 nSamples = 10000;
  };

  std::vector<SResult> Run(const std::vector<SHist> &Cases, const SParams &p)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_Exposure);
    d.Test.nTest[0] = (uint32)Cases.size();
    d.Test.nTest[1] = p.nSamples;
    d.Test.vTest[0] = p.fKey;
    d.Test.vTest[1] = p.fLow;
    d.Test.vTest[2] = p.fHigh;
    d.Test.vTest[3] = p.fHighlights;
    d.Test.vTest2[0] = p.fKneeLog2;
    d.Test.vTest2[1] = p.fFrom;
    d.Test.vTest2[2] = p.fFull;
    d.Test.vTest2[3] = p.fPercentile;
    d.nGroups[0] = Groups64(Cases.size());
    for (const SHist &h : Cases)
    {
      d.In.insert(d.In.end(), h.Luma.begin(), h.Luma.end());
      d.In.insert(d.In.end(), h.Sky.begin(), h.Sky.end());
    }
    d.nOut = 2 * Cases.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SResult> R;
    for (size_t i = 0; i < Cases.size(); i++)
      R.push_back({ At(Out, 2 * i, 0), At(Out, 2 * i, 1), At(Out, 2 * i, 2), At(Out, 2 * i, 3), At(Out, 2 * i + 1, 0), At(Out, 2 * i + 1, 1),
                    At(Out, 2 * i + 1, 2) });
    return R;
  }

  //! The bin of log2 of a light.
  int BinOf(double fLog2)
  {
    return (int)std::clamp(std::floor(HdrHistBinOf((float)fLog2)), 0.0f, (float)(XR_HDR_HIST_BINS - 1));
  }

  //! The mean of log2 between the shares a and b of a histogram, each bin's samples spread evenly over it (on the CPU, in
  //! doubles, sample by sample in thousandths of a bin).
  double WindowMean(const std::vector<uint32> &Bins, double a, double b)
  {
    const double fTotal = HdrHistTotal(Bins.data());
    double fSum = 0, fMean = 0, fWeight = 0;
    for (int i = 0; i < XR_HDR_HIST_BINS; i++)
    {
      for (int k = 0; k < 1000 && Bins[i]; k++)
      {
        // The k-th thousandth of the bin: its samples' shares and log2.
        const double lo = (fSum + Bins[i] * k / 1000.0) / fTotal, hi = (fSum + Bins[i] * (k + 1) / 1000.0) / fTotal;
        const double w = std::max(std::min(hi, b) - std::max(lo, a), 0.0);
        fMean += w * HdrHistLog2(i + (k + 0.5f) / 1000);
        fWeight += w;
      }
      fSum += Bins[i];
    }
    return fWeight > 0 ? fMean / fWeight : HdrHistPercentile(Bins.data(), a);
  }

  //! A small random generator (the same numbers on every run).
  uint32 Next(uint32 &nSeed)
  {
    nSeed = nSeed * 1664525u + 1013904223u;
    return nSeed >> 8;
  }
}

//! The kernel's percentiles of a histogram are the CPU's (HdrHistPercentile, which the panel and the log use), its window mean
//! that of the samples between the two shares (each bin's spread evenly over it), the whole window's the plain mean
//! (HdrHistMeanLog2), a window of one share that percentile; on random histograms, sparse ones, one bin, the first and the
//! last. Why: the panel shows what the GPU steered by; a mean off by a bin is a third of an EV.
XR_TEST(HdrExposure_Percentiles)
{
  std::vector<SHist> Cases;
  uint32 nSeed = 257;
  for (int c = 0; c < 24; c++)
  {
    SHist h;
    const int nFirst = c % 8 == 0 ? 0 : (int)(Next(nSeed) % 40), nCount = c % 5 == 0 ? 1 : 1 + (int)(Next(nSeed) % 24);
    for (int i = nFirst; i < std::min(nFirst + nCount, XR_HDR_HIST_BINS); i++)
      h.Luma[i] = c % 3 == 0 && i % 2 && i != nFirst ? 0 : 1 + Next(nSeed) % 200000;
    if (c == 23)
    {
      std::fill(h.Luma.begin(), h.Luma.end(), 0);
      h.Luma[XR_HDR_HIST_BINS - 1] = 1000;
    }
    Cases.push_back(h);
  }
  for (float fShare : { 0.0f, 0.05f, 0.5f, 0.9f, 0.95f, 1.0f })
  {
    SParams p;
    p.fPercentile = fShare;
    const std::vector<SResult> R = Run(Cases, p);
    for (size_t c = 0; c < Cases.size(); c++)
      CHECK_NEAR(R[c].fPercentile, HdrHistPercentile(Cases[c].Luma.data(), fShare), 2e-3);
  }
  for (const auto &Window : { std::make_pair(0.0f, 1.0f), std::make_pair(0.05f, 0.95f), std::make_pair(0.5f, 0.98f), std::make_pair(0.3f, 0.3f) })
  {
    SParams p;
    p.fLow = Window.first;
    p.fHigh = Window.second;
    const std::vector<SResult> R = Run(Cases, p);
    for (size_t c = 0; c < Cases.size(); c++)
    {
      const std::vector<uint32> &b = Cases[c].Luma;
      CHECK_NEAR(R[c].fWindow, WindowMean(b, p.fLow, p.fHigh), 2e-3);
      if (p.fLow == 0 && p.fHigh == 1)
        CHECK_NEAR(R[c].fWindow, HdrHistMeanLog2(b.data()), 2e-3);
      if (p.fLow == p.fHigh)
        CHECK_NEAR(R[c].fWindow, HdrHistPercentile(b.data(), p.fLow), 2e-3);
      // No sky: the target is the key over the window's mean, no ceiling.
      CHECK_NEAR(R[c].fTarget, p.fKey - R[c].fWindow, 1e-4);
      CHECK_NEAR(R[c].fMean, R[c].fWindow, 1e-5);
      CHECK(R[c].fCeiling == XR_EXPOSURE_NONE);
      CHECK(R[c].fShare == 0);
    }
  }
}

//! The window (5 to 95 %): a black border of 4 % of the weight and the sun's 2 % move the target by less than 0.03 EV, where
//! they move the plain mean's by more than 0.3 and 0.15 EV. Why: the old meter's mean took every pixel; black corners (a letterbox, a
//! wall in shadow at the edge) made the whole frame brighter, the sun darker.
XR_TEST(HdrExposure_Window)
{
  const int nMid = BinOf(-2.4);
  SHist Base, Dark, Sun, Both;
  Base.Luma[nMid] = 100000;
  Dark = Base;
  Dark.Luma[0] = 4167;                       // 4 % of the whole
  Sun = Base;
  Sun.Luma[XR_HDR_HIST_BINS - 2] = 2041;     // 2 %
  Both = Dark;
  Both.Luma[XR_HDR_HIST_BINS - 2] = 2128;
  SParams p;
  const std::vector<SResult> R = Run({ Base, Dark, Sun, Both }, p);
  for (int i = 1; i < 4; i++)
    CHECK(std::fabs(R[i].fTarget - R[0].fTarget) < 0.03f);
  SParams All;
  All.fLow = 0;
  All.fHigh = 1;
  const std::vector<SResult> A = Run({ Base, Dark, Sun }, All);
  CHECK(A[1].fTarget - A[0].fTarget > 0.3f);
  CHECK(A[0].fTarget - A[2].fTarget > 0.15f);
}

//! The sky's ceiling: a dark forest (log2 -5.5, the target far above d3d12_ExposureMax) with a dim sky between the crowns
//! (log2 -0.9): with the sky at 8 % of the samples or more the target is the EV that puts the sky's 90th percentile at the
//! knee; below 2 % (a window's worth) the key's target alone; in between it goes from one to the other and never up as the
//! sky grows. The ceiling only ever lowers the target (a dark sky leaves it), d3d12_ExposureHighlights 0 turns it off, a
//! dark room with 2.5 % of bright sky through a window still gets d3d12_ExposureMax (1.5), and an empty histogram EV 0.
//! Why: River's forest got +1.5 EV and its sky 2.4 times the game's light, white; a hut's window must not stop the hut
//! from being made brighter.
XR_TEST(HdrExposure_Sky)
{
  SParams p;
  const int nForest = BinOf(-5.5), nSky = BinOf(-0.9), nDarkSky = BinOf(-8.9), nRoom = BinOf(-7.3), nWindow = BinOf(-0.1);
  // The sky's 90th percentile (all of it in one bin) and the EV that puts it at the knee.
  const float fCeiling = p.fKneeLog2 - HdrHistLog2(nSky + 0.9f);
  std::vector<SHist> Cases;
  const std::vector<uint32> Shares = { 0, 100, 200, 300, 400, 500, 600, 700, 800, 1600 };   // of p.nSamples (10000)
  for (uint32 nSkySamples : Shares)
  {
    SHist h;
    h.Luma[nForest] = 2560000;
    h.Sky[nSky] = nSkySamples;
    Cases.push_back(h);
  }
  const std::vector<SResult> R = Run(Cases, p);
  const float fForest = R[0].fTarget;
  CHECK(fForest > 2.5f);
  CHECK(R[0].fCeiling == XR_EXPOSURE_NONE);
  for (size_t i = 1; i < Cases.size(); i++)
  {
    CHECK_NEAR(R[i].fShare, Shares[i] / 10000.0, 1e-6);
    CHECK_NEAR(R[i].fCeiling, fCeiling, 1e-3);
    CHECK_NEAR(R[i].fSkyPercentile, HdrHistLog2(nSky + 0.9f), 1e-3);
    CHECK(R[i].fTarget <= R[i - 1].fTarget + 1e-5f);
  }
  CHECK_NEAR(R[1].fTarget, fForest, 1e-4);     // 1 %
  CHECK_NEAR(R[2].fTarget, fForest, 1e-4);     // 2 %
  CHECK(R[5].fTarget < fForest - 0.1f && R[5].fTarget > fCeiling + 0.1f);
  CHECK_NEAR(R[8].fTarget, fCeiling, 1e-4);    // 8 %
  CHECK_NEAR(R[9].fTarget, fCeiling, 1e-4);    // 16 %

  // Only ever lower: a sky darker than the knee needs leaves the target; off: no ceiling.
  SHist DarkSky;
  DarkSky.Luma[nForest] = 2560000;
  DarkSky.Sky[nDarkSky] = 3000;
  SHist Big = Cases.back();
  SParams Off = p;
  Off.fHighlights = 0;
  CHECK_NEAR(Run({ DarkSky }, p)[0].fTarget, fForest, 1e-4);
  const SResult o = Run({ Big }, Off)[0];
  CHECK_NEAR(o.fTarget, fForest, 1e-4);
  CHECK(o.fCeiling == XR_EXPOSURE_NONE);
  CHECK_NEAR(o.fShare, 0.16, 1e-6);

  // A dark room with a bright window: still the most EV; nothing in the histogram: EV 0.
  SHist Room, Empty;
  Room.Luma[nRoom] = 2560000;
  Room.Sky[nWindow] = 250;
  const std::vector<SResult> E = Run({ Room, Empty }, p);
  CHECK(E[0].fCeiling < 0.0f);
  CHECK(std::clamp(E[0].fTarget, 0.0f, 1.5f) == 1.5f);
  CHECK(E[1].fTarget == 0.0f);
  CHECK(E[1].fCeiling == XR_EXPOSURE_NONE);
}
