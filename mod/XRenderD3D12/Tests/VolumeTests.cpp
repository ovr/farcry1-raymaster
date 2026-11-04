// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: VolumeTests.cpp
//	Description: Unit tests of the light in the ocean's water
//	(d3d12_WaterVolume, WaterVolume.hlsli): each channel's attenuation from
//	the level's water colour, the light the water scatters against its
//	integral in double, the segments along the view's ray whose sun it
//	takes, the refracted rays (Snell's law), Snell's window from below
//	(Fresnel's equations in double), and the shallows they give: a sandy
//	floor turquoise under Carrier's water, fading into its colour with the
//	depth, yellow-brown under the river's.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_VolumeAttenuation.h"
#include "Test_VolumeInscatter.h"
#include "Test_VolumeSegments.h"
#include "Test_VolumeRay.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr double ETA = 1.333;

  //! A level's fog volume colour (the game's, gamma encoded) in linear light, as Water.hlsl's WaterDecode below 1.
  SVec3 Linear(float r, float g, float b)
  {
    return { std::pow(r, 2.2f), std::pow(g, 2.2f), std::pow(b, 2.2f) };
  }

  //! The fog volume's density: ln 100 / (2 * its distance).
  float Density(float fDistance) { return std::log(100.0f) / (2 * fDistance); }

  const SVec3 CARRIER = Linear(0.180f, 0.353f, 0.573f);
  const float CARRIER_DENSITY = Density(60);
  const SVec3 RIVER = Linear(0.384f, 0.341f, 0.216f);
  const float RIVER_DENSITY = Density(16);

  float Comp(SVec3 v, int c) { return c == 0 ? v.x : c == 1 ? v.y : v.z; }

  std::vector<SVec3> Attenuations(const std::vector<std::pair<SVec3, float>> &Cases)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_VolumeAttenuation);
    for (const auto &c : Cases)
      Push(d.In, c.first, c.second);
    d.Test.nTest[0] = (uint32)Cases.size();
    d.nGroups[0] = Groups64(Cases.size());
    d.nOut = Cases.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SVec3> v;
    for (size_t i = 0; i < Cases.size(); i++)
      v.push_back(Vec(Out, i));
    return v;
  }

  struct SInscatter
  {
    SVec3 Deep, c, Kd;
    float fMu, fT0, fT1;
  };

  std::vector<SVec3> Inscatter(const std::vector<SInscatter> &Cases)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_VolumeInscatter);
    for (const auto &c : Cases)
    {
      Push(d.In, c.Deep, c.fT0);
      Push(d.In, c.c, c.fMu);
      Push(d.In, c.Kd, c.fT1);
    }
    d.Test.nTest[0] = (uint32)Cases.size();
    d.nGroups[0] = Groups64(Cases.size());
    d.nOut = Cases.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SVec3> v;
    for (size_t i = 0; i < Cases.size(); i++)
      v.push_back(Vec(Out, i));
    return v;
  }

  //! The light scattered towards the camera along the ray from t0 to t1 in double (Simpson's rule, 20000 steps): the light
  //! coming down J0 exp(-Kd mu t), J0 = deep (c + Kd), taken by the water on the way back, exp(-c t).
  double InscatterIntegral(double fDeep, double c, double Kd, double mu, double t0, double t1)
  {
    const int n = 20000;
    const double h = (t1 - t0) / n, J0 = fDeep * (c + Kd);
    double s = 0;
    for (int i = 0; i <= n; i++)
    {
      const double t = t0 + i * h, w = (i == 0 || i == n) ? 1 : (i & 1) ? 4 : 2;
      s += w * J0 * std::exp(-Kd * mu * t) * std::exp(-c * t);
    }
    return s * h / 3;
  }

  //! Fresnel's reflectance of unpolarized light from water to air at the cosine ci in the water (1 beyond the critical angle).
  double FresnelWaterToAir(double ci)
  {
    const double s2 = ETA * ETA * (1 - ci * ci);
    if (s2 >= 1)
      return 1;
    const double ct = std::sqrt(1 - s2);
    const double rs = (ETA * ci - ct) / (ETA * ci + ct), rp = (ci - ETA * ct) / (ci + ETA * ct);
    return 0.5 * (rs * rs + rp * rp);
  }
}

// Why: the level's water colour sets how fast each channel fades: its luminance at the fog volume's density (the fog's
// distance is how far one sees), a channel k times as dark k times as fast (deep water's colour is the light it scatters
// back over the light it absorbs), at most 64 times the density, never less than pure water absorbs (Pope and Fry); a
// black colour the density in all three. Carrier's clear lagoon is about as clear as water gets, Bunker's grey-blue sea
// loses green and blue faster than that, the river's brown water loses blue first.
XR_TEST(Volume_Attenuation)
{
  const float Pure[3] = { 0.2644f, 0.0565f, 0.0106f };
  const SVec3 Bunker = Linear(0.306f, 0.353f, 0.467f);
  const std::vector<std::pair<SVec3, float>> Cases = { { CARRIER, CARRIER_DENSITY }, { RIVER, RIVER_DENSITY }, { Bunker, Density(20) },
                                                       { { 0, 0.1f, 0.2f }, 1 }, { { 0, 0, 0 }, 0.2f }, { { 0.3f, 0.3f, 0.3f }, 0.5f } };
  const std::vector<SVec3> v = Attenuations(Cases);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SVec3 C = Cases[i].first;
    const float Y = 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z, fDensity = Cases[i].second;
    for (int k = 0; k < 3; k++)
    {
      const float a = Comp(v[i], k), e = std::max(Y > 0 ? fDensity * Y / std::max(Comp(C, k), Y / 64) : fDensity, Pure[k]);
      CHECK_MSG(std::fabs(a - e) <= 1e-5f * e, std::to_string(i) + "/" + std::to_string(k) + ": " + std::to_string(a) + " against " + std::to_string(e));
    }
  }
  // Carrier's: red and green pure water's, blue a little over it (clear ocean water's, 0.0128); Bunker's: green and blue
  // over pure water's, blue the slowest; the river's: blue faster than green.
  CHECK_NEAR(v[0].x, Pure[0], 1e-6);
  CHECK_NEAR(v[0].y, Pure[1], 1e-6);
  CHECK(v[0].z > Pure[2] && v[0].z < 1.5f * Pure[2]);
  CHECK(v[2].y > 1.5f * Pure[1] && v[2].z > 3 * Pure[2] && v[2].z < v[2].y && v[2].y < v[2].x);
  CHECK(v[1].z > v[1].y && v[1].y > 2 * Pure[1]);
  CHECK_NEAR(v[3].x, 64, 1e-4);
  CHECK_NEAR(v[4].x, 0.26440, 1e-6);
  CHECK_NEAR(v[4].y, 0.2, 1e-7);
  CHECK_NEAR(v[5].y, 0.5, 1e-6);
}

// Why: the light the water scatters towards the camera is the single scattering of the light coming down (fading with the
// depth) taken by the water on the way back: the closed form must match its integral, deep water seen straight down must be
// its colour, the segments must add up to the whole ray, and a slanting view must see 2 / (1 + mu) of it (the light it
// scatters comes from shallower water).
XR_TEST(Volume_Inscatter)
{
  const SVec3 Deep = CARRIER;
  const SVec3 c = { 0.5f, 0.12f, 0.04f }, Half = c * 0.5f;
  const float Mus[] = { 1, 0.8f, 0.6614f };
  const float Ranges[][2] = { { 0, 1 }, { 0.5f, 3 }, { 2, 40 }, { 0, 200 } };
  std::vector<SInscatter> Cases;
  for (const SVec3 &Kd : { c, Half })
    for (float mu : Mus)
      for (const auto &r : Ranges)
        Cases.push_back({ Deep, c, Kd, mu, r[0], r[1] });
  const size_t nIntegral = Cases.size();
  // Deep water: straight down, slanting; the sum of two segments.
  for (float mu : Mus)
    Cases.push_back({ Deep, c, c, mu, 0, 1e5f });
  Cases.push_back({ Deep, c, c, 0.8f, 0, 1.7f });
  Cases.push_back({ Deep, c, c, 0.8f, 1.7f, 9 });
  Cases.push_back({ Deep, c, c, 0.8f, 0, 9 });
  const std::vector<SVec3> v = Inscatter(Cases);
  double fWorst = 0;
  for (size_t i = 0; i < nIntegral; i++)
    for (int k = 0; k < 3; k++)
    {
      const SInscatter &s = Cases[i];
      const double e = InscatterIntegral(Comp(s.Deep, k), Comp(s.c, k), Comp(s.Kd, k), s.fMu, s.fT0, s.fT1);
      fWorst = std::max(fWorst, std::fabs(Comp(v[i], k) - e) / std::max(e, 1e-6));
    }
  CHECK_MSG(fWorst < 1e-4, "the closed form against the integral: " + std::to_string(fWorst));
  for (int m = 0; m < 3; m++)
    for (int k = 0; k < 3; k++)
      CHECK_NEAR(Comp(v[nIntegral + m], k), Comp(Deep, k) * 2 / (1 + Mus[m]), 1e-5 * Comp(Deep, k));
  const size_t j = nIntegral + 3;
  for (int k = 0; k < 3; k++)
    CHECK_NEAR(Comp(v[j], k) + Comp(v[j + 1], k), Comp(v[j + 2], k), 1e-6);
}

// Why: the sun's shadow in the light the water scatters is taken at a point of each of 4 segments along the view's ray:
// they must cover it from the surface out to where 95 % of the least attenuated channel's light comes from (or to the scene
// behind, nearer), shorter near the surface, each point inside its segment.
XR_TEST(Volume_Segments)
{
  const float Cases[][4] = { { 1, 0.2f, 0.07f, 1e4f }, { 1, 0.2f, 0.07f, 5 }, { 0.3f, 0.3f, 0.3f, 1e9f }, { 0, 0, 0, 2 } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_VolumeSegments);
  for (const auto &c : Cases)
    Push(d.In, c[0], c[1], c[2], c[3]);
  const size_t n = std::size(Cases);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  d.nOut = 3 * n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
  {
    const float kMin = std::max(std::min(Cases[i][0], std::min(Cases[i][1], Cases[i][2])), 1e-4f);
    const float fReach = At(Out, 3 * i, 0), e = std::min(Cases[i][3], 3 / kMin);
    CHECK_NEAR(fReach, e, 1e-5 * e);
    CHECK(At(Out, 3 * i, 1) == 4);
    float fPrev = 0;
    for (int j = 0; j < 4; j++)
    {
      const float fEnd = At(Out, 3 * i + 1, j), fPoint = At(Out, 3 * i + 2, j);
      CHECK_MSG(fEnd > fPrev && fPoint > fPrev && fPoint < fEnd, std::to_string(i) + "/" + std::to_string(j));
      if (j > 0)
        CHECK(fEnd - fPrev > At(Out, 3 * i + 1, 0) * 0.99f);
      fPrev = fEnd;
    }
    CHECK_NEAR(fPrev, fReach, 1e-6 * fReach);
  }
  // Out to the reach of a deep ray: 1 - exp(-3) of the least attenuated channel's light.
  CHECK_NEAR(1 - std::exp(-0.07 * At(Out, 0, 0)), 1 - std::exp(-3.0), 1e-5);
}

// Why: the view's ray bends into the water and the sun's ray to a point under the surface came down bent (Snell's law at
// 1.333); from below, the surface lets the light from the air through Snell's window, as Fresnel's equations do (98 %
// straight up), and none beyond the critical angle (48.6 degrees: the surface mirrors the water).
XR_TEST(Volume_Ray)
{
  struct SCase
  {
    SVec3 V, N;
    float fDepth, fCos;
  };
  std::vector<SCase> Cases;
  const SVec3 Up = { 0, 0, 1 }, Tilt = Normalize(SVec3{ 0.15f, -0.1f, 1 });
  const float Angles[] = { 0, 30, 60, 85, 89.9f };
  const float Cosines[] = { 1, 0.9f, 0.8f, 0.7f, 0.665f, 0.6614f, 0.66f, 0.3f, 0 };
  for (int t = 0; t < 2; t++)
    for (size_t a = 0; a < std::size(Angles); a++)
    {
      const float r = Angles[a] * 3.14159265f / 180;
      const SVec3 V = Normalize(SVec3{ std::sin(r) * 0.8f, std::sin(r) * 0.6f, std::cos(r) });
      // The sun in the normal's slot for VolumeSunEntry: the same direction (up), the point under the surface.
      Cases.push_back({ V, t ? Tilt : Up, 2.5f, Cosines[a + t * 4] });
    }
  // A facet turned away from the camera: refracted as if the view grazed it.
  Cases.push_back({ Normalize(SVec3{ 1, 0, 0.05f }), Normalize(SVec3{ -0.5f, 0, 1 }), 1, 0.5f });
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_VolumeRay);
  for (const SCase &c : Cases)
  {
    Push(d.In, c.V, c.fCos);
    Push(d.In, c.N, c.fDepth);
  }
  d.Test.nTest[0] = (uint32)Cases.size();
  d.nGroups[0] = Groups64(Cases.size());
  d.nOut = 2 * Cases.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SCase &c = Cases[i];
    const SVec3 T = Vec(Out, 2 * i);
    // A unit vector on the far side of the surface, in the plane of V and N, at Snell's angle.
    CHECK_NEAR(Length(T), 1, 1e-5);
    const double ci = std::max((double)Dot(c.N, c.V), 0.0), si = std::sqrt(1 - ci * ci);
    const double ct = -Dot(T, c.N), st = std::sqrt(std::max(0.0, 1 - ct * ct));
    CHECK_MSG(ct > 0 && std::fabs(st - si / ETA) < 1e-5, std::to_string(i) + ": " + std::to_string(st) + " against " + std::to_string(si / ETA));
    const SVec3 Side = { c.V.y * c.N.z - c.V.z * c.N.y, c.V.z * c.N.x - c.V.x * c.N.z, c.V.x * c.N.y - c.V.y * c.N.x };
    CHECK_NEAR(Dot(T, Side), 0, 1e-5);
    // The refracted cosine to the vertical of the view's z.
    CHECK_NEAR(At(Out, 2 * i, 3), std::sqrt(1 - (1 - (double)c.V.z * c.V.z) / (ETA * ETA)), 1e-6);
    // The sun's entry: the point (V) moved up by the depth along the sun's ray (N) refracted.
    const SVec3 E = Vec(Out, 2 * i + 1), D = E - c.V;
    CHECK_NEAR(D.z, c.fDepth, 1e-5);
    const SVec3 S = Normalize(D);
    const double fSunSin = std::sqrt(1 - (double)c.N.z * c.N.z), fInSin = std::sqrt((double)S.x * S.x + (double)S.y * S.y);
    CHECK_NEAR(fInSin, fSunSin / ETA, 1e-5);
    // Snell's window (just inside the critical angle the transmission falls steeply: a float's rounding of the cosine
    // moves it by 1e-5).
    CHECK_NEAR(At(Out, 2 * i + 1, 3), 1 - FresnelWaterToAir(c.fCos), c.fCos < 0.67f ? 3e-5 : 2e-6);
  }
  CHECK_NEAR(At(Out, 1, 3), 1 - std::pow((ETA - 1) / (ETA + 1), 2), 1e-6);
  // Straight down the refracted ray is the view's; at grazing 0.661 (the critical angle's cosine).
  CHECK_NEAR(At(Out, 0, 3), 1, 1e-7);
  CHECK_NEAR(At(Out, 8, 3), std::sqrt(1 - 1 / (ETA * ETA)), 1e-3);
}

// Why: what the step is for: under Carrier's water a sandy floor turns aqua and then turquoise in the shallows (red fades
// first; the floor seen straight down with the sun overhead: the light down to it and back up), darker with the depth and
// fading into the water's colour, while the river's brown water turns it yellow-brown (blue fades first).
XR_TEST(Volume_Shallows)
{
  const SVec3 Sand = { 0.45f, 0.40f, 0.30f };
  const float Depths[] = { 0.5f, 1, 2, 4, 8, 400 };
  const std::vector<SVec3> c = Attenuations({ { CARRIER, CARRIER_DENSITY }, { RIVER, RIVER_DENSITY } });
  std::vector<SInscatter> Cases;
  for (int w = 0; w < 2; w++)
    for (float fDepth : Depths)
      Cases.push_back({ w ? RIVER : CARRIER, c[w], c[w], 1, 0, fDepth });
  const std::vector<SVec3> In = Inscatter(Cases);
  const size_t n = std::size(Depths);
  std::vector<SVec3> Seen;
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const float d = Cases[i].fT1;
    const SVec3 &a = c[i / n];
    Seen.push_back(SVec3{ Sand.x * std::exp(-2 * a.x * d), Sand.y * std::exp(-2 * a.y * d), Sand.z * std::exp(-2 * a.z * d) } + In[i]);
  }
  // Carrier: at half a metre the sand's red still more than half; at 2 m green over red twice as much as the sand's (aqua),
  // at 4 m green and blue more than 2.5 times red (turquoise); its brightness falling with the depth; deep down the
  // water's colour.
  CHECK_MSG(Seen[0].x > 0.5f * Sand.x, std::to_string(Seen[0].x));
  CHECK_MSG(Seen[2].y / Seen[2].x > 2 * Sand.y / Sand.x, std::to_string(Seen[2].x) + " " + std::to_string(Seen[2].y));
  CHECK_MSG(Seen[3].y > 2.5f * Seen[3].x && Seen[3].z > 2.5f * Seen[3].x, std::to_string(Seen[3].x) + " " + std::to_string(Seen[3].y) + " " + std::to_string(Seen[3].z));
  for (size_t i = 1; i < n; i++)
    CHECK(Seen[i].x + Seen[i].y + Seen[i].z < Seen[i - 1].x + Seen[i - 1].y + Seen[i - 1].z);
  for (int k = 0; k < 3; k++)
    CHECK_NEAR(Comp(Seen[n - 1], k), Comp(CARRIER, k), 0.01 * Comp(CARRIER, k) + 1e-4);
  // The river: at 2 m red over blue by more than the sand's ratio.
  CHECK_MSG(Seen[n + 2].x / Seen[n + 2].z > 1.5f * Sand.x / Sand.z, std::to_string(Seen[n + 2].x / Seen[n + 2].z));
}
