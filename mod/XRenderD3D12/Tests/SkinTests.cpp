// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: SkinTests.cpp
//	Description: Unit tests of the characters' skin (r_Skin): on the CPU
//	which textures are skin (PbrCompanion.h's IsSkin: the faces and heads
//	under objects/characters/, not their normal and height maps); on the
//	GPU Skin.hlsli (TestHeaders.hlsl's CSSkinSpecular, CSSkinParts): the
//	highlight's share of the light head on (2.8 %) and towards grazing
//	views (Fresnel), none from behind, a rougher lobe lower; the roughness
//	by the gloss, the share by the gloss, the light wrapped round the line
//	where the light passes (lambert's without the scatter, red the
//	farthest) and the paths' G-buffer's skin (PathGBufferSkin) through
//	the RGBA8 target.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "PbrCompanion.h"

#include "Test_SkinSpecular.h"
#include "Test_SkinParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  constexpr double PI = 3.14159265358979;

  //! A light's highlight on skin: the normal, alpha^2, the directions towards the light and the eye.
  struct SSkinCase
  {
    SVec3 vN;
    float fAlpha2;
    SVec3 vToLight, vToEye;
  };

  //! SkinSpecular of each case.
  std::vector<float> RunSpecular(const std::vector<SSkinCase> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_SkinSpecular);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SSkinCase &c : In)
    {
      Push(d.In, c.vN, c.fAlpha2);
      Push(d.In, c.vToLight, 0);
      Push(d.In, c.vToEye, 0);
    }
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<float> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back(At(Out, i, 0));
    return R;
  }

  //! The share of the light from above that the skin's highlight reflects towards the eye at the angle fView (radians
  //! from the normal): SkinSpecular / pi (the BRDF times N.L) integrated over the lights' hemisphere.
  double DirectionalAlbedo(float fAlpha2, double fView)
  {
    const int nTheta = 96, nPhi = 192;
    const SVec3 vEye = { (float)std::sin(fView), 0, (float)std::cos(fView) };
    std::vector<SSkinCase> In;
    for (int t = 0; t < nTheta; t++)
      for (int p = 0; p < nPhi; p++)
      {
        const double th = (t + 0.5) / nTheta * (PI / 2), ph = (p + 0.5) / nPhi * (2 * PI);
        In.push_back({ { 0, 0, 1 }, fAlpha2, { (float)(std::sin(th) * std::cos(ph)), (float)(std::sin(th) * std::sin(ph)), (float)std::cos(th) }, vEye });
      }
    const std::vector<float> R = RunSpecular(In);
    double fSum = 0;
    for (int t = 0; t < nTheta; t++)
    {
      const double th = (t + 0.5) / nTheta * (PI / 2);
      for (int p = 0; p < nPhi; p++)
        fSum += R[(size_t)t * nPhi + p] / PI * std::sin(th);
    }
    return fSum * (PI / 2 / nTheta) * (2 * PI / nPhi);
  }

  //! The parts of one input: SkinRoughness, SkinSpecularShare, PathGBufferSkin's alpha and its roughness back, SkinWrap
  //! (r, g, b) and PathGBufferSkinRough of N.L taken as a stored alpha.
  struct SParts
  {
    float fRough, fShare, fStored, fBack, vWrap[3], fAsAlpha;
  };

  //! The parts of (gloss, matte roughness, oily roughness, N.L) items at the scatter fScatter.
  std::vector<SParts> RunParts(const std::vector<std::array<float, 4>> &In, float fScatter)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_SkinParts);
    d.Test.nTest[0] = (uint32)In.size();
    d.Test.vTest[0] = fScatter;
    d.nGroups[0] = Groups64(In.size());
    for (const auto &a : In)
      Push(d.In, a[0], a[1], a[2], a[3]);
    d.nOut = 2 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SParts> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ At(Out, 2 * i, 0), At(Out, 2 * i, 1), At(Out, 2 * i, 2), At(Out, 2 * i, 3),
                    { At(Out, 2 * i + 1, 0), At(Out, 2 * i + 1, 1), At(Out, 2 * i + 1, 2) }, At(Out, 2 * i + 1, 3) });
    return R;
  }
}

// Why: Texture.cpp marks a texture as skin by the file it was read from; a rule that missed the faces would leave them
// plastic, one that took a normal map or a helmet's texture would light those as skin (the physical materials do not
// tell it: most faces are mat_default).
XR_TEST(Skin_IsSkin)
{
  CHECK(PbrCompanion::IsSkin("objects/characters/pmodels/hero/jc_head_dark2"));
  CHECK(PbrCompanion::IsSkin("objects/characters/story_characters/valerie/valeriface"));
  CHECK(PbrCompanion::IsSkin("objects/characters/mercenaries/merc_rear/merc_rear_face_blood"));
  CHECK(PbrCompanion::IsSkin("objects/characters/story_characters/krieger/krieger_head"));
  CHECK(!PbrCompanion::IsSkin("objects/characters/pmodels/hero/jc_head_ddn"));
  CHECK(!PbrCompanion::IsSkin("objects/characters/story_characters/hero/jc_head_ddp"));
  CHECK(!PbrCompanion::IsSkin("objects/characters/mercenaries/shared/max_face_bump"));
  CHECK(!PbrCompanion::IsSkin("objects/characters/pmodels/hero/hero_shirt_body"));
  CHECK(!PbrCompanion::IsSkin("objects/characters/pmodels/hero/hero_hair2_dark"));
  CHECK(!PbrCompanion::IsSkin("objects/weapons/head"));                      // not a character's
  CHECK(!PbrCompanion::IsSkin("textures/faces/face"));
  CHECK(!PbrCompanion::IsSkin("objects/characters_old/face"));
  CHECK(!PbrCompanion::IsSkin("objects/characters/heads/body"));             // the folder's name does not count
  CHECK(!PbrCompanion::IsSkin(""));
}

// Why: skin reflects 2.8 % of the light head on (F0); the templates' lobe gave the face the highlight of a glossy
// plastic. Seen head on the highlight's share of the light over all its directions is about F0 (a little more: the
// lobe's grazing lights), towards grazing views more (Fresnel), never more than all of it.
XR_TEST(Skin_SpecularAlbedo)
{
  const float fAlpha2 = 0.0625f;   // the perceptual roughness 0.5
  const double fHeadOn = DirectionalAlbedo(fAlpha2, 0);
  CHECK(fHeadOn > 0.025 && fHeadOn < 0.05);
  const double fSide = DirectionalAlbedo(fAlpha2, 60 * PI / 180), fGrazing = DirectionalAlbedo(fAlpha2, 80 * PI / 180);
  CHECK(fSide > fHeadOn);
  CHECK(fGrazing > fSide);
  CHECK(fGrazing < 1);
  // A rougher skin about as much light (the share is F0's either way), a smoother one too.
  CHECK_NEAR(DirectionalAlbedo(0.2f, 0), fHeadOn, 0.012);
  CHECK_NEAR(DirectionalAlbedo(0.01f, 0), fHeadOn, 0.012);
}

// Why: the highlight is GGX's: head on (the light, the eye and the normal one) pi * D * V * F with D = 1 / (pi alpha^2),
// V = 1 / 4 and F = F0, so F0 / (4 alpha^2); none where the light is behind the surface; a rougher lobe
// lower at its peak, broader off it.
XR_TEST(Skin_SpecularLobe)
{
  const SVec3 n = { 0, 0, 1 };
  const std::vector<float> R = RunSpecular({
    { n, 0.0625f, n, n },                                  // head on
    { n, 0.2f, n, n },                                     // rougher
    { n, 0.0625f, { 0.6f, 0, -0.8f }, n },                 // the light behind
    { n, 0.0625f, { 0, 0, 1 }, { 0.8f, 0, 0.6f } },        // off the mirror direction
    { n, 0.2f, { 0, 0, 1 }, { 0.8f, 0, 0.6f } },           // the same, rougher
  });
  CHECK_NEAR(R[0], 0.028 / (4 * 0.0625), 1e-3);
  CHECK_NEAR(R[1], 0.028 / (4 * 0.2), 1e-3);
  CHECK(R[1] < R[0]);
  CHECK(R[2] == 0);
  CHECK(R[3] > 0 && R[3] < R[0]);
  CHECK(R[4] > R[3]);
}

// Why: the artists' gloss (the texture's alpha) is how much the skin shines, not its smoothness: no gloss the matte
// skin's roughness, from SKIN_GLOSS_OILY (0.3) the oily's, the wet rims' (gloss 1) smoother still but not under 0.25
// (a lobe that would sparkle), never rougher than the oily's; the highlight's share whole from the bare skin's gloss
// (0.08) up, less below (the stubble, the brows).
XR_TEST(Skin_Roughness)
{
  const std::vector<SParts> R = RunParts({ { 0, 0.55f, 0.35f, 0 },     { 0.15f, 0.55f, 0.35f, 0 }, { 0.3f, 0.55f, 0.35f, 0 },
                                           { 1, 0.55f, 0.35f, 0 },     { 0.65f, 0.55f, 0.35f, 0 }, { 1, 0.55f, 0.2f, 0 },
                                           { 0.04f, 0.55f, 0.35f, 0 }, { 0.08f, 0.55f, 0.35f, 0 } },
                                         1);
  CHECK_NEAR(R[0].fRough, 0.55, 1e-5);
  CHECK_NEAR(R[1].fRough, 0.45, 1e-5);
  CHECK_NEAR(R[2].fRough, 0.35, 1e-5);
  CHECK_NEAR(R[3].fRough, 0.25, 1e-5);
  CHECK_NEAR(R[4].fRough, 0.30, 1e-5);
  CHECK_NEAR(R[5].fRough, 0.2, 1e-5);   // an oily skin smoother than the wet: the oily's
  CHECK_NEAR(R[0].fShare, 0, 1e-6);
  CHECK_NEAR(R[6].fShare, 0.5, 1e-5);
  CHECK_NEAR(R[7].fShare, 1, 1e-5);
  CHECK_NEAR(R[3].fShare, 1, 1e-6);
}

// Why: the skin's light scatters under it, so it reaches a little past the line where the light passes it, red the
// farthest; lit head on it is as lambert's (the picture's brightness kept), without the scatter lambert's exactly.
XR_TEST(Skin_Wrap)
{
  std::vector<std::array<float, 4>> In;
  for (int i = 0; i <= 40; i++)
    In.push_back({ 0, 0.5f, 0.3f, -1 + i / 20.0f });
  const std::vector<SParts> R = RunParts(In, 1), L = RunParts(In, 0);
  for (size_t i = 0; i < In.size(); i++)
  {
    const float fNL = In[i][3];
    for (int c = 0; c < 3; c++)
      CHECK_NEAR(L[i].vWrap[c], std::max(fNL, 0.0f), 1e-5);
    // Red the farthest past the line, then green, then blue; none from -0.12 (red's reach) back.
    CHECK(R[i].vWrap[0] >= R[i].vWrap[1] - 1e-6f && R[i].vWrap[1] >= R[i].vWrap[2] - 1e-6f);
    if (fNL < -0.13f)
      CHECK(R[i].vWrap[0] == 0);
    if (i > 0)
      for (int c = 0; c < 3; c++)
        CHECK(R[i].vWrap[c] >= R[i - 1].vWrap[c] - 1e-6f);
  }
  const size_t nLine = 20, nHalf = 30, nFull = 40;   // N.L 0, 0.5, 1
  CHECK(R[nLine].vWrap[0] > 0.05f && R[nLine].vWrap[0] > 3 * R[nLine].vWrap[2]);   // at the line red the most
  CHECK(R[nHalf].vWrap[0] > 0.5f && R[nHalf].vWrap[0] < 0.53f);   // in the middle a few % more red
  for (int c = 0; c < 3; c++)
    CHECK_NEAR(R[nFull].vWrap[c], 1, 1e-5);
}

// Why: the paths' G-buffer keeps the skin's roughness in its light map target's alpha (RGBA8), 1 where it is not skin
// (every other surface writes 1); a roughness that came back as "not skin", or a non-skin texel read as skin, would light
// the wrong pixels with the skin's lobe.
XR_TEST(Skin_GBuffer)
{
  std::vector<std::array<float, 4>> In;
  for (int i = 0; i <= 20; i++)
    In.push_back({ i / 20.0f, 0.5f, 0.3f, 1 });
  const std::vector<SParts> R = RunParts(In, 1);
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK(R[i].fStored < 1);
    CHECK(R[i].fBack > 0);
    CHECK_NEAR(R[i].fBack, std::max(std::min(In[i][0], 254.0f / 255), 1.0f / 255), 0.6 / 255);
    CHECK(R[i].fAsAlpha == 0);   // the alpha 1: not skin
  }
}
