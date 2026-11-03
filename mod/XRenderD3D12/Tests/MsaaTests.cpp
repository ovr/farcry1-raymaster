// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: MsaaTests.cpp
//	Description: Unit tests of MSAA's resolve (d3d12_AA msaa2, msaa4,
//	msaa8, Msaa.cpp) on the GPU: Shaders/Msaa.hlsli as Msaa.hlsl's passes
//	use it (TestMsaa.hlsl), on the samples of single pixels.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_Msaa.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  //! A sample: its colour (rgb the game's colour, gamma encoded; a the alpha) and depth.
  struct SSample
  {
    float r, g, b, a, fDepth;
  };

  //! A pixel resolved: its colour and depth.
  struct SResolved
  {
    float r, g, b, a, fDepth;
  };

  //! The resolve of each pixel of nSamples samples (Samples: nSamples a pixel, one pixel after the other); bHdr the HDR
  //! target's colours at the exposure fExposure.
  std::vector<SResolved> Resolve(const std::vector<SSample> &Samples, uint32 nSamples, bool bHdr, float fExposure = 1)
  {
    const size_t n = Samples.size() / nSamples;
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_Msaa);
    d.Test.nTest[0] = (uint32)n;
    d.Test.nTest[1] = nSamples;
    d.Test.nTest[2] = bHdr ? 1 : 0;
    d.Test.vTest[0] = fExposure;
    d.nGroups[0] = Groups64(n);
    for (const SSample &s : Samples)
    {
      Push(d.In, s.r, s.g, s.b, s.a);
      Push(d.In, s.fDepth, 0, 0, 0);
    }
    d.nOut = 2 * n;
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SResolved> R;
    for (size_t i = 0; i < n; i++)
      R.push_back({ At(Out, i, 0), At(Out, i, 1), At(Out, i, 2), At(Out, i, 3), At(Out, n + i, 0) });
    return R;
  }

  //! HdrDecode / HdrEncode (HdrLight.hlsli) on the CPU.
  double Decode(double c) { return c <= 0 ? 0 : c <= 1 ? std::pow(c, 2.2) : 1 + 2.2 * (c - 1); }
  double Encode(double x) { return x <= 0 ? 0 : x <= 1 ? std::pow(x, 1 / 2.2) : 1 + (x - 1) / 2.2; }
  //! The ACES curve's fit (MsaaToneMap).
  double ToneMap(double x) { return x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14); }
}

// Why: where all of a pixel's samples are of one surface (all but the edges), the resolve must leave its colour as it
// is, a lamp's or the sky's above white as well as a dark one, at every count, LDR and HDR, else MSAA changes the whole
// picture.
XR_TEST(Msaa_UniformSamples)
{
  const float Colours[][4] = { { 0, 0, 0, 0 }, { 0.02f, 0.05f, 0.1f, 1 }, { 0.5f, 0.4f, 0.3f, 0.5f }, { 1, 1, 1, 1 }, { 3, 2, 1.2f, 1 },
                               { 400, 300, 200, 0.25f } };
  for (int bHdr = 0; bHdr < 2; bHdr++)
    for (uint32 nSamples : { 2u, 4u, 8u })
    {
      std::vector<SSample> In;
      const size_t nColours = bHdr ? std::size(Colours) : 4;   // the back buffer's are 0 .. 1
      for (size_t i = 0; i < nColours; i++)
        for (uint32 s = 0; s < nSamples; s++)
          In.push_back({ Colours[i][0], Colours[i][1], Colours[i][2], Colours[i][3], 0.5f });
      for (float fExposure : { 1.0f, 2.8f })
      {
        const std::vector<SResolved> R = Resolve(In, nSamples, bHdr != 0, fExposure);
        for (size_t i = 0; i < R.size(); i++)
        {
          const float *c = Colours[i];
          CHECK_NEAR(R[i].r, c[0], 2e-4 + c[0] * 2e-4);
          CHECK_NEAR(R[i].g, c[1], 2e-4 + c[1] * 2e-4);
          CHECK_NEAR(R[i].b, c[2], 2e-4 + c[2] * 2e-4);
          CHECK_NEAR(R[i].a, c[3], 1e-6);
          CHECK_NEAR(R[i].fDepth, 0.5f, 1e-7);
        }
      }
    }
}

// Why: a pixel the edge covers in part must show the mean of what the display shows of its samples (the render tests'
// reference is that, the view drawn at twice the size): on an edge against the bright sky a plain mean of the colours
// lets the sky take the pixel, and a stronger curve than the tone map's darkens the edges. The HDR target's pixel tone
// mapped must be the mean of its samples' tone maps, stepping evenly from the dark to the sky as its covered samples do,
// at each exposure; the back buffer's, the mean of their light.
XR_TEST(Msaa_EdgeMean)
{
  for (const float fBright : { 1.4f, 3.0f, 20.0f })
    for (const float fExposure : { 1.0f, 2.8f })
      for (uint32 nSamples : { 2u, 4u, 8u })
      {
        const float fDark = 0.2f;
        std::vector<SSample> In;
        for (uint32 k = 0; k <= nSamples; k++)
          for (uint32 s = 0; s < nSamples; s++)
          {
            const float c = s < k ? fBright : fDark;
            In.push_back({ c, c, c, 1, 1 });
          }
        const std::vector<SResolved> R = Resolve(In, nSamples, true, fExposure);
        double fPrev = -1;
        for (uint32 k = 0; k <= nSamples; k++)
        {
          const double fWant = (k * ToneMap(fExposure * Decode(fBright)) + (nSamples - k) * ToneMap(fExposure * Decode(fDark))) / nSamples;
          const double fGot = ToneMap(fExposure * Decode(R[k].r));
          CHECK_NEAR(fGot, fWant, 2e-4);
          CHECK_NEAR(R[k].g, R[k].r, 1e-6);
          CHECK_NEAR(R[k].b, R[k].r, 1e-6);
          CHECK(fGot > fPrev);
          fPrev = fGot;
          // Half covered by a bright light: far below the plain mean of the light, nearer the dark side's colour.
          if (2 * k == nSamples && fBright >= 3)
            CHECK(R[k].r < (fBright + fDark) / 2);
        }
      }
  // The back buffer: the mean of the light.
  std::vector<SSample> In = { { 1, 0.5f, 0, 1, 1 }, { 0, 0.5f, 1, 1, 1 } };
  const std::vector<SResolved> R = Resolve(In, 2, false);
  CHECK_NEAR(R[0].r, Encode(0.5), 1e-4);
  CHECK_NEAR(R[0].g, 0.5f, 1e-4);
  CHECK_NEAR(R[0].b, Encode(0.5), 1e-4);
}

// Why: a highlight in one sample (the sun on water, a lamp) must count as much as the display shows it, never more:
// the pixel no brighter than its brightest sample in any channel, not darker than the tone map's mean, and a channel
// below 0 (blending can leave one) must not break it.
XR_TEST(Msaa_Highlight)
{
  std::vector<SSample> In = {
    { 200, 20, 2, 1, 1 }, { 0.3f, 0.3f, 0.3f, 1, 1 }, { 0.3f, 0.3f, 0.3f, 1, 1 }, { 0.3f, 0.3f, 0.3f, 1, 1 },
    { -0.01f, 0.3f, 0.3f, 1, 1 }, { -0.01f, 0.3f, 0.3f, 1, 1 }, { -0.01f, 0.3f, 0.3f, 1, 1 }, { -0.01f, 0.3f, 0.3f, 1, 1 },
  };
  const std::vector<SResolved> R = Resolve(In, 4, true);
  const float Bright[3] = { 200, 20, 2 };
  const float Got[3] = { R[0].r, R[0].g, R[0].b };
  for (int c = 0; c < 3; c++)
  {
    const double fWant = (ToneMap(Decode(Bright[c])) + 3 * ToneMap(Decode(0.3))) / 4;
    CHECK_NEAR(ToneMap(Decode(Got[c])), fWant, 2e-4);
    CHECK(Got[c] <= Bright[c]);
    CHECK(Got[c] < 2);
  }
  CHECK(std::isfinite(R[1].r) && std::isfinite(R[1].g));
  CHECK_NEAR(R[1].r, 0, 1e-6);
  CHECK_NEAR(R[1].g, 0.3f, 1e-4);
}

// Why: the alpha is no light (the scene's fog and blending masks): a plain mean of the samples.
XR_TEST(Msaa_AlphaMean)
{
  std::vector<SSample> In = { { 8, 8, 8, 1, 1 }, { 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 1 }, { 0, 0, 0, 0.5f, 1 } };
  CHECK_NEAR(Resolve(In, 4, true)[0].a, 1.5f / 4, 1e-6);
  CHECK_NEAR(Resolve(In, 4, false)[0].a, 1.5f / 4, 1e-6);
}

// Why: what reads the depth per pixel in the scene (the ambient occlusion, the sun's mask by rays, the water's depth)
// must see the edge's nearer surface, not a depth between two surfaces that is of neither; the sky (1) only where
// no sample has a surface.
XR_TEST(Msaa_DepthNearest)
{
  for (uint32 nSamples : { 2u, 4u, 8u })
  {
    std::vector<SSample> In;
    const float Pixel[3][8] = { { 1, 1, 1, 1, 1, 1, 1, 1 }, { 0.9f, 0.3f, 0.7f, 1, 0.8f, 0.6f, 1, 0.95f }, { 1, 1, 1, 1, 1, 1, 1, 0.25f } };
    for (const auto &p : Pixel)
      for (uint32 s = 0; s < nSamples; s++)
        In.push_back({ 0, 0, 0, 0, p[8 - nSamples + s] });
    const std::vector<SResolved> R = Resolve(In, nSamples, true);
    for (int i = 0; i < 3; i++)
    {
      float fNearest = 1;
      for (uint32 s = 0; s < nSamples; s++)
        fNearest = std::min(fNearest, Pixel[i][8 - nSamples + s]);
      CHECK_NEAR(R[i].fDepth, fNearest, 0);
    }
  }
}
