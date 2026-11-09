// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrToneMapTests.cpp
//	Description: Unit tests of the ACES fit's soft toe (d3d12_ToneMapToe) and of the neutral tone map (d3d12_ToneMap 4,
//	Shaders/HdrToneMap.hlsli) on the GPU: the game's look below the knee,
//	the shoulder smooth at the knee, never decreasing and below its limit,
//	the hue kept, the light far above white white; the HDR output's tone
//	map: the SDR picture's up to white, the highlights' above (d3d12_HDRHighlights), the sky's glow towards
//	the sun (d3d12_HDRSkyAngle, HdrLight.hlsli) and the frame average limit (d3d12_HDRFrameAverage,
//	HdrFrameAverage.hlsli).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_ToneMap.h"
#include "Test_HdrOutput.h"
#include "Test_SkyGlow.h"
#include "Test_FrameAverage.h"
#include "Test_AcesToe.h"

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

  //! The HDR output's tone map of each light (CSHdrOutput): the output's display values with its knee, and the SDR
  //! picture's.
  struct SHdrOut
  {
    SRgb Hdr, Sdr;
    float fKnee;
  };
  std::vector<SHdrOut> HdrOutput(const std::vector<SRgb> &In, float k, float P, float fDesat, float fKnee)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_HdrOutput);
    d.Test.nTest[0] = (uint32)In.size();
    d.Test.vTest[0] = k;
    d.Test.vTest[1] = P;
    d.Test.vTest[2] = fDesat;
    d.Test.vTest[3] = fKnee;
    d.nGroups[0] = Groups64(In.size());
    for (const SRgb &c : In)
      Push(d.In, c.r, c.g, c.b, 0);
    d.nOut = 2 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SHdrOut> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ { At(Out, 2 * i, 0), At(Out, 2 * i, 1), At(Out, 2 * i, 2) }, { At(Out, 2 * i + 1, 0), At(Out, 2 * i + 1, 1), At(Out, 2 * i + 1, 2) },
                    At(Out, 2 * i, 3) });
    return R;
  }

  //! The display value of a peak of fNits over paper white (HdrOutput.cpp's vParams3.x: HdrEncode of the ratio).
  float PeakValue(double fNits, double fPaper) { return (float)Encode(fNits / fPaper); }

  //! Saturation as (max - min) / max.
  double Saturation(const SRgb &c) { return c.Max() > 0 ? (c.Max() - c.Min()) / c.Max() : 0; }
}

//! The ACES fit's soft toe (AcesCurve, d3d12_ToneMapToe T): above T the fit's curve as it is (middle grey too); below it
//! not black (the fit is 0 below 0.00325) but from 0 with the chord's slope r(T) / T, linear there, never decreasing, into
//! the curve at T with its value and slope. Why: the fit put all of a night scene's light under 0.00325 to black (Training's
//! opening, 3 to 5 times darker than the original's); a step or a kink at T would show as a band across a dark gradient.
XR_TEST(HdrToneMap_AcesToe)
{
  auto R = [](double v) { return (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081); };
  const int n = 4096;
  for (float T : { 0.005f, 0.01f, 0.02f })
  {
    std::vector<float> V;
    for (int i = 0; i < n; i++)
      V.push_back(i < n / 2 ? 2 * T * i / (n / 2) : 2 * T + (4 - 2 * T) * (i - n / 2) / (n / 2.0f));  // dense to 2 T, then to 4
    V.push_back(0.18f);
    const float h = T * 1e-3f;
    V.push_back(T - h);
    V.push_back(T);
    V.push_back(T * 1e-3f);
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_AcesToe);
    d.Test.nTest[0] = (uint32)V.size();
    d.Test.vTest[0] = T;
    d.nGroups[0] = Groups64(V.size());
    for (float v : V)
      Push(d.In, v, v, v, 0);
    d.nOut = V.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    const std::string What = "T " + std::to_string(T);
    float fLast = -1;
    for (int i = 0; i < n; i++)
    {
      const float v = V[i], g = At(Out, i, 0);
      CHECK_MSG(At(Out, i, 1) == g && At(Out, i, 2) == g, What + ": the channels apart");
      CHECK_MSG(g >= fLast - 1e-7f, What + ": down at " + std::to_string(v));
      fLast = g;
      if (v == 0)
        CHECK_MSG(g == 0, What + ": not black at 0");
      else
        CHECK_MSG(g > 0, What + ": black at " + std::to_string(v));
      if (v >= T)
        CHECK_MSG(std::fabs(g - R(v)) <= 1e-6 + 1e-5 * R(v), What + ": off the curve at " + std::to_string(v));
    }
    const size_t m = n;
    CHECK_NEAR(At(Out, m, 0), R(0.18), 1e-5);                      // middle grey as the fit
    const double rT = R(T), dT = (R(T + 1e-6) - R(T - 1e-6)) / 2e-6;
    CHECK_NEAR(At(Out, m + 2, 0), rT, 1e-6 + 1e-5 * rT);           // the curve's value at T
    const double fSlope = (At(Out, m + 2, 0) - At(Out, m + 1, 0)) / h;
    CHECK_MSG(std::fabs(fSlope / dT - 1) <= 0.03, What + ": the slope at T " + std::to_string(fSlope) + " of " + std::to_string(dT));
    const double fNear = At(Out, m + 3, 0) / (T * 1e-3);
    CHECK_MSG(std::fabs(fNear / (rT / T) - 1) <= 0.01, What + ": the slope at 0 " + std::to_string(fNear) + " of " + std::to_string(rT / T));
  }
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

//! The HDR output's tone map (HdrOutputToneMap, d3d12_HDRHighlights 2, the default), for a peak of 455 nits over 240, 1000
//! over 203 and 300 over 240: every colour the game shows at white or below (its brightest display value at most 1) comes
//! out as in the SDR picture (the layer's ratio 1: at paper white at most, also where the exposure put it in the shoulder);
//! from twice white up the neutral tone map to the peak; the grey's curve between smooth (no step in its slope), never
//! decreasing, never below the SDR picture, the peak far above; d3d12_HDRHighlights 0 the neutral tone map to the peak
//! throughout (as up to step 257). Why: with the tone map to the peak from the knee (0.85), a sky the exposure lifted to 1.2
//! times white went to 1.1 to 1.2 times paper white in HDR10 (the sky between the huts of Training, the sun's view).
XR_TEST(HdrToneMap_Output)
{
  const float k = 0.85f, fFull = 2;
  const float Peaks[] = { PeakValue(455, 240), PeakValue(1000, 203), PeakValue(300, 240) };
  std::vector<SRgb> Grid, Disp;
  for (int r = 0; r <= 20; r++)
    for (int g = 0; g <= 20; g++)
      for (int b = 0; b <= 20; b++)
      {
        Disp.push_back({ r / 20.0f, g / 20.0f, b / 20.0f });  // up to 1
        Grid.push_back(Light(r / 20.0, g / 20.0, b / 20.0));
      }
  const double h = 1.0 / 1024;
  const size_t nSweep = 4 * 1024;
  std::vector<SRgb> Grey;
  std::vector<double> M;
  for (size_t i = 0; i <= nSweep; i++)
  {
    M.push_back(i * h);
    const float x = (float)Decode(i * h);
    Grey.push_back({ x, x, x });
  }
  for (float x : { 1e3f, 1e5f })
  {
    M.push_back(Encode(x));
    Grey.push_back({ x, x, x });
  }
  const double fFullValue = Encode(fFull);
  for (float P : Peaks)
  {
    const std::vector<SHdrOut> Out = HdrOutput(Grid, k, P, 3.0f, fFull);
    for (size_t i = 0; i < Grid.size(); i++)
    {
      CHECK_NEAR(Out[i].Hdr.r, Out[i].Sdr.r, 1e-6);
      CHECK_NEAR(Out[i].Hdr.g, Out[i].Sdr.g, 1e-6);
      CHECK_NEAR(Out[i].Hdr.b, Out[i].Sdr.b, 1e-6);
      CHECK(Out[i].Hdr.Max() <= 1 + 1e-6f);
    }
    const std::vector<SHdrOut> G = HdrOutput(Grey, k, P, 3.0f, fFull);
    const double d = P - k;
    for (size_t i = 0; i < Grey.size(); i++)
    {
      const double m = M[i];
      if (m >= fFullValue)
      {
        const double fWant = k + d * (1 - std::exp(-(m - k) / d));
        CHECK_NEAR(G[i].Hdr.r, fWant, 2e-4 + 1e-5 * fWant);
      }
      CHECK(G[i].Hdr.r <= P + 1e-5f);
      CHECK(G[i].Hdr.r >= G[i].Sdr.r - 1e-5f);
      if (i > 0 && i <= nSweep)
        CHECK(G[i].Hdr.r >= G[i - 1].Hdr.r - 1e-5f);
      // The slope from a step to the next changes little: no kink where the highlights take over (at 1 and at 2 x white).
      if (i > 1 && i <= nSweep)
        CHECK(std::fabs((G[i].Hdr.r - G[i - 1].Hdr.r) - (G[i - 1].Hdr.r - G[i - 2].Hdr.r)) / h <= 0.02);
    }
    CHECK(G[Grey.size() - 1].Hdr.r >= P - 1e-4f);
    // d3d12_HDRHighlights 0: the neutral tone map to the peak from the knee.
    const std::vector<SHdrOut> Old = HdrOutput(Grey, k, P, 3.0f, 0);
    for (size_t i = 0; i < Grey.size(); i++)
    {
      const double m = M[i], fWant = m <= k ? m : k + d * (1 - std::exp(-(m - k) / d));
      CHECK_NEAR(Old[i].Hdr.r, fWant, 2e-4 + 1e-5 * fWant);
    }
  }
}

//! The sky's glow towards the sun (HdrSkyGlow, d3d12_HDRSky 2): its light d3d12_HDRSky times the game's at the sun, half way
//! between at d3d12_HDRSkyAngle (the cosine's power ln 0.5 / ln cos of it, as Hdr.cpp gives it), next to nothing at three
//! times the angle, none behind the camera's half of the sky, never brighter further from the sun; with d3d12_HDRSky 1 none.
//! Why: the lobe of up to step 257 (32, half way at 12 degrees) brightened a quarter of the sky towards the sun, white in SDR
//! and above paper white in HDR10.
XR_TEST(HdrSkyGlow_Lobe)
{
  for (double fAngle : { 2.0, 4.0, 11.89 })
  {
    const double a = fAngle * 3.14159265358979323846 / 180, fPower = std::log(0.5) / std::log(std::cos(a));
    std::vector<double> Angles;
    for (int i = 0; i <= 120; i++)
      Angles.push_back(a * i / 40.0);  // the sun to three times the angle
    Angles.push_back(1.2);
    Angles.push_back(2.5);
    for (float fGlow : { 2.0f, 1.0f })
    {
      SDispatch d;
      d.Shader = XR_BYTECODE(Test_SkyGlow);
      d.Test.nTest[0] = (uint32)Angles.size();
      d.nGroups[0] = Groups64(Angles.size());
      for (double t : Angles)
        Push(d.In, (float)std::cos(t), fGlow, (float)fPower, 0);
      d.nOut = Angles.size();
      const std::vector<uint32> Out = Gpu().Run(d);
      std::vector<double> L;
      for (size_t i = 0; i < Angles.size(); i++)
        L.push_back(std::pow(At(Out, i, 0), 2.2));  // the factor of the light
      CHECK_NEAR(L[0], fGlow, 1e-4);
      CHECK_NEAR(L[40], 1 + (fGlow - 1) / 2, 2e-3);
      CHECK(L[120] - 1 <= (fGlow - 1) * 0.01 + 1e-5);
      CHECK_NEAR(L[Angles.size() - 1], 1, 1e-6);
      for (size_t i = 1; i < Angles.size(); i++)
        CHECK(L[i] <= L[i - 1] + 1e-6);
    }
  }
}

//! The HDR output's frame average limit (FrameAverageFactor, HdrFrameAverage.hlsli; d3d12_HDRFrameAverage): where a frame is
//! above the limit, the factor of its highlights brings its average (the SDR picture's plus the highlights' times the factor)
//! to the limit exactly; 1 where it is within it or has no highlights; 0 where the SDR picture alone is at the limit or above
//! (the limit takes the highlights only); never larger with more highlights or a brighter SDR picture. Its steps
//! (FrameAverageStep): down by fDown at most, up by fUp at most, to the target exactly, so that a frame back within the limit
//! has the factor 1 again (its frames as without the limit); at once with no frame before. Frame by frame: from 1 to a target
//! of 0.3 in 4 frames of 0.2, back to 1 in 35 of 0.02 (36 with the rounding), then 1 exactly.
//! Why: a frame above the display's full frame light makes an OLED dim the whole picture by itself (ABL).
XR_TEST(HdrFrameAverage_Limit)
{
  struct SCase
  {
    float fSdr, fHighlights, fLimit;
  };
  std::vector<SCase> Cases;
  for (float fLimit : { 254 / 240.0f, 1.0f, 0.5f, 1.25f })
  {
    for (float fSdr : { 0.0f, 0.2f, 0.5f, 0.9f, 1.0f, 1.2f })
    {
      for (float fHighlights : { 0.0f, 0.01f, 0.1f, 0.5f, 1.0f, 3.0f })
        Cases.push_back({ fSdr, fHighlights, fLimit });
    }
  }
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_FrameAverage);
  d.Test.nTest[0] = (uint32)Cases.size();
  d.nGroups[0] = Groups64(Cases.size());
  for (const SCase &c : Cases)
    Push(d.In, c.fSdr, c.fHighlights, c.fLimit, 0);
  d.nOut = Cases.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SCase &c = Cases[i];
    const float f = At(Out, i, 0);
    CHECK(f >= 0 && f <= 1);
    if (c.fHighlights <= 0 || c.fSdr + c.fHighlights <= c.fLimit)
      CHECK(f == 1.0f);
    else if (c.fSdr >= c.fLimit)
      CHECK(f == 0.0f);
    else
      CHECK_NEAR(c.fSdr + f * c.fHighlights, c.fLimit, 1e-5);
    // More highlights (the next case), a brighter SDR picture (six cases on): never a larger factor.
    if (i % 6 != 5)
      CHECK(At(Out, i + 1, 0) <= f + 1e-7f);
    if ((i / 6) % 6 != 5)
      CHECK(At(Out, i + 6, 0) <= f + 1e-7f);
  }

  // The steps, one frame at a time.
  const auto Step = [](float fLast, float fTarget, float fDown, float fUp, bool bReset)
  {
    SDispatch s;
    s.Shader = XR_BYTECODE(Test_FrameAverage);
    s.Test.nTest[0] = 1;
    s.Test.nTest[1] = 1;
    s.Test.nTest[2] = bReset ? 1 : 0;
    s.nGroups[0] = 1;
    Push(s.In, fLast, fTarget, fDown, fUp);
    s.nOut = 1;
    return At(Gpu().Run(s), 0, 0);
  };
  CHECK(Step(1, 0.3f, 0.2f, 0.02f, true) == 0.3f);
  CHECK(Step(0.3f, 1, 0.2f, 0.02f, true) == 1.0f);
  CHECK(Step(0.5f, 0.5f, 0.2f, 0.02f, false) == 0.5f);
  CHECK(Step(0.99f, 1, 0.2f, 0.02f, false) == 1.0f);
  CHECK(Step(0.35f, 0.3f, 0.2f, 0.02f, false) == 0.3f);
  float f = 1;
  int nDown = 0, nUp = 0;
  while (f > 0.3f && nDown < 100)
  {
    const float fNext = Step(f, 0.3f, 0.2f, 0.02f, false);
    CHECK(fNext < f && fNext >= f - 0.2f - 1e-6f);
    f = fNext;
    nDown++;
  }
  CHECK(nDown == 4 && f == 0.3f);
  while (f < 1 && nUp < 100)
  {
    const float fNext = Step(f, 1, 0.2f, 0.02f, false);
    CHECK(fNext > f && fNext <= f + 0.02f + 1e-6f);
    f = fNext;
    nUp++;
  }
  CHECK(nUp >= 35 && nUp <= 36 && f == 1.0f);
}
