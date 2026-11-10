// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AlphaDepthTests.cpp
//	Description: Unit tests of the draws of the leaves' and the grass's
//	depth pass (AlphaDepth.h, d3d12_FoliagePrepass): the vegetation's
//	draws are in, each flag that changes the alpha after the texture is
//	read keeps a draw out, and every flag General.hlsl's ShadePixel reads
//	between the texture and the test is one of those or one classified
//	here as changing the colour alone.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestTypes.h"

#include "AlphaDepth.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace XrTest;

namespace
{
  //! A leaf or a blade of grass (Draw3D.cpp's eDC_AlphaTestVColor): the texture times the vertex colour, lit, the test at
  //! 128 / 255, the sun through it and the cascades.
  constexpr uint32 PLANT = DF_TEX0 | DF_ALPHATEST | DF_VCOLOR | DF_VCOLOR_X2 | DF_LIGHTING | DF_SHADOWS;
  constexpr uint32 PLANT2 = DF2_FOLIAGE | DF2_SUN_CASCADES | DF2_SHADOW_TINT;
  constexpr float REF = 128.0f / 255.0f;
}

// Why: the vegetation's leaves and grass are what the pass is for; the texture's alpha alone (DF_TEX0_ALPHA_ONLY) is the
// same product, and the light map's and the detail's second texture leave the alpha.
XR_TEST(AlphaDepth_Plants)
{
  CHECK(AlphaDepth::Covers(PLANT, PLANT2, REF, 0));
  CHECK(AlphaDepth::Covers(PLANT | DF_TEX0_ALPHA_ONLY, 0, REF, 0));
  CHECK(AlphaDepth::Covers(PLANT | DF_TEX1 | DF_TEX1_LIGHTMAP, 0, REF, 0));
  CHECK(AlphaDepth::Covers(PLANT | DF_TEX1 | DF_TEX1_DETAIL, 0, REF, 0));
}

// Why: a pixel the depth pass writes and the colour pass discards keeps the leaf's depth over what is behind it, and one
// the colour pass keeps where the depth pass wrote none is lost (its test EQUAL): no draw whose alpha at the test is
// other than the product the depth pass compares, nor one without the test or with alpha to coverage.
XR_TEST(AlphaDepth_OtherAlpha)
{
  for (int b = 0; b < 32; b++)
  {
    const uint32 f = 1u << b;
    if (AlphaDepth::OTHER_ALPHA & f)
      CHECK_MSG(!AlphaDepth::Covers(PLANT | f, PLANT2, REF, 0), "nFlags bit " + std::to_string(b));
    if (AlphaDepth::OTHER_ALPHA2 & f)
      CHECK_MSG(!AlphaDepth::Covers(PLANT, PLANT2 | f, REF, 0), "nFlags2 bit " + std::to_string(b));
  }
  CHECK(!AlphaDepth::Covers(PLANT & ~DF_TEX0, PLANT2, REF, 0));
  CHECK(!AlphaDepth::Covers(PLANT & ~DF_ALPHATEST, PLANT2, REF, 0));
  CHECK(!AlphaDepth::Covers(PLANT, PLANT2, 0, 0));
  CHECK(!AlphaDepth::Covers(PLANT, PLANT2, -REF, 0));
  CHECK(!AlphaDepth::Covers(PLANT, PLANT2, REF, 7));
  CHECK(!AlphaDepth::Covers(PLANT | DF_TEX1, PLANT2, REF, 0));
}

// Why: a flag added to ShadePixel between the texture's read and the test may change the alpha there; each flag it reads
// in that stretch is kept out by Covers or is classified below as changing the colour alone (read in the shader).
XR_TEST(AlphaDepth_SourceLint)
{
  namespace fs = std::filesystem;
  const fs::path Path = fs::path(XR_RENDERER_SOURCE_DIR) / "Shaders" / "General.hlsl";
  if (!fs::is_regular_file(Path))
    XR_SKIP("the renderer's sources are not where the tests were built from");
  std::ifstream f(Path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string Text = ss.str();
  const size_t nFrom = Text.find("  float4 vTex = float4(1, 1, 1, 1);");
  const size_t nTo = nFrom == std::string::npos ? nFrom : Text.find("    if (HasFlag(DF_ALPHATEST_LESS))", nFrom);
  CHECK_MSG(nFrom != std::string::npos && nTo != std::string::npos, "ShadePixel's texture read and alpha test not found");
  if (nFrom == std::string::npos || nTo == std::string::npos)
    return;
  // The rgb's alone (the light, the highlights, the shadows, the glows, the second texture of a light map or a detail).
  const std::set<std::string> Colour = {
    "DF_SPECULAR", "DF_LIGHTING", "DF_BUMP", "DF_CAUSTICS", "DF_SHADOWS", "DF_PIXEL_LIGHTING", "DF_LIGHTMAP_DIR", "DF_TERRAIN_CLIFF",
    "DF_TEX1_LIGHTMAP", "DF_TEX1_DETAIL", "DF_TEX0_ALPHA_ONLY",
    "DF2_SHADOW_TINT", "DF2_SUN_CASCADES", "DF2_MATERIAL_LIGHT", "DF2_GLOW_FALLOFF", "DF2_CASCADES_LIVE", "DF2_AO_BAKED",
    "DF2_AMBIENT_CUBE", "DF2_ALPHA_GLOW", "DF2_TERRAIN_LIGHT", "DF2_SKY_GLOW", "DF2_REFL_AMOUNT", "DF2_NO_SUN", "DF2_LIGHTMAP_LIGHTS",
    "DF2_HDR_CORE", "DF2_FOLIAGE", "DF2_ADD_TEX4", "DF2_PT_MASK", "DF2_PT_SHIFT" };
  // Those that change the alpha or the texture's, and the test's own: Covers keeps their draws out.
  const std::map<std::string, std::pair<uint32, uint32>> Out = {
    { "DF_TEX1", { DF_TEX1, 0 } }, { "DF_SHADOW_PASS", { DF_SHADOW_PASS, 0 } }, { "DF_ALPHA_FROM_TEX", { DF_ALPHA_FROM_TEX, 0 } },
    { "DF_ALPHATEST_LESS", { DF_ALPHATEST_LESS, 0 } }, { "DF_DECAL_FADE", { DF_DECAL_FADE, 0 } }, { "DF_CUBE0", { DF_CUBE0, 0 } },
    { "DF_TEX0_ARRAY", { DF_TEX0_ARRAY, 0 } }, { "DF2_GLASS", { 0, DF2_GLASS } }, { "DF2_PROJECTOR", { 0, DF2_PROJECTOR } },
    { "DF2_MODULATE", { 0, DF2_MODULATE } }, { "DF2_TEX4_LAYER", { 0, DF2_TEX4_LAYER } }, { "DF2_BUMP_OFFSET", { 0, DF2_BUMP_OFFSET } },
    { "DF2_CUBE_SHADER", { DF_CUBE0, 0 } } };
  const std::regex Flag(R"(\bDF2?_[A-Z0-9_]+\b)");
  const std::string Stretch = Text.substr(nFrom, nTo - nFrom);
  int nFlags = 0;
  for (std::sregex_iterator it(Stretch.begin(), Stretch.end(), Flag), end; it != end; ++it)
  {
    const std::string Name = it->str();
    nFlags++;
    if (Name == "DF_TEX0" || Name == "DF_ALPHATEST" || Colour.count(Name))
      continue;
    const auto o = Out.find(Name);
    if (!CHECK_MSG(o != Out.end(), Name + " is read between the texture and the test: classify it (AlphaDepth.h)"))
      continue;
    CHECK_MSG(!AlphaDepth::Covers(PLANT | o->second.first, PLANT2 | o->second.second, REF, 0), Name);
  }
  CHECK(nFlags > 40);
}
