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
#include "Test_SurfaceNormal.h"
#include "Test_LampLight.h"

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

//! PathSurfaceNormal and XR_PT_VEGETATION_MATERIAL: the vegetation's opaque parts (DF_VCOLOR_X2 without DF_ALPHATEST: a trunk,
//! its branches) take the face's normal and the lit material, its leaves (with DF_ALPHATEST) and every other draw the vertices'
//! normal turned to the face's side (the leaves the vegetation's material). Why: Far Cry's plants carry the crown's normal,
//! straight up, on all their vertices; a trunk's paths by it went up into the crown or, turned to the camera's side, down
//! into the ground, and the leaves' material sent them through the trunk: the film's river-temple and swamp-path trunks were
//! black in the path tracer (step 270).
XR_TEST(PathTrace_SurfaceNormal)
{
  CHECK(XR_PT_VEGETATION_MATERIAL(DF_VCOLOR_X2) == XR_PT_MAT_LIT);
  CHECK(XR_PT_VEGETATION_MATERIAL(DF_VCOLOR_X2 | DF_LIGHTING | DF_FOG) == XR_PT_MAT_LIT);
  CHECK(XR_PT_VEGETATION_MATERIAL(DF_VCOLOR_X2 | DF_ALPHATEST) == XR_PT_MAT_VEGETATION);
  NeedShaderModel65();
  struct SCase
  {
    uint32 nFlags;
    SVec3 vNormal, vFace;
    bool bFace;     //!< the face's normal wanted, else the vertices' on its side
  };
  const uint32 Trunks[] = { DF_VCOLOR_X2, DF_VCOLOR_X2 | DF_LIGHTING | DF_FOG, DF_VCOLOR_X2 | DF_VCOLOR };
  const uint32 Others[] = { 0, DF_VCOLOR_X2 | DF_ALPHATEST, DF_ALPHATEST, DF_VCOLOR, DF_LIGHTING | DF_TEX1_LIGHTMAP };
  CRandom Rng(270);
  std::vector<SCase> Cases;
  for (int i = 0; i < 256; i++)
  {
    // The plants' normal straight up, and any other; faces of every side (a trunk's mostly level).
    const SVec3 vNormal = i % 2 ? SVec3{ 0, 0, 1 } : Rng.Direction();
    const SVec3 vFace = i % 3 ? Normalize({ Rng.Range(-1, 1), Rng.Range(-1, 1), Rng.Range(-0.2f, 0.2f) }) : Rng.Direction();
    for (uint32 f : Trunks)
      Cases.push_back({ f, vNormal, vFace, true });
    for (uint32 f : Others)
      Cases.push_back({ f, vNormal, vFace, false });
  }
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_SurfaceNormal);
  d.Test.nTest[0] = (uint32)Cases.size();
  d.nGroups[0] = Groups64(Cases.size());
  for (const SCase &c : Cases)
  {
    Push(d.In, c.vNormal, Float(c.nFlags));
    Push(d.In, c.vFace, 0);
  }
  d.nOut = Cases.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SCase &c = Cases[i];
    const SVec3 vWant = c.bFace ? c.vFace : Dot(c.vNormal, c.vFace) < 0 ? -c.vNormal : c.vNormal, vGot = Vec(Out, i);
    CHECK_MSG(vGot.x == vWant.x && vGot.y == vWant.y && vGot.z == vWant.z,
              "case " + std::to_string(i) + ", flags " + std::to_string(c.nFlags));
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

//! PathLampLight's falloff (PathLampFalloff): by default (d3d12_PTBakedFalloff 0) a lamp baked into the light maps
//! (XR_PT_LIGHT_LM) falls off as any other, as the raster's dynamic light, (1 - smoothstep(0, 0.9, d / r))^2.2, and gives
//! nothing from 0.9 r on, bit for bit as the lamps without the flag; with d3d12_PTBakedFalloff k > 0 (g_PT.vShade.z) the
//! baked one falls off as ((1 - d / r)^k)^2.2 over its whole radius, the others as before. Why: Research's light maps light
//! its walls from lamps 0.9-1 r away, where the path tracer had nothing (step 271); the default must stay the picture it was.
XR_TEST(PathTrace_LampFalloff)
{
  NeedShaderModel65();
  const float r = 10;
  SPTLight Lights[2] = {};
  for (int l = 0; l < 2; l++)
  {
    Lights[l].vPos[3] = r;
    Lights[l].vColor[0] = Lights[l].vColor[1] = Lights[l].vColor[2] = 0.5f;   // twice it 1: the falloff alone
    Lights[l].nParams[0] = XR_PT_LIGHT_POINT;
    Lights[l].nParams[1] = l == 0 ? XR_PT_LIGHT_LM | XR_PT_LIGHT_STATIC : XR_PT_LIGHT_STATIC;
    Lights[l].nParams[2] = Lights[l].nParams[3] = XR_RAY_NONE;
  }
  const float Distances[] = { 0.5f, 1, 2.5f, 4.5f, 6, 7.5f, 8.5f, 8.95f, 9.05f, 9.5f, 9.95f, 10.05f, 12 };
  const uint32 nDistances = (uint32)(sizeof(Distances) / sizeof(Distances[0])), n = 2 * nDistances;
  auto Smooth = [](float x) { const float t = std::min(std::max(x / 0.9f, 0.0f), 1.0f); return t * t * (3 - 2 * t); };
  auto Dynamic = [&](float x) { return x >= 0.9f ? 0.0f : std::pow(1 - Smooth(x), 2.2f); };
  auto Baked = [](float x, float k) { return x >= 1 ? 0.0f : std::pow(std::pow(1 - x, k), 2.2f); };
  std::vector<uint32> Default;
  for (float k : { 0.0f, 0.5f, 1.0f, 2.0f })
  {
    SPathTraceConstants PT = {};
    PT.vSun[3] = 1;                // d3d12_PTLamps
    PT.vShade[2] = k;              // d3d12_PTBakedFalloff
    PT.nPath[0] = 2;
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_LampLight);
    d.pPathTrace = &PT;
    d.pLights = Lights;
    d.nLightBytes = sizeof(Lights);
    for (uint32 l = 0; l < 2; l++)
    {
      for (float dist : Distances)
      {
        Push(d.In, dist, 0, 0, 0);
        d.In.back() = l;           // the lamp's index, bits
        Push(d.In, -1, 0, 0, 0);   // facing it: N.L 1
      }
    }
    d.Test.nTest[0] = n;
    d.nGroups[0] = Groups64(n);
    d.nOut = n;
    const std::vector<uint32> Out = Gpu().Run(d);
    for (uint32 l = 0; l < 2; l++)
    {
      for (uint32 j = 0; j < nDistances; j++)
      {
        const uint32 i = l * nDistances + j;
        const float x = Distances[j] / r, fWant = l == 0 && k > 0 ? Baked(x, k) : Dynamic(x), fGot = At(Out, i, 0);
        CHECK_MSG(std::fabs(fGot - fWant) <= 1e-6f + 2e-4f * fWant, "falloff " + std::to_string(k) + (l == 0 ? ", baked" : ", dynamic") +
                                                                  " lamp at " + std::to_string(x) + " r: " + std::to_string(fGot) + ", not " +
                                                                  std::to_string(fWant));
        CHECK(At(Out, i, 0) == At(Out, i, 1) && At(Out, i, 1) == At(Out, i, 2));
        if (fWant > 0)
          CHECK_NEAR(At(Out, i, 3), Distances[j], 1e-5);
      }
    }
    if (k == 0)
      Default = Out;
    else
    {
      // The dynamic lamp is as by default, bit for bit; the baked one still lights at 0.95 r.
      for (uint32 j = 0; j < nDistances; j++)
        CHECK(Out[(nDistances + j) * 4] == Default[(nDistances + j) * 4]);
      CHECK(Distances[9] == 9.5f && At(Out, 9, 0) > 0);
    }
  }
  // By default the baked lamp is the dynamic one, bit for bit.
  for (uint32 j = 0; j < nDistances; j++)
    CHECK_MSG(Default[j * 4] == Default[(nDistances + j) * 4], "distance " + std::to_string(Distances[j]));
}
