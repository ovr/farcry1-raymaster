// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrToneMapTests.cpp
//	Description: Unit tests of the neutral tone map (d3d12_ToneMap 4,
//	Shaders/HdrToneMap.hlsli) on the GPU: the game's look below the knee,
//	the shoulder smooth at the knee, never decreasing and below its limit,
//	the hue kept, the light far above white white.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_ToneMap.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  struct SRgb
  {
    float r, g, b;
    float Max() const { return std::max(r, std::max(g, b)); }
    float Min() const { return std::min(r, std::min(g, b)); }
  };

  //! HdrEncode / HdrDecode (HdrLight.hlsli) on the CPU.
  double Encode(double x) { return x <= 0 ? 0 : x <= 1 ? std::pow(x, 1 / 2.2) : 1 + (x - 1) / 2.2; }
  double Decode(double c) { return c <= 0 ? 0 : c <= 1 ? std::pow(c, 2.2) : 1 + 2.2 * (c - 1); }

  //! The light of display values (each channel decoded).
  SRgb Light(double r, double g, double b) { return { (float)Decode(r), (float)Decode(g), (float)Decode(b) }; }

  //! NeutralToneMap of each light with the knee k, the limit P and the desaturation.
  std::vector<SRgb> ToneMap(const std::vector<SRgb> &In, float k, float P, float fDesat)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_ToneMap);
    d.Test.nTest[0] = (uint32)In.size();
    d.Test.vTest[0] = k;
    d.Test.vTest[1] = P;
    d.Test.vTest[2] = fDesat;
    d.nGroups[0] = Groups64(In.size());
    for (const SRgb &c : In)
      Push(d.In, c.r, c.g, c.b, 0);
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SRgb> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ At(Out, i, 0), At(Out, i, 1), At(Out, i, 2) });
    return R;
  }

  //! Saturation as (max - min) / max.
  double Saturation(const SRgb &c) { return c.Max() > 0 ? (c.Max() - c.Min()) / c.Max() : 0; }
}

//! Below the knee the game's look: every colour whose brightest channel is at most the knee comes out as it went in (its
//! display values), for the SDR picture (P 1) and the HDR output's layer (P 1.9, a 455 nit peak over 240). Why: the
//! tone map is to touch only what was above the knee; a colour cast or a contrast change below it changes the whole game.
XR_TEST(HdrToneMap_BelowKnee)
{
  const float k = 0.85f;
  std::vector<SRgb> In, Want;
  for (int r = 0; r <= 17; r++)
    for (int g = 0; g <= 17; g++)
      for (int b = 0; b <= 17; b++)
      {
        Want.push_back({ r / 20.0f, g / 20.0f, b / 20.0f });  // up to 0.85
        In.push_back(Light(r / 20.0, g / 20.0, b / 20.0));
      }
  for (float P : { 1.0f, 1.9f })
  {
    const std::vector<SRgb> Out = ToneMap(In, k, P, 0.3f);
    for (size_t i = 0; i < In.size(); i++)
    {
      CHECK_NEAR(Out[i].r, Want[i].r, 2e-4);
      CHECK_NEAR(Out[i].g, Want[i].g, 2e-4);
      CHECK_NEAR(Out[i].b, Want[i].b, 2e-4);
    }
  }
}

//! The shoulder of grey (no hue, so no desaturation): k + d (1 - e^(-(m - k) / d)), d = P - k, for the brightest display value m
//! above the knee; continuous at the knee with the slope 1 on both sides, never decreasing, below P and at P far above.
//! Why: a kink at the knee shows as a band across a bright sky; a shoulder that goes down or above P inverts or clips.
XR_TEST(HdrToneMap_Shoulder)
{
  const float k = 0.85f;
  const double h = 1.0 / 1024;
  const size_t nSweep = 20 * 1024;
  std::vector<SRgb> In;
  std::vector<double> M;
  for (size_t i = 0; i <= nSweep; i++)
  {
    const double m = i * h;
    M.push_back(m);
    const float x = (float)Decode(m);
    In.push_back({ x, x, x });
  }
  for (float x : { 1e3f, 1e5f })
  {
    M.push_back(Encode(x));
    In.push_back({ x, x, x });
  }
  for (float P : { 1.0f, 1.9f })
  {
    const std::vector<SRgb> Out = ToneMap(In, k, P, 0.3f);
    const double d = P - k;
    for (size_t i = 0; i < In.size(); i++)
    {
      const double m = M[i], fWant = m <= k ? m : k + d * (1 - std::exp(-(m - k) / d));
      CHECK_NEAR(Out[i].r, fWant, 2e-4 + 1e-5 * fWant);
      CHECK(Out[i].r == Out[i].g && Out[i].g == Out[i].b);
      CHECK(Out[i].r <= P + 1e-5f);
      if (i > 0 && i <= nSweep)
        CHECK(Out[i].r >= Out[i - 1].r - 1e-5f);
    }
    // The slope at the knee from either side (the knee 0.85 at 870.4 steps: the steps around it).
    const size_t n = (size_t)std::ceil(k / h);
    CHECK_NEAR((Out[n + 1].r - Out[n].r) / h, 1, 0.02);
    CHECK_NEAR((Out[n - 1].r - Out[n - 2].r) / h, 1, 0.02);
    CHECK(Out[In.size() - 1].r >= P - 1e-4f);
  }
}

//! The hue kept: without desaturation every colour above the knee comes out with its channels in the ratios its display
//! values had (a sky blue, a lamp's orange), whatever its brightness; with the default desaturation (3) a sky up to 1.2
//! above white keeps at least 0.9 of its saturation, the brighter the less saturated, an orange just above white keeps
//! its hue and three times white goes yellow (the game's clip of each channel), and the light far above white goes white
//! (each channel at least 0.98 P). Why: the per channel shoulder (d3d12_ToneMap 1) turned the bright blue sky cyan and
//! white; with the hue kept alone the sun was a peach disc and fire red around a white core.
XR_TEST(HdrToneMap_Hue)
{
  const float k = 0.85f;
  const SRgb Colours[] = { { 0.55f, 0.75f, 1.0f }, { 1.0f, 0.6f, 0.2f }, { 0.3f, 1.0f, 0.4f }, { 1.0f, 0.95f, 0.9f } };
  const size_t nSteps = 65;
  std::vector<SRgb> In, Disp;
  for (const SRgb &c : Colours)
    for (size_t s = 0; s < nSteps; s++)
    {
      const double fScale = 0.9 + s / 16.0;  // the brightest display value 0.9 .. 4.9
      Disp.push_back({ (float)(c.r * fScale), (float)(c.g * fScale), (float)(c.b * fScale) });
      In.push_back(Light(c.r * fScale, c.g * fScale, c.b * fScale));
    }
  for (float P : { 1.0f, 1.9f })
  {
    const std::vector<SRgb> Out = ToneMap(In, k, P, 0);
    for (size_t i = 0; i < In.size(); i++)
    {
      const float m = Disp[i].Max(), o = Out[i].Max();
      CHECK(o <= P + 1e-5f);
      CHECK_NEAR(Out[i].r / o, Disp[i].r / m, 1e-4);
      CHECK_NEAR(Out[i].g / o, Disp[i].g / m, 1e-4);
      CHECK_NEAR(Out[i].b / o, Disp[i].b / m, 1e-4);
    }
  }
  // The default desaturation, the SDR picture: the sky (the first colour) up to 1.2 keeps its saturation; each colour is
  // less saturated the brighter.
  const std::vector<SRgb> Out = ToneMap(In, k, 1, 3.0f);
  for (size_t s = 0; s < nSteps && Disp[s].Max() <= 1.2f; s++)
    CHECK(Saturation(Out[s]) >= 0.9 * Saturation(Disp[s]) - 1e-4);
  for (size_t c = 0; c < 4; c++)
    for (size_t s = 1; s < nSteps; s++)
      CHECK(Saturation(Out[c * nSteps + s]) <= Saturation(Out[c * nSteps + s - 1]) + 1e-4);
  std::vector<SRgb> Far;
  for (const SRgb &c : Colours)
    Far.push_back({ 1e3f * c.r, 1e3f * c.g, 1e3f * c.b });
  for (float P : { 1.0f, 1.9f })
    for (const SRgb &o : ToneMap(Far, k, P, 3.0f))
    {
      CHECK(o.Min() >= 0.98f * P);
      CHECK(o.Max() <= P + 1e-5f);
    }
  // Fire's orange (1, 0.5, 0.15): just above white (1) its hue, at three times white yellow (green at least 0.9 of red,
  // blue at most 0.6 of it).
  const std::vector<SRgb> Fire = ToneMap({ Light(1, 0.5, 0.15), Light(3, 1.5, 0.45) }, k, 1, 3.0f);
  CHECK_NEAR(Fire[0].g / Fire[0].r, 0.5, 0.02);
  CHECK_NEAR(Fire[0].b / Fire[0].r, 0.15, 0.02);
  CHECK(Fire[1].g >= 0.9f * Fire[1].r);
  CHECK(Fire[1].b <= 0.6f * Fire[1].r);
}
