// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterLightTests.cpp
//	Description: Unit tests of the sun's light on water surfaces
//	(Shaders/WaterLight.hlsli on the GPU, TestHeaders.hlsl): the glint's
//	distribution of the facets, its light against the sun's, the slopes
//	finer than a pixel (LEAN) as the facets they average, its lobe, and
//	its cap; the light through the crests and its colour.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_WaterGlint.h"
#include "Test_WaterCap.h"
#include "Test_WaterCrest.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr double PI = 3.14159265358979323846;

  //! The unit normal of a facet of slopes s (dH/dx, dH/dy), as Water.hlsl's WaveNormal makes it.
  SVec3 FacetNormal(double sx, double sy)
  {
    return Normalize(SVec3{ (float)-sx, (float)-sy, 1 });
  }

  //! The direction at elevation e (radians) and azimuth a.
  SVec3 Direction(double e, double a)
  {
    return { (float)(std::cos(e) * std::cos(a)), (float)(std::cos(e) * std::sin(a)), (float)std::sin(e) };
  }

  //! d with the light L mirrored about H.
  SVec3 Mirror(SVec3 L, SVec3 H)
  {
    return H * (2 * Dot(L, H)) - L;
  }

  //! One item of the glint's kernel.
  struct SGlintItem
  {
    SVec3 N, V, L;
    float fVar;
  };

  //! The kernel's (glint, density, masking, fresnel) of each item.
  std::vector<uint32> RunGlint(const std::vector<SGlintItem> &Items)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_WaterGlint);
    d.Test.nTest[0] = (uint32)Items.size();
    d.nGroups[0] = Groups64(Items.size());
    for (const SGlintItem &it : Items)
    {
      Push(d.In, it.N, it.fVar);
      Push(d.In, it.V, 0);
      Push(d.In, it.L, 0);
    }
    d.nOut = Items.size();
    return Gpu().Run(d);
  }

  //! The facets' slopes on a grid of n x n over s0 +- fReach RMS (per axis) of the variance fVar of the two together:
  //! each with its slopes and the area of its cell.
  struct SSlope
  {
    double sx, sy, fArea;
  };
  std::vector<SSlope> SlopeGrid(double s0x, double s0y, double fVar, int n, double fReach)
  {
    const double fSigma = std::sqrt(fVar / 2), fStep = 2 * fReach * fSigma / n;
    std::vector<SSlope> Grid;
    for (int j = 0; j < n; j++)
      for (int i = 0; i < n; i++)
        Grid.push_back({ s0x + (i + 0.5 - n / 2.0) * fStep, s0y + (j + 0.5 - n / 2.0) * fStep, fStep * fStep });
    return Grid;
  }
}

// Why: the glint's light is the sun's mirrored by as many facets as point the right way; the distribution must count
// each facet once (its integral times H.z over the hemisphere 1, about a tilted normal too), else the sun's path
// is brighter or darker with the waves' roughness than the sun's light allows.
XR_TEST(Water_GlintNormalised)
{
  struct SCase
  {
    double s0x, s0y, fVar;
  };
  const SCase Cases[] = { { 0, 0, 0.002 }, { 0, 0, 0.01 }, { 0, 0, 0.05 }, { 0.1, -0.05, 0.004 }, { -0.2, 0.15, 0.03 } };
  for (const SCase &c : Cases)
  {
    // The half vector of each cell: V = L = H.
    const std::vector<SSlope> Grid = SlopeGrid(c.s0x, c.s0y, c.fVar, 96, 6);
    std::vector<SGlintItem> Items;
    const SVec3 N = FacetNormal(c.s0x, c.s0y);
    for (const SSlope &s : Grid)
    {
      const SVec3 H = FacetNormal(s.sx, s.sy);
      Items.push_back({ N, H, H, (float)c.fVar });
    }
    const std::vector<uint32> Out = RunGlint(Items);
    double fSum = 0;
    for (size_t i = 0; i < Grid.size(); i++)
    {
      // dw = cos^3 ds^2, and D H.z.
      const double z = Items[i].V.z;
      fSum += At(Out, i, 1) * z * z * z * z * Grid[i].fArea;
    }
    CHECK_NEAR(fSum, 1.0, 0.02);
  }
}

// Why: the water must not reflect more of the sun than reaches it, and the roughness spreads the light it mirrors
// rather than adds to it: its albedo, the integral of the glint's light over the directions to the camera against
// the sun's light on the plane, is about the fresnel term at the mirror's angle and nearly the same for calm and
// rough water (a little lost to the masking and to directions below the horizon towards a low sun).
XR_TEST(Water_GlintEnergy)
{
  const double Elevations[] = { 60, 30, 15 };
  const double Vars[] = { 0.002, 0.008, 0.03 };
  for (const double fElevation : Elevations)
  {
    const SVec3 L = Direction(fElevation * PI / 180, 0.3);
    double fFirst = 0;
    for (const double fVar : Vars)
    {
      // Over the half vectors (dw_v = 4 (L.H) dw_h, dw_h = H.z^3 ds^2), about the plane's normal.
      const std::vector<SSlope> Grid = SlopeGrid(0, 0, fVar, 128, 6);
      std::vector<SGlintItem> Items;
      std::vector<double> Weights;
      for (const SSlope &s : Grid)
      {
        const SVec3 H = FacetNormal(s.sx, s.sy);
        const SVec3 V = Mirror(L, H);
        if (V.z <= 0)
          continue;
        Items.push_back({ { 0, 0, 1 }, V, L, (float)fVar });
        Weights.push_back(4 * Dot(L, H) * H.z * H.z * H.z * s.fArea);
      }
      const std::vector<uint32> Out = RunGlint(Items);
      double fAlbedo = 0;
      for (size_t i = 0; i < Items.size(); i++)
        fAlbedo += At(Out, i, 0) * Items[i].V.z * Weights[i];
      fAlbedo /= PI * L.z;
      const double c = std::sin(fElevation * PI / 180), m = 1 - c;
      const double fMirror = 0.02 + 0.98 * m * m * m * m * m;
      CHECK(fAlbedo < 1);
      if (fFirst == 0)
      {
        fFirst = fAlbedo;
        CHECK_NEAR(fAlbedo / fMirror, 1.0, 0.1);
      }
      else if (fElevation >= 30)
        CHECK_NEAR(fAlbedo / fFirst, 1.0, 0.15);
      else
        CHECK(fAlbedo <= fFirst * 1.15 && fAlbedo >= fFirst * 0.5);
    }
  }
}

// Why: the glint's roughness is the reflection's LEAN: the slopes the texture's mip levels smoothed out of a pixel are
// facets each mirroring the sun by its own slope, and the glint of their mean normal with their variance added must
// be the mean of their glints, else the sun's path far away (where most slopes are lost) is not what the waves would
// show up close.
XR_TEST(Water_GlintLean)
{
  const double fBase = 0.002, fLost = 0.006;
  const SVec3 L = Direction(25 * PI / 180, 1.1);
  const SVec3 Mirrored = Mirror(L, { 0, 0, 1 });
  // Directions to the camera across the lobe: the mirror and around it.
  std::vector<SVec3> Views;
  for (double de : { -0.12, -0.06, 0.0, 0.06, 0.12 })
    for (double da : { -0.15, 0.0, 0.15 })
    {
      const double e = std::asin((double)Mirrored.z) + de, a = std::atan2((double)Mirrored.y, (double)Mirrored.x) + da;
      Views.push_back(Direction(e, a));
    }
  // The facets: the lost slopes' gaussian on a grid, each cell's weight its share.
  const std::vector<SSlope> Grid = SlopeGrid(0, 0, fLost, 64, 4.5);
  std::vector<double> Share;
  double fTotal = 0;
  for (const SSlope &s : Grid)
  {
    Share.push_back(std::exp(-(s.sx * s.sx + s.sy * s.sy) / fLost) / (PI * fLost) * s.fArea);
    fTotal += Share.back();
  }
  std::vector<SGlintItem> Items;
  for (const SVec3 &V : Views)
  {
    Items.push_back({ { 0, 0, 1 }, V, L, (float)(fBase + fLost) });
    for (const SSlope &s : Grid)
      Items.push_back({ FacetNormal(s.sx, s.sy), V, L, (float)fBase });
  }
  const std::vector<uint32> Out = RunGlint(Items);
  const size_t nPer = 1 + Grid.size();
  double fPeak = 0;
  for (size_t v = 0; v < Views.size(); v++)
    fPeak = std::max(fPeak, (double)At(Out, v * nPer, 0));
  CHECK(fPeak > 0);
  for (size_t v = 0; v < Views.size(); v++)
  {
    const double fLean = At(Out, v * nPer, 0);
    double fMean = 0;
    for (size_t i = 0; i < Grid.size(); i++)
      fMean += At(Out, v * nPer + 1 + i, 0) * Share[i];
    fMean /= fTotal;
    if (fLean > 0.1 * fPeak)
      CHECK_NEAR(fMean / fLean, 1.0, 0.05);
    else
      CHECK_NEAR(fMean, fLean, 0.01 * fPeak);
  }
}

// Why: the glint must be the sun's mirror image spread by the slopes: the facets that mirror it are densest about the
// direction the plane mirrors the sun into, their lobe twice as wide and a quarter as dense for four times the variance;
// the glint itself is brightest a little nearer the horizon (the fresnel term grows towards grazing, and the camera
// sees more of the plane there: 1 / V.z), the more so the rougher the water: the sun's path reaching towards the
// horizon, not up from it. Nothing with the sun or the camera below the horizon.
XR_TEST(Water_GlintPeak)
{
  const double fElevation = 20 * PI / 180;
  const SVec3 L = Direction(fElevation, 0);
  struct SLobe
  {
    double fPeakAt, fPeak, fWidth;
  };
  // Of the kernel's word k (0 the glint, 1 the density) over the camera's elevations in the plane of incidence.
  auto Lobe = [&](double fVar, int k)
  {
    std::vector<SGlintItem> Items;
    std::vector<double> Angles;
    for (double e = 0.5; e <= 70; e += 0.05)
    {
      Angles.push_back(e);
      Items.push_back({ { 0, 0, 1 }, Direction(e * PI / 180, PI), L, (float)fVar });
    }
    const std::vector<uint32> Out = RunGlint(Items);
    SLobe r{ 0, 0, 0 };
    for (size_t i = 0; i < Items.size(); i++)
      if (At(Out, i, k) > r.fPeak)
      {
        r.fPeak = At(Out, i, k);
        r.fPeakAt = Angles[i];
      }
    double fLow = 1e9, fHigh = -1e9;
    for (size_t i = 0; i < Items.size(); i++)
      if (At(Out, i, k) >= r.fPeak / 2)
      {
        fLow = std::min(fLow, Angles[i]);
        fHigh = std::max(fHigh, Angles[i]);
      }
    r.fWidth = fHigh - fLow;
    return r;
  };
  const SLobe da = Lobe(0.002, 1), db = Lobe(0.008, 1);
  CHECK_NEAR(da.fPeakAt, 20, 0.1);
  CHECK_NEAR(db.fPeakAt, 20, 0.1);
  CHECK_NEAR(db.fWidth / da.fWidth, 2, 0.1);
  CHECK_NEAR(db.fPeak / da.fPeak, 0.25, 0.01);
  const SLobe a = Lobe(0.002, 0), b = Lobe(0.008, 0);
  CHECK(a.fPeakAt <= 20 && a.fPeakAt >= 17);
  CHECK(b.fPeakAt < a.fPeakAt && b.fPeakAt >= 10);
  CHECK(b.fPeak < a.fPeak);

  // The sun or the camera below the horizon.
  const std::vector<SGlintItem> Items = { { { 0, 0, 1 }, Direction(fElevation, PI), Direction(-0.01, 0), 0.01f },
                                          { { 0, 0, 1 }, Direction(-0.01, PI), L, 0.01f },
                                          { FacetNormal(0.3, 0), Direction(0.2, PI), Direction(-0.05, 0), 0.05f } };
  const std::vector<uint32> Out = RunGlint(Items);
  for (size_t i = 0; i < Items.size(); i++)
    CHECK_NEAR(At(Out, i, 0), 0, 0);
}

// Why: what mirrors the sun is no brighter than the sun the game draws (its flare, d3d12_HDRSun in the HDR targets),
// and the cap must keep the glint's hue (a sunset's orange stays orange): below it the light as it is.
XR_TEST(Water_GlintCap)
{
  struct SCase
  {
    float In[3], fCap, Out[3];
  };
  const SCase Cases[] = {
    { { 2, 1, 0.5f }, 8, { 2, 1, 0.5f } },
    { { 16, 8, 4 }, 8, { 8, 4, 2 } },
    { { 0.5f, 3, 1.5f }, 1, { 0.5f / 3, 1, 0.5f } },
    { { 0, 0, 0 }, 8, { 0, 0, 0 } },
  };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_WaterCap);
  d.Test.nTest[0] = (uint32)std::size(Cases);
  for (const SCase &c : Cases)
    Push(d.In, c.In[0], c.In[1], c.In[2], c.fCap);
  d.nOut = std::size(Cases);
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < std::size(Cases); i++)
    for (int k = 0; k < 3; k++)
      CHECK_NEAR(At(Out, i, k), Cases[i].Out[k], 1e-6);
}

namespace
{
  //! One item of the crests' kernel.
  struct SCrestItem
  {
    SVec3 N, V, L;
    float fHeight;
    SVec3 Deep;
    float fSlopeRMS = 0.3f;
  };

  //! The kernel's (light, colour) of each item.
  std::vector<uint32> RunCrest(const std::vector<SCrestItem> &Items)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_WaterCrest);
    d.Test.nTest[0] = (uint32)Items.size();
    d.nGroups[0] = Groups64(Items.size());
    for (const SCrestItem &it : Items)
    {
      Push(d.In, it.N, it.fHeight);
      Push(d.In, it.V, it.fSlopeRMS);
      Push(d.In, it.L, 0);
      Push(d.In, it.Deep, 0);
    }
    d.nOut = Items.size();
    return Gpu().Run(d);
  }
}

// Why: the light through a crest is the sun's crossing the thin top of a wave lit from behind; it must come only where
// that happens (looking towards the sun, on a crest, more on its face turned away from the sun than on a level one,
// none on a face turned to the sun), none in the troughs, with the sun behind the camera or set, and grow towards the
// crest and towards the sun, never above the sun's.
XR_TEST(Water_CrestLight)
{
  const double D = PI / 180;
  // The sun at azimuth 0 (+x) 20 degrees up; the camera on the -x side looking at it 10 degrees down; the face sloping
  // up towards the sun (turned towards the camera, away from the sun) by the waves' RMS slope 0.3.
  const SVec3 L = Direction(20 * D, 0), V = Direction(10 * D, PI), Back = FacetNormal(0.3, 0), Flat = { 0, 0, 1 };
  const SVec3 Deep = { 0.18f, 0.35f, 0.57f };
  std::vector<SCrestItem> Items = {
    { Back, V, L, 0.5f, Deep },                     // 0: a low crest against the sun
    { Back, V, Direction(20 * D, PI), 2.5f, Deep }, // 1: the sun behind the camera
    { Back, V, L, 0.0f, Deep },                     // 2: the waves' mean height
    { Back, V, L, -1.0f, Deep },                    // 3: the trough
    { Flat, V, L, 0.5f, Deep },                     // 4: a level face of the low crest
    { Back, V, Direction(-2 * D, 0), 2.5f, Deep },  // 5: the sun set
    { FacetNormal(-0.4, 0), V, L, 2.5f, Deep },     // 6: a face turned to the sun by more than the RMS
    { Back, V, L, 2.0f, Deep },                     // 7: twice the RMS high: all of it, at most 1
    { Back, V, Direction(90 * D, 0), 2.5f, Deep },  // 8: the sun overhead
    { Back, V, L, 2.5f, Deep, 0.0f },               // 9: no waves
    { Back, V, L, 3.5f, Deep },                     // 10: higher as at twice the RMS
  };
  const size_t nHeights = Items.size();
  for (float h : { 0.1f, 0.2f, 0.3f, 0.4f })
    Items.push_back({ Back, V, L, h, Deep });
  const size_t nAzimuths = Items.size();
  for (double a : { 0.0, 20.0, 40.0, 60.0 })
    Items.push_back({ Back, V, Direction(20 * D, a * D), 0.5f, Deep });
  const size_t nSlopes = Items.size();
  for (float s : { 0.6f, 0.45f, 0.3f })
    Items.push_back({ Back, V, L, 0.5f, Deep, s });
  const std::vector<uint32> Out = RunCrest(Items);
  // 0: the closed form: 8 times the crest's smoothstep at a quarter (0.156), the forward cosine 0.866 squared, a half
  // and the face's slope away from the sun (0.287 / 0.958) over twice the RMS 0.3 (0.999), the slant 1 - 0.449: 0.516.
  CHECK(At(Out, 0, 0) > 0.50f && At(Out, 0, 0) < 0.53f);
  // 4: the level face half of that at its slant 1 - 0.174: 0.387.
  CHECK(At(Out, 4, 0) > 0.37f && At(Out, 4, 0) < 0.40f);
  for (size_t i : { 1, 2, 3, 5, 6, 8, 9 })
    CHECK_NEAR(At(Out, i, 0), 0, 0);
  CHECK_NEAR(At(Out, 7, 0), 1, 1e-6);
  CHECK_NEAR(At(Out, 10, 0), 1, 1e-6);
  // The same face against calmer waves is more of a crest's back (relative to their slopes).
  for (size_t i = nSlopes + 1; i < Items.size(); i++)
    CHECK(At(Out, i, 0) > At(Out, i - 1, 0));
  CHECK_NEAR(At(Out, Items.size() - 1, 0), At(Out, 0, 0), 1e-6);
  for (size_t i = nHeights + 1; i < nAzimuths; i++)
    CHECK(At(Out, i, 0) > At(Out, i - 1, 0));
  for (size_t i = nAzimuths + 1; i < nSlopes; i++)
    CHECK(At(Out, i, 0) < At(Out, i - 1, 0));
  for (size_t i = 0; i < Items.size(); i++)
    CHECK(At(Out, i, 0) >= 0 && At(Out, i, 0) <= 1);
}

// Why: the crests' light is turquoise (the water absorbs the red on the way through), and half of it takes the hue of
// the level's deep water so that a lagoon and the open sea keep their own colours: a deep water in the turquoise's
// hue leaves it as it is, no deep colour gives the turquoise, any other lies halfway in hue.
XR_TEST(Water_CrestColor)
{
  const double D = PI / 180;
  const SVec3 L = Direction(20 * D, 0), V = Direction(10 * D, PI), N = FacetNormal(0.3, 0);
  const std::vector<SCrestItem> Items = {
    { N, V, L, 2.5f, { 0.10f, 0.62f, 0.50f } },
    { N, V, L, 2.5f, { 0.05f, 0.31f, 0.25f } },
    { N, V, L, 2.5f, { 0, 0, 0 } },
    { N, V, L, 2.5f, { 0.18f, 0.35f, 0.57f } },
  };
  const std::vector<uint32> Out = RunCrest(Items);
  // WATER_CREST_PEAK (4) times the turquoise.
  const float k = 4;
  for (size_t i = 0; i < 3; i++)
  {
    CHECK_NEAR(At(Out, i, 1), k * 0.10f, 1e-5);
    CHECK_NEAR(At(Out, i, 2), k * 0.62f, 1e-5);
    CHECK_NEAR(At(Out, i, 3), k * 0.50f, 1e-5);
  }
  // The deep blue (0.18, 0.35, 0.57) scaled to 0.62 at its brightest, halfway: (0.148, 0.500, 0.560).
  CHECK_NEAR(At(Out, 3, 1), k * 0.5f * (0.10f + 0.18f * 0.62f / 0.57f), 1e-4);
  CHECK_NEAR(At(Out, 3, 2), k * 0.5f * (0.62f + 0.35f * 0.62f / 0.57f), 1e-4);
  CHECK_NEAR(At(Out, 3, 3), k * 0.5f * (0.50f + 0.62f), 1e-4);
  CHECK(At(Out, 3, 1) < At(Out, 3, 2));
}
