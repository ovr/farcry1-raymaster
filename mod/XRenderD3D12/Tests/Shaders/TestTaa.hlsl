// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestTaa.hlsl
//	Description: The test kernels of TAA (d3d12_AA taa): the renderer's
//	Taa.hlsli as Taa.hlsl includes it, on the pictures of g_Textures
//	(GpuHarness.h, t0 space1) with the linear clamp sampler of Common.hlsli
//	(s1). The picture is g_Test.nTest.y x g_Test.nTest.z pixels, n =
//	g_Test.nTest.x of them, a thread a pixel (i = y * width + x);
//	g_Test.nTest.w: 1 no history, 2 the colours are HDR light; g_Test.vTest:
//	x the frame's least share at rest, y the exposure (with 2), z the
//	sharpening, w the least share in motion.
//	  CSTaaResolve  g_Textures[0] the frame's colours (rgb), [1] the depth
//	                (r), [2] the motion vectors (rg), [3] the history
//	                (rgba): out[i] = (the pixel's colour, the new
//	                history's alpha) as Taa.hlsl's resolve writes them (the
//	                motion TaaDilate's, as its first kernel gives it),
//	                out[n + i] = (the frame's share, the clip, the luma, 0)
//	  CSTaaDilate   the same textures [1] and [2]: out[i] = (TaaDilate, 0, 0)
//	  CSTaaSharpen  g_Textures[0] the picture, g_TestIn[0] and [1] CAS's
//	                constants (CasSetup's const0, const1 as uint4): out[i]
//	                = its pixel sharpened as Taa.hlsl's last kernel
//	                sharpens it (TaaCas.hlsli)
//	  CSTaaWorking  g_TestIn[i] a colour: out[i] = TaaToWorking of it,
//	                out[n + i] = TaaFromWorking of that
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"

Texture2D g_Textures[] : register(t0, space1);
SamplerState g_SamplerLinearClamp : register(s1);

int2 TestTaaClamp(int2 p)
{
  return clamp(p, int2(0, 0), int2(g_Test.nTest.y, g_Test.nTest.z) - 1);
}

float3 TaaLoadColor(int2 p) { return g_Textures[0].Load(int3(TestTaaClamp(p), 0)).rgb; }
float TaaLoadDepth(int2 p) { return g_Textures[1].Load(int3(TestTaaClamp(p), 0)).r; }
float2 TaaLoadVelocity(int2 p) { return g_Textures[2].Load(int3(TestTaaClamp(p), 0)).rg; }
float4 TaaSampleHistory(float2 uv) { return g_Textures[3].SampleLevel(g_SamplerLinearClamp, uv, 0); }
float2 TaaDilate(int2 p);
float2 TaaLoadMotion(int2 p) { return TaaDilate(p); }

#include "Taa.hlsli"

int2 TestTaaPixel(uint i)
{
  return int2(i % g_Test.nTest.y, i / g_Test.nTest.y);
}

float4 TestTaaSize()
{
  return float4(g_Test.nTest.y, g_Test.nTest.z, 1.0 / g_Test.nTest.y, 1.0 / g_Test.nTest.z);
}

float TestTaaExposure()
{
  return (g_Test.nTest.w & 2) ? g_Test.vTest.y : 0;
}

float3 TaaCasPicture(int2 p)
{
  return TaaLoadColor(p);
}

float TaaCasExposure()
{
  return TestTaaExposure();
}

#include "TaaCas.hlsli"

[numthreads(64, 1, 1)]
void CSTaaResolve(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x;
  if (i >= n)
    return;
  const STaaPixel r = TaaResolve(TestTaaPixel(i), TestTaaSize(), TestTaaExposure(), g_Test.vTest.x, g_Test.vTest.w, (g_Test.nTest.w & 1) != 0);
  TestOut(i, float4(r.vColor, r.fFrames));
  TestOut(n + i, float4(r.fShare, r.fClip, r.fLuma, 0));
}

[numthreads(64, 1, 1)]
void CSTaaDilate(uint i : SV_DispatchThreadID)
{
  if (i < g_Test.nTest.x)
    TestOut(i, float4(TaaDilate(TestTaaPixel(i)), 0, 0));
}

[numthreads(64, 1, 1)]
void CSTaaSharpen(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const uint4 vConst0 = g_TestIn[0], vConst1 = g_TestIn[1];
  TestOut(i, float4(TaaCas(TestTaaPixel(i), vConst0, vConst1), 1));
}

[numthreads(64, 1, 1)]
void CSTaaWorking(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x;
  if (i >= n)
    return;
  const float e = TestTaaExposure();
  const float3 w = TaaToWorking(TestIn(i).rgb, e);
  TestOut(i, float4(w, 0));
  TestOut(n + i, float4(TaaFromWorking(w, e), 0));
}
