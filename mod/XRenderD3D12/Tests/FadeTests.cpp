// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FadeTests.cpp
//	Description: Unit tests of the objects the engine fades in or out by
//	their distance (d3d12_FadeBlend): General.hlsl's alpha test, then
//	AlphaCoverage.hlsli's FadeAlpha (TestHeaders.hlsl's CSFadeAlpha) on
//	the GPU: the alpha a fading object is blended by is its fade alone,
//	whatever its texture's alpha, the test still cutting out its holes;
//	without a fade the texture's alpha as before.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_FadeAlpha.h"

#include <cmath>
#include <vector>

using namespace XrTest;

namespace
{
  //! A pixel of a draw: its texture's alpha, the alpha test's reference (0: none), the draw's fade (0: none).
  struct SFadePixel
  {
    float a, fRef, fFade;
  };

  //! What the alpha test and FadeAlpha leave of a pixel.
  struct SFadeOut
  {
    bool bKept;     //!< the alpha test keeps it
    float fAlpha;   //!< the alpha it is blended by
  };

  std::vector<SFadeOut> Run(const std::vector<SFadePixel> &Pixels)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_FadeAlpha);
    d.Test.nTest[0] = (uint32)Pixels.size();
    d.nGroups[0] = Groups64(Pixels.size());
    for (const SFadePixel &p : Pixels)
      Push(d.In, p.a, p.fRef, p.fFade, 0);
    d.nOut = Pixels.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SFadeOut> R;
    for (size_t i = 0; i < Pixels.size(); i++)
      R.push_back({ At(Out, i, 0) > 0.5f, At(Out, i, 1) });
    return R;
  }
}

// Why: most of the bump templates keep their gloss in the texture's alpha (TemplBumpSpec_GlossAlpha_PS20: the crates on
// Carrier's deck), and the original's ambient pass gives a fading object's alpha alone (its c0.w), whatever the
// texture's; blended by the texture's alpha times the fade the crates would fade in through holes where their gloss is
// low, and stay see-through there until the fade ends.
XR_TEST(Fade_Uniform)
{
  std::vector<SFadePixel> In;
  for (int t = 0; t <= 16; t++)
    for (const float fFade : { 1e-6f, 0.01f, 0.25f, 0.5f, 0.87f, 0.99f, 1.0f })
      In.push_back({ t / 16.0f, 0, fFade });
  const std::vector<SFadeOut> R = Run(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK(R[i].bKept);
    CHECK_NEAR(R[i].fAlpha, In[i].fFade, 0);
  }
}

// Why: every draw that is not a fading object has no fade (vEdgeFade.y 0): its alpha must stay the texture's (times its
// colour's) bit for bit, else the blended surfaces, the glass and the decals change with d3d12_FadeBlend.
XR_TEST(Fade_Off)
{
  std::vector<SFadePixel> In;
  for (int t = 0; t <= 64; t++)
    for (const float fRef : { 0.0f, 0.5f })
      In.push_back({ t / 64.0f, fRef, 0 });
  const std::vector<SFadeOut> R = Run(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK(R[i].bKept == (In[i].fRef <= 0 || In[i].a >= In[i].fRef));
    CHECK_NEAR(R[i].fAlpha, In[i].a, 0);
  }
}

// Why: an alpha tested object (a grating, a fence) that fades must keep its holes: the test on its texture's alpha
// first, its fade only the alpha of what the test keeps. With the fade in the test (the texture's alpha times it) the
// grating would thin out from its thin bars and vanish under the reference before its fade ends.
XR_TEST(Fade_AlphaTest)
{
  std::vector<SFadePixel> In;
  for (const float fRef : { 1.0f / 255, 0.25f, 0.5f, 128.0f / 255 })
    for (int t = 0; t <= 32; t++)
      for (const float fFade : { 0.05f, 0.5f, 0.95f })
        In.push_back({ t / 32.0f, fRef, fFade });
  const std::vector<SFadeOut> R = Run(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    CHECK(R[i].bKept == (In[i].a >= In[i].fRef));
    if (R[i].bKept)
      CHECK_NEAR(R[i].fAlpha, In[i].fFade, 0);
  }
}

// Why: the point of the blend: as the engine's fade rises evenly the object's pixels go evenly from what is behind it
// to its colour (SRCALPHA, INVSRCALPHA), no frame changing a pixel by more than its share of the step; drawn opaque the
// whole difference came in one frame, at the far end of the fade.
XR_TEST(Fade_Even)
{
  constexpr int nSteps = 64;
  std::vector<SFadePixel> In;
  for (int k = 1; k <= nSteps; k++)
    In.push_back({ 0.3f, 0, (float)k / nSteps });
  const std::vector<SFadeOut> R = Run(In);
  const float fBack = 0.2f, fObject = 0.9f;
  float fPrev = fBack;
  for (size_t i = 0; i < In.size(); i++)
  {
    const float c = fObject * R[i].fAlpha + fBack * (1 - R[i].fAlpha);
    CHECK(c >= fPrev);
    CHECK_NEAR(c - fPrev, (fObject - fBack) / nSteps, 1e-5);
    fPrev = c;
  }
  CHECK_NEAR(fPrev, fObject, 1e-5);
}
