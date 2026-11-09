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
#include "Test_LampShadow.h"
#include "Test_LightmapFill.h"
#include "Test_ShadeFloor.h"
#include "Test_LampTarget.h"
#include "Test_DecalRatio.h"

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

//! The rays towards a lamp end short of it by its gap (PathLampGap): d3d12_RTLightShadowGap (0.1 m, g_PT.vShade.y), a
//! projector's d3d12_RTProjectorShadowGap (SPTLight::vColor.w) where that is more. A lamp 3 m over the floor with a square of
//! its shade 0.3 m from it on the ray to a point of the floor, and something solid 1.6 m from it on the ray to another: the
//! shade shadows the point lamp's light and a projector's with gap 0 (step 273: Treehouse's hanging lamps cut a hard-edged
//! trapezoid into their own picture), not the projector's with 0.5 m; the solid thing shadows them all; a third point sees
//! them all; with d3d12_RTLightShadowGap 0.6 the shade shadows none of them.
XR_TEST(PathTrace_ProjectorShadowGap)
{
  NeedRaytracing();
  const SVec3 vLamp{ 0, 0, 3 };
  SRayScene Scene;
  // A horizontal square of side s about c (both windings: the rays see it from below).
  auto Square = [&](SVec3 c, float s)
  {
    const SVec3 p[4] = { c + SVec3{ -s, -s, 0 }, c + SVec3{ s, -s, 0 }, c + SVec3{ s, s, 0 }, c + SVec3{ -s, s, 0 } };
    const int Tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
    for (const auto &t : Tri)
      for (int k = 0; k < 3; k++)
        Scene.Corners.insert(Scene.Corners.end(), { p[t[k]].x, p[t[k]].y, p[t[k]].z });
  };
  const SVec3 vUnderShade{ 1.2f, 0, 0 }, vUnderSolid{ -1.2f, 0, 0 }, vOpen{ 0, 1.2f, 0 };
  // Where the rays from those points to the lamp cross z = 2.72 (0.30 m from the lamp) and z = 1.5 (1.62 m from it).
  Square(vLamp + (vUnderShade - vLamp) * (0.28f / 3), 0.1f);
  Square(vLamp + (vUnderSolid - vLamp) * (1.5f / 3), 0.1f);
  Scene.nMask = XR_RAY_DYNAMIC;
  SRayInstance &I = Scene.Instance;
  I.nGeom[0] = 0;
  I.nGeom[2] = XR_RAY_NONE;
  I.nLayout[0] = 12;
  I.nLayout[1] = 0xffffffffu;
  I.nLayout[2] = XR_RAY_NONE;
  // The lamps: a point lamp, a projector with the gap 0.5 m, a projector with 0 (all of them at the same place).
  SPTLight Lights[3] = {};
  const float Gaps[3] = { 0, 0.5f, 0 };
  for (int l = 0; l < 3; l++)
  {
    const float vPos[4] = { vLamp.x, vLamp.y, vLamp.z, 10 }, vColor[4] = { 0.5f, 0.5f, 0.5f, Gaps[l] };
    std::copy(vPos, vPos + 4, Lights[l].vPos);
    std::copy(vColor, vColor + 4, Lights[l].vColor);
    Lights[l].nParams[0] = l == 0 ? XR_PT_LIGHT_POINT : XR_PT_LIGHT_PROJECTOR;
    Lights[l].nParams[2] = Lights[l].nParams[3] = XR_RAY_NONE;
  }
  const SVec3 Points[3] = { vUnderShade, vUnderSolid, vOpen };
  for (float fLightGap : { 0.1f, 0.6f })
  {
    SPathTraceConstants PT = {};
    PT.vShade[1] = fLightGap;
    PT.nFlags[3] = XR_RAY_STATIC | XR_RAY_DYNAMIC;
    PT.nPath[0] = 3;
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_LampShadow);
    d.pPathTrace = &PT;
    d.pScene = &Scene;
    d.pLights = Lights;
    d.nLightBytes = sizeof(Lights);
    for (const SVec3 &p : Points)
      for (uint32 l = 0; l < 3; l++)
      {
        Push(d.In, p, 0);
        d.In.back() = l;
        Push(d.In, { 0, 0, 1 }, 0);
      }
    const uint32 n = 9;
    d.Test.nTest[0] = n;
    d.nOut = n;
    const std::vector<uint32> Out = Gpu().Run(d);
    for (uint32 p = 0; p < 3; p++)
      for (uint32 l = 0; l < 3; l++)
      {
        const uint32 i = p * 3 + l;
        const float fGap = std::max(fLightGap, Gaps[l]);
        // Under the shade only the gap that reaches past it (more than 0.30 m) lets the light through.
        const float fWant = p == 2 ? 1.0f : p == 1 ? 0.0f : fGap > 0.3f ? 1.0f : 0.0f;
        const std::string What = "gap " + std::to_string(fLightGap) + ", lamp " + std::to_string(l) + ", point " + std::to_string(p);
        CHECK_MSG(At(Out, i, 1) == fGap, What + ": gap " + std::to_string(At(Out, i, 1)) + ", not " + std::to_string(fGap));
        CHECK_MSG(At(Out, i, 0) == fWant, What + ": seen " + std::to_string(At(Out, i, 0)) + ", not " + std::to_string(fWant));
        if (fWant == 0)
          CHECK_MSG(std::fabs(At(Out, i, 2) * Length(vLamp - Points[p]) - (Length(vLamp - Points[p]) - (p == 0 ? 0.3016f : 1.6155f))) < 0.05f,
                    What + ": hit at " + std::to_string(At(Out, i, 2)));
      }
  }
}

//! PathLightmapFill (d3d12_PTLightMaps): what a light map lights a point with beyond the path tracer's own light is the
//! raster's light (4 * the map + the ambient light, the game's light) as light, less the ambient light and the light of the
//! lamps baked into the maps (XR_PT_LIGHT_LM) there without their shadows, 0 at least; a lamp that is not in the maps takes
//! nothing off, and without a light map there is none. Why: Research's halls are lit by their light maps' flat light, which
//! the path tracer's lamps do not reach (its SUB-LEVEL wall had 0.43 of the raster's light, step 277); near the lamps their
//! light and shadows must stay the path tracer's.
XR_TEST(PathTrace_LightmapFill)
{
  NeedShaderModel65();
  const float r = 10;
  SPTLight Lights[2] = {};
  for (int l = 0; l < 2; l++)
  {
    Lights[l].vPos[0] = 100;
    Lights[l].vPos[1] = 200;
    Lights[l].vPos[2] = 30;
    Lights[l].vPos[3] = r;
    Lights[l].vColor[0] = Lights[l].vColor[1] = Lights[l].vColor[2] = 0.5f;   // twice it 1: the falloff alone
    Lights[l].nParams[0] = XR_PT_LIGHT_POINT;
    Lights[l].nParams[1] = XR_PT_LIGHT_STATIC | (l == 0 ? XR_PT_LIGHT_LM : 0);
    Lights[l].nParams[2] = Lights[l].nParams[3] = XR_RAY_NONE;
  }
  struct SCase
  {
    float fMap, fAmbient, fDistance;
  };
  // Along -x from the lamps, the normal +x: facing them (N.L 1).
  const SCase Cases[] = { { 0, 0.1f, 20 }, { 0.1f, 0.1f, 20 }, { 0.1f, 0.1f, 1 }, { 0.1f, 0.1f, 7 }, { 0.1f, 0.1f, 8.5f },
                          { 0.05f, 0.05f, 9.5f }, { 0.25f, 0, 4 }, { 0.02f, 0.2f, 15 } };
  const uint32 n = (uint32)(sizeof(Cases) / sizeof(Cases[0]));
  auto Smooth = [](float x) { const float t = std::min(std::max(x / 0.9f, 0.0f), 1.0f); return t * t * (3 - 2 * t); };
  auto Lamp = [&](float d) { const float x = d / r; return x >= 0.9f ? 0.0f : std::pow(1 - Smooth(x), 2.2f); };
  auto Light = [](float g) { return std::pow(std::max(g, 0.0f), 2.2f); };
  for (int nLM = 0; nLM < 2; nLM++)
  {
    // nLM 0: the first lamp without XR_PT_LIGHT_LM too (neither is in the maps).
    SPTLight Run[2] = { Lights[0], Lights[1] };
    if (nLM == 0)
      Run[0].nParams[1] = XR_PT_LIGHT_STATIC;
    SPathTraceConstants PT = {};
    PT.vSun[3] = 1;   // d3d12_PTLamps
    PT.nPath[0] = 2;
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_LightmapFill);
    d.pPathTrace = &PT;
    d.pLights = Run;
    d.nLightBytes = sizeof(Run);
    d.Test.nTest[0] = n;
    d.Test.vTest[0] = 1;
    for (const SCase &c : Cases)
    {
      Push(d.In, c.fMap, c.fMap, c.fMap, 0);
      Push(d.In, c.fAmbient, c.fAmbient, c.fAmbient, 0);
      Push(d.In, 100 - c.fDistance, 200, 30, 0);
    }
    d.nGroups[0] = Groups64(n);
    d.nOut = n;
    const std::vector<uint32> Out = Gpu().Run(d);
    for (uint32 i = 0; i < n; i++)
    {
      const SCase &c = Cases[i];
      const float fRaster = Light(4 * c.fMap + 2 * c.fAmbient) - Light(2 * c.fAmbient);
      const float fWant = c.fMap <= 0 ? 0 : std::max(fRaster - (nLM ? Lamp(c.fDistance) : 0), 0.0f), fGot = At(Out, i, 0);
      CHECK_MSG(std::fabs(fGot - fWant) <= 1e-5f + 1e-3f * fWant, std::string(nLM ? "baked" : "dynamic") + " lamp, case " + std::to_string(i) +
                                                                  ": " + std::to_string(fGot) + ", not " + std::to_string(fWant));
      CHECK(At(Out, i, 0) == At(Out, i, 1) && At(Out, i, 1) == At(Out, i, 2));
    }
    if (nLM)
    {
      CHECK(At(Out, 0, 0) == 0);     // no light map
      CHECK(At(Out, 1, 0) > 0.25f);  // beyond the lamps: the map's light
      CHECK(At(Out, 2, 0) == 0);     // by the lamp: its own light
    }
  }
}

//! PathShadeFloor (d3d12_PTShadeFloor): where the sun is hidden the composite lights a surface with no less than the
//! raster's shade there, times the share of it the shadow covers. The terrain (in a frame with the sun): the light baked
//! into its texture, which has the shadows of what does not move (the raster's terrain takes the sun's shadow of the live
//! casters alone, DF2_CASCADES_LIVE), times the shadow's tint where a live caster hides the sun. A lit surface: the
//! raster's light, 4 * its light map's light (times the live casters' visibility) + the ambient light, as light. Nothing
//! for the vegetation, the unlit, the terrain in a frame without the sun, or where nothing is in the shadow. Why: the paths
//! alone lit the sand under Carrier's wreck (a static caster: the raster keeps the light baked there) at 0.48 of RT's colour
//! (13.4 of 27.7, the original 52), Training's bunker wall by the pipe at 0.16 (0.8 of 4.9: its lamp's rays hidden where
//! its light map is lit), now 27.6 and 5.3.
XR_TEST(PathTrace_ShadeFloor)
{
  NeedShaderModel65();
  const float vTint[3] = { 0.26f, 0.31f, 0.36f };   // Carrier's
  struct SCase
  {
    int nMaterial;   // the path material plus 1
    float fShade;
    bool bTerrain;
    float fBaked, fMap, fHalfAmbient;
  };
  const int LIT = XR_PT_MAT_LIT + 1, TERRAIN = XR_PT_MAT_TERRAIN + 1, VEGETATION = XR_PT_MAT_VEGETATION + 1;
  const SCase Cases[] = {
    { TERRAIN, 1, true, 1.1f, 1, 0 },          // a static caster's shadow: the baked light
    { TERRAIN, 1, true, 1.1f, 0, 0 },          // a live caster's: times the tint
    { TERRAIN, 0.5f, true, 0.8f, 0.5f, 0 },    // half in the shadow, half the live casters'
    { TERRAIN, 0, true, 1.1f, 0, 0 },          // in the sun
    { TERRAIN, 1, false, 1.1f, 1, 0 },         // a frame without the sun
    { LIT, 1, false, 1, 0.1f, 0.05f },         // a light map and the ambient
    { LIT, 0.25f, true, 1, 0.1f, 0.05f },      // a quarter in the shadow
    { LIT, 1, true, 1, 0, 0.2f },              // the ambient alone (no light map, or a live caster's shadow)
    { LIT, 0, true, 1, 0.1f, 0.05f },          // in the sun
    { VEGETATION, 1, true, 1, 0.2f, 0.1f },
    { 0, 1, true, 1, 0.2f, 0.1f },             // unlit or no surface
  };
  const uint32 n = (uint32)(sizeof(Cases) / sizeof(Cases[0]));
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_ShadeFloor);
  d.Test.nTest[0] = n;
  for (int c = 0; c < 3; c++)
    d.Test.vTest[c] = vTint[c];
  for (const SCase &c : Cases)
  {
    Push(d.In, c.fBaked, c.fBaked, c.fBaked, (float)c.nMaterial);
    Push(d.In, c.fMap, c.fMap, c.fMap, c.fShade);
    Push(d.In, c.fHalfAmbient, c.fHalfAmbient, c.fHalfAmbient, c.bTerrain ? 1.0f : 0.0f);
  }
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (uint32 i = 0; i < n; i++)
  {
    const SCase &c = Cases[i];
    for (int k = 0; k < 3; k++)
    {
      float fWant = 0;
      if (c.fShade > 0 && c.nMaterial == TERRAIN && c.bTerrain)
        fWant = c.fShade * c.fBaked * std::pow(vTint[k] + (1 - vTint[k]) * c.fMap, 2.2f);
      else if (c.fShade > 0 && c.nMaterial == LIT)
        fWant = c.fShade * std::pow(4 * c.fMap + 2 * c.fHalfAmbient, 2.2f);
      const float fGot = At(Out, i, k);
      CHECK_MSG(std::fabs(fGot - fWant) <= 1e-5f + 1e-3f * fWant, "case " + std::to_string(i) + ", channel " + std::to_string(k) + ": " +
                                                                  std::to_string(fGot) + ", not " + std::to_string(fWant));
      // Never more than the raster's light where the sun is: the terrain's baked light, scaled by the share.
      if (c.nMaterial == TERRAIN)
        CHECK(fGot <= c.fShade * c.fBaked + 1e-5f);
    }
  }
  CHECK(At(Out, 0, 0) > At(Out, 1, 0));   // a static caster's shadow lighter than a live one's
  CHECK(At(Out, 1, 0) < At(Out, 1, 2));   // the tint's blue
}

//! PathLitLight and PathDecalRatio (d3d12_PTDecalLight): a draw lit by its light map after the paths (a decal blended over
//! the hull it lies on) takes the light the composite gives the lit surface under it in the raster's ratio of their lights:
//! its light map's light times the composite's light of the surface (the sun times N.L and the raster's visibility, the
//! paths, with d3d12_PTShadeFloor no less than the raster's shade) as the game's colour over the raster's light of it
//! (4 * its light map's light + the ambient), per channel, from 1 / 4 to 4; 1 where the raster's is none, 1 where the
//! two are the same light. Why: the paths lit Carrier's hull brighter than the raster, and the ship's name over it kept
//! the raster's light (its own light map 2 to 3 times darker than the hull's, as in the original): dark letters stood out.
XR_TEST(PathTrace_DecalRatio)
{
  NeedShaderModel65();
  const float vTint[3] = { 0.26f, 0.31f, 0.36f };
  struct SCase
  {
    float fSun, fNL, fPaths, fVisible, fMap, fShadeMode, fHalfAmbient;
  };
  const SCase Cases[] = {
    { 1.2f, 0.8f, 0.30f, 1, 0.15f, 1, 0.20f },     // in the sun: brighter by the paths than the raster
    { 1.2f, 0.8f, 0.02f, 0, 0.05f, 1, 0.10f },     // in the shadow: the raster's shade, ratio 1
    { 0.0f, 0.0f, 0.01f, 1, 0.08f, 2, 0.05f },     // a frame without the sun: the raster's at least
    { 0.0f, 0.0f, 0.20f, 1, 0.00f, 0, 0.00f },     // no raster light at all: 1
    { 4.0f, 1.0f, 3.00f, 1, 0.01f, 0, 0.01f },     // far brighter: at most 4
    { 0.0f, 0.0f, 0.0001f, 1, 0.20f, 0, 0.20f },   // far darker (no floor): at least 1 / 4
    { 1.0f, -0.5f, 0.05f, 1, 0.10f, 0, 0.05f },    // facing away from the sun: the paths alone
  };
  const uint32 n = (uint32)(sizeof(Cases) / sizeof(Cases[0]));
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_DecalRatio);
  d.Test.nTest[0] = n;
  for (int c = 0; c < 3; c++)
    d.Test.vTest[c] = vTint[c];
  for (const SCase &c : Cases)
  {
    Push(d.In, c.fSun, c.fSun, c.fSun, c.fNL);
    Push(d.In, c.fPaths, c.fPaths, c.fPaths, c.fVisible);
    Push(d.In, c.fMap, c.fMap, c.fMap, c.fShadeMode);
    Push(d.In, c.fHalfAmbient, c.fHalfAmbient, c.fHalfAmbient, 0);
  }
  d.nGroups[0] = Groups64(n);
  d.nOut = 2 * n;
  const std::vector<uint32> Out = Gpu().Run(d);
  auto Encode = [](float x) { return x > 1 ? 1 + (x - 1) / 2.2f : std::pow(std::max(x, 0.0f), 1 / 2.2f); };
  for (uint32 i = 0; i < n; i++)
  {
    const SCase &c = Cases[i];
    const float fRaster = 4 * c.fMap + 2 * c.fHalfAmbient;
    float fLight = c.fSun * std::max(c.fNL, 0.0f) * c.fVisible + c.fPaths;
    if (c.fShadeMode > 0)
    {
      const float fShade = c.fShadeMode > 1.5f ? 1 : 1 - c.fVisible;
      fLight = std::max(fLight, fShade > 0 ? fShade * std::pow(fRaster, 2.2f) : 0.0f);
    }
    const float fRatio = fRaster > 1e-4f ? std::clamp(Encode(fLight) / fRaster, 0.25f, 4.0f) : 1;
    for (int k = 0; k < 3; k++)
    {
      const float fGotLight = At(Out, 2 * i, k), fGotRatio = At(Out, 2 * i + 1, k);
      const std::string sCase = "case " + std::to_string(i) + ", channel " + std::to_string(k) + ": ";
      CHECK_MSG(std::fabs(fGotLight - fLight) <= 1e-5f + 1e-3f * fLight, sCase + "light " + std::to_string(fGotLight) + ", not " + std::to_string(fLight));
      CHECK_MSG(std::fabs(fGotRatio - fRatio) <= 1e-4f + 1e-3f * fRatio, sCase + "ratio " + std::to_string(fGotRatio) + ", not " + std::to_string(fRatio));
      CHECK(fGotRatio >= 0.25f - 1e-6f && fGotRatio <= 4 + 1e-6f);
    }
  }
  CHECK(At(Out, 1, 0) > 1.1f);                        // the paths' brighter hull: the decal brighter with it
  CHECK(std::fabs(At(Out, 3, 0) - 1) <= 1e-3f);       // the raster's shade: as the raster
  CHECK(std::fabs(At(Out, 5, 0) - 1) <= 1e-3f);       // a frame without the sun: at least as the raster
  CHECK(At(Out, 7, 0) == 1);                          // no raster light
  CHECK(std::fabs(At(Out, 9, 0) - 4) <= 1e-5f);
  CHECK(std::fabs(At(Out, 11, 0) - 0.25f) <= 1e-5f);
}

//! PathLampTarget (d3d12_PTLampSize): a lamp's shadow ray goes towards a point of a disc of the lamp's radius across the way
//! to its centre, picked evenly by u, v (the share of the points within half the radius a quarter, their mean the centre),
//! the radius at most half the distance, the gap at least the radius; none with no radius. Why: a point lamp's shadows
//! are hard, the cut scene lamp's on Jack's face in Training's opening as cut out (the original's light map lit him
//! softly).
XR_TEST(PathTrace_LampTarget)
{
  NeedShaderModel65();
  struct SWay
  {
    float x, y, z, fDistance, fGap, fRadius;
  };
  const SWay Ways[] = { { 0, 0, 1, 10, 0.1f, 0.5f }, { 1, 2, -0.5f, 30, 0.6f, 0.25f }, { 0.3f, -1, 0, 1, 0.1f, 2 }, { -1, 0, 0, 5, 0.2f, 0 } };
  const uint32 s = 16, nWay = s * s, nWays = (uint32)(sizeof(Ways) / sizeof(Ways[0])), n = nWay * nWays;
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_LampTarget);
  d.Test.nTest[0] = n;
  for (const SWay &w : Ways)
    for (uint32 j = 0; j < nWay; j++)
    {
      Push(d.In, w.x, w.y, w.z, w.fDistance);
      Push(d.In, (j % s + 0.5f) / s, (j / s + 0.5f) / s, w.fGap, w.fRadius);
    }
  d.nGroups[0] = Groups64(n);
  d.nOut = n;
  const std::vector<uint32> Out = Gpu().Run(d);
  for (uint32 k = 0; k < nWays; k++)
  {
    const SWay &w = Ways[k];
    const float fLen = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
    const float vDir[3] = { w.x / fLen, w.y / fLen, w.z / fLen };
    const float r = std::min(w.fRadius, 0.5f * w.fDistance);
    const std::string What = "way " + std::to_string(k);
    float vMean[3] = {}, fMax = 0;
    uint32 nInner = 0;
    for (uint32 j = 0; j < nWay; j++)
    {
      const uint32 i = k * nWay + j;
      const float o[3] = { At(Out, i, 0), At(Out, i, 1), At(Out, i, 2) };
      const float fOff = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
      CHECK_MSG(std::fabs(o[0] * vDir[0] + o[1] * vDir[1] + o[2] * vDir[2]) <= 1e-3f * w.fDistance, What + ": off the disc");
      CHECK_MSG(fOff <= r * 1.001f + 1e-5f, What + ": " + std::to_string(fOff) + " beyond the radius " + std::to_string(r));
      CHECK_MSG(std::fabs(At(Out, i, 3) - std::max(w.fGap, r)) <= 1e-6f, What + ": the gap " + std::to_string(At(Out, i, 3)));
      for (int c = 0; c < 3; c++)
        vMean[c] += o[c] / nWay;
      fMax = std::max(fMax, fOff);
      nInner += fOff < 0.5f * r ? 1 : 0;
    }
    if (r <= 0)
    {
      CHECK_MSG(fMax <= 1e-5f, What + ": moved without a radius");
      continue;
    }
    CHECK_MSG(fMax > 0.9f * r, What + ": the disc's edge not reached");
    CHECK_MSG(std::sqrt(vMean[0] * vMean[0] + vMean[1] * vMean[1] + vMean[2] * vMean[2]) <= 0.02f * r, What + ": the mean off the centre");
    CHECK_MSG(std::fabs((float)nInner / nWay - 0.25f) <= 0.04f, What + ": " + std::to_string(nInner) + " points within half the radius");
  }
}
