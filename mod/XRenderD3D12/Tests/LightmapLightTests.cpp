// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LightmapLightTests.cpp
//	Description: Unit tests of a dot3 light map's light on the GPU
//	(Shaders/LightmapLight.hlsli): the bumps' share of the light map's
//	light mixed towards 1 by its alpha (d3d12_LightmapDot3), the falloff
//	and the highlight of a lamp baked into the light map
//	(d3d12_LightmapSpecular) as the original's light-map bump shaders.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"

#include "Test_LightmapLight.h"

#include <cmath>
#include <iterator>
#include <vector>

using namespace XrTest;

namespace
{
  struct SIn
  {
    float x, fND, fAlpha;      //!< the distance over the radius, N'.D, the light map's alpha
    float N[3], L[3], V[3];    //!< the normal, the directions to the lamp and to the eye
  };
  struct SOut
  {
    float fFalloff, fMixed, fPlain, fHighlight;
  };

  std::vector<SOut> Run(const std::vector<SIn> &In)
  {
    SDispatch d;
    d.Shader = XR_BYTECODE(Test_LightmapLight);
    d.Test.nTest[0] = (uint32)In.size();
    d.nGroups[0] = Groups64(In.size());
    for (const SIn &v : In)
      Push(d.In, v.x, v.fND, v.fAlpha, 0);
    for (const SIn &v : In)
      Push(d.In, v.N[0], v.N[1], v.N[2], 0);
    for (const SIn &v : In)
      Push(d.In, v.L[0], v.L[1], v.L[2], 0);
    for (const SIn &v : In)
      Push(d.In, v.V[0], v.V[1], v.V[2], 0);
    d.nOut = In.size();
    const std::vector<uint32> Out = Gpu().Run(d);
    std::vector<SOut> R;
    for (size_t i = 0; i < In.size(); i++)
      R.push_back({ At(Out, i, 0), At(Out, i, 1), At(Out, i, 2), At(Out, i, 3) });
    return R;
  }

  SIn Item(float x, float fND = 0, float fAlpha = 0)
  {
    SIn v = { x, fND, fAlpha, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 } };
    return v;
  }

  SIn Highlight(const float (&L)[3], const float (&V)[3])
  {
    SIn v = Item(0);
    for (int c = 0; c < 3; c++)
    {
      v.L[c] = L[c];
      v.V[c] = V[c];
    }
    return v;
  }
}

//! The falloff of a lamp baked into a light map: the values the original's falloff texture gives where its light-map bump
//! shaders look it up (read from our own tracer's dump of Research's frames, 8 bits each, so within 0.01), 1 at the lamp,
//! 0 from its radius on, never rising with the distance. Why: the highlight is as bright as the original's at every
//! distance; (1 - x)^2 alone (the texture's other rows) gives 0.65 instead of 0.82 at a fifth of the radius.
XR_TEST(LightmapLight_Falloff)
{
  const float Probe[][2] = { { 0.196f, 0.824f }, { 0.294f, 0.749f }, { 0.501f, 0.500f }, { 0.751f, 0.220f }, { 0.880f, 0.098f } };
  std::vector<SIn> In;
  for (const auto &p : Probe)
    In.push_back(Item(p[0]));
  const size_t nSweep = 1200;
  for (size_t i = 0; i <= nSweep; i++)
    In.push_back(Item(i / 1000.0f));
  const std::vector<SOut> Out = Run(In);
  for (size_t i = 0; i < std::size(Probe); i++)
    CHECK_NEAR(Out[i].fFalloff, Probe[i][1], 0.01);
  const size_t n0 = std::size(Probe);
  CHECK_NEAR(Out[n0].fFalloff, 1, 1e-6);
  for (size_t i = 1; i <= nSweep; i++)
  {
    CHECK(Out[n0 + i].fFalloff <= Out[n0 + i - 1].fFalloff);
    if (i >= 1000)
      CHECK(Out[n0 + i].fFalloff == 0);
  }
}

//! The bumps' share of a dot3 light map's light: with d3d12_LightmapDot3 N'.D towards 1 by the light map's alpha (1 where the
//! alpha is 0, N'.D where it is 1, linear between), without it N'.D whatever the alpha. Why: where the light map's alpha is 0
//! the original's bump shaders do not darken its light by the bumps at all; a quarter of Research's light-map texels are such.
XR_TEST(LightmapLight_Dot3Mix)
{
  std::vector<SIn> In;
  for (int nd = 0; nd <= 10; nd++)
    for (int a = 0; a <= 10; a++)
      In.push_back(Item(0, nd / 10.0f, a / 10.0f));
  const std::vector<SOut> Out = Run(In);
  for (size_t i = 0; i < In.size(); i++)
  {
    const double fND = In[i].fND, fA = In[i].fAlpha;
    CHECK_NEAR(Out[i].fMixed, 1 + (fND - 1) * fA, 1e-6);
    CHECK_NEAR(Out[i].fPlain, fND, 1e-6);
  }
}

//! The highlight of such a lamp: saturate(R.V)^32, R the direction to the lamp reflected about the normal. 1 in the mirror
//! direction (head-on and at 45 degrees), cos(10 degrees)^32 ten degrees off it, 0 where R points away from the eye. Why:
//! the original's power is 32; a wrong reflection or power moves or widens the highlight on the cave's rock.
XR_TEST(LightmapLight_Highlight)
{
  const float s = std::sqrt(0.5f);
  const float c10 = std::cos(10 * 3.14159265f / 180), s10 = std::sin(10 * 3.14159265f / 180);
  std::vector<SIn> In = {
    Highlight({ 0, 0, 1 }, { 0, 0, 1 }),
    Highlight({ s, 0, s }, { -s, 0, s }),
    Highlight({ 0, 0, 1 }, { s10, 0, c10 }),
    Highlight({ s, 0, s }, { s, 0, s }),          // R.V = 0
    Highlight({ s, 0, s }, { s, 0, -s }),         // R.V < 0
  };
  const std::vector<SOut> Out = Run(In);
  CHECK_NEAR(Out[0].fHighlight, 1, 1e-5);
  CHECK_NEAR(Out[1].fHighlight, 1, 1e-4);
  CHECK_NEAR(Out[2].fHighlight, std::pow((double)c10, 32.0), 1e-4);
  CHECK_NEAR(Out[3].fHighlight, 0, 1e-6);
  CHECK_NEAR(Out[4].fHighlight, 0, 1e-6);
}
