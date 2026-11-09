// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: VolumetricTests.cpp
//	Description: Unit tests of the volumetric light's sun by rays
//	(d3d12_VolumetricLightRays, Shaders/Volumetric.hlsli): the sun's state
//	at a froxel's point from its rays, the part of the air around in the
//	shadow from the states of its points, and the froxel a world position
//	is in (the injection finds the states of the disc's points so) against
//	the froxels' world positions, with a view as Volumetric.cpp makes it
//	(the projection off its centre too: the upscaler's jitter); the
//	textures whose surfaces are pieces of the sky, which its rays pass
//	(PbrCompanion::IsSky).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_VolSun.h"
#include "Test_VolFroxel.h"
#include "PbrCompanion.h"

#include <cmath>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr float LIT = 1, SHADOW = 0, NOT_AIR = -1, OUTSIDE = 2;

  struct SSunCase
  {
    bool bNear, bFar;
    float s[8];
  };

  //! (VolSunState, VolAirInShadow) of each case.
  std::vector<std::pair<float, float>> RunSun(const std::vector<SSunCase> &Cases)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_VolSun);
    for (const SSunCase &c : Cases)
    {
      Push(d.In, c.bNear ? 1.0f : 0.0f, c.bFar ? 1.0f : 0.0f, 0, 0);
      Push(d.In, c.s[0], c.s[1], c.s[2], c.s[3]);
      Push(d.In, c.s[4], c.s[5], c.s[6], c.s[7]);
    }
    d.Test.nTest[0] = (uint32)Cases.size();
    d.nGroups[0] = Groups64(Cases.size());
    d.nOut = Cases.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<std::pair<float, float>> v;
    for (size_t i = 0; i < Cases.size(); i++)
      v.push_back({ At(Out, i, 0), At(Out, i, 1) });
    return v;
  }

  SVec3 Cross(SVec3 a, SVec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

  //! A view as the renderer's: the camera C looking along F (the up U towards +z), its world to view matrix V and the
  //! projection P (row vectors, the game's: w the view depth) of the given focal terms and centre offsets (the jitter);
  //! the froxels' rays as Volumetric.cpp makes them from the two.
  struct SView
  {
    float WorldToClip[16];
    SVec3 C, RayO, RayX, RayY;
  };

  SView MakeView(SVec3 C, SVec3 F, float fP0, float fP5, float fP8, float fP9)
  {
    const SVec3 R = Normalize(Cross(F, { 0, 0, 1 })), U = Cross(R, F), B = -F;
    float V[16] = { R.x, U.x, B.x, 0, R.y, U.y, B.y, 0, R.z, U.z, B.z, 0, -Dot(C, R), -Dot(C, U), -Dot(C, B), 1 };
    const float fNear = 0.25f, fFar = 700;
    const float P[16] = { fP0, 0, 0, 0, 0, fP5, 0, 0, fP8, fP9, fFar / (fNear - fFar), -1, 0, 0, fNear * fFar / (fNear - fFar), 0 };
    SView v;
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
      {
        float f = 0;
        for (int k = 0; k < 4; k++)
          f += V[r * 4 + k] * P[k * 4 + c];
        v.WorldToClip[r * 4 + c] = f;
      }
    // Volumetric.cpp: the view's axes from V's columns, vRayX = right / P0, vRayY = up / P5, vRayO = axis / P11 - vRayX
    // P8 / P11 - vRayY P9 / P11.
    const SVec3 Right = { V[0], V[4], V[8] }, Up = { V[1], V[5], V[9] }, Axis = { V[2], V[6], V[10] };
    const float fW = P[11];
    v.C = C;
    v.RayX = Right * (1 / P[0]);
    v.RayY = Up * (1 / P[5]);
    v.RayO = Axis * (1 / fW) + v.RayX * (-P[8] / fW) + v.RayY * (-P[9] / fW);
    return v;
  }
}

// Why: a froxel's point is air the sun reaches when neither of its rays hits, in a shadow when only the one beyond
// SHAFT_SURFACE does, and not air (under a roof, inside a wall or the ground) when the near one does, whatever the far
// one found (it is not traced then). The air around in the shadow counts the shadowed points of those that are air,
// leaving out the points not in the air and those out of the volume: a shaft through a roof's hole (its air around under
// the roof, in the shadow) gets all of the sun's light, open air none, and a point with no air around none either.
XR_TEST(Volumetric_SunStates)
{
  const std::vector<SSunCase> Cases = {
    { false, false, { LIT, LIT, LIT, LIT, LIT, LIT, LIT, LIT } },
    { false, true, { SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW, SHADOW } },
    { true, false, { NOT_AIR, NOT_AIR, NOT_AIR, NOT_AIR, NOT_AIR, NOT_AIR, NOT_AIR, NOT_AIR } },
    { true, true, { LIT, SHADOW, LIT, SHADOW, LIT, SHADOW, LIT, SHADOW } },
    { false, false, { SHADOW, SHADOW, NOT_AIR, NOT_AIR, LIT, OUTSIDE, OUTSIDE, SHADOW } },
    { false, false, { OUTSIDE, OUTSIDE, OUTSIDE, OUTSIDE, OUTSIDE, OUTSIDE, OUTSIDE, OUTSIDE } },
  };
  const auto v = RunSun(Cases);
  CHECK_NEAR(v[0].first, LIT, 0);
  CHECK_NEAR(v[1].first, SHADOW, 0);
  CHECK_NEAR(v[2].first, NOT_AIR, 0);
  CHECK_NEAR(v[3].first, NOT_AIR, 0);
  CHECK_NEAR(v[0].second, 0, 0);         // open air: none in the shadow
  CHECK_NEAR(v[1].second, 1, 0);         // all of it
  CHECK_NEAR(v[2].second, 0, 0);         // no air around
  CHECK_NEAR(v[3].second, 0.5, 1e-6);
  CHECK_NEAR(v[4].second, 3.0f / 4, 1e-6);  // 3 shadowed of the 4 points in the air
  CHECK_NEAR(v[5].second, 0, 0);         // all out of the volume
}

// d3d12_VolumetricLightRaysHalf. Why: a froxel that no thread takes in either of two frames keeps a state from a view
// long gone (a shaft that stays where a door closed), one taken in both saves nothing; the froxels of a frame must be
// spread evenly, each one's neighbours the other frame's, or a whole slice would lag; and a froxel, tracing in every
// other frame, must still take its points from all of the sequence (every other one of it in turn lies in one half of
// the froxel).
XR_TEST(Volumetric_SunHalf)
{
  constexpr int W = 6, H = 4, Z = 4;   // the froxels; the threads W / 2 across with the rays halved
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_VolSun);
  struct SItem
  {
    int x, y, z, nFrame;
    bool bHalf;
  };
  std::vector<SItem> Items;
  for (int nFrame : { 0, 1, 2, 1001 })
    for (int z = 0; z < Z; z++)
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
          for (int h = 0; h < 2; h++)
            if (!h || x < W / 2)
              Items.push_back({ x, y, z, nFrame, h != 0 });
  for (const SItem &t : Items)
  {
    Push(d.In, 0.0f, 0.0f, (float)t.nFrame, t.bHalf ? 2.0f : 1.0f);
    Push(d.In, (float)t.x, (float)t.y, (float)t.z, 0.0f);
    Push(d.In, 0.0f, 0.0f, 0.0f, 0.0f);
  }
  d.Test.nTest[0] = (uint32)Items.size();
  d.nGroups[0] = Groups64(Items.size());
  d.nOut = Items.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  // The froxels taken in a frame: how often each.
  const auto Taken = [&](int nFrame, bool bHalf)
  {
    std::vector<int> n(W * H * Z, 0);
    for (size_t i = 0; i < Items.size(); i++)
    {
      const SItem &t = Items[i];
      if (t.nFrame != nFrame || t.bHalf != bHalf)
        continue;
      const uint32 x = Out[4 * i], y = Out[4 * i + 1], z = Out[4 * i + 2];
      CHECK_MSG(x < (uint32)W && y == (uint32)t.y && z == (uint32)t.z, "the froxel of a thread");
      if (x < (uint32)W)
        n[(z * H + y) * W + x]++;
    }
    return n;
  };
  for (int nFrame : { 0, 1, 1001 })
    for (int k : Taken(nFrame, false))
      CHECK(k == 1);                     // without it every froxel, every frame
  const std::vector<int> a = Taken(0, true), b = Taken(1, true), c = Taken(2, true), e = Taken(1001, true);
  for (int z = 0; z < Z; z++)
  {
    for (int y = 0; y < H; y++)
    {
      for (int x = 0; x < W; x++)
      {
        const int i = (z * H + y) * W + x;
        CHECK(a[i] + b[i] == 1);         // in one of two frames that follow each other
        CHECK(a[i] == c[i]);             // and again two frames later
        // Its neighbours in the other frame.
        if (x + 1 < W)
          CHECK(e[i] != e[i + 1]);
        if (y + 1 < H)
          CHECK(e[i] != e[i + W]);
        if (z + 1 < Z)
          CHECK(e[i] != e[i + W * H]);
      }
    }
  }
  // The points' sequence: all 32 places in 32 frames, and all 32 in the 32 even frames and in the 32 odd ones of 64.
  SDispatch p;
  p.Shader = XR_BYTECODE(Test_VolSun);
  for (int nFrame = 0; nFrame < 64; nFrame++)
  {
    Push(p.In, 0.0f, 0.0f, (float)(1000 + nFrame), 0.0f);
    Push(p.In, 0.0f, 0.0f, 0.0f, 0.0f);
    Push(p.In, 0.0f, 0.0f, 0.0f, 0.0f);
  }
  p.Test.nTest[0] = 64;
  p.nGroups[0] = 1;
  p.nOut = 64;
  const std::vector<uint32> Points = Gpu().Run(p);
  uint32 nAll = 0, nEven = 0, nOdd = 0;
  for (int nFrame = 0; nFrame < 64; nFrame++)
  {
    const int k = (int)At(Points, nFrame, 2);
    CHECK(k >= 0 && k < 32);
    if (nFrame < 32)
      nAll |= 1u << (k & 31);
    ((nFrame & 1) ? nOdd : nEven) |= 1u << (k & 31);
  }
  CHECK(nAll == 0xffffffffu && nEven == 0xffffffffu && nOdd == 0xffffffffu);
}

// Why: the injection looks up the state of each point of the disc around a froxel's point in the froxel the point is in,
// by the view's world to clip matrix (VolFroxelCoord), while the rays' kernel places each froxel's point by the view's
// rays (VolFroxelPos at VolSliceDepth); the two must agree over the whole volume, also with the projection off its centre
// (the upscaler's jitter moves it each frame), or the shafts' air around would be looked up a froxel off. The disc's
// points lie across the sun's direction within its radius.
XR_TEST(Volumetric_FroxelCoords)
{
  const float fNear = 0.5f, fFar = 60;
  const float fLog = std::log2(fFar / fNear);
  for (int nView = 0; nView < 3; nView++)
  {
    const SView View = nView == 0   ? MakeView({ 317.6f, 999.2f, 21.7f }, Normalize({ -0.472f, 0.852f, 0.225f }), 0.8418f, 1.4966f, 0, 0)
                       : nView == 1 ? MakeView({ 10, -20, 5 }, Normalize({ 0.3f, 0.2f, -0.9f }), 0.8418f, 1.4966f, 0.0007f, -0.0011f)
                                    : MakeView({ 0, 0, 100 }, Normalize({ 1, 0, 0 }), 1.2f, 2.1333f, -0.0004f, 0.0009f);
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_VolFroxel);
    for (int r = 0; r < 4; r++)
      Push(d.In, View.WorldToClip[r * 4], View.WorldToClip[r * 4 + 1], View.WorldToClip[r * 4 + 2], View.WorldToClip[r * 4 + 3]);
    Push(d.In, View.C, 1);
    Push(d.In, View.RayO, 0);
    Push(d.In, View.RayX, 0);
    Push(d.In, View.RayY, 0);
    Push(d.In, fNear, fLog, 1 / fLog, fFar);
    CRandom Rnd(0x324 + nView);
    std::vector<SVec3> Cells;
    for (int i = 0; i < 512; i++)
    {
      const SVec3 c = { Rnd.Range(0.001f, 0.999f), Rnd.Range(0.001f, 0.999f), Rnd.Range(0.001f, 0.999f) };
      Cells.push_back(c);
      Push(d.In, c, Rnd.Unit());
    }
    const SVec3 Sun = Normalize({ 0.3f, -0.5f, 0.8f });
    const float fRadius = 4;
    d.Test.nTest[0] = (uint32)Cells.size();
    d.Test.vTest[0] = Sun.x;
    d.Test.vTest[1] = Sun.y;
    d.Test.vTest[2] = Sun.z;
    d.Test.vTest[3] = fRadius;
    d.nGroups[0] = Groups64(Cells.size());
    d.nOut = 2 * Cells.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    for (size_t i = 0; i < Cells.size(); i++)
    {
      const SVec3 c = Vec(Out, 2 * i), t = Vec(Out, 2 * i + 1);
      const std::string Where = std::to_string(nView) + "/" + std::to_string(i);
      // A thousandth of the volume each way: far less than a froxel (1/160, 1/90, 1/128).
      CHECK_MSG(std::fabs(c.x - Cells[i].x) < 1e-3f && std::fabs(c.y - Cells[i].y) < 1e-3f && std::fabs(c.z - Cells[i].z) < 1e-3f,
                Where + ": (" + std::to_string(c.x) + " " + std::to_string(c.y) + " " + std::to_string(c.z) + ") against (" +
                  std::to_string(Cells[i].x) + " " + std::to_string(Cells[i].y) + " " + std::to_string(Cells[i].z) + ")");
      CHECK_MSG(Length(t) <= fRadius * 1.0001f && std::fabs(Dot(t, Sun)) < 1e-4f, Where + ": the disc's point");
    }
  }
}

// Why: Training's bunker closes its roof's openings with planes of its sky box's side (Textures\SKYS\TRAINING\box_5.dds,
// lit by themselves); in the scene for rays they hid the sun from the whole room, so its air had no shafts. A texture of
// the sky boxes makes its surfaces pieces of the sky (XR_RAY_SKY), which the volumetric light's rays towards the sun
// pass; no other texture may (a wall, a texture whose name only has "sky" in it, one in another folder).
XR_TEST(Volumetric_SkyTextures)
{
  CHECK(PbrCompanion::IsSky("textures/skys/training/box_5"));
  CHECK(PbrCompanion::IsSky("textures/skys/sky_day/box_1"));
  CHECK(!PbrCompanion::IsSky("textures/glm/ww2style/walls/concrete_axx5"));
  CHECK(!PbrCompanion::IsSky("objects/outdoor/lights/sun_rays_textureb"));
  CHECK(!PbrCompanion::IsSky("textures/sky_box"));
  CHECK(!PbrCompanion::IsSky("levels/training/textures/skys/box_5"));
  CHECK(!PbrCompanion::IsSky("textures/skys"));
  CHECK(!PbrCompanion::IsSky(""));
}
