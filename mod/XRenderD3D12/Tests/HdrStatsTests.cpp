// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrStatsTests.cpp
//	Description: Unit tests of the HDR statistics (d3d12_DisplayHDR,
//	d3d12_HDRLog): the histogram's bins and the meter's weight on the GPU
//	(Shaders/HdrShared.hlsli), the highlight layer's ratio as the output
//	pass and the statistics take it (Shaders/HdrLight.hlsli), and the CPU's
//	percentiles of a histogram (HdrStatsMath.h).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Shaders/Constants.h"
#include "HdrStatsMath.h"
#include "Test_HdrStats.h"

#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  //! The bin of log2 of a light as the CPU sees it, clamped to the histogram.
  int BinOf(double fLog2)
  {
    return (int)std::clamp(std::floor(HdrHistBinOf((float)fLog2)), 0.0f, (float)(XR_HDR_HIST_BINS - 1));
  }

  //! The fingerprint of a colour as the output pass computes it (HighlightFingerprint).
  float Fingerprint(float r, float g, float b)
  {
    const auto Byte = [](float c) { return std::round(std::clamp(c, 0.0f, 1.0f) * 255); };
    return (0.3f * Byte(r) + 0.45f * Byte(g) + 0.25f * Byte(b)) / 255;
  }
}

//! HdrHistogramBin: the light at the middle of each bin falls in it, the light of each bin's edges in it or its neighbour (the
//! GPU's log2 is not exact), nothing (0, negative) and the darkest in the first, the brightest in the last; and it agrees with
//! the CPU's HdrHistLog2 / HdrHistBinOf, which the panel and the log place things by. MeterWeight: 1 in the middle, the
//! meter's exp(-0.75 d^2) by the distance in half heights elsewhere. Why: a bin off by one moves the percentiles the
//! exposure will be steered by (step 256) by a third of an EV.
XR_TEST(HdrStats_HistogramBin)
{
  std::vector<float> Light;
  std::vector<int> Want;
  std::vector<bool> bEdge;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
  {
    Light.push_back(std::exp2(HdrHistLog2(i + 0.5f)));
    Want.push_back(i);
    bEdge.push_back(false);
    Light.push_back(std::exp2(HdrHistLog2((float)i)));
    Want.push_back(i);
    bEdge.push_back(true);
  }
  for (float x : { 0.0f, -1.0f, 1e-30f, 1e-6f })
  {
    Light.push_back(x);
    Want.push_back(0);
    bEdge.push_back(false);
  }
  for (float x : { 256.0f, 1000.0f, 65000.0f })
  {
    Light.push_back(x);
    Want.push_back(XR_HDR_HIST_BINS - 1);
    bEdge.push_back(x == 256.0f);
  }
  // The meter's weight at the middle, a corner and half way to the right edge (1920 x 1080: 16 / 9 half heights across).
  const float UV[3][2] = { { 0.5f, 0.5f }, { 0, 0 }, { 0.75f, 0.5f } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_HdrStats);
  d.Test.nTest[0] = (uint32)Light.size();
  d.Test.nTest[1] = 0;
  d.nGroups[0] = Groups64(Light.size());
  for (size_t i = 0; i < Light.size(); i++)
    Push(d.In, Light[i], UV[i % 3][0], UV[i % 3][1], 0);
  d.nOut = Light.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < Light.size(); i++)
  {
    const int nBin = (int)Out[i * 4];
    if (bEdge[i])
      CHECK_MSG(nBin == Want[i] || nBin == Want[i] - 1, "light " + std::to_string(Light[i]) + ": bin " + std::to_string(nBin));
    else
      CHECK_MSG(nBin == Want[i], "light " + std::to_string(Light[i]) + ": bin " + std::to_string(nBin) + ", not " + std::to_string(Want[i]));
    if (Light[i] > 0 && !bEdge[i])
      CHECK(nBin == BinOf(std::log2((double)Light[i])));
    const double dx = (UV[i % 3][0] - 0.5) * 2 * 1920.0 / 1080.0, dy = (UV[i % 3][1] - 0.5) * 2;
    CHECK_NEAR(At(Out, i, 1), std::exp(-0.75 * (dx * dx + dy * dy)), 1e-5);
  }
}

//! LayerRatio: the layer's ratio (at least 1 a channel) where its fingerprint is the frame colour's, also with one channel a
//! step of 8 bits off (rounding); 1 where the colour is another (something drawn over the scene). Why: the statistics' nits
//! and the output pass take the same pixels as highlights, so that the panel's numbers are what the display shows.
XR_TEST(HdrStats_LayerRatio)
{
  struct SCase { float f[3]; float Layer[4]; float Ratio[3]; bool bScene; };
  const float r = 0.8f, g = 0.6f, b = 0.3f, fp = Fingerprint(r, g, b);
  const SCase Cases[] = {
    { { r, g, b }, { 2.0f, 1.5f, 0.5f, fp }, { 2.0f, 1.5f, 1.0f }, true },                    // the scene: the ratio, never below 1
    { { r, g + 1.0f / 255, b }, { 3.0f, 3.0f, 3.0f, fp }, { 3.0f, 3.0f, 3.0f }, true },       // green a step off: still the scene
    { { 0.2f, 0.2f, 0.9f }, { 3.0f, 3.0f, 3.0f, fp }, { 1.0f, 1.0f, 1.0f }, false },          // drawn over: 1
    { { r, g, b }, { 1.0f, 1.0f, 1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }, false },                // the layer as a frame starts: 1
  };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_HdrStats);
  d.Test.nTest[0] = (uint32)std::size(Cases);
  d.Test.nTest[1] = 1;
  d.nGroups[0] = 1;
  for (const SCase &c : Cases)
  {
    Push(d.In, c.f[0], c.f[1], c.f[2], 0);
    Push(d.In, c.Layer[0], c.Layer[1], c.Layer[2], c.Layer[3]);
  }
  d.nOut = std::size(Cases);
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < std::size(Cases); i++)
  {
    for (int k = 0; k < 3; k++)
      CHECK_NEAR(At(Out, i, k), Cases[i].Ratio[k], 1e-6);
    CHECK((At(Out, i, 3) > 0.5f) == Cases[i].bScene);
  }
}

//! HdrHistPercentile, HdrHistShareAbove, HdrHistMeanLog2 (HdrStatsMath.h), on the CPU: an empty histogram gives the bottom; one
//! bin spreads its samples evenly over itself; a flat histogram is linear in log2; the share above a percentile's light is
//! the rest; the mean of one bin is its middle. Why: the panel's and the log's percentiles, and later the exposure's
//! window of percentiles (step 256), come from these.
XR_TEST(HdrStats_Percentiles)
{
  uint32 Bins[XR_HDR_HIST_BINS] = {};
  CHECK_NEAR(HdrHistPercentile(Bins, 0.5), XR_HDR_HIST_MIN, 1e-6);
  CHECK_NEAR(HdrHistShareAbove(Bins, 0), 0, 1e-12);
  const double fBinEV = (double)(XR_HDR_HIST_MAX - XR_HDR_HIST_MIN) / XR_HDR_HIST_BINS;
  // One bin.
  Bins[40] = 1000;
  CHECK_NEAR(HdrHistPercentile(Bins, 0.0), HdrHistLog2(40), 1e-5);
  CHECK_NEAR(HdrHistPercentile(Bins, 0.5), HdrHistLog2(40.5f), 1e-5);
  CHECK_NEAR(HdrHistPercentile(Bins, 1.0), HdrHistLog2(41), 1e-5);
  CHECK_NEAR(HdrHistMeanLog2(Bins), HdrHistLog2(40.5f), 1e-5);
  CHECK_NEAR(HdrHistShareAbove(Bins, HdrHistLog2(40.25f)), 0.75, 1e-6);
  CHECK_NEAR(HdrHistShareAbove(Bins, HdrHistLog2(39)), 1.0, 1e-12);
  CHECK_NEAR(HdrHistShareAbove(Bins, HdrHistLog2(41)), 0.0, 1e-12);
  // Flat over all of them.
  for (uint32 &n : Bins)
    n = 100;
  for (double p : { 0.01, 0.1, 0.25, 0.5, 0.9, 0.99 })
  {
    const float x = HdrHistPercentile(Bins, p);
    CHECK_NEAR(x, XR_HDR_HIST_MIN + p * (XR_HDR_HIST_MAX - XR_HDR_HIST_MIN), 1e-4);
    CHECK_NEAR(HdrHistShareAbove(Bins, x), 1 - p, 1e-6);
  }
  CHECK_NEAR(HdrHistMeanLog2(Bins), 0.5 * (XR_HDR_HIST_MIN + XR_HDR_HIST_MAX), 1e-5);
  // Two bins far apart: the median between them is at the top of the lower one.
  for (uint32 &n : Bins)
    n = 0;
  Bins[10] = 500;
  Bins[50] = 500;
  CHECK_NEAR(HdrHistPercentile(Bins, 0.5), HdrHistLog2(11), 1e-5);
  CHECK_NEAR(HdrHistPercentile(Bins, 0.75), HdrHistLog2(50.5f), 1e-5);
  CHECK_NEAR(HdrHistLog2(1) - HdrHistLog2(0), fBinEV, 1e-6);
}
