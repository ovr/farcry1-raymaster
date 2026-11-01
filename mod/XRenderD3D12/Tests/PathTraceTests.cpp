// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathTraceTests.cpp
//	Description: Unit tests of the functions of the path tracer's
//	kernels (Shaders/PathTrace.hlsl, Shaders/PathCheckerboard.hlsli),
//	run on the GPU through TestPathTrace.hlsl, which includes
//	PathTrace.hlsl as it is (Shader Model 6.5: skipped on a device
//	without it).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_LeafSide.h"
#include "Test_Cosine.h"
#include "Test_Checkerboard.h"

#include <cstdio>

using namespace XrTest;

namespace
{
  void NeedShaderModel65()
  {
    if (!Gpu().HasShaderModel65())
      XR_SKIP("the device has no Shader Model 6.5 (PathTrace.hlsl's kernels)");
  }

  //! PathTraced over a w x h grid with nCheckerboard (x, y): 1 where traced.
  std::vector<int> Traced(uint32 w, uint32 h, uint32 x, uint32 y)
  {
    SPathTraceConstants pt = {};
    pt.nCheckerboard[0] = x;
    pt.nCheckerboard[1] = y;
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_Checkerboard);
    d.pPathTrace = &pt;
    d.Test.nTest[0] = w;
    d.Test.nTest[1] = h;
    d.nGroups[0] = (w + 7) / 8;
    d.nGroups[1] = (h + 7) / 8;
    d.nOut = w * h;
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<int> r(w * h);
    for (size_t i = 0; i < r.size(); i++)
      r[i] = (int)Out[i * 4];
    return r;
  }
}

//! PathLeafSide: the leaf's face on the side the path leaves by: vFace where the direction is on its side, -vFace where it
//! is behind, exactly one of the two (no blend). Why: a path under a leaf's face goes on through the leaf from its other
//! side (the crown's normal, step 237); the wrong sign starts it inside the leaf.
XR_TEST(PathTrace_LeafSide)
{
  NeedShaderModel65();
  CRandom Rng(11);
  std::vector<SVec3> Faces, Dirs;
  for (int i = 0; i < 4096; i++)
  {
    Faces.push_back(Rng.Direction());
    Dirs.push_back(Rng.Direction() * Rng.Range(0.1f, 2));
  }
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_LeafSide);
  d.Test.nTest[0] = (uint32)Faces.size();
  d.nGroups[0] = Groups64(Faces.size());
  for (size_t i = 0; i < Faces.size(); i++)
  {
    Push(d.In, Faces[i], 0);
    Push(d.In, Dirs[i], 0);
  }
  d.nOut = Faces.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < Faces.size(); i++)
  {
    const float fSide = Dot(Dirs[i], Faces[i]);
    if (std::fabs(fSide) < 1e-5f)
      continue;
    const SVec3 vWant = fSide < 0 ? -Faces[i] : Faces[i], vGot = Vec(Out, i);
    CHECK_MSG(vGot.x == vWant.x && vGot.y == vWant.y && vGot.z == vWant.z, "case " + std::to_string(i));
  }
}

//! CosineDirection: unit directions, all in the hemisphere of vN, spread as the cosine (mean cos 2/3 and mean cos^2 1/2 over
//! a 128 x 128 grid of u, v; a uniform spread would give 1/2 and 1/3), no lean across the hemisphere (the mean direction is
//! along vN), u = 0 gives vN itself and u = 1 a direction on the hemisphere's rim; for normals on both sides of the
//! function's switch of its tangent (|vN.z| 0.9). Why: the paths' bounces are weighed as if spread by the cosine; another
//! spread brightens or darkens every bounce.
XR_TEST(PathTrace_CosineDirection)
{
  NeedShaderModel65();
  const SVec3 Normals[] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 }, Normalize({ 0.1f, 0.2f, 0.97f }), Normalize({ 0.6f, 0, 0.8f }),
                            Normalize({ -1, 1, -1 }), Normalize({ 0, 0.4359f, 0.9f }) };
  const uint32 S = 128, N = S * S, nNormals = (uint32)(sizeof(Normals) / sizeof(Normals[0]));
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_Cosine);
  d.Test.nTest[0] = N * nNormals;
  d.Test.nTest[1] = N;
  d.Test.nTest[2] = S;
  d.nGroups[0] = Groups64(N * nNormals);
  for (const SVec3 &n : Normals)
    Push(d.In, n, 0);
  d.nOut = N * nNormals;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (uint32 k = 0; k < nNormals; k++)
  {
    const SVec3 &n = Normals[k];
    double fCos = 0, fCos2 = 0;
    SVec3 vMean;
    int nBelow = 0, nNotUnit = 0;
    for (uint32 j = 0; j < N; j++)
    {
      const SVec3 v = Vec(Out, k * N + j);
      const float c = Dot(v, n);
      nBelow += c < -1e-6f;
      nNotUnit += std::fabs(Length(v) - 1) > 1e-5f;
      fCos += c;
      fCos2 += c * c;
      vMean = vMean + v;
    }
    fCos /= N;
    fCos2 /= N;
    vMean = vMean * (1.0f / N);
    printf("    normal %u: mean cos %.5f, mean cos^2 %.5f\n", k, fCos, fCos2);
    CHECK_MSG(nBelow == 0, std::to_string(nBelow) + " below the hemisphere of normal " + std::to_string(k));
    CHECK_MSG(nNotUnit == 0, std::to_string(nNotUnit) + " not of unit length, normal " + std::to_string(k));
    CHECK_NEAR(fCos, 2.0 / 3, 2e-3);
    CHECK_NEAR(fCos2, 0.5, 2e-3);
    CHECK(Distance(vMean, n * (float)fCos) < 2e-3f);
  }

  // The ends of u: the normal itself, the rim.
  SDispatch e;
  e.Shader = XR_BYTECODE(Test_Cosine);
  const float V[] = { 0, 0.25f, 0.5f, 0.9f };
  for (const SVec3 &n : Normals)
  {
    for (float v : V)
    {
      Push(e.In, n, 0);
      Push(e.In, 0, v, 0, 0);
      Push(e.In, n, 0);
      Push(e.In, 1, v, 0, 0);
    }
  }
  const uint32 nEnds = nNormals * 8;
  e.Test.nTest[0] = nEnds;
  e.nGroups[0] = Groups64(nEnds);
  e.nOut = nEnds;
  const std::vector<uint32> Ends = Gpu().Run(e);
  for (uint32 i = 0; i < nEnds; i += 2)
  {
    const SVec3 &n = Normals[i / 8];
    CHECK(Distance(Vec(Ends, i), n) < 1e-5f);
    CHECK_NEAR(Dot(Vec(Ends, i + 1), n), 0, 1e-4);
  }
}

//! PathTraced (PathCheckerboard.hlsli) with its real g_PT: with nCheckerboard.x 1 exactly half of the pixels a frame, one of
//! each two of a row (CSTrace's thread for each two pixels), the two frames' (nCheckerboard.y 0 and 1) complementary, and
//! every pixel left out has its four neighbours traced (the fill-in takes their light); with x 0 every pixel. Why: a pixel
//! traced in neither frame gets no light, one traced in both wastes half the paths.
XR_TEST(PathTrace_Checkerboard)
{
  NeedShaderModel65();
  const uint32 w = 64, h = 34;
  const std::vector<int> Even = Traced(w, h, 1, 0), Odd = Traced(w, h, 1, 1), All = Traced(w, h, 0, 0);
  int nEven = 0, nOdd = 0, nAll = 0;
  for (uint32 y = 0; y < h; y++)
  {
    for (uint32 x = 0; x < w; x++)
    {
      const size_t i = x + y * w;
      nEven += Even[i];
      nOdd += Odd[i];
      nAll += All[i];
      CHECK(Even[i] + Odd[i] == 1);
      if (x % 2 == 0)
        CHECK(Even[i] + Even[i + 1] == 1);
      for (const std::vector<int> *p : { &Even, &Odd })
      {
        if ((*p)[i])
          continue;
        CHECK(x == 0 || (*p)[i - 1]);
        CHECK(x == w - 1 || (*p)[i + 1]);
        CHECK(y == 0 || (*p)[i - w]);
        CHECK(y == h - 1 || (*p)[i + w]);
      }
    }
  }
  CHECK(nEven == (int)(w * h / 2) && nOdd == (int)(w * h / 2));
  CHECK(nAll == (int)(w * h));
}
