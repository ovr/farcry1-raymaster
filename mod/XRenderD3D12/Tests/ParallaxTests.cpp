// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: ParallaxTests.cpp
//	Description: Unit tests of the RayMaster maps (r_Parallax,
//	r_PbrNormals): on the CPU which file a texture's maps are
//	(PbrCompanion.h), a BC3 map's height decoded and found flat, which
//	maps are fit to draw; on the GPU parallax occlusion mapping as
//	General.hlsl takes it (Parallax.hlsli, TestParallax.hlsl): no offset
//	at the reference height and seen straight on, the analytic offset
//	of a constant height and of a ramp, the step's wall within a step of
//	the march, the shift bounded edge on and none from behind, the fade
//	by the depth's size in pixels, the texture matrix's turn without its
//	scale, the steps by the view's angle.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "PbrCompanion.h"

#include "Test_ParallaxUV.h"
#include "Test_ParallaxParts.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr float REF = PbrCompanion::FLAT_HEIGHT / 255.0f;   //!< Draw3D.cpp's vParallax.w

  //! A BC3 block whose alpha half has the end points a0, a1 and every texel's index n (the colour half zeros).
  std::vector<uint8_t> AlphaBlock(int a0, int a1, int n)
  {
    std::vector<uint8_t> b(16, 0);
    b[0] = (uint8_t)a0;
    b[1] = (uint8_t)a1;
    uint64_t nBits = 0;
    for (int i = 0; i < 16; i++)
      nBits |= (uint64_t)(n & 7) << (3 * i);
    for (int i = 0; i < 6; i++)
      b[2 + i] = (uint8_t)(nBits >> (8 * i));
    return b;
  }

  //! One march: the geometry's uv, the shift from the height 0 to 1, the reference, the steps.
  struct SMarch
  {
    float u, v, su, sv, fRef;
    int nSteps;
  };

  //! What ParallaxUV gave each march (CSParallaxUV): (u, v, the height there) for the height nHeight (TestParallax.hlsl's
  //! g_Test.nTest.y) with its values a, b; Textures for nHeight 3.
  std::vector<SVec3> March(const std::vector<SMarch> &In, uint32 nHeight, float a = 0, float b = 0, std::vector<STestTexture> Textures = {})
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_ParallaxUV);
    d.Test.nTest[0] = (uint32)In.size();
    d.Test.nTest[1] = nHeight;
    d.Test.vTest[0] = a;
    d.Test.vTest[1] = b;
    d.nGroups[0] = Groups64(In.size());
    d.Textures = std::move(Textures);
    for (const SMarch &m : In)
    {
      Push(d.In, m.u, m.v, m.su, m.sv);
      Push(d.In, m.fRef, (float)m.nSteps, 1.0f / 256, 0);
    }
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SVec3> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back(Vec(Out, i));
    return R;
  }

  //! One view for CSParallaxParts: towards the eye, the depth, the tangent frame, the gradients' lengths.
  struct SView
  {
    SVec3 vToEye;
    float fDepth;
    SVec3 T, B, N;
    float fDx, fDy;
  };

  struct SParts
  {
    SVec3 vView;
    float fSteps;
    float vShift[2], vTurned[2];
    float fFade;
  };

  //! CSParallaxParts of each view, the texture matrix's rows (r00, r01), (r10, r11).
  std::vector<SParts> Parts(const std::vector<SView> &In, float r00 = 1, float r01 = 0, float r10 = 0, float r11 = 1)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_ParallaxParts);
    d.Test.nTest[0] = (uint32)In.size();
    d.Test.vTest2[0] = r00;
    d.Test.vTest2[1] = r01;
    d.Test.vTest2[2] = r10;
    d.Test.vTest2[3] = r11;
    d.nGroups[0] = Groups64(In.size());
    for (const SView &v : In)
    {
      Push(d.In, v.vToEye, v.fDepth);
      Push(d.In, v.T, 0);
      Push(d.In, v.B, 0);
      Push(d.In, v.N, 0);
      Push(d.In, v.fDx, 0, 0, v.fDy);
    }
    d.nOut = 3 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SParts> R;
    for (size_t i = 0; i < In.size(); i++)
    {
      SParts p;
      p.vView = Vec(Out, 3 * i);
      p.fSteps = At(Out, 3 * i, 3);
      p.vShift[0] = At(Out, 3 * i + 1, 0);
      p.vShift[1] = At(Out, 3 * i + 1, 1);
      p.vTurned[0] = At(Out, 3 * i + 1, 2);
      p.vTurned[1] = At(Out, 3 * i + 1, 3);
      p.fFade = At(Out, 3 * i + 2, 0);
      R.push_back(p);
    }
    return R;
  }

  //! A view of the unit frame towards vToEye, depth 0.03, gradients of a pixel 1 / 1024.
  SView View(SVec3 vToEye)
  {
    return { vToEye, 0.03f, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, 1.0f / 1024, 1.0f / 1024 };
  }
}

// Why: Texture.cpp finds a texture's maps by the file it was read from (lower case, '/' separated, without the
// extension) in the game's folder RayMaster, as AssetViewer writes them (<the pak's path>_pbr_m.dds); a wrong name finds
// none and the feature silently does nothing.
XR_TEST(Pbr_Path)
{
  CHECK(PbrCompanion::Path("objects/indoor/cave/rock01", "_pbr_m") == "raymaster/objects/indoor/cave/rock01_pbr_m.dds");
  CHECK(PbrCompanion::Path("textures/common/concrete", "_pbr_n") == "raymaster/textures/common/concrete_pbr_n.dds");
}

// Why: the height is the BC3 map's alpha; the flatness check decodes it as the GPU does, in both of the block's modes
// (a0 > a1: six values between them; else four, indices 6 and 7 the ends 0 and 255). A wrong palette calls a map with
// hollows flat (no parallax) or a flat one not.
XR_TEST(Pbr_AlphaBlock)
{
  uint8_t a[16];
  const int vEight[8] = { 200, 100, 185, 171, 157, 142, 128, 114 };
  for (int n = 0; n < 8; n++)
  {
    PbrCompanion::DecodeAlphaBlock(AlphaBlock(200, 100, n).data(), a);
    for (uint8_t x : a)
      CHECK_NEAR(x, vEight[n], 0);
  }
  const int vSix[8] = { 100, 200, 120, 140, 160, 180, 0, 255 };
  for (int n = 0; n < 8; n++)
  {
    PbrCompanion::DecodeAlphaBlock(AlphaBlock(100, 200, n).data(), a);
    for (uint8_t x : a)
      CHECK_NEAR(x, vSix[n], 0);
  }
  // Each texel its own index.
  std::vector<uint8_t> b = AlphaBlock(255, 0, 0);
  uint64_t nBits = 0;
  for (int i = 0; i < 16; i++)
    nBits |= (uint64_t)(i & 7) << (3 * i);
  for (int i = 0; i < 6; i++)
    b[2 + i] = (uint8_t)(nBits >> (8 * i));
  PbrCompanion::DecodeAlphaBlock(b.data(), a);
  for (int i = 0; i < 16; i++)
    CHECK_NEAR(a[i], i % 8 == 0 ? 255 : i % 8 == 1 ? 0 : ((7 - (i % 8 - 1)) * 255) / 7, 0);
}

// Why: AssetViewer writes a map of the reference height (0.5) where its network found no relief (31 of 2286 in the
// game's set); those must not march (no offset, all the cost), and one texel off the plane must.
XR_TEST(Pbr_FlatHeight)
{
  std::vector<uint8_t> Map;
  for (int i = 0; i < 4; i++)
  {
    const std::vector<uint8_t> b = AlphaBlock(128, 128, 0);
    Map.insert(Map.end(), b.begin(), b.end());
  }
  CHECK(PbrCompanion::IsFlatHeight(Map.data(), 4));
  // 129 to 127 between: within 1 of 128.
  for (int n = 0; n < 8; n++)
    CHECK(PbrCompanion::IsFlatHeight(AlphaBlock(129, 127, n).data(), 1));
  // 130 is out, and the 6 value mode's 0 and 255 at the indices 6 and 7.
  CHECK(!PbrCompanion::IsFlatHeight(AlphaBlock(130, 128, 0).data(), 1));
  CHECK(!PbrCompanion::IsFlatHeight(AlphaBlock(128, 129, 6).data(), 1));
  CHECK(!PbrCompanion::IsFlatHeight(AlphaBlock(128, 129, 7).data(), 1));
  std::vector<uint8_t> Last = Map;
  Last[16 * 3 + 0] = 140;
  CHECK(!PbrCompanion::IsFlatHeight(Last.data(), 4));
  CHECK(!PbrCompanion::IsFlatHeight(Map.data(), 0));
  CHECK(!PbrCompanion::IsFlatHeight(nullptr, 4));
}

// Why: a compressed map gets no levels made (Texture.cpp), and one level alone shimmers under parallax as the view
// moves; a cube or a map of another aspect is not the texture's (the offset would land elsewhere on it).
XR_TEST(Pbr_Usable)
{
  CHECK(PbrCompanion::IsUsable(512, 512, 10, 1, 512, 512));
  CHECK(PbrCompanion::IsUsable(512, 512, 8, 1, 512, 512));    // to 4 x 4: BC's last full blocks
  CHECK(!PbrCompanion::IsUsable(512, 512, 7, 1, 512, 512));
  CHECK(!PbrCompanion::IsUsable(512, 512, 1, 1, 512, 512));
  CHECK(!PbrCompanion::IsUsable(512, 512, 11, 1, 512, 512));  // more levels than a chain has
  CHECK(PbrCompanion::IsUsable(512, 256, 10, 1, 1024, 512));  // the texture's aspect, a level smaller
  CHECK(!PbrCompanion::IsUsable(512, 256, 10, 1, 512, 512));
  CHECK(!PbrCompanion::IsUsable(512, 512, 10, 6, 512, 512));
  CHECK(PbrCompanion::IsUsable(4, 4, 1, 1, 4, 4));
  CHECK(!PbrCompanion::IsUsable(8, 8, 1, 1, 8, 8));
  CHECK(!PbrCompanion::IsUsable(0, 512, 10, 1, 512, 512));
  CHECK(PbrCompanion::IsUsable(256, 64, 7, 1, 0, 0));          // a texture of no known size: its aspect not checked
}

// Why: the reference height is the plane of the geometry: there the offset must be none, or the whole of a flat wall
// slides against its light map and its decals as the view moves.
XR_TEST(Parallax_Reference)
{
  std::vector<SMarch> In;
  for (float su : { -0.3f, -0.05f, 0.0f, 0.02f, 0.4f })
    for (float sv : { -0.2f, 0.0f, 0.1f })
      for (int nSteps : { 2, 8, 17, 24 })
        In.push_back({ 0.37f, 0.61f, su, sv, REF, nSteps });
  const std::vector<SVec3> R = March(In, 0, REF);
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK_NEAR(R[i].x, In[i].u, 2e-6);
    CHECK_NEAR(R[i].y, In[i].v, 2e-6);
  }
}

// Why: a constant height h is a plane (h - ref) deep: the view meets it at uv + (h - ref) shift, exactly (the secant of
// two linear heights), the top (h 1) at the march's first step, the bottom (h 0) at its last.
XR_TEST(Parallax_Constant)
{
  for (float h : { 0.0f, 0.1f, 0.35f, 0.5f, 0.8f, 0.97f, 1.0f })
  {
    std::vector<SMarch> In;
    for (int nSteps : { 3, 8, 24 })
      In.push_back({ 0.25f, 0.75f, 0.12f, -0.07f, REF, nSteps });
    const std::vector<SVec3> R = March(In, 0, h);
    for (size_t i = 0; i < In.size(); i++)
    {
      CHECK_NEAR(R[i].x, 0.25f + (h - REF) * 0.12f, 2e-6);
      CHECK_NEAR(R[i].y, 0.75f + (h - REF) * -0.07f, 2e-6);
    }
  }
}

// Why: on a ramp h = u the view meets the slope where h = u0 + (h - ref) s, h = (u0 - ref s) / (1 - s), which a linear
// march with the secant finds exactly; the place must move on with the geometry's uv (no folds).
XR_TEST(Parallax_Ramp)
{
  for (float s : { 0.2f, -0.2f, 0.05f })
  {
    std::vector<SMarch> In;
    for (int k = 0; k <= 20; k++)
      In.push_back({ 0.3f + 0.4f * k / 20, 0.5f, s, 0, REF, 24 });
    const std::vector<SVec3> R = March(In, 1);
    for (size_t i = 0; i < In.size(); i++)
    {
      const float h = (In[i].u - REF * s) / (1 - s);
      CHECK_NEAR(R[i].x, In[i].u + (h - REF) * s, 2e-5);
      CHECK_NEAR(R[i].z, h, 2e-5);
      if (i > 0)
        CHECK(R[i].x > R[i - 1].x);
    }
  }
}

// Why: at a step (a wall in the height) the view meets the wall: the secant lands within one step of the march of it,
// never beyond the ray's ends; seen from the other side the low side's plane alone.
XR_TEST(Parallax_Step)
{
  const float a = 0.2f, b = 0.9f;
  const int nSteps = 24;
  // The ray all over the high side, then all over the low side: their planes.
  std::vector<SMarch> In = { { 0.75f, 0.5f, 0.1f, 0, REF, nSteps }, { 0.45f, 0.5f, -0.1f, 0, REF, nSteps },
                             { 0.47f, 0.5f, -0.2f, 0, REF, nSteps } };
  const std::vector<SVec3> R = March(In, 2, a, b);
  CHECK_NEAR(R[0].x, 0.75f + (b - REF) * 0.1f, 2e-6);
  CHECK_NEAR(R[1].x, 0.45f + (a - REF) * -0.1f, 2e-6);
  // Over the low side down to the wall at u 0.5, then the high side in front of the ray: the wall, within a step.
  CHECK(std::fabs(R[2].x - 0.5f) <= 0.2f / nSteps + 1e-6);
  for (size_t i = 0; i < In.size(); i++)
  {
    const float u0 = In[i].u - REF * In[i].su, u1 = In[i].u + (1 - REF) * In[i].su;
    CHECK(R[i].x >= std::fmin(u0, u1) - 1e-6 && R[i].x <= std::fmax(u0, u1) + 1e-6);
  }
}

// Why: the march reads the height map by the geometry's gradients, its own uv having none: a texture of the reference
// height everywhere gives no offset through the sampler as through the constant.
XR_TEST(Parallax_Texture)
{
  STestTexture t;
  t.nWidth = t.nHeight = 16;
  t.Format = DXGI_FORMAT_R8_UNORM;
  t.Data.assign(256, (uint8_t)PbrCompanion::FLAT_HEIGHT);
  std::vector<SMarch> In = { { 0.13f, 0.91f, 0.3f, -0.1f, REF, 24 }, { 1.7f, -2.2f, -0.05f, 0.2f, REF, 9 } };
  std::vector<SVec3> R = March(In, 3, 0, 0, { t });
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK_NEAR(R[i].x, In[i].u, 2e-6);
    CHECK_NEAR(R[i].y, In[i].v, 2e-6);
  }
  // A texture of one height h: the plane h - ref deep.
  t.Data.assign(256, 230);
  R = March(In, 3, 0, 0, { t });
  for (size_t i = 0; i < In.size(); i++)
    CHECK_NEAR(R[i].x, In[i].u + (230 / 255.0f - REF) * In[i].su, 2e-6);
}

// Why: seen straight on there is nothing to offset; the frame comes interpolated, not normalised (its lengths are no
// share of the shift), and a frame of no size gives none.
XR_TEST(Parallax_View)
{
  std::vector<SView> In = { View({ 0, 0, 5 }), View({ 1, 1, 1 }), View({ 1, 1, 1 }), View({ 3, 0, 4 }) };
  In[2].T = { 2, 0, 0 };
  In[2].B = { 0, 3, 0 };
  In[2].N = { 0, 0, 0.5f };
  In[3].T = { 0, 0, 0 };
  const std::vector<SParts> R = Parts(In);
  CHECK_NEAR(R[0].vView.z, 1, 1e-6);
  CHECK_NEAR(R[0].vShift[0], 0, 0);
  CHECK_NEAR(R[0].vShift[1], 0, 0);
  for (int i = 1; i <= 2; i++)
  {
    CHECK_NEAR(R[i].vView.x, 1 / std::sqrt(3.0f), 1e-6);
    CHECK_NEAR(R[i].vView.y, 1 / std::sqrt(3.0f), 1e-6);
    CHECK_NEAR(R[i].vView.z, 1 / std::sqrt(3.0f), 1e-6);
    // fDepth V.xy / V.z
    CHECK_NEAR(R[i].vShift[0], 0.03f, 1e-6);
    CHECK_NEAR(R[i].vShift[1], 0.03f, 1e-6);
  }
  CHECK_NEAR(R[3].vView.x, 0, 0);
  CHECK_NEAR(R[3].vView.z, 1, 0);
}

// Why: edge on V.xy / V.z grows without bound (the texture stretched to the horizon, swimming); the depth shrinks below
// the cosine PARALLAX_MIN_COS to none at 90 degrees, and seen from behind (two-sided materials) there is none.
XR_TEST(Parallax_Grazing)
{
  std::vector<SView> In;
  for (int k = 0; k <= 40; k++)
  {
    const float c = 1 - k / 40.0f;
    In.push_back(View({ std::sqrt(1 - c * c), 0, c }));
  }
  In.push_back(View({ 1, 0, -0.3f }));
  In.push_back(View({ 0.2f, 0.3f, -1 }));
  const std::vector<SParts> R = Parts(In);
  for (size_t i = 0; i + 2 < In.size(); i++)
  {
    CHECK(R[i].vShift[0] >= 0);
    CHECK(R[i].vShift[0] <= 0.03f / 0.2f + 1e-6);
    CHECK_NEAR(R[i].vShift[1], 0, 1e-7);
  }
  CHECK_NEAR(R[40].vShift[0], 0, 1e-6);
  CHECK_NEAR(R[41].vShift[0], 0, 0);
  CHECK_NEAR(R[42].vShift[0], 0, 0);
  CHECK_NEAR(R[42].vShift[1], 0, 0);
}

// Why: far off the shift is under a pixel and the march all cost: the depth fades by its size in pixels seen straight
// on, all of it from 4 pixels, none at 1 (Draw3D.cpp's PARALLAX_FADE_FROM, _TO), whatever the texture's size.
XR_TEST(Parallax_Fade)
{
  std::vector<SView> In;
  for (float fPixels : { 16.0f, 4.0f, 2.5f, 2.0f, 1.0f, 0.5f })
  {
    SView v = View({ 0, 0, 1 });
    v.fDx = 0.03f / fPixels;
    v.fDy = 0.5f * v.fDx;
    In.push_back(v);
  }
  // The longer gradient counts.
  In.push_back(In[2]);
  In.back().fDy = 0.03f / 1.0f;
  const std::vector<SParts> R = Parts(In);
  CHECK_NEAR(R[0].fFade, 1, 0);
  CHECK_NEAR(R[1].fFade, 1, 1e-6);
  CHECK_NEAR(R[2].fFade, 0.5f, 1e-6);
  CHECK_NEAR(R[3].fFade, 1 / 3.0f, 1e-6);
  CHECK_NEAR(R[4].fFade, 0, 1e-6);
  CHECK_NEAR(R[5].fFade, 0, 0);
  CHECK_NEAR(R[6].fFade, 0, 1e-6);
}

// Why: the tangent frame is the geometry's uv's, the textures are read at the texture matrix's uv' = M uv + t
// (DF_TEXMATRIX): the shift turns and stretches with M, its scale taken out (the depth a share of the texture's side).
XR_TEST(Parallax_TexMatrix)
{
  const std::vector<SView> In = { View({ 0.6f, 0.3f, 0.74f }) };
  const SParts Plain = Parts(In)[0];
  const float sx = Plain.vShift[0], sy = Plain.vShift[1];
  CHECK_NEAR(Plain.vTurned[0], sx, 1e-7);
  CHECK_NEAR(Plain.vTurned[1], sy, 1e-7);
  SParts p = Parts(In, 2, 0, 0, 2)[0];
  CHECK_NEAR(p.vTurned[0], sx, 1e-6);
  CHECK_NEAR(p.vTurned[1], sy, 1e-6);
  p = Parts(In, 0, -1, 1, 0)[0];
  CHECK_NEAR(p.vTurned[0], -sy, 1e-6);
  CHECK_NEAR(p.vTurned[1], sx, 1e-6);
  p = Parts(In, 2, 0, 0, 1)[0];
  CHECK_NEAR(p.vTurned[0], 2 * sx / std::sqrt(2.0f), 1e-6);
  CHECK_NEAR(p.vTurned[1], sy / std::sqrt(2.0f), 1e-6);
  p = Parts(In, 1, 2, 2, 4)[0];
  CHECK_NEAR(p.vTurned[0], sx, 0);
  CHECK_NEAR(p.vTurned[1], sy, 0);
}

// Why: the slanting views cross more of the height and need more steps (thin walls skipped otherwise); straight on a
// third of them.
XR_TEST(Parallax_Steps)
{
  const std::vector<SParts> R = Parts({ View({ 0, 0, 1 }), View({ 1, 0, 1 }), View({ 1, 0, 0 }), View({ 1, 0, 0.1f }) });
  CHECK_NEAR(R[0].fSteps, 8, 0);
  CHECK_NEAR(R[1].fSteps, 24 - 16 * std::sqrt(0.5f), 0.5);
  CHECK_NEAR(R[2].fSteps, 24, 0);
  CHECK(R[3].fSteps > R[1].fSteps && R[3].fSteps <= 24);
}
