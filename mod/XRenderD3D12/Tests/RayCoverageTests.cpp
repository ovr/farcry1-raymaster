// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayCoverageTests.cpp
//	Description: Unit tests of the alpha tests' shares by triangle
//	(d3d12_RTAlphaCoverage, RayShade.hlsli's RayTriangleCoverage and
//	RayCoverageDraw, TestPathTrace.hlsl's CSTriangleCoverage and
//	CSCoverageDraw): triangles over a transparent, an opaque and a
//	checkered part of a texture get shares 0, 255 and half; a share is a
//	hit for its part of the rays, none at 0, all at 255.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_TriangleCoverage.h"
#include "Test_CoverageDraw.h"

#include "Shaders/Constants.h"

#include <cstring>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  void NeedRaytracing()
  {
    if (!Gpu().HasShaderModel65())
      XR_SKIP("the device has no Shader Model 6.5 (RayShade.hlsli's kernels)");
    if (!Gpu().HasRaytracing())
      XR_SKIP("the device has no raytracing tier 1.1 (a scene for rays)");
  }
}

//! RayTriangleCoverage: a 6 x 2 texture read by the world's x / 3 and y (point sampling), its alpha 0 in columns 0-1, 255 in
//! 2-3, a checker in 4-5; a triangle over each part (alpha test >= 0.5) gets the share of it that passes: 0, 255 and half
//! (the checker's two texels a triangle covers of four, 128 within the noise of 64 points). Why: the rays that take a leaf
//! by its share (the path tracer's bounces, the rays' GI) see as much light through a crown as the exact test lets.
XR_TEST(RayCoverage_TriangleShare)
{
  NeedRaytracing();
  SRayScene Scene;
  for (float x0 : { 0.05f, 1.05f, 2.05f })
  {
    for (float c : { x0, 0.05f, 0.0f, x0 + 0.9f, 0.05f, 0.0f, x0, 0.95f, 0.0f })
      Scene.Corners.push_back(c);
  }
  SRayInstance &I = Scene.Instance;
  I.nGeom[0] = 0;
  I.nGeom[2] = XR_RAY_NONE;
  I.nLayout[0] = 12;
  I.nLayout[1] = 0xffffffffu;
  I.nLayout[2] = XR_RAY_NONE;
  I.nMaterial[0] = DF_ALPHATEST | DF_TEX0 | DF_ALPHA_FROM_TEX | DF_TEXGEN_WORLD;
  I.nMaterial[2] = 0;
  I.nMaterial2[0] = XR_SAMPLER_POINT_WRAP;
  const float fRef = 0.5f;
  memcpy(&I.nMaterial2[1], &fRef, 4);
  I.vColor[0] = I.vColor[1] = I.vColor[2] = I.vColor[3] = 1;
  I.vTexGenU[0] = 1.0f / 3;
  I.vTexGenV[1] = 1;
  I.nPath[3] = XR_RAY_NONE;

  STestTexture Tex;
  Tex.nWidth = 6;
  Tex.nHeight = 2;
  for (uint32 y = 0; y < 2; y++)
  {
    for (uint32 x = 0; x < 6; x++)
    {
      const bool bOpaque = x >= 4 ? ((x + y) & 1) != 0 : x >= 2;
      Tex.Data.insert(Tex.Data.end(), { 128, 128, 128, (uint8_t)(bOpaque ? 255 : 0) });
    }
  }

  SDispatch d;
  d.Shader = XR_BYTECODE(Test_TriangleCoverage);
  d.pScene = &Scene;
  d.Textures = { Tex };
  d.Test.nTest[0] = 3;
  for (uint32 t = 0; t < 3; t++)
    PushWords(d.In, t, 0, 0, 0);
  d.nOut = 3;
  const std::vector<uint32> Out = Gpu().Run(d);
  CHECK_MSG(Out[0] == 0, "transparent: " + std::to_string(Out[0]));
  CHECK_MSG(Out[4] == 255, "opaque: " + std::to_string(Out[4]));
  CHECK_MSG(Out[8] >= 115 && Out[8] <= 141, "checker: " + std::to_string(Out[8]));
}

//! RayCoverageDraw: over 64 rays' seeds and 256 triangles a share of n / 255 is a hit for about n / 256 of them (within 2 %),
//! 0 for none and 255 for all, for two instances. Why: a leaf taken at random by its share lets through as much light as
//! the exact test on average, and no ray sees an opaque triangle open or an empty one closed.
XR_TEST(RayCoverage_Draw)
{
  if (!Gpu().HasShaderModel65())
    XR_SKIP("the device has no Shader Model 6.5 (RayShade.hlsli's kernels)");
  const uint32 Shares[] = { 0, 1, 64, 128, 192, 254, 255 };
  constexpr uint32 SEEDS = 64, TRIANGLES = 256;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_CoverageDraw);
  for (uint32 nInstance : { 0u, 1234u })
  {
    for (uint32 s : Shares)
      PushWords(d.In, s, nInstance, 0, 0);
  }
  const size_t n = d.In.size() / 4;
  d.Test.nTest[0] = (uint32)n;
  d.Test.nTest[1] = SEEDS;
  d.Test.nTest[2] = TRIANGLES;
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < n; i++)
  {
    const uint32 s = d.In[i * 4];
    const double f = Out[i * 4] / double(SEEDS * TRIANGLES);
    const std::string sCase = "share " + std::to_string(s) + ", instance " + std::to_string(d.In[i * 4 + 1]) + ": " + std::to_string(f);
    if (s == 0)
      CHECK_MSG(Out[i * 4] == 0, sCase);
    else if (s == 255)
      CHECK_MSG(Out[i * 4] == SEEDS * TRIANGLES, sCase);
    else
      CHECK_MSG(std::fabs(f - s / 256.0) <= 0.02, sCase);
  }
}
