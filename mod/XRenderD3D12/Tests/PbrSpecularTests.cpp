// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PbrSpecularTests.cpp
//	Description: Unit tests of the specular materials' highlights by the
//	RayMaster maps' roughness (r_PbrRoughness): PbrSpecular.hlsli's
//	PbrSpecAlpha2 and PbrSpecLobe on the GPU (TestHeaders.hlsl's
//	CSPbrSpecLobe): the templates' lobe at their roughness, rougher
//	broader and fainter, as much light reflected, wider where the normal
//	changes across the pixel, bounded.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_PbrSpecLobe.h"

#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  //! A lobe: alpha^2, its power of saturate(R.V), its height against the templates' lobe.
  struct SLobe
  {
    float fAlpha2, fPower, fHeight;
  };

  //! The lobes of (roughness, the normal's change across the pixel) pairs.
  std::vector<SLobe> Run(const std::vector<std::pair<float, float>> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_PbrSpecLobe);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const auto &p : In)
      Push(d.In, p.first, p.second, 0, 0);
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SLobe> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ At(Out, i, 0), At(Out, i, 1), At(Out, i, 2) });
    return R;
  }

  //! The light a lobe reflects: its height times the integral of saturate(R.V)^n over the directions about R (the
  //! sphere, by the angle to R in steps), against the templates' lobe (32, height 1).
  double Reflected(double fPower, double fHeight)
  {
    auto Integral = [](double n) {
      const int nSteps = 20000;
      double fSum = 0;
      for (int i = 0; i < nSteps; i++)
      {
        const double t = (i + 0.5) / nSteps * (3.14159265358979 / 2);
        fSum += std::pow(std::cos(t), n) * std::sin(t);
      }
      return fSum * (3.14159265358979 / 2) / nSteps;
    };
    return fHeight * Integral(fPower) / Integral(32);
  }
}

// Why: r_PbrRoughness takes the templates' lobe for the roughness the texture tool gives their gloss (about 0.35); at
// that roughness the highlight must stay what it was, else every specular material changes even where its map says
// nothing new.
XR_TEST(Pbr_SpecTemplate)
{
  const std::vector<SLobe> R = Run({ { 0.35f, 0 } });
  CHECK(R[0].fPower > 28 && R[0].fPower < 37);
  CHECK(R[0].fHeight > 0.87f && R[0].fHeight < 1.13f);
}

// Why: a rough stone (0.8 in its map) must have a broad, faint highlight and a smooth metal (0.2) a narrow, bright one;
// the power and the height must fall as the roughness grows, never rise.
XR_TEST(Pbr_SpecMonotonic)
{
  std::vector<std::pair<float, float>> In;
  for (int i = 0; i <= 50; i++)
    In.push_back({ i / 50.0f, 0 });
  const std::vector<SLobe> R = Run(In);
  for (size_t i = 1; i < R.size(); i++)
  {
    CHECK(R[i].fPower <= R[i - 1].fPower);
    CHECK(R[i].fHeight <= R[i - 1].fHeight);
  }
  CHECK(R[40].fPower < 4);   // 0.8: a broad lobe
  CHECK(R[40].fHeight < 0.2f);
  CHECK(R[10].fPower > 200); // 0.2: a narrow one
  CHECK(R[10].fHeight > 6);
}

// Why: the lobe's power must stay within the ends whatever the map holds (0 to 1, out of range clamped): a power of 0
// would light the whole hemisphere, an unbounded one a single pixel as bright as the sun.
XR_TEST(Pbr_SpecBounds)
{
  const std::vector<SLobe> R = Run({ { 0, 0 }, { -1, 0 }, { 1, 0 }, { 2, 0 }, { 0.5f, -5 } });
  CHECK_NEAR(R[0].fPower, 512, 0);
  CHECK_NEAR(R[1].fPower, 512, 0);
  CHECK_NEAR(R[2].fPower, 1, 0);
  CHECK_NEAR(R[3].fPower, 1, 0);
  CHECK_NEAR(R[0].fHeight, 513.0f / 33, 1e-4f);
  CHECK_NEAR(R[2].fHeight, 2.0f / 33, 1e-6f);
  CHECK_NEAR(R[4].fAlpha2, 0.0625f, 1e-6f);  // a negative change counts as none
}

// Why: the roughness changes how the sun's and the lamps' light is spread, not how much of it the material reflects:
// the lobe's light about R must stay the templates' within the range the power is not clamped at.
XR_TEST(Pbr_SpecEnergy)
{
  std::vector<std::pair<float, float>> In;
  for (int i = 18; i <= 90; i += 4)
    In.push_back({ i / 100.0f, 0 });
  const std::vector<SLobe> R = Run(In);
  for (size_t i = 0; i < R.size(); i++)
  {
    if (R[i].fPower <= 1 || R[i].fPower >= 512)
      continue;
    CHECK_NEAR(Reflected(R[i].fPower, R[i].fHeight), 1.0, 0.01);
  }
}

// Why: where the normal map's bumps change the normal from pixel to pixel (a stone wall far off), a narrow lobe lights
// single pixels that move with the view (sparkle); the lobe must widen with the normal's change, by at most kappa, and
// be the roughness's own where the normal does not change.
XR_TEST(Pbr_SpecNormalChange)
{
  const std::vector<SLobe> R = Run({ { 0.2f, 0 }, { 0.2f, 0.01f }, { 0.2f, 0.1f }, { 0.2f, 100 }, { 0.6f, 0 }, { 0.6f, 100 } });
  CHECK_NEAR(R[0].fAlpha2, 0.0016f, 1e-6f);
  CHECK(R[1].fPower < R[0].fPower);
  CHECK(R[2].fPower < R[1].fPower);
  CHECK_NEAR(R[3].fAlpha2, 0.0016f + 0.18f, 1e-5f);
  CHECK_NEAR(R[4].fAlpha2, 0.1296f, 1e-5f);
  CHECK_NEAR(R[5].fAlpha2, 0.1296f + 0.18f, 1e-5f);
}
