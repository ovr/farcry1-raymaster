// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestSmaa.hlsl
//	Description: The test kernels of SMAA 1x (d3d12_AA smaa): the
//	renderer's Smaa.hlsli (SMAA.hlsl of third_party/smaa) and
//	SmaaBlend.hlsli as AntiAliasSmaa.hlsl and AntiAlias.hlsl include them,
//	on the pictures of g_Textures (GpuHarness.h, t0 space1) with the
//	samplers of Common.hlsli (s1 linear clamp, s3 point clamp). Compiled
//	with a preset each (SMAA_PRESET_*, Tests/CMakeLists.txt).
//	The picture is g_Test.nTest.y x g_Test.nTest.z pixels, n =
//	g_Test.nTest.x of them, a thread a pixel (i = y * width + x):
//	  CSSmaaEdges    g_Textures[0] the picture (R8G8B8A8): out[i] = (its
//	                 edges, 0, 0), as AntiAliasSmaa.hlsl's CSEdges writes
//	  CSSmaaWeights  g_Textures[0] the edges (R8G8), [1] the area texture,
//	                 [2] the search texture: out[i] = its blending weights
//	  CSSmaaBlend    g_Textures[0] the picture, [1] the weights (R8G8B8A8):
//	                 out[i] = the pixel's colour as AntiAlias.hlsl leaves
//	                 it (the picture's where it does not blend), out[n + i]
//	                 = (the shares of its two neighbours, 1 blended, 1 the
//	                 neighbours left and right, 0 up and down)
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"

Texture2D g_Textures[] : register(t0, space1);
SamplerState g_SamplerLinearClamp : register(s1);
SamplerState g_SamplerPointClamp : register(s3);

#define SMAA_RT_METRICS float4(1.0 / g_Test.nTest.y, 1.0 / g_Test.nTest.z, g_Test.nTest.y, g_Test.nTest.z)
#define SMAA_LINEAR_SAMPLER g_SamplerLinearClamp
#define SMAA_POINT_SAMPLER g_SamplerPointClamp
#include "Smaa.hlsli"

int2 TestSmaaClamp(int2 p)
{
  return clamp(p, int2(0, 0), int2(g_Test.nTest.y, g_Test.nTest.z) - 1);
}

float4 SmaaLoadColor(int2 p)
{
  return g_Textures[0].Load(int3(TestSmaaClamp(p), 0));
}

float4 SmaaLoadWeights(int2 p)
{
  return g_Textures[1].Load(int3(TestSmaaClamp(p), 0));
}

#include "SmaaBlend.hlsli"

int2 TestSmaaPixel(uint i)
{
  return int2(i % g_Test.nTest.y, i / g_Test.nTest.y);
}

float2 TestSmaaCoord(int2 p)
{
  return ((float2)p + 0.5) * SMAA_RT_METRICS.xy;
}

[numthreads(64, 1, 1)]
void CSSmaaEdges(uint i : SV_DispatchThreadID)
{
  if (i < g_Test.nTest.x)
    TestOut(i, float4(SmaaEdges(TestSmaaCoord(TestSmaaPixel(i)), g_Textures[0]), 0, 0));
}

[numthreads(64, 1, 1)]
void CSSmaaWeights(uint i : SV_DispatchThreadID)
{
  if (i < g_Test.nTest.x)
    TestOut(i, SmaaWeights(TestSmaaCoord(TestSmaaPixel(i)), g_Textures[0], g_Textures[1], g_Textures[2]));
}

[numthreads(64, 1, 1)]
void CSSmaaBlend(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x;
  if (i >= n)
    return;
  const int2 p = TestSmaaPixel(i);
  const SSmaaBlend b = SmaaBlend(p);
  TestOut(i, SmaaBlendColor(p, b));
  TestOut(n + i, float4(b.fShare[0], b.fShare[1], b.bBlend ? 1 : 0, abs(b.vStep[0].x)));
}
