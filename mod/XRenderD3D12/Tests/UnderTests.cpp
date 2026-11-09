// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: UnderTests.cpp
//	Description: Unit tests of the view from under the ocean's surface
//	(d3d12_WaterUnder, Underwater.hlsli): the view's path through the water
//	up to the surface, sea water's phase function (its mean over the sphere
//	1, strongly forwards), the segments of the view's ray whose sun the
//	light it scatters takes (each the same share of that light), the light
//	scattered towards a camera under the surface against its integral in
//	double (its segments' weights in closed form against their loop; the
//	4 x 4 window of taps in the segments' order), the
//	surface seen from below, the light in the water over a
//	blended object (the closed form the plants take, against the view's
//	pass with the sun's light its mean), and what they give under Carrier's
//	water: the water bluer and darker with the depth, the floor fading into
//	it, a glow towards the sun, shafts where the caustics focus the sun.
//	d3d12_WaterUnderClear: the light coming down through the surface, the
//	light clear water scatters of it, the surface beyond Snell's window
//	meeting the water at the horizon, and under Fort's water the water far
//	off darker than the floor the sun lights, bluer, darker looking down;
//	the same light worked out on the CPU once a frame (UnderwaterLight.h).
//	d3d12_WaterUnderAdapt and d3d12_WaterUnderExposure: the eye's white
//	balance at the camera's depth (none at the surface, the light there
//	the colour of the light at the surface, its luminance kept, red's
//	factor the most) and its exposure coming in under the surface.
//	d3d12_WaterUnderLine: a camera just over the level whose near plane
//	dips under it, the water from where a ray goes under the level before
//	the near plane, none where the surface is drawn; how far under the
//	camera the near plane reaches (Fort's dive: on 0.12 m over the level).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_UnderPath.h"
#include "Test_UnderLine.h"
#include "Test_UnderPhase.h"
#include "Test_UnderSegments.h"
#include "Test_UnderInscatter.h"
#include "Test_UnderInscatterClosed.h"
#include "Test_UnderWindow.h"
#include "Test_UnderSurface.h"
#include "Test_UnderFog.h"
#include "Test_UnderClear.h"
#include "Test_VolumeAttenuation.h"
#include "UnderwaterLight.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr int TAPS = 16;
  constexpr double PI = 3.14159265358979;

  SVec3 Linear(float r, float g, float b)
  {
    return { std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f) };
  }

  const SVec3 CARRIER = Linear(0.180f, 0.353f, 0.573f);
  const float CARRIER_DENSITY = std::log(100.0f) / (2 * 60.0f);

  float Comp(SVec3 v, int c) { return c == 0 ? v.x : c == 1 ? v.y : v.z; }

  SVec3 CarrierAttenuation()
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_VolumeAttenuation);
    Push(d.In, CARRIER, CARRIER_DENSITY);
    d.Test.nTest[0] = 1;
    d.nGroups[0] = 1;
    d.nOut = 1;
    return Vec(Gpu().Run(d), 0);
  }

  struct SInscatter
  {
    SVec3 Deep, c;
    float fDepth, fMu, fPath, fReach, fShare, fPhase;
    float fSun0 = 1, fSunStep = 0;   //!< the sun's light at segment j: fSun0 + j fSunStep (at least 0)
    bool bSource = false;            //!< Deep the light the water scatters a metre at the surface (else its colour, UnderDeepSource)
  };

  struct SLight
  {
    SVec3 L;
    float fLit;
  };

  //! UnderInscatter of the cases (bClosed: UnderInscatterClosed).
  std::vector<SLight> Inscatter(const std::vector<SInscatter> &Cases, bool bClosed = false)
  {
    SDispatch d;
    d.Shader = bClosed ? XR_BYTECODE(Test_UnderInscatterClosed) : XR_BYTECODE(Test_UnderInscatter);
    for (const SInscatter &c : Cases)
    {
      Push(d.In, c.Deep, c.fDepth);
      Push(d.In, c.c, c.fMu);
      Push(d.In, c.fPath, c.fReach, c.fShare, c.fPhase);
      Push(d.In, c.fSun0, c.fSunStep, c.bSource ? 1.0f : 0.0f, 0.0f);
    }
    d.Test.nTest[0] = (uint32)Cases.size();
    d.nGroups[0] = Groups64(Cases.size());
    d.nOut = Cases.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SLight> v;
    for (size_t i = 0; i < Cases.size(); i++)
      v.push_back({ Vec(Out, i), At(Out, i, 3) });
    return v;
  }

  //! The light scattered towards a camera fDepth under the level along a ray going down at mu, from 0 to fPath, in double
  //! (Simpson's rule): the light coming down 2 c deep exp(-c (fDepth + mu t)), taken by the water on the way back.
  double InscatterIntegral(double fDeep, double c, double fDepth, double mu, double fPath)
  {
    const int n = 20000;
    const double h = fPath / n;
    double s = 0;
    for (int i = 0; i <= n; i++)
    {
      const double t = i * h, w = (i == 0 || i == n) ? 1 : (i & 1) ? 4 : 2;
      s += w * 2 * c * fDeep * std::exp(-c * (fDepth + mu * t)) * std::exp(-c * t);
    }
    return s * h / 3;
  }

  struct SClear
  {
    SVec3 Down, Source, Below;
  };

  //! d3d12_WaterUnderClear's functions (Test_UnderClear) of the attenuation c, the sun's height fSunZ, its light Sun and the
  //! sky's Sky: UnderDownLight, UnderClearSource and UnderClearBelow at the cosine fCos with the phase function's share fScatter.
  std::vector<SClear> Clear(const std::vector<std::tuple<SVec3, float, SVec3, SVec3, float, float>> &Cases)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_UnderClear);
    for (const auto &[c, fSunZ, Sun, Sky, fCos, fScatter] : Cases)
    {
      Push(d.In, c, fSunZ);
      Push(d.In, Sun, fCos);
      Push(d.In, Sky, fScatter);
    }
    d.Test.nTest[0] = (uint32)Cases.size();
    d.nGroups[0] = Groups64(Cases.size());
    d.nOut = 3 * Cases.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SClear> v;
    for (size_t i = 0; i < Cases.size(); i++)
      v.push_back({ Vec(Out, 3 * i), Vec(Out, 3 * i + 1), Vec(Out, 3 * i + 2) });
    return v;
  }

  //! The game's light of a colour (HdrLight.hlsli's HdrDecode): gamma 2.2, above 1 linear on.
  float Decode(float c)
  {
    return c > 1 ? 1 + 2.2f * (c - 1) : std::pow(std::max(c, 0.0f), 2.2f);
  }

  //! Sea water's phase function in double (Underwater.hlsli's UnderPhase).
  double Phase(double fCos)
  {
    const double g = 0.924, f = 1 + g * g - 2 * g * fCos;
    return 0.5 * 0.06225 * 4 * PI * (1 + 0.835 * fCos * fCos) + 0.5 * (1 - g * g) / (f * std::sqrt(f));
  }
}

// Why: the light the water scatters is taken from the camera up to what the view meets: down and level all of the way to
// the scene, up at most to the surface (the level; with the ocean's grid its crests over it, where the depth copy has
// none of the surface).
XR_TEST(Under_Path)
{
  const float Cases[][5] = { // distance, the direction's z, the camera's depth, the cap; the path
    { 30, -0.5f, 4, 0, 30 }, { 30, 0, 4, 0, 30 }, { 30, 0.5f, 4, 0, 8 }, { 5, 0.5f, 4, 0, 5 }, { 1e4f, 1, 10, 0, 10 },
    { 1e4f, 0.25f, 2, 4, 24 }, { 3, 1, 0, 0, 0 } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderPath);
  for (const auto &c : Cases)
    Push(d.In, c[0], c[1], c[2], c[3]);
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
    CHECK_MSG(std::fabs(At(Out, i, 0) - Cases[i][4]) <= 1e-5f * Cases[i][4] + 1e-6f, std::to_string(i) + ": " + std::to_string(At(Out, i, 0)));
}

// Why: a camera just over the level (Fort's dive, film frames 1323-1325: 0.12 m over it, the near plane 0.25 m) sees the floor
// where the near plane clips the surface away: those rays take the water from where they go under the level, before the near
// plane; the rays that meet the level beyond it see the surface drawn (the water's light the surface's), up and level rays no
// water; a camera under the level its path from the camera (0: the frames before, to the bit).
XR_TEST(Under_Line)
{
  const float Cases[][4] = { // the camera's height, the direction's z, the ray's length to the near plane; the start
    { 0.12f, -0.6f, 0.3f, 0.2f }, { 0.12f, -0.3f, 0.3f, -1 }, { 0.12f, -0.5f, 0.3f, 0.24f }, { 0.12f, 0, 0.3f, -1 },
    { 0.12f, 0.3f, 0.3f, -1 }, { 0, -0.2f, 0.3f, 0 }, { 0, 0, 0.3f, -1 }, { 0.5f, -1, 0.3f, -1 }, { -2, 0.5f, 0.3f, 0 },
    { -0.01f, -0.5f, 0.3f, 0 }, { 0.3f, -1, 0.3f, -1 } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderLine);
  for (const auto &c : Cases)
    Push(d.In, c[0], c[1], c[2], 0);
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
    CHECK_MSG(std::fabs(At(Out, i, 0) - Cases[i][3]) <= 1e-6f, std::to_string(i) + ": " + std::to_string(At(Out, i, 0)));
}

// Why: the waterline's pass runs when the near plane reaches under the level: for a level view its lower edge tan(half the
// vertical angle) times the near plane under the camera, looking down more, looking up less (all of it over the camera
// looking up past the half angle), rolled the corner lower still; Fort's dive (the film's 120 degrees across a 16:9 view, the
// near plane 0.25 m) 0.167 m: the camera 0.12 m over the level on, 0.2 m off.
XR_TEST(Under_NearPlaneDrop)
{
  const float fNear = 0.25f, fRatio = 9.0f / 16, fTanY = std::tan(2.094f * fRatio / 2), fTanX = fTanY / fRatio;
  const float fLevel = UnderwaterLight::NearPlaneDrop(fNear, fTanX, fTanY, 0, 0, 1);
  CHECK_NEAR(fLevel, fNear * fTanY, 1e-6);
  CHECK_NEAR(fLevel, 0.167f, 1e-3);
  CHECK(0.12f < fLevel && 0.2f > fLevel);
  // Pitched by a: forward (cos a, sin a) in z sin a, up z cos a.
  auto Pitched = [&](float a) { return UnderwaterLight::NearPlaneDrop(fNear, fTanX, fTanY, std::sin(a), 0, std::cos(a)); };
  CHECK(Pitched(-0.3f) > fLevel && Pitched(0.3f) < fLevel && Pitched(0.3f) > 0);
  CHECK(Pitched(std::atan(fTanY) + 0.05f) < 0);
  CHECK_NEAR(Pitched(-1.5707963f), fNear, 1e-5);
  // Rolled by r: right z sin r, up z cos r; the lowest corner tan x |sin r| + tan y cos r, more than level.
  const float r = 0.4f;
  CHECK_NEAR(UnderwaterLight::NearPlaneDrop(fNear, fTanX, fTanY, 0, std::sin(r), std::cos(r)),
             fNear * (fTanX * std::sin(r) + fTanY * std::cos(r)), 1e-6);
  CHECK(UnderwaterLight::NearPlaneDrop(fNear, fTanX, fTanY, 0, -std::sin(r), std::cos(r)) > fLevel);
  // A brute force check: the corners of the near plane in the world, the lowest under the camera.
  for (const float a : { -0.8f, -0.2f, 0.0f, 0.35f, 0.9f })
    for (const float rr : { -0.6f, 0.0f, 0.25f })
    {
      // The view's axes: forward pitched by a, then rolled by rr round it.
      const float F[3] = { std::cos(a), 0, std::sin(a) };
      const float U0[3] = { -std::sin(a), 0, std::cos(a) }, Rt0[3] = { 0, -1, 0 };
      float U[3], Rt[3];
      for (int k = 0; k < 3; k++)
      {
        U[k] = std::cos(rr) * U0[k] + std::sin(rr) * Rt0[k];
        Rt[k] = std::cos(rr) * Rt0[k] - std::sin(rr) * U0[k];
      }
      float fLowest = 1e9f;
      for (const float sx : { -1.0f, 1.0f })
        for (const float sy : { -1.0f, 1.0f })
          fLowest = std::min(fLowest, fNear * (F[2] + sx * fTanX * Rt[2] + sy * fTanY * U[2]));
      CHECK_MSG(std::fabs(UnderwaterLight::NearPlaneDrop(fNear, fTanX, fTanY, F[2], Rt[2], U[2]) + fLowest) < 1e-6f,
                std::to_string(a) + " " + std::to_string(rr));
    }
}

// Why: the phase function shares the light the water scatters among the ways it goes: its mean over all ways must be 1 (the
// light the water scatters is the same as with the even phase function, deep water seen from above still its colour), as
// the molecules' (Morel) and the particles' (Henyey and Greenstein at 0.924) are each; towards the sun hundreds of times
// brighter than away from it, a glow a few degrees wide.
XR_TEST(Under_Phase)
{
  const int n = 40001;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderPhase);
  std::vector<double> Cos;
  for (int i = 0; i < n; i++)
  {
    // Denser towards 1 (the glow's peak): cos = 1 - 2 u^2.
    const double u = (double)i / (n - 1);
    Cos.push_back(1 - 2 * u * u);
    Push(d.In, (float)Cos.back(), 0.0f, 0.0f, 0.0f);
  }
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  // The mean over the sphere: 1/2 the integral over the cosine, by the trapezoid rule in u (dcos = 4 u du).
  double fMean = 0, fWorst = 0;
  for (int i = 0; i < n; i++)
  {
    const double u = (double)i / (n - 1), w = (i == 0 || i == n - 1) ? 0.5 : 1;
    fMean += w * At(Out, i, 0) * 4 * u / (n - 1) * 0.5;
    // (Against the cosine as the GPU has it: near 1 f = 1 + g^2 - 2 g cos loses digits in a float.)
    const double e = Phase((double)(float)Cos[i]);
    fWorst = std::max(fWorst, std::fabs(At(Out, i, 0) - e) / e);
  }
  CHECK_MSG(std::fabs(fMean - 1) < 2e-3, std::to_string(fMean));
  CHECK_MSG(fWorst < 2e-4, std::to_string(fWorst));
  const double fForward = At(Out, 0, 0), fBack = At(Out, n - 1, 0);
  CHECK_MSG(fForward > 150 && fForward / fBack > 200, std::to_string(fForward) + " " + std::to_string(fBack));
  // Over half of the glow's peak within 2 degrees of the sun, under half beyond 4, under a tenth beyond 10 (its particles'
  // part: 3.5 and 8.6 degrees).
  CHECK(Phase(std::cos(2 * PI / 180)) > 0.5 * fForward && Phase(std::cos(4 * PI / 180)) < 0.5 * fForward &&
        Phase(std::cos(10 * PI / 180)) < 0.1 * fForward);
  // Sideways the molecules' part (Rayleigh-like) brighter at 0 and 180 than at 90 degrees.
  CHECK(Phase(-1) > Phase(0));
}

// Why: the sun's light is taken at a point of each of 16 segments of the view's ray: they must be the same share each of
// the light the water scatters towards the camera (in the green channel), the nearer shorter, together reaching out to
// the reach, and even where the light hardly fades along the ray (looking up at the surface).
XR_TEST(Under_Segments)
{
  const float Cases[][3] = { { 0.0565f, 53, 0.5f }, { 0.0565f, 5, 0.25f }, { 0.11f, 27, 0.75f }, { 1e-6f, 8, 0.5f }, { 0, 12, 0.3f }, { 2, 1.5f, 0.9f } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderSegments);
  for (const auto &c : Cases)
    Push(d.In, c[0], c[1], c[2], 0.0f);
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = 5 * n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
  {
    const double k = Cases[i][0], R = Cases[i][1];
    CHECK(At(Out, 5 * i + 4, 1) == TAPS);
    const double fAll = k * R > 1e-3 ? (1 - std::exp(-k * R)) / k : R;
    double t0 = 0;
    for (int j = 0; j < TAPS; j++)
    {
      const double t1 = At(Out, 5 * i + j / 4, j % 4);
      const double w = k * R > 1e-3 ? (std::exp(-k * t0) - std::exp(-k * t1)) / k : t1 - t0;
      CHECK_MSG(t1 > t0 && std::fabs(w - fAll / TAPS) < 2e-4 * fAll, std::to_string(i) + "/" + std::to_string(j) + ": " + std::to_string(w * TAPS / fAll));
      if (j > 0 && k * R > 1e-3)
        CHECK(t1 - t0 > (At(Out, 5 * i, 0)) * 0.999);
      t0 = t1;
    }
    CHECK_NEAR(t0, R, 1e-6 * R);
    // A share of it: where that share of the light comes from nearer.
    const double f = Cases[i][2], t = At(Out, 5 * i + 4, 0);
    const double fNear = k * R > 1e-3 ? (1 - std::exp(-k * t)) / k : t;
    CHECK_MSG(std::fabs(fNear - f * fAll) < 2e-4 * fAll, std::to_string(i) + ": " + std::to_string(fNear / fAll));
  }
}

// Why: the light the water scatters towards a camera under the surface is the single scattering of the light coming down,
// fading with the depth of each point of the view's ray, taken by the water on the way back: its segments' closed forms
// must add up to its integral, at the surface looking down it must be the light the water seen from above scatters
// (VolumeInscatter's 2 / (1 + mu) of its colour), straight up the light from the camera's depth to the surface, a camera
// deeper must see it as much darker as the light coming down is there; the sun's share must go in the shadow and grow by
// the phase function, and fLit must be the sun's share left.
XR_TEST(Under_Inscatter)
{
  const SVec3 Deep = CARRIER, c = CarrierAttenuation();
  std::vector<SInscatter> Cases;
  const float Mus[] = { 1, 0.5f, 0, -0.5f, -0.9f };
  const float Depths[] = { 0, 2, 9 };
  for (float mu : Mus)
    for (float d0 : Depths)
    {
      const float fPath = mu < 0 ? d0 / -mu + 0.01f : 120, fReach = std::min(fPath, 3 / (c.y * (1 + mu)));
      Cases.push_back({ Deep, c, d0, mu, fPath, fReach, 0.6f, 1 });
    }
  const size_t nIntegral = Cases.size();
  // At the surface looking down into deep water; straight up from 6 m; in the shadow; the glow; fLit of a gradient.
  Cases.push_back({ Deep, c, 0, 0.8f, 1e5f, 3 / (c.y * 1.8f), 0.6f, 1 });
  Cases.push_back({ Deep, c, 6, -1, 6, 6, 0.6f, 1 });
  Cases.push_back({ Deep, c, 3, 0.2f, 60, 40, 0.6f, 1, 0, 0 });
  Cases.push_back({ Deep, c, 3, 0.2f, 60, 40, 0.6f, 3 });
  Cases.push_back({ Deep, c, 3, 0.2f, 40, 40, 0.6f, 1, 0, 1 / 15.0f });
  Cases.push_back({ Deep, c, 3, 0.2f, 60, 40, 0.6f, 1 });
  const std::vector<SLight> v = Inscatter(Cases);
  double fWorst = 0;
  for (size_t i = 0; i < nIntegral; i++)
  {
    const SInscatter &s = Cases[i];
    for (int k = 0; k < 3; k++)
    {
      const double e = InscatterIntegral(Comp(s.Deep, k), Comp(s.c, k), s.fDepth, s.fMu, s.fPath);
      fWorst = std::max(fWorst, std::fabs(Comp(v[i].L, k) - e) / std::max(e, 1e-7));
    }
    CHECK_NEAR(v[i].fLit, 1, 1e-6);
  }
  CHECK_MSG(fWorst < 2e-4, "the segments against the integral: " + std::to_string(fWorst));
  const size_t j = nIntegral;
  for (int k = 0; k < 3; k++)
  {
    // From the surface down: VolumeInscatter's 2 / (1 + mu) of the water's colour.
    CHECK_NEAR(Comp(v[j].L, k), Comp(Deep, k) * 2 / 1.8, 1e-4 * Comp(Deep, k));
    // Straight up: 2 c deep exp(-c d0) d0, the light hardly fading along the ray (k 0).
    const double ck = Comp(c, k);
    CHECK_NEAR(Comp(v[j + 1].L, k), 2 * ck * Comp(Deep, k) * std::exp(-ck * 6) * 6, 1e-4 * Comp(v[j + 1].L, k));
    // In the shadow the sun's share gone; three times the even phase function the sun's share three times.
    CHECK_NEAR(Comp(v[j + 2].L, k), 0.4 * Comp(v[j + 5].L, k), 1e-5 * Comp(v[j + 5].L, k));
    CHECK_NEAR(Comp(v[j + 3].L, k), (0.4 + 0.6 * 3) * Comp(v[j + 5].L, k), 1e-5 * Comp(v[j + 5].L, k));
  }
  // A camera deeper: the light coming down there exp(-c d) as bright (the level views at 0 and 9 m, mu 0).
  for (int k = 0; k < 3; k++)
    CHECK_NEAR(Comp(v[2 * 3 + 2].L, k) / Comp(v[2 * 3].L, k), std::exp(-Comp(c, k) * 9), 1e-4);
  CHECK_NEAR(v[j + 2].fLit, 0, 1e-7);
  CHECK_NEAR(v[j + 4].fLit, 0.5, 1e-5);
}

// Why: with d3d12_WaterUnderShare the view's pass weighs the segments in closed form (each the same share in green by its
// making, red's and blue's from the light at the segments' ends); it must give UnderInscatter's light and fLit to a float's
// rounding for any ray, sun and path: down, level, up nearly straight (the light hardly fading along the ray: red and blue
// segment by segment there, green too where the segments are the same length), a surface a few centimetres to a hundred
// metres off, the sun even, in the shadow, three times its mean, a
// gradient along the ray.
XR_TEST(Under_Closed)
{
  const SVec3 Deep = CARRIER, c = CarrierAttenuation();
  std::vector<SInscatter> Cases;
  const float Mus[] = { 1, 0.7f, 0.3f, 0, -0.3f, -0.7f, -0.95f, -0.999f };
  const float Depths[] = { 0.2f, 3, 12 };
  const float Paths[] = { 0.05f, 0.4f, 2, 9, 40, 150 };
  const float Suns[][2] = { { 1, 0 }, { 0, 0 }, { 3, 0 }, { 0, 1 / 15.0f }, { 2.5f, -0.15f } };
  int nClosed = 0, nLoop = 0;
  for (float mu : Mus)
    for (float d0 : Depths)
      for (float p : Paths)
        for (const auto &s : Suns)
        {
          // Up only to the surface, as UnderPath.
          const float fPath = mu < 0 ? std::min(p, d0 / -mu) : p;
          const float fReach = std::min(fPath, 3 / std::max(c.y * (1 + mu), 1e-4f));
          Cases.push_back({ Deep, c, d0, mu, fPath, fReach, 0.6f, 1.7f, s[0], s[1] });
          const float fLeast = std::min(c.x, std::min(c.y, c.z)) * (1 + mu) * fReach;
          (fLeast < 0.05f ? nLoop : nClosed)++;
        }
  const std::vector<SLight> a = Inscatter(Cases), b = Inscatter(Cases, true);
  double fWorst = 0, fWorstLit = 0;
  for (size_t i = 0; i < Cases.size(); i++)
  {
    for (int k = 0; k < 3; k++)
    {
      const double e = Comp(a[i].L, k);
      fWorst = std::max(fWorst, std::fabs(Comp(b[i].L, k) - e) / std::max(std::fabs(e), 1e-9));
    }
    fWorstLit = std::max(fWorstLit, (double)std::fabs(b[i].fLit - a[i].fLit));
  }
  CHECK_MSG(nClosed > 100 && nLoop > 20, "cases in closed form " + std::to_string(nClosed) + ", by the loop " + std::to_string(nLoop));
  CHECK_MSG(fWorst < 1e-4, "the closed form against the segments' loop: " + std::to_string(fWorst));
  CHECK_MSG(fWorstLit < 1e-5, "fLit: " + std::to_string(fWorstLit));
}

// Why: with d3d12_WaterUnderShare each pixel takes the sun's light at its ray's sixteen segments from the 4 x 4 window round
// it, each texel of which took the segment of its place modulo 4 in the taps pass; the window's texels must come out in
// the order of those segments, each once, wherever the window starts (its place modulo 4 turns its rows and columns): a
// texel in the wrong place weighs one segment's sun as another's.
XR_TEST(Under_Window)
{
  std::vector<std::pair<uint32, uint32>> Starts;
  for (uint32 y = 0; y < 8; y++)
    for (uint32 x = 0; x < 8; x++)
      Starts.push_back({ x, y });
  Starts.push_back({ 1913, 1077 });
  Starts.push_back({ 3836, 2156 });
  Starts.push_back({ 1278, 717 });
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderWindow);
  for (const auto &s : Starts)
    Push(d.In, (float)s.first, (float)s.second, 0.0f, 0.0f);
  const size_t n = Starts.size();
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = 4 * n;
  const std::vector<uint32> Out = Gpu().Run(d);
  int nWrong = 0;
  std::string First;
  for (size_t i = 0; i < n; i++)
  {
    uint32 nSeen = 0;
    for (uint32 j = 0; j < 16; j++)
    {
      const uint32 v = (uint32)At(Out, 4 * i + j / 4, j % 4);
      const uint32 p = v / 16, a = p % 4, b = p / 4;
      // The texel's segment as the taps pass gave it (UnderBlockSegment) and as its place in the window says.
      const uint32 nSegment = (Starts[i].first + a) % 4 + 4 * ((Starts[i].second + b) % 4);
      const bool bRight = p < 16 && v % 16 == j && nSegment == j && !(nSeen & (1u << p));
      nSeen |= p < 16 ? 1u << p : 0;
      if (!bRight && nWrong++ == 0)
        First = "start " + std::to_string(Starts[i].first) + ", " + std::to_string(Starts[i].second) + ": segment " + std::to_string(j) +
                " got the texel " + std::to_string(a) + ", " + std::to_string(b) + " (" + std::to_string(v) + ")";
    }
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " wrong, the first " + First);
}

// Why: from below the surface lets the light from the air through Snell's window, and mirrors the water under it outside
// (and the share of the window Fresnel reflects): the deep water's light seen along the mirrored ray, the light the water
// seen from above at that angle scatters (2 / (1 + mu) of its colour).
XR_TEST(Under_Surface)
{
  const SVec3 Above = { 3, 2.5f, 2 };
  const float Cases[][2] = { { 0, 0.5f }, { 0, 1 }, { 0, 0 }, { 1, 1 }, { 0.6f, 0.9f } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderSurface);
  for (const auto &c : Cases)
  {
    Push(d.In, CARRIER, c[0]);
    Push(d.In, Above, c[1]);
  }
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
    for (int k = 0; k < 3; k++)
    {
      const double fMirror = Comp(CARRIER, k) * 2 / (1 + Cases[i][1]);
      const double e = fMirror * (1 - Cases[i][0]) + Comp(Above, k) * Cases[i][0];
      CHECK_NEAR(Comp(Vec(Out, i), k), e, 1e-5 * e);
    }
}

// Why: a blended object under the water (the plants) is drawn after the view's pass and takes the light in the water over its
// own colour: blended over the water behind by its alpha, a·(colour T + L) + (1 - a)·(what is behind, already so), it must
// be what the view's pass gives a surface there with the sun's light its mean along the way (the same path up to the surface,
// the same attenuation along it and down to the surface's depth, the same scattered light and glow towards the sun), so that
// a plant matches the water round it near and far; none of the sun's glow with the sun under the horizon.
XR_TEST(Under_Fog)
{
  const SVec3 Deep = CARRIER, c = CarrierAttenuation();
  struct SCase
  {
    float fCamera, fDist;   //!< the camera's depth; the distance (negative: the flat surface, no cap)
    SVec3 D;                //!< the view's direction (normalised below)
    float fDepth;           //!< the surface's depth
    SVec3 L;                //!< the sun
  };
  const SVec3 Sun = { -0.85f, -0.08f, 0.52f }, Night = { 0.3f, 0.2f, -0.6f };
  const SCase Cases[] = {
    { 5, 3, { 0, 1, 0 }, 5, Sun },        { 5, 25, { 0.3f, 1, -0.2f }, 8, Sun }, { 5, 60, { 0, 1, -0.8f }, 13, Sun },
    { 5, 12, { -0.638f, -0.06f, 0.768f }, 0, Sun }, { 5, -40, { 0.1f, 1, 0.5f }, 0, Sun }, { 5, 40, { 0.1f, 1, 0.5f }, 0, Sun },
    { 1, 8, { 1, 0, 0.1f }, 0.2f, Sun },  { 12, 20, { 0, -1, 0 }, 12, Night }, { 0.5f, 0.2f, { 0, 0, -1 }, 0.7f, Sun } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_UnderFog);
  std::vector<SVec3> Dirs;
  for (const SCase &k : Cases)
  {
    const float n = std::sqrt(k.D.x * k.D.x + k.D.y * k.D.y + k.D.z * k.D.z);
    Dirs.push_back({ k.D.x / n, k.D.y / n, k.D.z / n });
    Push(d.In, Deep, k.fCamera);
    Push(d.In, c, k.fDist);
    Push(d.In, Dirs.back(), k.fDepth);
    Push(d.In, k.L, 0.6f);
  }
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = 2 * n;
  const std::vector<uint32> Out = Gpu().Run(d);
  // The view's pass of the same rays, its sun's light 1 at every segment (UnderInscatter: the same path and reach).
  std::vector<SInscatter> Pass;
  for (size_t i = 0; i < n; i++)
  {
    const SCase &k = Cases[i];
    const float fDist = std::fabs(k.fDist), fCap = k.fDist > 0 ? 4.0f : 0.0f, mu = -Dirs[i].z;
    const float fPath = Dirs[i].z > 0 ? std::min(fDist, (k.fCamera + fCap) / Dirs[i].z) : fDist;
    Pass.push_back({ Deep, c, k.fCamera, mu, fPath, std::min(fPath, 3 / std::max(c.y * (1 + mu), 1e-4f)), 0.6f, At(Out, 2 * i, 3) });
  }
  const std::vector<SLight> v = Inscatter(Pass);
  const double eta = 1.333;
  for (size_t i = 0; i < n; i++)
  {
    const SCase &k = Cases[i];
    const SVec3 S = Vec(Out, 2 * i), T = Vec(Out, 2 * i + 1);
    // The phase towards the camera: the sun refracted into the water, against the double's.
    double fPhase = 1;
    if (k.L.z > 0)
    {
      const double hx = k.L.x / eta, hy = k.L.y / eta, hz = std::sqrt(1 - hx * hx - hy * hy);
      fPhase = Phase(hx * Dirs[i].x + hy * Dirs[i].y + hz * Dirs[i].z);
    }
    CHECK_MSG(std::fabs(At(Out, 2 * i, 3) - fPhase) < 2e-4 * fPhase, std::to_string(i) + ": " + std::to_string(At(Out, 2 * i, 3)));
    const double fLz = std::max((double)k.L.z, 0.0), fCos = std::sqrt(1 - (1 - fLz * fLz) / (eta * eta));
    for (int q = 0; q < 3; q++)
    {
      const double e = Comp(v[i].L, q);
      CHECK_MSG(std::fabs(Comp(S, q) - e) <= 3e-4 * e + 1e-8, std::to_string(i) + "/" + std::to_string(q) + ": " + std::to_string(Comp(S, q) / e));
      const double fT = std::exp(-Comp(c, q) * (Pass[i].fPath + k.fDepth / fCos));
      CHECK_MSG(std::fabs(Comp(T, q) - fT) <= 1e-5 * fT + 1e-9, std::to_string(i) + "/" + std::to_string(q) + ": " + std::to_string(Comp(T, q)));
    }
  }
  // Up through the surface: the path ends at the surface (5 m up at 0.45: 11.2 m, with the grid's cap 20 m): the flat
  // surface's light less.
  CHECK(At(Out, 2 * 4, 1) < At(Out, 2 * 5, 1));
  // The glow: looking up the sun's refracted rays (case 3) many times as bright as across them (case 1).
  CHECK(At(Out, 2 * 3, 3) > 20 * At(Out, 2 * 1, 3));
  CHECK_NEAR(At(Out, 2 * 7, 3), 1, 1e-6);
}

// Why: what the step is for, under Carrier's water: looking level the water is blue-green, brighter than the deep water
// seen from above and darker the deeper the camera; a sandy floor fades into the water's colour with the distance; the
// glow round the sun makes the water towards it many times as bright as away from it; and a ray through a shaft where the
// caustics focus the sun (twice its light) is several times as bright as one through the shadow between: shafts.
XR_TEST(Under_Carrier)
{
  const SVec3 c = CarrierAttenuation();
  const float kg = c.y;
  auto Reach = [&](float mu, float fPath) { return std::min(fPath, 3 / (kg * (1 + mu))); };
  std::vector<SInscatter> Cases;
  for (float d0 : { 2.0f, 5.0f, 15.0f })
    Cases.push_back({ CARRIER, c, d0, 0, 400, Reach(0, 400), 0.6f, 1 });
  // A floor 10, 30 and 400 m off (the view level from 5 m), the scene's attenuation exp(-c t).
  for (float t : { 10.0f, 30.0f, 400.0f })
    Cases.push_back({ CARRIER, c, 5, 0, t, Reach(0, t), 0.6f, 1 });
  // Towards the sun (the phase at 0 degrees) and away from it (180), from 5 m, 30 degrees up.
  Cases.push_back({ CARRIER, c, 5, -0.5f, 10, 10, 0.6f, (float)Phase(1) });
  Cases.push_back({ CARRIER, c, 5, -0.5f, 10, 10, 0.6f, (float)Phase(-1) });
  // The caustics' shafts: a ray through one (the sun's light 2 along it), one through the shadow between.
  Cases.push_back({ CARRIER, c, 5, 0, 20, 20, 0.6f, 1, 2, 0 });
  Cases.push_back({ CARRIER, c, 5, 0, 20, 20, 0.6f, 1, 0, 0 });
  const std::vector<SLight> v = Inscatter(Cases);
  // Level from 2 m: blue over green over red, brighter than deep water from above (the colour); deeper darker.
  CHECK_MSG(v[0].L.z > v[0].L.y && v[0].L.y > v[0].L.x && v[0].L.z > CARRIER.z, std::to_string(v[0].L.z) + " " + std::to_string(CARRIER.z));
  CHECK(v[1].L.y < v[0].L.y && v[2].L.y < 0.6f * v[1].L.y && v[2].L.z < v[1].L.z);
  // The sand (0.45, 0.40, 0.30) seen through the water: at 10 m still more of it than the water's light in green, at
  // 400 m the water's alone.
  const SVec3 Sand = { 0.45f, 0.40f, 0.30f };
  auto Seen = [&](size_t i, float t) { return SVec3{ Sand.x * std::exp(-c.x * t), Sand.y * std::exp(-c.y * t), Sand.z * std::exp(-c.z * t) } + v[i].L; };
  const SVec3 s10 = Seen(3, 10), s400 = Seen(5, 400);
  CHECK_MSG(Sand.y * std::exp(-c.y * 10) > v[3].L.y, std::to_string(s10.y));
  for (int k = 0; k < 3; k++)
    CHECK_NEAR(Comp(s400, k), Comp(v[1].L, k), 0.01 * Comp(v[1].L, k) + 1e-5);
  // Red fades first: at 30 m the floor's red under a tenth of its own.
  CHECK(Sand.x * std::exp(-c.x * 30) < 0.1f * Sand.x);
  // The glow: towards the sun many times as bright as away.
  CHECK_MSG(v[6].L.y > 20 * v[7].L.y, std::to_string(v[6].L.y / v[7].L.y));
  // The shafts: the sun's light twice its mean along one (fLit 2), the ray through the shadow between a quarter as bright
  // (only the light not the sun's: 0.4 against 1.6).
  CHECK_NEAR(v[8].fLit, 2, 1e-5);
  CHECK_NEAR(v[9].L.y, 0.25f * v[8].L.y, 1e-5 * v[8].L.y);
}

// Why: with d3d12_WaterUnderClear the light the water scatters comes from the scene's light coming down through the surface:
// the sun's through the flat surface (Fresnel's transmission at its refracted ray, its beam's light over a horizontal face's:
// none under the horizon) and the sky's (0.934 through, within Snell's window, mean cosine 0.83), with as much again coming
// back up; the water scatters as pure water's molecules and its particles do (in green all it takes beyond pure water,
// in red and blue as 1 / wavelength, at most what each takes beyond pure water; pure water its molecules' alone); beyond
// Snell's window the surface sends down the light the water below scatters up along the mirrored ray, the same as a level
// view at the surface's depth sees far off (at the horizon the two meet).
XR_TEST(Under_ClearSource)
{
  const SVec3 c = { 0.275f, 0.0702f, 0.0347f }, Sun = { 3.0f, 2.7f, 1.8f }, Sky = { 0.12f, 0.27f, 0.59f };
  const SVec3 Pure = { 0.2644f, 0.0565f, 0.0106f }, Molecules = { 0.00122f, 0.00191f, 0.00376f };
  const double Spectrum[3] = { 550.0 / 610, 1, 550.0 / 470 };
  auto Scattering = [&](SVec3 a, int k) {
    const double g = std::max(a.y - Pure.y - Molecules.y, 0.0f), m = std::max(Comp(a, k) - Comp(Pure, k) - Comp(Molecules, k), 0.0f);
    return Comp(Molecules, k) + std::min(g * Spectrum[k], m);
  };
  const float Heights[] = { 1, 0.853f, 0.3f, 0.05f, 0, -0.4f };
  std::vector<std::tuple<SVec3, float, SVec3, SVec3, float, float>> Cases;
  for (float z : Heights)
    Cases.push_back({ c, z, Sun, Sky, 0.0f, 1.0f });
  // Pure water: no scattering; the mirrored ray at 0.5 and 0.9 with the phase's share 1.3.
  Cases.push_back({ Pure, 0.853f, Sun, Sky, 0.0f, 1.0f });
  Cases.push_back({ c, 0.853f, Sun, Sky, 0.5f, 1.3f });
  Cases.push_back({ c, 0.853f, Sun, Sky, 0.9f, 1.3f });
  const std::vector<SClear> v = Clear(Cases);
  const double eta = 1.333;
  for (size_t i = 0; i < std::size(Heights); i++)
  {
    const double z = std::max((double)Heights[i], 0.0), cw = std::sqrt(1 - (1 - z * z) / (eta * eta));
    // Fresnel's transmission (unpolarised) at the refracted ray.
    const double st2 = eta * eta * (1 - cw * cw), ct = std::sqrt(std::max(1 - st2, 0.0));
    const double rs = (eta * cw - ct) / (eta * cw + ct), rp = (cw - eta * ct) / (cw + eta * ct);
    const double fT = st2 >= 1 ? 0 : 1 - 0.5 * (rs * rs + rp * rp);
    for (int k = 0; k < 3; k++)
    {
      const double e = 2 * 0.25 * (Comp(Sun, k) * fT * z / cw + 0.934 / 0.83 * Comp(Sky, k));
      CHECK_MSG(std::fabs(Comp(v[i].Down, k) - e) <= 2e-5 * e, std::to_string(Heights[i]) + "/" + std::to_string(k) + ": " +
                                                                    std::to_string(Comp(v[i].Down, k) / e));
      CHECK_NEAR(Comp(v[i].Source, k), Scattering(c, k) * Comp(v[i].Down, k), 1e-5 * Comp(v[i].Down, k));
      // At the horizon (cos 0) the water's light at the surface far off along a level ray: source / c.
      CHECK_NEAR(Comp(v[i].Below, k), Comp(v[i].Source, k) / Comp(c, k), 1e-5 * Comp(v[i].Source, k) / Comp(c, k));
    }
    // The sun straight up: 0.98 of its light through (Fresnel), less as it sinks; under the horizon the sky's alone.
    if (i > 0)
      CHECK(v[i].Down.y < v[i - 1].Down.y || Heights[i] <= 0);
  }
  CHECK_NEAR(v[0].Down.y, 0.5 * (0.9796 * Sun.y + 0.934 / 0.83 * Sky.y), 1e-4 * v[0].Down.y);
  CHECK_NEAR(v[5].Down.y, 0.5 * 0.934 / 0.83 * Sky.y, 1e-5);
  CHECK_NEAR(v[4].Down.y, v[5].Down.y, 1e-6);
  // Pure water its molecules' scattering alone; Fort's water blue most, red hardly any of what it takes: far off its red
  // under a tenth of its blue; blue's particles green's times 550 / 470.
  for (int k = 0; k < 3; k++)
    CHECK_NEAR(Comp(v[6].Source, k), Comp(Molecules, k) * Comp(v[6].Down, k), 1e-5 * Comp(v[6].Down, k));
  CHECK_NEAR(Scattering(c, 2) - Molecules.z, (Scattering(c, 1) - Molecules.y) * 550 / 470, 1e-6);
  CHECK_MSG(v[1].Below.x < 0.1f * v[1].Below.z && v[1].Source.y < v[1].Source.z, std::to_string(v[1].Below.x / v[1].Below.z));
  // Down the mirrored ray steeper the water below darker: 1 / (1 + cos), times the phase's share.
  for (int k = 0; k < 3; k++)
  {
    CHECK_NEAR(Comp(v[7].Below, k), 1.3 * Comp(v[1].Below, k) / 1.5, 1e-5 * Comp(v[1].Below, k));
    CHECK(Comp(v[8].Below, k) < Comp(v[7].Below, k));
  }
}

// Why: what d3d12_WaterUnderClear is for, under Fort's water (its fog volume (0.341, 0.635, 0.875), 30 m, the sun of the
// level's start): as the water seen from above shows it (twice its colour) the water far off was as bright as the sand
// the sun lights a few metres away, a milky fog; from the scene's sun and sky, clear water scattering little of what it
// takes, it is under half as bright as that sand, about the fog colour's hue (a little bluer), darker deeper and looking
// down; the surface beyond Snell's window meets it at the horizon.
XR_TEST(Under_ClearFort)
{
  const SVec3 Deep = Linear(0.341f, 0.635f, 0.875f);
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_VolumeAttenuation);
  Push(d.In, Deep, std::log(100.0f) / (2 * 30.0f));
  d.Test.nTest[0] = 1;
  d.nGroups[0] = 1;
  d.nOut = 1;
  const SVec3 c = Vec(Gpu().Run(d), 0);
  const float fSunZ = 0.853f;
  const SVec3 Sun = { Decode(2 * 0.96f), Decode(2 * 0.89f), Decode(2 * 0.68f) }, Sky = { Decode(0.384f), Decode(0.549f), Decode(0.788f) };
  const std::vector<SClear> s = Clear({ { c, fSunZ, Sun, Sky, 0.001f, 1.0f } });
  const SVec3 Source = s[0].Source;
  auto Reach = [&](float mu, float fPath) { return std::min(fPath, 3 / std::max(c.y * (1 + mu), 1e-4f)); };
  std::vector<SInscatter> Cases;
  // Level from 3 m and 8 m far off; down at 0.7 from 3 m (a floor 40 m off); up at 0.5 from 3 m to the surface; level
  // just under the surface far off; the same level from 3 m as the water seen from above shows it.
  for (float d0 : { 3.0f, 8.0f })
    Cases.push_back({ Source, c, d0, 0, 400, Reach(0, 400), 0.6f, 1, 1, 0, true });
  Cases.push_back({ Source, c, 3, 0.7f, 400, Reach(0.7f, 400), 0.6f, 1, 1, 0, true });
  Cases.push_back({ Source, c, 3, -0.5f, 6, Reach(-0.5f, 6), 0.6f, 1, 1, 0, true });
  Cases.push_back({ Source, c, 0, 0, 1e4f, Reach(0, 1e4f), 0, 1, 1, 0, true });
  Cases.push_back({ Deep, c, 3, 0, 400, Reach(0, 400), 0.6f, 1 });
  const std::vector<SLight> v = Inscatter(Cases, true);
  // The sand (0.45, 0.40, 0.30) 4 m down lit by the sun (its ray refracted, 0.92 to the vertical) and the sky, seen 5 m off
  // from 3 m (the water in front added).
  const SVec3 Sand = { 0.45f, 0.40f, 0.30f };
  const double cw = std::sqrt(1 - (1 - fSunZ * fSunZ) / (1.333 * 1.333));
  SVec3 Floor;
  for (int k = 0; k < 3; k++)
  {
    const double ck = Comp(c, k), fLit = Comp(Sand, k) * (Comp(Sun, k) * cw + Comp(Sky, k)) * std::exp(-ck * 4 / cw);
    const float f = (float)(fLit * std::exp(-ck * 5));
    (k == 0 ? Floor.x : k == 1 ? Floor.y : Floor.z) = f;
  }
  // The fog's way: the water far off as bright as the sand (its luminance, Rec. 709, at least 0.9 of it); now under half.
  auto Y = [](SVec3 a) { return 0.2126f * a.x + 0.7152f * a.y + 0.0722f * a.z; };
  CHECK_MSG(Y(v[5].L) > 0.9f * Y(Floor), std::to_string(Y(v[5].L) / Y(Floor)));
  CHECK_MSG(Y(v[0].L) < 0.5f * Y(Floor), std::to_string(Y(v[0].L) / Y(Floor)));
  // Its hue about the fog colour's: blue over green at least as the colour's, under 1.5 times; red under a sixth of green.
  const float fBlue = v[0].L.z / v[0].L.y, fDeepBlue = Deep.z / Deep.y;
  CHECK_MSG(fBlue > fDeepBlue && fBlue < 1.5f * fDeepBlue, std::to_string(fBlue) + " " + std::to_string(fDeepBlue));
  CHECK_MSG(v[0].L.x < v[0].L.y / 6, std::to_string(v[0].L.x / v[0].L.y));
  // Deeper darker; looking down darker, up brighter than level.
  CHECK(v[1].L.y < 0.8f * v[0].L.y && v[1].L.z < v[0].L.z);
  CHECK(v[2].L.y < 0.8f * v[0].L.y && v[2].L.z < 0.8f * v[0].L.z);
  // Up at 30 degrees to the surface 6 m off: the surface beyond Snell's window, the water below mirrored (UnderClearBelow at
  // 0.5), over the water in front; darker than at the horizon (the ring round the window), brighter than looking down.
  const std::vector<SClear> m = Clear({ { c, fSunZ, Sun, Sky, 0.5f, 1.0f } });
  for (int k = 1; k < 3; k++)
  {
    const double fUp = Comp(v[3].L, k) + Comp(m[0].Below, k) * std::exp(-Comp(c, k) * 6);
    CHECK_MSG(Comp(m[0].Below, k) < Comp(s[0].Below, k) && fUp > Comp(v[2].L, k),
              std::to_string(k) + ": " + std::to_string(fUp) + " " + std::to_string(Comp(v[2].L, k)));
  }
  // The horizon: the surface beyond the window grazing (cos 0.001) the same as the water level at its depth far off.
  for (int k = 0; k < 3; k++)
    CHECK_NEAR(Comp(s[0].Below, k), Comp(v[4].L, k), 2e-3 * Comp(v[4].L, k));
}

// Why: the view's pass and the surface seen from below take the light the water scatters a metre at the surface from their
// constants, worked out once a frame on the CPU (UnderwaterLight.h's ClearSource from the game's colours) in place of in
// each pixel: it must be the shaders' (VolumeAttenuation of the decoded colour, UnderDownLight, UnderClearSource), else the
// surface and the water below it would part at the horizon from the plants' (which still work it out in their program).
XR_TEST(Under_ClearCpu)
{
  struct SCase
  {
    float Fog[3], fMaxDist, Sun[3], Sky[3], fSunZ;
  };
  const SCase Cases[] = {
    { { 0.341f, 0.635f, 0.875f }, 30, { 0.96f, 0.89f, 0.68f }, { 0.384f, 0.549f, 0.788f }, 0.853f },  // Fort
    { { 0.180f, 0.353f, 0.573f }, 60, { 0.9f, 0.85f, 0.75f }, { 0.5f, 0.6f, 0.7f }, 0.52f },          // Carrier's colour
    { { 0.25f, 0.45f, 0.30f }, 12, { 0.7f, 0.6f, 0.5f }, { 0.2f, 0.25f, 0.3f }, 0.15f },              // green, a low sun
    { { 0.341f, 0.635f, 0.875f }, 30, { 0.3f, 0.3f, 0.4f }, { 0.05f, 0.06f, 0.1f }, -0.3f },         // the sun down
  };
  SDispatch a;
  a.Shader = XR_BYTECODE(Test_VolumeAttenuation);
  for (const SCase &k : Cases)
    Push(a.In, SVec3{ Decode(k.Fog[0]), Decode(k.Fog[1]), Decode(k.Fog[2]) }, std::log(100.0f) / (2 * k.fMaxDist));
  const size_t n = std::size(Cases);
  a.Test.nTest[0] = (uint32)n;
  a.nGroups[0] = Groups64(n);
  a.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(a);
  std::vector<std::tuple<SVec3, float, SVec3, SVec3, float, float>> Gpus;
  for (size_t i = 0; i < n; i++)
  {
    const SCase &k = Cases[i];
    Gpus.push_back({ Vec(Out, i), k.fSunZ, SVec3{ Decode(2 * k.Sun[0]), Decode(2 * k.Sun[1]), Decode(2 * k.Sun[2]) },
                     SVec3{ Decode(k.Sky[0]), Decode(k.Sky[1]), Decode(k.Sky[2]) }, 0.0f, 1.0f });
  }
  const std::vector<SClear> v = Clear(Gpus);
  for (size_t i = 0; i < n; i++)
  {
    const SCase &k = Cases[i];
    float vSource[3];
    UnderwaterLight::ClearSource(k.Fog, std::log(100.0f) / (2 * k.fMaxDist), k.Sun, k.Sky, k.fSunZ, vSource);
    for (int q = 0; q < 3; q++)
    {
      const float e = Comp(v[i].Source, q);
      CHECK_MSG(e > 0 && std::fabs(vSource[q] - e) <= 2e-5f * e, std::to_string(i) + "/" + std::to_string(q) + ": " +
                                                                     std::to_string(vSource[q]) + " " + std::to_string(e));
    }
  }
}

// Why: under the water the eye takes the light at its depth, which the water has taken its red from first, towards the light
// just under the surface (d3d12_WaterUnderAdapt): the HDR resolve's factors must leave the view through the surface as it is
// (none at the surface, else the picture would jump as the camera crosses it), keep the exposure (the luminance of the light
// at the depth), give that light the surface's colour with all of the balance and part of the way with part of it (von Kries'
// factors raised to the degree), red's factor the most; deeper (Fort's from 5.7 m) the factors no more than BALANCE_MOST
// apart (at 20 m red's would be 53 times: a lamp or the sky in Snell's window red), the luminance still kept; without light
// (no sun, no sky) none (the raw factors would make whatever else lights the water red); the exposure under the water
// (d3d12_WaterUnderExposure) coming in over the first half metre, none at the surface.
XR_TEST(Under_Balance)
{
  // Fort's water (its fog volume, 30 m) and sun (Under_ClearFort's).
  const float Fog[3] = { 0.341f, 0.635f, 0.875f }, Sun[3] = { 0.96f, 0.89f, 0.68f }, Sky[3] = { 0.384f, 0.549f, 0.788f };
  float c[3], vDown[3];
  UnderwaterLight::FogAttenuation(Fog, std::log(100.0f) / 60, c);
  UnderwaterLight::DownLight(Sun, Sky, 0.853f, vDown);
  auto Y = [](const float v[3]) { return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2]; };
  float g[3];
  UnderwaterLight::Balance(c, vDown, 0, 1, g);
  for (int i = 0; i < 3; i++)
    CHECK_NEAR(g[i], 1, 1e-6);
  UnderwaterLight::Balance(c, vDown, 4, 0, g);
  for (int i = 0; i < 3; i++)
    CHECK_NEAR(g[i], 1, 1e-6);
  for (const float fDepth : { 1.0f, 4.0f, 5.5f })
  {
    float vLight[3], vAfter[3], g1[3], gh[3];
    for (int i = 0; i < 3; i++)
      vLight[i] = vDown[i] * std::exp(-c[i] * fDepth);
    UnderwaterLight::Balance(c, vDown, fDepth, 1, g1);
    UnderwaterLight::Balance(c, vDown, fDepth, 0.5f, gh);
    for (int i = 0; i < 3; i++)
      vAfter[i] = vLight[i] * g1[i];
    const std::string d = std::to_string(fDepth) + " m";
    CHECK_MSG(std::fabs(Y(vAfter) / Y(vLight) - 1) < 1e-5f, d);
    for (int i = 0; i < 3; i++)
    {
      CHECK_MSG(std::fabs((vAfter[i] / Y(vAfter)) / (vDown[i] / Y(vDown)) - 1) < 1e-4f, d + " / " + std::to_string(i));
      CHECK_MSG(std::fabs(std::log(gh[i] / gh[1]) - 0.5f * std::log(g1[i] / g1[1])) < 1e-5f, d + " / " + std::to_string(i));
    }
    CHECK_MSG(g1[0] > g1[1] && g1[1] > g1[2] && gh[0] > 1 && gh[2] < 1, d);
  }
  // Deeper the factors BALANCE_MOST apart at most, all of the balance or half of it, the luminance kept, red's still the most.
  for (const float fDepth : { 6.0f, 12.0f, 20.0f, 30.0f })
    for (const float fDegree : { 1.0f, 0.5f })
    {
      float vLight[3], vAfter[3], gd[3];
      for (int i = 0; i < 3; i++)
        vLight[i] = vDown[i] * std::exp(-c[i] * fDepth);
      UnderwaterLight::Balance(c, vDown, fDepth, fDegree, gd);
      for (int i = 0; i < 3; i++)
        vAfter[i] = vLight[i] * gd[i];
      const std::string d = std::to_string(fDepth) + " m, " + std::to_string(fDegree);
      const float fFull = std::exp(fDegree * (c[0] - c[2]) * fDepth);
      CHECK_MSG(std::fabs(gd[0] / gd[2] - std::min(fFull, UnderwaterLight::BALANCE_MOST)) < 1e-3f * gd[0] / gd[2], d);
      CHECK_MSG(std::fabs(Y(vAfter) / Y(vLight) - 1) < 1e-5f, d);
      CHECK_MSG(gd[0] > gd[1] && gd[1] > gd[2], d);
    }
  // Without the light all 1.
  const float vNone[3] = { 0, 0, 0 };
  UnderwaterLight::Balance(c, vNone, 4, 1, g);
  for (int i = 0; i < 3; i++)
    CHECK_NEAR(g[i], 1, 1e-6);
  CHECK_NEAR(UnderwaterLight::UnderExposure(1, 0), 1, 1e-6);
  CHECK_NEAR(UnderwaterLight::UnderExposure(1, 0.25f), std::sqrt(2.0f), 1e-5);
  CHECK_NEAR(UnderwaterLight::UnderExposure(1, 0.5f), 2, 1e-5);
  CHECK_NEAR(UnderwaterLight::UnderExposure(1, 8), 2, 1e-5);
  CHECK_NEAR(UnderwaterLight::UnderExposure(0, 8), 1, 1e-6);
  CHECK_NEAR(UnderwaterLight::UnderExposure(1, -1), 1, 1e-6);
}
