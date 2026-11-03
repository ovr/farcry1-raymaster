// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathTraceTests.cpp
//	Description: Unit tests of the functions of the path tracer's
//	kernels (Shaders/PathTrace.hlsl, Shaders/PathCheckerboard.hlsli),
//	run on the GPU through TestPathTrace.hlsl, which includes
//	PathTrace.hlsl as it is (Shader Model 6.5: skipped on a device
//	without it; the pixel's point on the ray scene's surface also needs
//	raytracing tier 1.1).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_LeafSide.h"
#include "Test_Cosine.h"
#include "Test_Checkerboard.h"
#include "Test_PixelSurface.h"
#include "Test_TerrainShare.h"

#include "Shaders/Constants.h"

#include <algorithm>
#include <cstdio>

using namespace XrTest;

namespace
{
  void NeedShaderModel65()
  {
    if (!Gpu().HasShaderModel65())
      XR_SKIP("the device has no Shader Model 6.5 (PathTrace.hlsl's kernels)");
  }

  void NeedRaytracing()
  {
    NeedShaderModel65();
    if (!Gpu().HasRaytracing())
      XR_SKIP("the device has no raytracing tier 1.1 (a scene for rays)");
  }

  SVec3 Cross(SVec3 a, SVec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

  //! A hill side of Pier's canyon as the flight of the film's pier-canyon shot sees it: the camera 150 m over the sea at
  //! Far Cry's coordinates (single floats lose some 1e-4 m there), a slope of the terrain (XR_RAY_TERRAIN) 300-700 m off
  //! facing it and the sun (Pier's), 1 km of it in triangles of 10 m (the terrain's are a few metres), with Pier's sun.
  struct SHillSide
  {
    SVec3 vCamera{ 964.07f, 1154.23f, 152.62f };
    SVec3 vSun = Normalize({ -0.501f, -0.245f, 0.830f });
    SVec3 vNormal = Normalize({ -0.25f, -0.45f, 1.0f });
    SVec3 vCentre{ 1150, 1600, 60 };
    SVec3 vU, vV;            //!< along the slope
    SRayScene Scene;
    SPathTraceConstants PT = {};
    SFrameConstants Frame = {};

    SHillSide()
    {
      vU = Normalize(Cross(vNormal, { 0, 0, 1 }));
      vV = Cross(vNormal, vU);
      AddQuad(vCentre, 500);
      Scene.nMask = XR_RAY_DYNAMIC | XR_RAY_TERRAIN;
      SRayInstance &I = Scene.Instance;
      I.nGeom[0] = 0;                 // g_Buffers[0], from its start
      I.nGeom[2] = XR_RAY_NONE;       // not indexed
      I.nLayout[0] = 12;              // positions alone
      I.nLayout[1] = 0xffffffffu;     // no normal, colour, uv0 or uv1
      I.nLayout[2] = XR_RAY_NONE;
      I.nPath[0] = XR_PT_MAT_TERRAIN;
      PT.vCameraPos[0] = vCamera.x;
      PT.vCameraPos[1] = vCamera.y;
      PT.vCameraPos[2] = vCamera.z;
      PT.vParams[1] = 2 / (0.577f * 2160);   // a pixel at 1 m: 120 degrees across a 4K picture's height
      PT.vShade[0] = 2;                      // d3d12_PTTerrainDelight's default
      PT.nPath[2] = 4;
      PT.nFlags[3] = XR_RAY_STATIC | XR_RAY_DYNAMIC;
      Frame.vSunDir[0] = vSun.x;
      Frame.vSunDir[1] = vSun.y;
      Frame.vSunDir[2] = vSun.z;
    }

    //! A square of side 2 r about c in the slope's plane in squares of 10 m, two triangles each (corners shared, as the
    //! terrain's: no cracks), wound one way or the other.
    void AddQuad(SVec3 c, float r, bool bFlip = false)
    {
      const int n = std::max(1, (int)(2 * r / 10));
      const float s = 2 * r / n;
      std::vector<SVec3> Grid((n + 1) * (n + 1));
      for (int i = 0; i <= n; i++)
        for (int j = 0; j <= n; j++)
          Grid[i * (n + 1) + j] = c + vU * (i * s - r) + vV * (j * s - r);
      for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
        {
          const int a = i * (n + 1) + j;
          const int p[4] = { a, a + n + 1, a + n + 2, a + 1 };
          const int Tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
          for (const auto &t : Tri)
            for (int k = 0; k < 3; k++)
            {
              const SVec3 &q = Grid[p[t[bFlip ? 2 - k : k]]];
              Scene.Corners.insert(Scene.Corners.end(), { q.x, q.y, q.z });
            }
        }
    }

    //! The point of the slope at (u, v) m from its centre as the depth buffer gives it: off along the camera's ray by e of
    //! its distance (beyond the surface for e > 0).
    SVec3 Point(float u, float v, float e) const
    {
      const SVec3 p = vCentre + vU * u + vV * v;
      return vCamera + (p - vCamera) * (1 + e);
    }

    SDispatch Dispatch(const SBytecode &Shader, size_t n)
    {
      SDispatch d;
      d.Shader = Shader;
      d.pPathTrace = &PT;
      d.pFrame = &Frame;
      d.pScene = &Scene;
      d.Test.nTest[0] = (uint32)n;
      d.nGroups[0] = Groups64(n);
      return d;
    }
  };

  //! The view depths' errors of the depth buffer the tests take (of the distance): what Pier's canyon showed at 300-700 m
  //! (some 1e-4 to 1e-3 of it, either way, other in each frame).
  const float DepthErrors[] = { -1.6e-3f, -8e-4f, -4e-4f, -1e-4f, 0, 1e-4f, 4e-4f, 8e-4f, 1.6e-3f };

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

//! PixelSurface: a pixel's point off the ray scene's surface along the camera's ray (the depth buffer's error, DepthErrors)
//! comes back onto it, on the same ray, with the triangle's normal on the camera's side (both windings); farther off than
//! PIXEL_DEPTH_ERROR (not on WARP, see below), of the vegetation, or among a mask without the surface, it stays as it was. Why: the depth buffer's
//! view depths at Far Cry's coordinates are off by up to some 1e-3 of themselves, a few tenths of a metre at 300 m, under
//! the surface in some frames: the path tracer's rays from there found the surface they left (step 268: streaks of shadow
//! over Pier's hills that came and went from frame to frame).
XR_TEST(PathTrace_PixelSurface)
{
  NeedRaytracing();
  for (bool bFlip : { false, true })
  {
    SHillSide Hill;
    if (bFlip)
    {
      Hill.Scene.Corners.clear();
      Hill.AddQuad(Hill.vCentre, 500, true);
    }
    struct SCase
    {
      SVec3 vPos, vFace;
      uint32 nMaterial, nMask;
      bool bMoves;
      float e;
    };
    std::vector<SCase> Cases;
    const SVec3 vFaceIn = Normalize({ 0.3f, 0.2f, 0.9f });   // the depth buffer's face: another one
    for (float u = -296.3f; u <= 300; u += 150)
      for (float v = -297.1f; v <= 300; v += 150)
      {
        for (float e : DepthErrors)
          Cases.push_back({ Hill.Point(u, v, e), vFaceIn, XR_PT_MAT_TERRAIN + 1, XR_RAY_STATIC | XR_RAY_DYNAMIC, true, e });
        // WARP reports hits nearer than a ray's TMin (some 2 m before it at 790 m): not there.
        if (!Gpu().IsWarp())
          Cases.push_back({ Hill.Point(u, v, 5e-3f), vFaceIn, XR_PT_MAT_TERRAIN + 1, XR_RAY_DYNAMIC, false, 5e-3f });
        Cases.push_back({ Hill.Point(u, v, 8e-4f), vFaceIn, XR_PT_MAT_VEGETATION + 1, XR_RAY_DYNAMIC, false, 8e-4f });
        Cases.push_back({ Hill.Point(u, v, 8e-4f), vFaceIn, XR_PT_MAT_LIT + 1, XR_RAY_STATIC, false, 8e-4f });
      }
    SDispatch d = Hill.Dispatch(XR_BYTECODE(Test_PixelSurface), Cases.size());
    d.Test.nTest[1] = XR_RAY_STATIC | XR_RAY_DYNAMIC;
    for (const SCase &c : Cases)
    {
      Push(d.In, c.vPos, Float(c.nMaterial));
      Push(d.In, c.vFace, 0);
    }
    d.nOut = Cases.size() * 2;
    // One mask a run (g_Test.nTest.y): the cases of another mask in a run of their own.
    std::vector<uint32> Out = Gpu().Run(d);
    SDispatch dStatic = d;
    dStatic.Test.nTest[1] = XR_RAY_STATIC;
    const std::vector<uint32> OutStatic = Gpu().Run(dStatic);
    for (size_t i = 0; i < Cases.size(); i++)
    {
      const SCase &c = Cases[i];
      const std::vector<uint32> &O = c.nMask == XR_RAY_STATIC ? OutStatic : Out;
      const SVec3 vPos = Vec(O, 2 * i), vFace = Vec(O, 2 * i + 1);
      const std::string sCase = "case " + std::to_string(i) + (bFlip ? " (flipped)" : "") + ", e " + std::to_string(c.e);
      if (!c.bMoves)
      {
        CHECK_MSG(vPos.x == c.vPos.x && vPos.y == c.vPos.y && vPos.z == c.vPos.z, sCase + ": the point moved");
        CHECK_MSG(vFace.x == c.vFace.x && vFace.y == c.vFace.y && vFace.z == c.vFace.z, sCase + ": the face changed");
        continue;
      }
      const float fOff = std::fabs(Dot(vPos - Hill.vCentre, Hill.vNormal));
      CHECK_MSG(fOff < 2e-3f, sCase + ": " + std::to_string(fOff) + " m off the surface");
      const SVec3 vRay = Normalize(c.vPos - Hill.vCamera);
      CHECK_MSG(Length(Cross(vPos - Hill.vCamera, vRay)) < 2e-3f, sCase + ": off the pixel's ray");
      CHECK_MSG(Dot(vFace, Hill.vNormal) > 0.9999f, sCase + ": the face is not the triangle's on the camera's side");
    }
  }
}

//! PixelTerrainShare: on a lit slope towards the sun the terrain's share of its baked light is the lit one,
//! ((1 + (r - 1) N.L) / r)^2.2, wherever the depth buffer's error puts the pixel's point (DepthErrors), and the shadowed
//! one, (1 / r)^2.2, under a hill side over it (towards the sun); TerrainBakedShare from the point as the depth buffer gives
//! it shades some of the lit slope (the scene shows the fault). Why: the terrain's sun ray (XR_RAY_TERRAIN) left from under
//! the surface found the terrain it left: its albedo divided by a share too small, in streaks other in each frame (step
//! 268, the film's pier-canyon shot jumped and shook).
XR_TEST(PathTrace_TerrainShare)
{
  NeedRaytracing();
  SHillSide Hill;
  // The shadow: a hill side 40 m over the slope's part about (-350, 0) (its own normal; the sun's rays from there pass
  // through it, the camera's rays to the lit part do not).
  const SVec3 vShaded = Hill.vCentre - Hill.vU * 350;
  Hill.AddQuad(vShaded + Hill.vSun * (40 / Dot(Hill.vSun, Hill.vNormal)), 60);
  struct SCase
  {
    SVec3 vPos;
    float d;
    bool bLit;
    float e;
  };
  std::vector<SCase> Cases;
  for (float u = -146.3f; u <= 350; u += 25)
    for (float v = -297.1f; v <= 300; v += 25)
      for (float e : DepthErrors)
      {
        const SVec3 p = Hill.Point(u, v, e);
        Cases.push_back({ p, Length(p - Hill.vCamera), true, e });
      }
  const size_t nLit = Cases.size();
  for (float u = -16.3f; u <= 20; u += 10)
    for (float v = -17.1f; v <= 20; v += 10)
      for (float e : DepthErrors)
      {
        const SVec3 p = Hill.vCamera + (vShaded + Hill.vU * u + Hill.vV * v - Hill.vCamera) * (1 + e);
        Cases.push_back({ p, Length(p - Hill.vCamera), false, e });
      }
  SDispatch d = Hill.Dispatch(XR_BYTECODE(Test_TerrainShare), Cases.size());
  for (const SCase &c : Cases)
  {
    Push(d.In, c.vPos, c.d);
    Push(d.In, Hill.vNormal, 0);
  }
  d.nOut = Cases.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  const float r = Hill.PT.vShade[0], fNL = Dot(Hill.vNormal, Hill.vSun);
  const float fLit = std::pow((1 + (r - 1) * fNL) / r, 2.2f), fShadow = std::pow(1 / r, 2.2f);
  size_t nRawShaded = 0;
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SCase &c = Cases[i];
    const float fWant = c.bLit ? fLit : fShadow;
    CHECK_MSG(std::fabs(At(Out, i, 0) - fWant) < 1e-4f, "case " + std::to_string(i) + (c.bLit ? " (lit)" : " (shadowed)") + ", e " +
                                                          std::to_string(c.e) + ": " + std::to_string(At(Out, i, 0)) + ", not " +
                                                          std::to_string(fWant));
    if (i < nLit && std::fabs(At(Out, i, 1) - fLit) > 1e-4f)
      nRawShaded++;
  }
  // The fault this guards against, in this scene: from the depth buffer's points some of the lit slope is in shadow.
  CHECK_MSG(nRawShaded > nLit / 20, std::to_string(nRawShaded) + " of " + std::to_string(nLit) +
                                        " lit points shaded from the depth buffer's points: the scene no longer shows the fault");
}
