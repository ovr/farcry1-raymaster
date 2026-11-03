// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RenderModeTests.cpp
//	Description: Unit tests of the render modes' colour math on the GPU
//	(Shaders/RenderMode.hlsli): the adapted luminance's target, the
//	glare's source, the cold contrast, the scene with its glare and the
//	cartoon's colours and edges, against the values of the original's
//	screen process (its pixel shaders' constants, traced).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_RenderMode.h"

#include <vector>

using namespace XrTest;

namespace
{
  struct SRgb
  {
    float r, g, b;
  };

  //! One item: f the scene's colour, d the reduced scene's (or the glare), a the adapted luminance (or the left
  //! colour), r the right colour; the glare's threshold and gain, the share of the luminance.
  struct SIn
  {
    SRgb f, d, a, r;
    float fThreshold, fGain, fGrey;
  };
  struct SOut
  {
    SRgb Source, Cold, Glared, Cartoon;
    float fAdapt, fEdge;
  };

  SRgb Grey(float v)
  {
    return { v, v, v };
  }

  std::vector<SOut> Run(const std::vector<SIn> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_RenderMode);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SIn &v : In)
      Push(d.In, v.f.r, v.f.g, v.f.b, v.fThreshold);
    for (const SIn &v : In)
      Push(d.In, v.d.r, v.d.g, v.d.b, v.fGain);
    for (const SIn &v : In)
      Push(d.In, v.a.r, v.a.g, v.a.b, v.fGrey);
    for (const SIn &v : In)
      Push(d.In, v.r.r, v.r.g, v.r.b, 0);
    d.nOut = 4 * In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SOut> R;
    for (size_t i = 0; i < In.size(); i++)
    {
      SOut o;
      o.Source = { At(Out, 4 * i, 0), At(Out, 4 * i, 1), At(Out, 4 * i, 2) };
      o.fAdapt = At(Out, 4 * i, 3);
      o.Cold = { At(Out, 4 * i + 1, 0), At(Out, 4 * i + 1, 1), At(Out, 4 * i + 1, 2) };
      o.fEdge = At(Out, 4 * i + 1, 3);
      o.Glared = { At(Out, 4 * i + 2, 0), At(Out, 4 * i + 2, 1), At(Out, 4 * i + 2, 2) };
      o.Cartoon = { At(Out, 4 * i + 3, 0), At(Out, 4 * i + 3, 1), At(Out, 4 * i + 3, 2) };
      R.push_back(o);
    }
    return R;
  }

  SIn Item(SRgb f, SRgb d = Grey(0), SRgb a = Grey(0), SRgb r = Grey(0))
  {
    return { f, d, a, r, -0.1f, 0.8f, 0.1f };
  }

  void CheckRgb(const SRgb &v, float r, float g, float b, double fTol)
  {
    CHECK_NEAR(v.r, r, fTol);
    CHECK_NEAR(v.g, g, fTol);
    CHECK_NEAR(v.b, b, fTol);
  }
}

//! The adapted luminance's target: the luminance (0.3, 0.59, 0.11) of 4 m^2. Mid grey gives white, a quarter grey a
//! quarter of it, black none, a red of 0.5 0.3, more than white is taken as white (4). Why: the original's 2 x 2 pass
//! squares the mean and doubles it twice; with it the glare (times 1 - A) fades in daylight and stays strong in the
//! dark bunker (A 22 / 255 there).
XR_TEST(RenderMode_AdaptTarget)
{
  const std::vector<SIn> In = { Item(Grey(0.5f)), Item(Grey(0.25f)), Item(Grey(0)), Item({ 0.5f, 0, 0 }), Item(Grey(2)) };
  const std::vector<SOut> Out = Run(In);
  CHECK_NEAR(Out[0].fAdapt, 1, 1e-6);
  CHECK_NEAR(Out[1].fAdapt, 0.25, 1e-6);
  CHECK_NEAR(Out[2].fAdapt, 0, 1e-6);
  CHECK_NEAR(Out[3].fAdapt, 0.3, 1e-6);
  CHECK_NEAR(Out[4].fAdapt, 4, 1e-5);
}

//! The glare's source of Paradise (threshold -0.1, gain 0.8) and Improved (-0.25, 0.8): 2 (saturate(d - t) 0.8)^2 times
//! 1 - A. Below the threshold none; 0.6 gives 0.32, half of it at A 0.5, none at A 1; Improved's 0.6 gives 0.1568.
//! Why: these are the original's pixel shader constants; a wrong threshold or a missing square changes how much of
//! the bunker's shafts glows.
XR_TEST(RenderMode_GlareSource)
{
  std::vector<SIn> In = { Item(Grey(0), Grey(0.1f)), Item(Grey(0), Grey(0.6f)), Item(Grey(0), Grey(0.6f), Grey(0.5f)),
                          Item(Grey(0), Grey(0.6f), Grey(1)), Item(Grey(0), { 0.6f, 0.35f, 0.1f }) };
  SIn Improved = Item(Grey(0), Grey(0.6f));
  Improved.fThreshold = -0.25f;
  In.push_back(Improved);
  const std::vector<SOut> Out = Run(In);
  CheckRgb(Out[0].Source, 0, 0, 0, 1e-6);
  CheckRgb(Out[1].Source, 0.32f, 0.32f, 0.32f, 1e-5);
  CheckRgb(Out[2].Source, 0.16f, 0.16f, 0.16f, 1e-5);
  CheckRgb(Out[3].Source, 0, 0, 0, 1e-6);
  CheckRgb(Out[4].Source, 0.32f, 2 * 0.2f * 0.2f, 0, 1e-5);
  CheckRgb(Out[5].Source, 2 * 0.28f * 0.28f, 2 * 0.28f * 0.28f, 2 * 0.28f * 0.28f, 1e-5);
}

//! Cold's contrast: 2 saturate(0.75 f - 0.3 d), up to white. 0.8 over 0.5 gives 0.9, white over black white (1.5
//! clamped), a dark pixel in a bright surround black. Why: the original's cold mode darkens the shadows and brightens
//! the light before its glare; without the clamp the HDR layer's ratio would see more than white.
XR_TEST(RenderMode_Cold)
{
  const std::vector<SIn> In = { Item(Grey(0.8f), Grey(0.5f)), Item(Grey(1), Grey(0)), Item(Grey(0.2f), Grey(0.6f)),
                                Item({ 0.4f, 0.6f, 0.8f }, Grey(0.5f)) };
  const std::vector<SOut> Out = Run(In);
  CheckRgb(Out[0].Cold, 0.9f, 0.9f, 0.9f, 1e-5);
  CheckRgb(Out[1].Cold, 1, 1, 1, 1e-6);
  CheckRgb(Out[2].Cold, 0, 0, 0, 1e-6);
  CheckRgb(Out[3].Cold, 0.3f, 0.6f, 0.9f, 1e-5);
}

//! The scene with its glare: f + (1 - L) g, then towards L by k. White gets no glare; (0.2 0.4 0.6) with a glare of
//! 0.5 at Paradise's k 0.1 gives (0.519 0.719 0.919) * 0.9 + 0.0362; k 0 keeps the colour, k 1 is grey. Why: the
//! original adds the glare less to bright pixels and takes a share of the saturation away in every mode with glare.
XR_TEST(RenderMode_Glared)
{
  std::vector<SIn> In = { Item(Grey(1), Grey(0.5f)), Item({ 0.2f, 0.4f, 0.6f }, Grey(0.5f)) };
  SIn Keep = Item({ 0.2f, 0.4f, 0.6f }, Grey(0));
  Keep.fGrey = 0;
  SIn Flat = Item({ 0.2f, 0.4f, 0.6f }, Grey(0));
  Flat.fGrey = 1;
  In.push_back(Keep);
  In.push_back(Flat);
  const std::vector<SOut> Out = Run(In);
  CheckRgb(Out[0].Glared, 1, 1, 1, 1e-6);
  const float l = 0.3f * 0.2f + 0.59f * 0.4f + 0.11f * 0.6f, g = (1 - l) * 0.5f;
  CheckRgb(Out[1].Glared, (0.2f + g) * 0.9f + 0.1f * l, (0.4f + g) * 0.9f + 0.1f * l, (0.6f + g) * 0.9f + 0.1f * l, 1e-5);
  CheckRgb(Out[2].Glared, 0.2f, 0.4f, 0.6f, 1e-6);
  CheckRgb(Out[3].Glared, l, l, l, 1e-6);
}

//! The cartoon's colour: (0.75 f + 0.225 L) times (1.25 1.24 1.2), clamped. Mid grey gives (0.609 0.605 0.585), pure
//! red (1 0.084 0.081), black stays black. Why: the original's flatter, warmer colours (its shader's constants).
XR_TEST(RenderMode_CartoonColor)
{
  const std::vector<SIn> In = { Item(Grey(0.5f)), Item({ 1, 0, 0 }), Item(Grey(0)) };
  const std::vector<SOut> Out = Run(In);
  CheckRgb(Out[0].Cartoon, 0.4875f * 1.25f, 0.4875f * 1.24f, 0.4875f * 1.2f, 1e-5);
  CheckRgb(Out[1].Cartoon, 1, 0.0675f * 1.24f, 0.0675f * 1.2f, 1e-5);
  CheckRgb(Out[2].Cartoon, 0, 0, 0, 1e-6);
}

//! The cartoon's edges: saturate(1 - saturate(2 |a|^2) - 2 |b|^2), a = 2 saturate(above - below), b = 2 saturate(left -
//! right). Flat colour 1; a fall of 0.1 downwards 0.76, to the right 0.76, both 0.52; a rise either way 1; a fall of
//! 0.25 downwards black. Why: the original darkens only where the colour falls downwards or to the right (one-sided
//! differences), so its outlines are one pixel wide.
XR_TEST(RenderMode_Edge)
{
  const SRgb m = Grey(0.5f), hi = Grey(0.6f), lo = Grey(0.4f);
  std::vector<SIn> In = {
    Item(m, m, m, m),
    Item(hi, Grey(0.5f), m, m),          // above 0.6, below 0.5
    Item(m, m, hi, Grey(0.5f)),          // left 0.6, right 0.5
    Item(hi, Grey(0.5f), hi, Grey(0.5f)),
    Item(lo, Grey(0.5f), lo, Grey(0.5f)), // rises
    Item(Grey(0.75f), m, m, m),          // a fall of 0.25 downwards
  };
  const std::vector<SOut> Out = Run(In);
  CHECK_NEAR(Out[0].fEdge, 1, 1e-6);
  CHECK_NEAR(Out[1].fEdge, 0.76, 1e-5);
  CHECK_NEAR(Out[2].fEdge, 0.76, 1e-5);
  CHECK_NEAR(Out[3].fEdge, 0.52, 1e-5);
  CHECK_NEAR(Out[4].fEdge, 1, 1e-6);
  CHECK_NEAR(Out[5].fEdge, 0, 1e-6);
}
