// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestParallax.hlsl
//	Description: The test kernels of parallax occlusion mapping
//	(r_Parallax): the renderer's Parallax.hlsli as General.hlsl includes
//	it, its height PARALLAX_HEIGHT by g_Test.nTest.y: 0 the constant
//	g_Test.vTest.x, 1 a ramp frac(u), 2 a step (frac(u) < 0.5:
//	vTest.x, else vTest.y), 3 g_Textures[0] (GpuHarness.h, t0 space1;
//	r) by the point wrap sampler of Common.hlsli (s2) and the gradients;
//	n = g_Test.nTest.x items.
//	  CSParallaxUV     g_TestIn[2i] (uv, the shift), [2i + 1] (the
//	                   reference, the steps, the gradients' length (dx
//	                   along u, dy along v), 0): out[i] = (ParallaxUV,
//	                   the height there, 0)
//	  CSParallaxParts  g_TestIn[5i] (towards the eye, the depth), [5i +
//	                   1..3] the tangent frame T, B, N (w 0), [5i + 4]
//	                   (dx, dy): out[3i] = (ParallaxView, ParallaxSteps
//	                   of its z and 24), out[3i + 1] = (ParallaxShift,
//	                   ParallaxTexMatrix of it by the rows g_Test.vTest2.xy
//	                   and .zw), out[3i + 2] = (ParallaxFade of the depth by
//	                   the gradients, from 4 pixels to 1, 0, 0, 0)
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"

Texture2D g_Textures[] : register(t0, space1);
SamplerState g_SamplerPointWrap : register(s2);

float TestParallaxHeight(float2 uv, float2 dx, float2 dy)
{
  switch (g_Test.nTest.y)
  {
  case 1:
    return frac(uv.x);
  case 2:
    return frac(uv.x) < 0.5 ? g_Test.vTest.x : g_Test.vTest.y;
  case 3:
    return g_Textures[0].SampleGrad(g_SamplerPointWrap, uv, dx, dy).r;
  default:
    return g_Test.vTest.x;
  }
}

#define PARALLAX_HEIGHT(uv, dx, dy) TestParallaxHeight(uv, dx, dy)
#include "Parallax.hlsli"

[numthreads(64, 1, 1)]
void CSParallaxUV(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(2 * i), b = TestIn(2 * i + 1);
  const float2 dx = float2(b.z, 0), dy = float2(0, b.z);
  const float2 uv = ParallaxUV(a.xy, dx, dy, a.zw, b.x, (uint)b.y);
  TestOut(i, float4(uv, TestParallaxHeight(uv, dx, dy), 0));
}

[numthreads(64, 1, 1)]
void CSParallaxParts(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 e = TestIn(5 * i), g = TestIn(5 * i + 4);
  const float3 vView = ParallaxView(e.xyz, TestIn(5 * i + 1).xyz, TestIn(5 * i + 2).xyz, TestIn(5 * i + 3).xyz);
  const float2 vShift = ParallaxShift(vView, e.w);
  TestOut(3 * i, float4(vView, ParallaxSteps(vView.z, 24)));
  TestOut(3 * i + 1, float4(vShift, ParallaxTexMatrix(vShift, g_Test.vTest2.xy, g_Test.vTest2.zw)));
  TestOut(3 * i + 2, float4(ParallaxFade(e.w, g.xy, g.zw, 4, 1), 0, 0, 0));
}
