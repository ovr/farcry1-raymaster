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
//	changes across the pixel, bounded; the reflections by rays of
//	d3d12_RTPbrReflections (CSPbrReflect): PbrReflectDir's mirror, above
//	the surface, spreading with alpha; PbrEnvBrdf's share and
//	PbrReflectWeight's fade, a dielectric's sooner than a metal's.
//	d3d12_RTPbrScreen (CSPbrPrev): a point's place in the last frame's
//	picture with its jitter, the depth buffer's distance, the share where
//	that frame saw it, hidden, uncovered and at the picture's sides.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_PbrSpecLobe.h"
#include "Test_PbrReflect.h"
#include "Test_PbrPrev.h"
#include "Test_PbrQuad.h"
#include "Test_PbrQuadShares.h"
#include "Test_PbrSpecOcclusion.h"

#include <algorithm>
#include <array>
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


namespace
{
  struct SVec
  {
    double x, y, z;
  };
  SVec Normalized(SVec v)
  {
    const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / l, v.y / l, v.z / l };
  }
  double Dot(SVec a, SVec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  //! The view vToEye reflected about vN (both unit): the mirror's direction.
  SVec Mirror(SVec v, SVec n)
  {
    const double d = 2 * Dot(v, n);
    return { d * n.x - v.x, d * n.y - v.y, d * n.z - v.z };
  }

  //! An item of CSPbrReflect.
  struct SReflectIn
  {
    SVec vToEye;
    float fAlpha;
    SVec vN;
    float fRough;
    float u0, u1, fNV;
    float fMetal = 1;
  };
  struct SReflectOut
  {
    SVec vR;
    float fWeight, fScale, fBias;
  };

  std::vector<SReflectOut> RunReflect(const std::vector<SReflectIn> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_PbrReflect);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SReflectIn &r : In)
    {
      Push(d.In, (float)r.vToEye.x, (float)r.vToEye.y, (float)r.vToEye.z, r.fAlpha);
      Push(d.In, (float)r.vN.x, (float)r.vN.y, (float)r.vN.z, r.fRough);
      Push(d.In, r.u0, r.u1, r.fNV, r.fMetal);
    }
    d.nOut = 2 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SReflectOut> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ { At(Out, 2 * i, 0), At(Out, 2 * i, 1), At(Out, 2 * i, 2) }, At(Out, 2 * i, 3), At(Out, 2 * i + 1, 0), At(Out, 2 * i + 1, 1) });
    return R;
  }

  //! Some shading normals and views: tilted, either sign of z (the surface's frame's two branches), grazing.
  const SVec s_vNormals[] = { { 0, 0, 1 }, { 0.3, -0.5, 0.81 }, { 0, 0, -1 }, { -0.6, 0.2, -0.77 }, { 1, 0, 0 } };
  const SVec s_vViews[] = { { 0, 0, 1 }, { 0.5, 0.1, 0.86 }, { -0.9, 0.3, 0.3 }, { 0.2, -0.95, 0.24 } };

  //! A view on the normal's side (flipped where it is behind it).
  SVec Facing(SVec v, SVec n)
  {
    v = Normalized(v), n = Normalized(n);
    return Dot(v, n) > 0.05 ? v : Normalized({ v.x + 2 * n.x, v.y + 2 * n.y, v.z + 2 * n.z });
  }
}

// Why: a mirror (the least alpha) must send its ray where the view's mirror image is, whatever way the normal faces
// (the surface's frame has a branch on the normal's z), or a smooth metal reflects the wrong part of the room.
XR_TEST(Pbr_ReflectMirror)
{
  std::vector<SReflectIn> In;
  for (SVec n : s_vNormals)
    for (SVec v : s_vViews)
      In.push_back({ Facing(v, n), 0.0f, Normalized(n), 0.1f, 0.37f, 0.81f, 0.5f });
  const std::vector<SReflectOut> R = RunReflect(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    const SVec m = Mirror(In[i].vToEye, In[i].vN);
    CHECK(Dot(Normalized(R[i].vR), m) > 0.999);
  }
}

// Why: a ray under the surface would hit the surface's own back or the floor beneath it: every direction must leave on
// the normal's side, unit long, for any alpha and numbers.
XR_TEST(Pbr_ReflectAbove)
{
  std::vector<SReflectIn> In;
  for (SVec n : s_vNormals)
    for (SVec v : s_vViews)
      for (float a : { 0.05f, 0.3f, 0.8f })
        for (int k = 0; k < 16; k++)
          In.push_back({ Facing(v, n), a, Normalized(n), 0.5f, (k % 4 + 0.5f) / 4, (k / 4 + 0.5f) / 4, 0.5f });
  const std::vector<SReflectOut> R = RunReflect(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK(Dot(R[i].vR, In[i].vN) > 0);
    CHECK_NEAR(Dot(R[i].vR, R[i].vR), 1.0, 1e-3);
  }
}

// Why: the rougher the surface, the wider its reflection's rays spread about the mirror's direction (the blur), and a
// smooth one's rays must keep near it: the mean of their angle to the mirror grows with alpha.
XR_TEST(Pbr_ReflectSpread)
{
  const SVec n = Normalized({ 0.1, 0.2, 0.97 }), v = Normalized({ 0.4, -0.3, 0.87 });
  const float vAlpha[] = { 0.05f, 0.2f, 0.5f };
  std::vector<SReflectIn> In;
  for (float a : vAlpha)
    for (int k = 0; k < 64; k++)
      In.push_back({ v, a, n, 0.5f, (k % 8 + 0.5f) / 8, (k / 8 + 0.5f) / 8, 0.5f });
  const std::vector<SReflectOut> R = RunReflect(In);
  const SVec m = Mirror(v, n);
  double vMean[3] = { 0, 0, 0 };
  for (size_t i = 0; i < In.size(); i++)
    vMean[i / 64] += std::acos(std::min(1.0, Dot(Normalized(R[i].vR), m))) / 64;
  CHECK(vMean[0] < 0.2);   // about 2.7 alpha for GGX's long tails (0.135 at 0.05)
  CHECK(vMean[0] < vMean[1]);
  CHECK(vMean[1] < vMean[2]);
}

// Why: the share a surface reflects over its lobe (Karis's fit) must be the split sum's: about F0 head on for a mirror
// (scale near 1, bias near 0), more towards grazing, never above 1 for F0 1, and a metal's reflection must fade out over
// the rough (none beyond 0.65, all up to 0.45).
XR_TEST(Pbr_EnvBrdf)
{
  std::vector<SReflectIn> In;
  const float vRough[] = { 0.0f, 0.3f, 0.45f, 0.55f, 0.65f, 0.9f };
  const float vNV[] = { 1.0f, 0.5f, 0.1f };
  for (float r : vRough)
    for (float nv : vNV)
      In.push_back({ { 0, 0, 1 }, 0.1f, { 0, 0, 1 }, r, 0.5f, 0.5f, nv });
  const std::vector<SReflectOut> R = RunReflect(In);
  CHECK_NEAR(R[0].fScale, 0.994f, 0.01f);   // a mirror head on
  CHECK_NEAR(R[0].fBias, 0.006f, 0.01f);
  CHECK(R[2].fBias > R[0].fBias);           // ... more at grazing
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK(R[i].fScale >= 0 && R[i].fBias >= 0);
    CHECK(R[i].fScale + R[i].fBias <= 1.001f);
  }
  CHECK_NEAR(R[0].fWeight, 1.0f, 1e-6f);    // 0
  CHECK_NEAR(R[2 * 3].fWeight, 1.0f, 1e-6f);  // 0.45
  CHECK_NEAR(R[3 * 3].fWeight, 0.5f, 1e-3f);  // 0.55
  CHECK_NEAR(R[4 * 3].fWeight, 0.0f, 1e-6f);  // 0.65
  CHECK_NEAR(R[5 * 3].fWeight, 0.0f, 1e-6f);  // 0.9
}

// Why: a dielectric reflects 4 % head on: a ray is worth it on the glossy alone (all up to 0.2, none beyond 0.35), or the
// network's roughness of about 0.45 on feathers, cloth and stone gives them rays they barely show; between a dielectric
// and a metal by the metalness, and never more with more roughness.
XR_TEST(Pbr_ReflectWeight)
{
  std::vector<SReflectIn> In;
  const float vRough[] = { 0.0f, 0.2f, 0.275f, 0.35f, 0.45f, 0.55f, 0.65f, 0.9f };
  const float vMetal[] = { 0.0f, 0.5f, 1.0f };
  for (float m : vMetal)
    for (float r : vRough)
    {
      SReflectIn x = { { 0, 0, 1 }, 0.1f, { 0, 0, 1 }, r, 0.5f, 0.5f, 1.0f };
      x.fMetal = m;
      In.push_back(x);
    }
  const std::vector<SReflectOut> R = RunReflect(In);
  const size_t n = std::size(vRough);
  CHECK_NEAR(R[1].fWeight, 1.0f, 1e-6f);     // a dielectric at 0.2
  CHECK_NEAR(R[2].fWeight, 0.5f, 1e-3f);     // ... 0.275
  CHECK_NEAR(R[3].fWeight, 0.0f, 1e-6f);     // ... 0.35
  CHECK_NEAR(R[2 * n + 4].fWeight, 1.0f, 1e-6f);  // a metal at 0.45
  CHECK_NEAR(R[2 * n + 5].fWeight, 0.5f, 1e-3f);  // ... 0.55
  CHECK_NEAR(R[2 * n + 6].fWeight, 0.0f, 1e-6f);  // ... 0.65
  for (size_t m = 0; m < std::size(vMetal); m++)
    for (size_t i = 1; i < n; i++)
      CHECK(R[m * n + i].fWeight <= R[m * n + i - 1].fWeight);
  for (size_t i = 0; i < n; i++)
  {
    CHECK(R[n + i].fWeight >= R[i].fWeight);   // half a metal between
    CHECK(R[n + i].fWeight <= R[2 * n + i].fWeight);
  }
}

namespace
{
  //! An item of CSPbrPrev.
  struct SPrevIn
  {
    float c[4];
    float fJx, fJy, fDepth, m22, m32, fSeen, fWidth;
  };
  struct SPrevOut
  {
    float u, v, fW, fDistance, fShare;
  };

  std::vector<SPrevOut> RunPrev(const std::vector<SPrevIn> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_PbrPrev);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SPrevIn &r : In)
    {
      Push(d.In, r.c[0], r.c[1], r.c[2], r.c[3]);
      Push(d.In, r.fJx, r.fJy, r.fDepth, r.m22);
      Push(d.In, r.m32, r.fSeen, r.fWidth, 0.0f);
    }
    d.nOut = 2 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SPrevOut> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ At(Out, 2 * i, 0), At(Out, 2 * i, 1), At(Out, 2 * i, 2), At(Out, 2 * i, 3), At(Out, 2 * i + 1, 0) });
    return R;
  }

  //! The game's projection (row vectors, the camera down -z): the near 0.25 m, the far 65 m (Treehouse's lab: m22
  //! -1.00386, m32 -0.250965), 90 degrees across 16:9.
  const double s_fNear = 0.25, s_fFar = 65, s_m00 = 1 / (16.0 / 9), s_m11 = 1;
  const double s_m22 = s_fFar / (s_fNear - s_fFar), s_m32 = s_fNear * s_fFar / (s_fNear - s_fFar);

  //! The item of a point (x, y, z) in the view (z < 0 in front) with the jitter (jx, jy) in clip space, the depth buffer
  //! holding the point's own depth, seen fSeen along the view (0: the point's own distance).
  SPrevIn PrevOf(double x, double y, double z, float jx, float jy, double fSeen = 0, float fWidth = 0)
  {
    SPrevIn r = {};
    r.c[0] = (float)(x * s_m00);
    r.c[1] = (float)(y * s_m11);
    r.c[2] = (float)(z * s_m22 + s_m32);
    r.c[3] = (float)-z;
    r.fJx = jx;
    r.fJy = jy;
    r.fDepth = (float)((z * s_m22 + s_m32) / -z);
    r.m22 = (float)s_m22;
    r.m32 = (float)s_m32;
    r.fSeen = (float)(fSeen > 0 ? fSeen : -z);
    r.fWidth = fWidth;
    return r;
  }
}

// Why: a ray's hit takes the last frame's light from where that frame drew it: its uv must be the projection's with that
// frame's jitter (else the reflection shifts by up to a pixel and a lamp's edge smears), and the depth buffer's value must
// give the distance back, near and far (else the test below takes a visible point for a hidden one).
XR_TEST(Pbr_PrevReproject)
{
  const double vPoints[][3] = { { 0, 0, -0.3 }, { 0.4, -0.2, -1 }, { -3, 2, -5 }, { 10, -4, -20 }, { -20, 30, -60 } };
  const float vJitter[][2] = { { 0, 0 }, { 0.0004f, -0.0007f } };
  std::vector<SPrevIn> In;
  for (const auto &p : vPoints)
    for (const auto &j : vJitter)
      In.push_back(PrevOf(p[0], p[1], p[2], j[0], j[1]));
  const std::vector<SPrevOut> R = RunPrev(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    const double w = In[i].c[3];
    const double u = 0.5 + 0.5 * (In[i].c[0] / w + In[i].fJx), v = 0.5 - 0.5 * (In[i].c[1] / w + In[i].fJy);
    CHECK_NEAR(R[i].u, u, 1e-5);
    CHECK_NEAR(R[i].v, v, 1e-5);
    CHECK_NEAR(R[i].fW, w, 1e-5 * w);
    CHECK_NEAR(R[i].fDistance, w, 1e-3 * w);   // the depth's float near the far: 0.002 m at 60 m
  }
  CHECK(R[3].u > R[2].u);                       // the jitter to the right: more u
  CHECK(R[3].v > R[2].v);                       // ... down in clip space: more v
}

// Why: where the last frame saw the hit, its light is all of the reflection's; where something else was there (the hit
// hidden behind a nearer surface, or uncovered since: the picture farther), behind that camera or off its picture,
// none, or a wall's light shows in a floor's reflection of the room behind it; at the picture's sides it fades, no seam.
XR_TEST(Pbr_PrevShare)
{
  std::vector<SPrevIn> In;
  In.push_back(PrevOf(0.5, 0.2, -4, 0, 0));                 // 0 seen
  In.push_back(PrevOf(0.5, 0.2, -4, 0, 0, 4 * 1.029));      // 1 within 3 %
  In.push_back(PrevOf(0.5, 0.2, -4, 0, 0, 4 * 0.9));        // 2 hidden
  In.push_back(PrevOf(0.5, 0.2, -4, 0, 0, 4 * 1.5));        // 3 uncovered
  In.push_back(PrevOf(0.5, 0.2, -4, 0, 0, 4 * 1.2, 0.9f));  // 4 within the cone's width
  In.push_back(PrevOf(0, 0, -0.5, 0, 0, 0.5 + 0.049));      // 5 the least tolerance near
  In.push_back(PrevOf(0.5, 0.2, 4, 0, 0));                  // 6 behind the camera
  In.push_back(PrevOf(8, 0, -4, 0, 0));                     // 7 off the picture
  In.push_back(PrevOf(4 * 16.0 / 9 * 0.98, 0, -4, 0, 0));   // 8 1 % of the picture from its side
  const std::vector<SPrevOut> R = RunPrev(In);
  CHECK_NEAR(R[0].fShare, 1.0f, 1e-6f);
  CHECK_NEAR(R[1].fShare, 1.0f, 1e-6f);
  CHECK_NEAR(R[2].fShare, 0.0f, 1e-6f);
  CHECK_NEAR(R[3].fShare, 0.0f, 1e-6f);
  CHECK_NEAR(R[4].fShare, 1.0f, 1e-6f);
  CHECK_NEAR(R[5].fShare, 1.0f, 1e-6f);
  CHECK_NEAR(R[6].fShare, 0.0f, 1e-6f);
  CHECK_NEAR(R[6].fW, 0.0f, 1e-6f);
  CHECK_NEAR(R[7].fShare, 0.0f, 1e-6f);
  CHECK_NEAR(R[8].u, 0.99f, 1e-4f);
  CHECK_NEAR(R[8].fShare, 0.2f, 1e-3f);                     // (1 - 0.99) / 0.05
}

namespace
{
  //! CSPbrQuad of the items (mask, start, quad x, quad y): the lane, the starts at the frames 0 to 4.
  struct SQuadOut
  {
    int nLane;
    int nStart[5];
  };

  std::vector<SQuadOut> RunQuad(const std::vector<std::array<float, 4>> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_PbrQuad);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const auto &r : In)
      Push(d.In, r[0], r[1], r[2], r[3]);
    d.nOut = 2 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SQuadOut> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ (int)At(Out, 2 * i, 0),
                    { (int)At(Out, 2 * i, 1), (int)At(Out, 2 * i, 2), (int)At(Out, 2 * i, 3), (int)At(Out, 2 * i + 1, 0), (int)At(Out, 2 * i + 1, 1) } });
    return R;
  }
}

//! d3d12_RTPbrQuad: the lane tracing the quad's ray is the first of those that reflect from the start on, wrapping;
//! none (4) where none of them does.
XR_TEST(Pbr_QuadLane)
{
  std::vector<std::array<float, 4>> In;
  for (int m = 0; m < 16; m++)
    for (int k = 0; k < 4; k++)
      In.push_back({ (float)m, (float)k, 0, 0 });
  const std::vector<SQuadOut> R = RunQuad(In);
  for (int m = 0; m < 16; m++)
    for (int k = 0; k < 4; k++)
    {
      int nWant = 4;
      for (int i = 0; i < 4 && nWant == 4; i++)
        if (m & (1 << ((k + i) & 3)))
          nWant = (k + i) & 3;
      CHECK(R[m * 4 + k].nLane == nWant);
    }
}

//! Each quad's starts over 4 frames are its 4 lanes (0 then 3, the diagonal, at the quad (0, 0)), again from the fifth
//! frame; the quads beside it and below it start with another lane in the same frame.
XR_TEST(Pbr_QuadStart)
{
  std::vector<std::array<float, 4>> In;
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++)
      In.push_back({ 0, 0, (float)x, (float)y });
  const std::vector<SQuadOut> R = RunQuad(In);
  CHECK(R[0].nStart[0] == 0);
  CHECK(R[0].nStart[1] == 3);
  for (int i = 0; i < 16; i++)
  {
    int nSeen = 0;
    for (int f = 0; f < 4; f++)
    {
      CHECK(R[i].nStart[f] >= 0 && R[i].nStart[f] < 4);
      nSeen |= 1 << R[i].nStart[f];
    }
    CHECK(nSeen == 15);
    CHECK(R[i].nStart[4] == R[i].nStart[0]);
    const int x = i % 4, y = i / 4;
    if (x < 3)
      CHECK(R[i].nStart[0] != R[i + 1].nStart[0]);
    if (y < 3)
      CHECK(R[i].nStart[0] != R[i + 4].nStart[0]);
  }
}

//! A pixel takes the quad's ray where its mirror direction is within PBR_QUAD_ANGLE (0.03 rad) and half its alpha of the
//! lane's: not across a smooth metal's bumps, across a rough one's lobe.
XR_TEST(Pbr_QuadShares)
{
  // (the angle apart in degrees, alpha, shares)
  const struct { double fDegrees, fAlpha; bool bShares; } Cases[] = {
    { 0, 0, true }, { 1.5, 0, true }, { 2, 0, false }, { 5, 0.12, true }, { 6, 0.12, false }, { 11, 0.36, true }, { 13, 0.36, false },
    { 90, 1, false }, { 180, 0.12, false } };
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_PbrQuadShares);
  const size_t n = sizeof(Cases) / sizeof(Cases[0]);
  d.Test.nTest[0] = (uint32)n;
  d.nGroups[0] = Groups64(n);
  for (const auto &c : Cases)
  {
    // the mirror direction tilted from (0.6, 0, 0.8) about y
    const double a = c.fDegrees * 3.14159265358979 / 180, x = 0.6, z = 0.8;
    Push(d.In, (float)x, 0.0f, (float)z, (float)c.fAlpha);
    Push(d.In, (float)(x * std::cos(a) + z * std::sin(a)), 0.0f, (float)(-x * std::sin(a) + z * std::cos(a)), 0.0f);
  }
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
    CHECK_MSG((At(Out, i, 0) != 0) == Cases[i].bShares, std::to_string(Cases[i].fDegrees) + " degrees, alpha " + std::to_string(Cases[i].fAlpha));
}

namespace
{
  //! PbrSpecOcclusion of (N.V, the ambient occlusion, alpha) triples on the GPU.
  std::vector<float> SpecOcclusion(const std::vector<std::array<float, 3>> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_PbrSpecOcclusion);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const auto &c : In)
      Push(d.In, c[0], c[1], c[2], 0.0f);
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<float> r;
    for (size_t i = 0; i < In.size(); i++)
      r.push_back(At(Out, i, 0));
    return r;
  }

  //! Lagarde and de Rousiers 2014's specular occlusion in double precision.
  double SpecOcclusionRef(double fNV, double fAO, double fAlpha)
  {
    return std::clamp(std::pow(fNV + fAO, std::exp2(-16 * fAlpha - 1)) - 1 + fAO, 0.0, 1.0);
  }
}

XR_TEST(Pbr_SpecOcclusion)
{
  // The reference, nothing occluded (1), a closed texel (0 from the side), a rough lobe about the occlusion, a narrow
  // one seen from above more than it, bounded and more with more of the hemisphere open.
  std::vector<std::array<float, 3>> In;
  for (float fNV : { 0.05f, 0.3f, 0.7f, 1.0f })
    for (float fAO : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
      for (float fAlpha : { 0.0f, 0.12f, 0.36f, 1.0f })
        In.push_back({ fNV, fAO, fAlpha });
  const std::vector<float> Out = SpecOcclusion(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    const auto &c = In[i];
    const std::string Case = "N.V " + std::to_string(c[0]) + " ao " + std::to_string(c[1]) + " alpha " + std::to_string(c[2]);
    CHECK_MSG(std::abs(Out[i] - SpecOcclusionRef(c[0], c[1], c[2])) < 2e-4, Case);
    CHECK_MSG(Out[i] >= 0 && Out[i] <= 1, Case);
    if (c[1] == 1.0f)
      CHECK_MSG(Out[i] > 0.9999f, Case);
    if (c[2] == 1.0f)
      CHECK_MSG(std::abs(Out[i] - c[1]) < 2e-4, Case + ": a rough lobe about the occlusion");
    if (c[0] == 1.0f && c[2] == 0.0f && c[1] > 0 && c[1] < 1)
      CHECK_MSG(Out[i] > c[1] + 0.05f, Case + ": a narrow lobe from above more than the occlusion");
    if (i % 20 >= 4)   // the ambient occlusion one step less at the same N.V and alpha
      CHECK_MSG(Out[i] >= Out[i - 4] - 1e-6f, Case + ": more with more of the hemisphere open");
  }
  // Closed from the side: none.
  CHECK(SpecOcclusion({ { 0.05f, 0.0f, 0.12f } })[0] < 1e-6f);
}
