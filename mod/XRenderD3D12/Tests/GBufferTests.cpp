// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: GBufferTests.cpp
//	Description: Unit tests of the path tracer's G-buffer encoding
//	(Shaders/PathGBuffer.hlsli) and of the HDR targets' light curve
//	(Shaders/HdrLight.hlsli), run on the GPU (TestHeaders.hlsl).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_Oct.h"
#include "Test_GBufferNormal.h"
#include "Test_Hdr.h"

#include <algorithm>
#include <cstdio>

using namespace XrTest;

namespace
{
  //! What R10G10B10A2's 10 bits leave of an octahedral normal at most: half a step (1 / 1023 of the 0..1 map, 2 / 1023 of
  //! its -1..1 square) along both axes, stretched by up to about 3 where the map is most compressed (near the octahedron's
  //! edges). Measured over 400000 directions: 4.09e-3 at most (0.23 degrees), 1.46e-3 on average. A tolerance of 1e-3 is
  //! not reachable with 10 bits a channel; these are the bounds of the encoding itself.
  constexpr float OCT10_MAX_ERROR = 4.5e-3f;
  constexpr float OCT10_MEAN_ERROR = 2e-3f;
  //! Without the quantisation the round trip is exact to float precision.
  constexpr float OCT_FLOAT_ERROR = 1e-5f;

  //! Directions that cover the sphere (a Fibonacci spiral), the axes and the diagonals of each octant (the map's corners and
  //! its centre), points on the octahedron's edges (the equator, where n.z < 0 starts to fold), and negative zeros.
  std::vector<SVec3> TestDirections()
  {
    std::vector<SVec3> v;
    const int n = 20000;
    for (int i = 0; i < n; i++)
    {
      const float z = 1 - 2 * (i + 0.5f) / n, r = std::sqrt(1 - z * z), a = 3.14159265f * (1 + std::sqrt(5.0f)) * i;
      v.push_back({ r * std::cos(a), r * std::sin(a), z });
    }
    for (int a = 0; a < 3; a++)
    {
      for (float s : { 1.0f, -1.0f })
      {
        SVec3 e;
        (a == 0 ? e.x : a == 1 ? e.y : e.z) = s;
        v.push_back(e);
      }
    }
    for (int k = 0; k < 8; k++)
      v.push_back(Normalize({ k & 1 ? -1.0f : 1.0f, k & 2 ? -1.0f : 1.0f, k & 4 ? -1.0f : 1.0f }));
    for (int k = 0; k < 64; k++)
    {
      const float a = 6.2831853f * k / 64;
      v.push_back({ std::cos(a), std::sin(a), 0 });
      v.push_back(Normalize({ std::cos(a), std::sin(a), -1e-4f }));
    }
    v.push_back({ -0.0f, -0.0f, 1 });
    v.push_back({ -0.0f, -0.0f, -1 });
    v.push_back({ -0.0f, 1, -0.0f });
    return v;
  }

  //! One run of CSGBufferNormal over the cases (normal, face, fog, material field).
  struct SNormalCase
  {
    SVec3 vNormal, vFace;
    float fFog;
    uint32 nMaterial;
  };

  std::vector<uint32> RunGBufferNormal(const std::vector<SNormalCase> &Cases)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_GBufferNormal);
    d.Test.nTest[0] = (uint32)Cases.size();
    d.nGroups[0] = Groups64(Cases.size());
    for (const SNormalCase &c : Cases)
    {
      Push(d.In, c.vNormal, c.fFog);
      Push(d.In, c.vFace, 0);
      d.In.back() = c.nMaterial;
    }
    d.nOut = Cases.size() * 3;
    return Gpu().Run(d);
  }
}

//! PathOctEncode / PathOctDecode: every direction comes back from its point, to float precision as it is and within the
//! encoding's own bound once the point is stored as R10G10B10A2 stores the G-buffer's normal (D3D12_PT_NORMAL_FORMAT); the
//! point is always in 0..1 (a UNORM target clamps anything else). Why: the paths start from this normal and the
//! checkerboard's fill-in weighs neighbours by it; an encoding that folds a hemisphere wrong lights a surface from behind.
XR_TEST(PathGBuffer_OctRoundTrip)
{
  const std::vector<SVec3> Dirs = TestDirections();
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_Oct);
  d.Test.nTest[0] = (uint32)Dirs.size();
  d.nGroups[0] = Groups64(Dirs.size());
  for (const SVec3 &v : Dirs)
    Push(d.In, v, 0);
  d.nOut = Dirs.size() * 3;
  const std::vector<uint32> Out = Gpu().Run(d);
  double fSum = 0, fMax = 0, fMaxFloat = 0;
  for (size_t i = 0; i < Dirs.size(); i++)
  {
    const float ex = At(Out, i * 3, 0), ey = At(Out, i * 3, 1);
    CHECK(ex >= 0 && ex <= 1 && ey >= 0 && ey <= 1);
    const double fFloat = Distance(Vec(Out, i * 3 + 1), Dirs[i]), fQuant = Distance(Vec(Out, i * 3 + 2), Dirs[i]);
    CHECK_NEAR(fFloat, 0, OCT_FLOAT_ERROR);
    CHECK_NEAR(fQuant, 0, OCT10_MAX_ERROR);
    CHECK_NEAR(Length(Vec(Out, i * 3 + 2)), 1, 1e-5);
    fSum += fQuant;
    fMax = std::max(fMax, fQuant);
    fMaxFloat = std::max(fMaxFloat, fFloat);
  }
  const double fMean = fSum / Dirs.size();
  printf("    %zu directions: float round trip %.2e at most, 10 bits %.2e at most, %.2e on average\n", Dirs.size(), fMaxFloat, fMax, fMean);
  CHECK(fMean < OCT10_MEAN_ERROR);
}

//! PathGBufferNormal's normal: normalised, turned to the side of vFace (the side of its triangle the camera sees) for every
//! material but the vegetation's, whose normal is the crown's and is never turned (the field XR_PT_MAT_VEGETATION + 1, also
//! with bits above the field's two set: the shader masks them). Why: step 237's fix; a leaf's normal turned by its side
//! flips every other leaf, one turned by the view a line over the crown.
XR_TEST(PathGBuffer_NormalSide)
{
  CHECK(XR_PT_MAT_VEGETATION + 1 == 3);
  CRandom Rng(237);
  std::vector<SNormalCase> Cases;
  for (int i = 0; i < 8000; i++)
  {
    SNormalCase c;
    c.vNormal = Rng.Direction() * Rng.Range(0.25f, 4);
    c.vFace = Rng.Direction();
    c.fFog = 1;
    c.nMaterial = (uint32)(i % 8);
    Cases.push_back(c);
  }
  // A vegetation normal against its face, the clearest case of "never turned".
  for (uint32 m : { 3u, 7u })
    Cases.push_back({ { 0, 0, 1 }, { 0, 0, -1 }, 1, m });
  const std::vector<uint32> Out = RunGBufferNormal(Cases);
  int nTurned = 0, nVegetationKept = 0;
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SNormalCase &c = Cases[i];
    const SVec3 n = Normalize(c.vNormal);
    const float fSide = Dot(n, c.vFace);
    if (std::fabs(fSide) < 1e-4f)
      continue;  // on the face's plane: either side is right
    const bool bVegetation = (c.nMaterial & 3) == XR_PT_MAT_VEGETATION + 1;
    const SVec3 vWant = !bVegetation && fSide < 0 ? -n : n;
    const SVec3 vGot = Vec(Out, i * 3 + 1);
    CHECK_MSG(Distance(vGot, vWant) < 1e-5f, "case " + std::to_string(i) + " material " + std::to_string(c.nMaterial));
    CHECK(Distance(Vec(Out, i * 3 + 2), vWant) < OCT10_MAX_ERROR);
    nTurned += !bVegetation && fSide < 0;
    nVegetationKept += bVegetation && fSide < 0;
  }
  CHECK(nTurned > 1000 && nVegetationKept > 500);  // both branches were taken often
}

//! A normal of zero length (a degenerate normal map texel, a vertex without one) becomes (0, 0, 1), then goes to the face's
//! side like any other: (0, 0, 1) with an upward face, (0, 0, -1) with a downward one, the vegetation's (0, 0, 1) either way.
//! Why: normalize(0) is NaN, and a NaN normal makes NaN light that NRD spreads over the screen.
XR_TEST(PathGBuffer_ZeroNormal)
{
  std::vector<SNormalCase> Cases;
  for (uint32 m = 0; m < 4; m++)
  {
    for (float fz : { 1.0f, -1.0f })
    {
      Cases.push_back({ { 0, 0, 0 }, { 0, 0, fz }, 1, m });
      Cases.push_back({ { 1e-7f, 0, 0 }, { 0, 0, fz }, 1, m });  // squared length below the shader's 1e-12
    }
  }
  const std::vector<uint32> Out = RunGBufferNormal(Cases);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const bool bTurn = Cases[i].vFace.z < 0 && Cases[i].nMaterial != XR_PT_MAT_VEGETATION + 1;
    const SVec3 vGot = Vec(Out, i * 3 + 1);
    CHECK(std::isfinite(vGot.x) && std::isfinite(vGot.y) && std::isfinite(vGot.z));
    CHECK_MSG(Distance(vGot, { 0, 0, bTurn ? -1.0f : 1.0f }) < 1e-6f, "case " + std::to_string(i));
  }
}

//! The texel's b and a: b the fog's share, clamped to 0..1; a the material field (nMaterial & 3) / 3, which
//! PathGBufferMaterial gives back, also after R10G10B10A2's 2 bits of alpha. Why: the path tracer lights a pixel only where
//! the field is not 0, the terrain and the vegetation are told by it.
XR_TEST(PathGBuffer_MaterialAndFog)
{
  std::vector<SNormalCase> Cases;
  const float Fogs[] = { -0.5f, 0, 0.3f, 1, 2 };
  for (uint32 m = 0; m < 8; m++)
    for (float f : Fogs)
      Cases.push_back({ { 0.3f, 0.4f, 0.866f }, { 0, 0, 1 }, f, m });
  const std::vector<uint32> Out = RunGBufferNormal(Cases);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const uint32 m = Cases[i].nMaterial & 3;
    CHECK_NEAR(At(Out, i * 3, 2), std::clamp(Cases[i].fFog, 0.0f, 1.0f), 0);
    CHECK_NEAR(At(Out, i * 3, 3), m / 3.0f, 1e-7);
    CHECK(Out[(i * 3 + 1) * 4 + 3] == m);  // PathGBufferMaterial of the texel
    CHECK(Out[(i * 3 + 2) * 4 + 3] == m);  // of the texel as the target stores it
  }
}

//! HdrDecode / HdrEncode (HdrLight.hlsli): c^2.2 up to 1, 1 + 2.2 (c - 1) above (the tangent at 1: continuous with a
//! continuous slope), never decreasing, 0 for negatives, and each the other's inverse. Why: every HDR target goes through
//! them; a kink at 1 shows as a band in the bloom, a round trip that drifts as light that creeps from frame to frame.
XR_TEST(HdrLight_RoundTrip)
{
  std::vector<float> X = { -1, -0.5f, 0, 1e-4f, 1e-3f, 0.01f, 0.1f, 0.5f, 0.9f, 0.999f, 0.9999f, 1, 1.0001f, 1.001f, 1.5f, 2, 4, 8, 16, 64 };
  for (int i = 0; i <= 1024; i++)
    X.push_back(i / 256.0f);
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_Hdr);
  d.Test.nTest[0] = (uint32)X.size();
  d.nGroups[0] = Groups64(X.size());
  for (float x : X)
    Push(d.In, x, 0, 0, 0);
  d.nOut = X.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < X.size(); i++)
  {
    const float x = X[i], fDec = At(Out, i, 0), fEnc = At(Out, i, 1);
    const double fWantDec = x <= 0 ? 0 : x <= 1 ? std::pow((double)x, 2.2) : 1 + 2.2 * (x - 1);
    const double fWantEnc = x <= 0 ? 0 : x <= 1 ? std::pow((double)x, 1 / 2.2) : 1 + (x - 1) / 2.2;
    // The GPU's pow is exp2(y log2(x)) of the hardware's precision (about 2^-21 relative): 1e-4 relative is far below any
    // error of the curve itself.
    CHECK_NEAR(fDec, fWantDec, 1e-5 + 1e-4 * fWantDec);
    CHECK_NEAR(fEnc, fWantEnc, 1e-5 + 1e-4 * fWantEnc);
    if (x >= 0)
    {
      CHECK_NEAR(At(Out, i, 2), x, 2e-5 + 1e-4 * x);  // encode(decode(x))
      CHECK_NEAR(At(Out, i, 3), x, 2e-5 + 1e-4 * x);  // decode(encode(x))
    }
  }
  // Monotonic over the sweep (items 20 on, x = 0 .. 4 in 1/256 steps), and the slope's two sides at 1.
  for (size_t i = 21; i < X.size(); i++)
    CHECK(At(Out, i, 0) >= At(Out, i - 1, 0));
  const size_t n1 = 20 + 256;  // x = 1
  CHECK_NEAR(At(Out, n1, 0), 1, 1e-6);
  const float fLeft = (At(Out, n1, 0) - At(Out, n1 - 1, 0)) * 256, fRight = (At(Out, n1 + 1, 0) - At(Out, n1, 0)) * 256;
  CHECK_NEAR(fRight, 2.2, 1e-3);
  CHECK_NEAR(fLeft, 2.2, 0.01);  // the chord of c^2.2 over the last 1/256 below 1
}
