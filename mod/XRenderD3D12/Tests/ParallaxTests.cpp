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
//	scale, the steps by the view's angle; the depth table's rows
//	(r_ParallaxMaterials) and the march's gradients stretched to its step
//	(r_ParallaxFilter); the upscaled textures' file, fitness and original
//	sides (r_TexUpscaled).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "PbrCompanion.h"
#include "CommandLine.h"

#include "Test_ParallaxUV.h"
#include "Test_ParallaxParts.h"
#include "Test_ParallaxMarchGrad.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
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

  //! One march's gradients for CSParallaxMarchGrad: the shift, the steps, the pixel's dx and dy, the least length.
  struct SGrad
  {
    float su, sv;
    int nSteps;
    float dx[2], dy[2];
    float fLeast = 0;
  };

  //! ParallaxMarchGrad of each: In with its dx and dy replaced by what it gave.
  std::vector<SGrad> MarchGrad(std::vector<SGrad> In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_ParallaxMarchGrad);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SGrad &g : In)
    {
      Push(d.In, g.su, g.sv, (float)g.nSteps, g.fLeast);
      Push(d.In, g.dx[0], g.dx[1], g.dy[0], g.dy[1]);
    }
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    for (size_t i = 0; i < In.size(); i++)
      for (int k = 0; k < 2; k++)
      {
        In[i].dx[k] = At(Out, i, k);
        In[i].dy[k] = At(Out, i, 2 + k);
      }
    return In;
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

// Why: r_TexUpscaled takes RayMaster/<the found file's base>.dds (beside its maps, no suffix) in place of the texture's
// picture; a path off by its folder or extension finds none (or another texture's, or a map).
XR_TEST(Pbr_UpscaledPath)
{
  CHECK(PbrCompanion::UpscaledPath("objects/vehicles/hemtt/front") == "raymaster/objects/vehicles/hemtt/front.dds");
  CHECK(PbrCompanion::UpscaledPath("textures/glm/m5_steamregulator/stedoubledoor1_d") ==
        "raymaster/textures/glm/m5_steamregulator/stedoubledoor1_d.dds");
  CHECK(PbrCompanion::UpscaledPath("objects/x/foo_ddn") == "raymaster/objects/x/foo_ddn.dds");
  CHECK(PbrCompanion::UpscaledPath("objects/x/foo") != PbrCompanion::Path("objects/x/foo", "_pbr_m"));
}

// Why: the upscaled picture replaces the texture's for every draw of it: a cube's face, another aspect (the uv would
// land elsewhere on it), a smaller picture or one compressed with too few levels (none are made for BC: it would shimmer
// far off) must not; an uncompressed one without levels gets them made, as the texture's would.
XR_TEST(Pbr_UpscaledUsable)
{
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 1024, 11, 1, 512, 512, 10, 1));    // twice the side, a full chain
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 512, 11, 1, 512, 256, 10, 1));     // its aspect
  CHECK(PbrCompanion::IsUpscaledUsable(2048, 2048, 12, 1, 512, 512, 10, 1));    // four times
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 11, 1, 1024, 1024, 11, 1)); // no larger
  CHECK(!PbrCompanion::IsUpscaledUsable(512, 512, 10, 1, 1024, 1024, 11, 1));   // smaller
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 512, 11, 1, 512, 512, 10, 1));    // another aspect
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 11, 6, 512, 512, 10, 6));   // cubes
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 11, 1, 512, 512, 10, 6));
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 12, 1, 512, 512, 10, 1));   // more levels than a chain has
  // The texture's levels to 4 x 4 (8 of 512): the upscaled one's to 4 or 8 will do, to 16 not; one level alone not.
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 1024, 9, 1, 512, 512, 8, 1));
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 1024, 8, 1, 512, 512, 8, 1));
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 7, 1, 512, 512, 8, 1));
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 1, 1, 512, 512, 10, 1));
  // A full chain's last levels: to 4 x 4 is enough (BC's last full blocks).
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 1024, 9, 1, 512, 512, 10, 1));
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 8, 1, 512, 512, 10, 1));
  // A texture without levels (FT_NOMIPS's, a compressed one without): any levels.
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 1024, 1, 1, 512, 512, 1, 1));
  CHECK(PbrCompanion::IsUpscaledUsable(1024, 1024, 11, 1, 512, 512, 1, 1));
  CHECK(!PbrCompanion::IsUpscaledUsable(0, 1024, 11, 1, 512, 512, 10, 1));
  CHECK(!PbrCompanion::IsUpscaledUsable(1024, 1024, 11, 1, 0, 0, 10, 1));
}

// Why: the interface maps its texel rectangles by GetOriginalWidth (UISystem.cpp): an upscaled texture tells the file's
// own sides, as much smaller as r_TexResolution dropped its levels.
XR_TEST(Pbr_UpscaledOriginalSide)
{
  CHECK(PbrCompanion::UpscaledOriginalSide(512, 1024, 1024) == 512);
  CHECK(PbrCompanion::UpscaledOriginalSide(512, 1024, 512) == 256);    // r_TexResolution 1
  CHECK(PbrCompanion::UpscaledOriginalSide(256, 1024, 256) == 64);
  CHECK(PbrCompanion::UpscaledOriginalSide(300, 600, 600) == 300);
  CHECK(PbrCompanion::UpscaledOriginalSide(4, 8, 1) == 1);             // at least 1
  CHECK(PbrCompanion::UpscaledOriginalSide(512, 0, 1024) == 512);
}

// Why: the engine sets the command line's console variables after its start has loaded the menu's and the shaders'
// textures (34 of them upscaled with "r_TexUpscaled 0" there); r_TexUpscaled is taken from the line when registered.
// A longer name that starts with it, or a value missing, must not count; the last one does.
XR_TEST(Pbr_UpscaledCommandLine)
{
  CHECK(CommandLine::IntValue("FarCry.exe -DEVMODE \"r_TexUpscaled 0\"", "r_TexUpscaled", 1) == 0);
  CHECK(CommandLine::IntValue("FarCry.exe -DEVMODE", "r_TexUpscaled", 1) == 1);
  CHECK(CommandLine::IntValue("FarCry.exe r_texupscaled=0", "r_TexUpscaled", 1) == 0);
  CHECK(CommandLine::IntValue("FarCry.exe +R_TEXUPSCALED 0", "r_TexUpscaled", 1) == 0);
  CHECK(CommandLine::IntValue("FarCry.exe \"r_TexUpscaled 0\" \"r_TexUpscaled 1\"", "r_TexUpscaled", 7) == 1);
  CHECK(CommandLine::IntValue("FarCry.exe \"r_TexUpscaledMore 0\"", "r_TexUpscaled", 1) == 1);  // another name
  CHECK(CommandLine::IntValue("FarCry.exe \"xr_TexUpscaled 0\"", "r_TexUpscaled", 1) == 1);
  CHECK(CommandLine::IntValue("FarCry.exe \"r_TexUpscaled\"", "r_TexUpscaled", 1) == 1);           // no value
  CHECK(CommandLine::IntValue("FarCry.exe \"r_TexUpscaled x\"", "r_TexUpscaled", 1) == 1);
  CHECK(CommandLine::IntValue("r_TexUpscaled 2", "r_TexUpscaled", 1) == 2);
}

// Why: r_ParallaxMaterials takes each texture's depth from RayMaster/pbr_depth.tsv (AssetViewer's --pbr-depth) by the
// texture's file as Texture.cpp keys it (lower case, '/' separated); a row read wrongly gives a texture another
// material's depth (metal as deep as stone) or a glass one a relief. Comments, the Windows line ends, a missing last line
// end and a third column are the file's; a share not a number is left out, out of range clamped, the last row counts.
XR_TEST(Pbr_DepthTable)
{
  std::map<std::string, float> m;
  const std::string Text = "# texture\tshare of r_ParallaxDepth\tphysical material\n"
                           "textures/stone\t1.00\tmat_rock\r\n"
                           "Objects\\Indoor\\Glass\t0.00\tmat_glass\r\n"
                           "textures/metal\t0.25\n"
                           "\n"
                           "\r\n"
                           "textures/bad\tabc\tmat_rock\n"
                           "textures/half\t0.5x\n"
                           "textures/nan\tnan\n"
                           "textures/none\n"
                           "textures/empty\t\tmat_rock\n"
                           "\t0.5\n"
                           "textures/huge\t9\n"
                           "textures/neg\t-1\n"
                           "textures/stone\t0.7\tmat_rock\n"
                           "textures/last\t0.3";
  const int nRows = PbrCompanion::ParseDepthTable(Text, [&](const std::string &Base, float f) { m[Base] = f; });
  CHECK(nRows == 7);
  CHECK(m.size() == 6);
  CHECK(!m.count("textures/bad") && !m.count("textures/half") && !m.count("textures/nan") && !m.count("textures/none") &&
        !m.count("textures/empty"));
  CHECK_NEAR(m["textures/stone"], 0.7f, 0);
  CHECK_NEAR(m["objects/indoor/glass"], 0, 0);
  CHECK_NEAR(m["textures/metal"], 0.25f, 0);
  CHECK_NEAR(m["textures/huge"], PbrCompanion::DEPTH_SHARE_MOST, 0);
  CHECK_NEAR(m["textures/neg"], 0, 0);
  CHECK_NEAR(m["textures/last"], 0.3f, 0);
  CHECK(PbrCompanion::ParseDepthTable("", [&](const std::string &, float) { m.clear(); }) == 0);
  CHECK(m.size() == 6);
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

// Why: a height map's fine noise cut r_ParallaxDepth deep stood out of River's temple threshold as spikes at a slanting
// view; with r_ParallaxFilter N the march reads the height over N texels at least and over its step (each gradient that
// long, its direction kept; a gradient of none along its axis), the longer ones as they are; isotropic (a coarser level,
// not the anisotropic filter's taps along the march, which cost more). With r_ParallaxFilter 0 General.hlsl calls none.
XR_TEST(Parallax_MarchGrad)
{
  const float p = 1.0f / 1024, t = 4.0f / 512;
  std::vector<SGrad> In = {
    { 0.15f, 0, 24, { p, 0 }, { 0, p } },                 // the step (0.00625) longer than the pixel: both to it
    { 0.005f, 0, 8, { p, 0 }, { 0, p } },                 // the step under the pixel: as they are
    { 0, 0, 8, { p, 0 }, { 0, p } },                      // no shift, no least: as they are
    { 0, 0, 8, { p, 0 }, { 0, p }, t },                   // the least t: both to it
    { 0, 0, 8, { 0.6f * p, 0.8f * p }, { 0.02f, 0 }, t }, // dx turned, to t along itself; dy longer: as it is
    { 0, 0, 8, { 0, 0 }, { 0, 0 }, t },                   // none: t along u and v
    { 0.3f, 0, 4, { p, 0 }, { 0, p }, t },                // the step (0.075) longer than t: both to the step
    { 0.01f, 0.004f, 8, { -p, 0.3f * p }, { 0.2f * p, -p }, t },
  };
  const std::vector<SGrad> R = MarchGrad(In);
  CHECK_NEAR(R[0].dx[0], 0.15f / 24, 1e-7);
  CHECK_NEAR(R[0].dy[1], 0.15f / 24, 1e-7);
  CHECK_NEAR(R[4].dx[0], 0.6f * t, 1e-7);
  CHECK_NEAR(R[4].dx[1], 0.8f * t, 1e-7);
  CHECK(R[4].dy[0] == 0.02f && R[4].dy[1] == 0);
  CHECK_NEAR(R[5].dx[0], t, 1e-7);
  CHECK_NEAR(R[5].dy[1], t, 1e-7);
  CHECK_NEAR(R[6].dx[0], 0.3f / 4, 1e-7);
  CHECK_NEAR(R[6].dy[1], 0.3f / 4, 1e-7);
  for (size_t i = 0; i < In.size(); i++)
  {
    const float fLen = std::fmax(In[i].fLeast, std::hypot(In[i].su, In[i].sv) / In[i].nSteps);
    for (int k = 0; k < 2; k++)
    {
      const float *a = k ? In[i].dy : In[i].dx, *b = k ? R[i].dy : R[i].dx;
      const float la = std::hypot(a[0], a[1]), lb = std::hypot(b[0], b[1]);
      // As long as it was or the least, whichever is longer; its direction kept; unchanged when long enough.
      CHECK_NEAR(lb, std::fmax(la, fLen), 1e-7);
      if (la > 0)
      {
        CHECK_NEAR(a[0] * b[1] - a[1] * b[0], 0, 1e-9);
        CHECK(a[0] * b[0] + a[1] * b[1] > 0);
      }
      if (la >= fLen)
        CHECK(a[0] == b[0] && a[1] == b[1]);
    }
  }
}
