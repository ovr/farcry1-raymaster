// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestMsaa.hlsl
//	Description: The test kernel of MSAA's resolve (d3d12_AA msaa2, msaa4,
//	msaa8): the renderer's Msaa.hlsli as Msaa.hlsl includes it, its
//	samples from g_TestIn. n = g_Test.nTest.x pixels of s = g_Test.nTest.y
//	samples, a thread a pixel; sample k of pixel i is g_TestIn[2 (i s + k)]
//	(its colour, rgba) and g_TestIn[2 (i s + k) + 1].x (its depth).
//	g_Test.nTest.z 1: the HDR target's colours, g_Test.vTest.x the
//	exposure's factor.
//	  CSMsaa  out[i] = MsaaResolveColor of pixel i, out[n + i] =
//	          (MsaaResolveDepth, 0, 0, 0)
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"

float4 MsaaLoadColor(int2 p, uint s) { return TestIn(2 * (p.x * g_Test.nTest.y + s)); }
float MsaaLoadDepth(int2 p, uint s) { return TestIn(2 * (p.x * g_Test.nTest.y + s) + 1).x; }

#include "Msaa.hlsli"

[numthreads(64, 1, 1)]
void CSMsaa(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x;
  if (i >= n)
    return;
  TestOut(i, MsaaResolveColor(int2(i, 0), g_Test.nTest.y, g_Test.nTest.z != 0, g_Test.vTest.x));
  TestOut(n + i, float4(MsaaResolveDepth(int2(i, 0), g_Test.nTest.y), 0, 0, 0));
}
