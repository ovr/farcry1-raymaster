// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestHeaders.hlsl
//	Description: The unit tests' kernels over the renderer's shader
//	headers that stand alone (Shader Model 6.0): PathGBuffer.hlsli,
//	HdrLight.hlsli, HdrToneMap.hlsli, HdrExposure.hlsli, HdrFrameAverage.hlsli, PathCache.hlsli and LightmapLight.hlsli, included as the renderer's
//	programs include them. Each kernel runs the tested functions on the
//	items of g_TestIn and writes what they give to g_TestOut; ShaderTests.cpp
//	checks it. One compile per kernel (Tests/CMakeLists.txt).
//
//////////////////////////////////////////////////////////////////////

#include "TestCommon.hlsli"
#include "RayConstants.h"
#include "PathGBuffer.hlsli"
#include "HdrLight.hlsli"
#include "HdrShared.hlsli"
#include "HdrToneMap.hlsli"
#include "HdrExposure.hlsli"
#include "HdrFrameAverage.hlsli"
#include "PathCache.hlsli"
#include "LightmapLight.hlsli"

//! PathOctEncode / PathOctDecode of the unit vector g_TestIn[i].xyz: out[3i] the map's point, out[3i + 1] its vector, out[3i + 2]
//! the vector of the point as the 10 bits of R10G10B10A2 keep it.
[numthreads(64, 1, 1)]
void CSOct(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float2 e = PathOctEncode(TestIn(i).xyz);
  TestOut(i * 3, float4(e, 0, 0));
  TestOut(i * 3 + 1, float4(PathOctDecode(e), 0));
  TestOut(i * 3 + 2, float4(PathOctDecode(QuantizeR10G10B10A2(float4(e, 0, 0)).rg), 0));
}

//! PathGBufferNormal of (normal g_TestIn[2i].xyz, fog g_TestIn[2i].w, face g_TestIn[2i + 1].xyz, material g_TestIn[2i + 1].w):
//! out[3i] the texel, out[3i + 1] its normal and (w) PathGBufferMaterial, out[3i + 2] the same of the texel as R10G10B10A2 stores it.
[numthreads(64, 1, 1)]
void CSGBufferNormal(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i * 2), b = TestIn(i * 2 + 1);
  const float4 t = PathGBufferNormal(a.xyz, b.xyz, a.w, g_TestIn[i * 2 + 1].w);
  const float4 q = QuantizeR10G10B10A2(t);
  TestOut(i * 3, t);
  TestOut(i * 3 + 1, float4(PathOctDecode(t.rg), asfloat(PathGBufferMaterial(t))));
  TestOut(i * 3 + 2, float4(PathOctDecode(q.rg), asfloat(PathGBufferMaterial(q))));
}

//! HdrDecode and HdrEncode of x = g_TestIn[i].x: out[i] = (decode x, encode x, encode decode x, decode encode x).
[numthreads(64, 1, 1)]
void CSHdr(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float x = TestIn(i).x;
  TestOut(i, float4(HdrDecode(x), HdrEncode(x), HdrEncode(HdrDecode(x)), HdrDecode(HdrEncode(x))));
}

//! NeutralToneMap of the light g_TestIn[i].rgb, the knee g_Test.vTest.x, the limit P g_Test.vTest.y, the desaturation
//! g_Test.vTest.z: out[i] = (the display values, 0).
[numthreads(64, 1, 1)]
void CSToneMap(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  TestOut(i, float4(NeutralToneMap(TestIn(i).rgb, g_Test.vTest.x, g_Test.vTest.y, g_Test.vTest.z), 0));
}

//! The HDR output's tone map (HdrOutputToneMap, Hdr.hlsl's highlight layer) of the light g_TestIn[i].rgb: the knee
//! g_Test.vTest.x, the peak's display value P g_Test.vTest.y, the desaturation g_Test.vTest.z, d3d12_HDRHighlights
//! g_Test.vTest.w. out[2i] = (the output's display values, 0), out[2i + 1] = (the SDR picture's, NeutralToneMap to 1, 0).
[numthreads(64, 1, 1)]
void CSHdrOutput(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float3 vLight = TestIn(i).rgb;
  const float k = g_Test.vTest.x, P = g_Test.vTest.y;
  TestOut(2 * i, float4(HdrOutputToneMap(vLight, k, P, g_Test.vTest.z, g_Test.vTest.w), 0));
  TestOut(2 * i + 1, float4(NeutralToneMap(vLight, k, 1, g_Test.vTest.z), 0));
}

//! A dot3 light map's light (LightmapLight.hlsli): out[i] = (LightmapLampFalloff(g_TestIn[i].x), LightmapDot3 of the
//! N'.D g_TestIn[i].y and the light map's alpha g_TestIn[i].z mixed and not, LightmapLampHighlight of the normal
//! g_TestIn[i + n], the direction to the lamp g_TestIn[i + 2n] and to the eye g_TestIn[i + 3n]), n = g_Test.nTest.x.
[numthreads(64, 1, 1)]
void CSLightmapLight(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x;
  if (i >= n)
    return;
  const float4 v = TestIn(i);
  TestOut(i, float4(LightmapLampFalloff(v.x), LightmapDot3(v.y, v.z, true), LightmapDot3(v.y, v.z, false),
                    LightmapLampHighlight(TestIn(i + n).xyz, TestIn(i + 2 * n).xyz, TestIn(i + 3 * n).xyz)));
}

//! The sky's glow towards the sun (HdrLight.hlsli): out[i] = (HdrSkyGlow(g_TestIn[i].x the cosine, y the glow, z the power), 0, 0, 0).
[numthreads(64, 1, 1)]
void CSSkyGlow(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 v = TestIn(i);
  TestOut(i, float4(HdrSkyGlow(v.x, v.y, v.z), 0, 0, 0));
}

//! The HDR output's frame average limit (HdrFrameAverage.hlsli): g_Test.nTest.y 0, out[i] = (FrameAverageFactor(g_TestIn[i].x the
//! SDR picture's average, y what the highlights add, z the limit), 0, 0, 0); 1, out[i] = (FrameAverageStep(x the last factor, y
//! the target, z down, w up, g_Test.nTest.z the reset), 0, 0, 0).
[numthreads(64, 1, 1)]
void CSFrameAverage(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 v = TestIn(i);
  const float f = g_Test.nTest.y == 0 ? FrameAverageFactor(v.x, v.y, v.z) : FrameAverageStep(v.x, v.y, v.z, v.w, g_Test.nTest.z != 0);
  TestOut(i, float4(f, 0, 0, 0));
}

//! The automatic exposure's target (HdrExposure.hlsli) of case i: g_TestIn[32i .. 32i + 15] its luminance histogram (four bins
//! an item), g_TestIn[32i + 16 .. 32i + 31] the sky's; g_Test.nTest.y the samples, g_Test.vTest the parameters (vParams),
//! g_Test.vTest2 x log2 of the knee, y z the sky's shares (from, full), w a percentile. out[2i] = ExposureTarget, out[2i + 1] =
//! (the luminance's percentile vTest2.w, its mean between vTest.y and z, the sky's percentile vTest.w, 0).
[numthreads(64, 1, 1)]
void CSExposure(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  uint vLuma[XR_HDR_HIST_BINS], vSky[XR_HDR_HIST_BINS];
  for (int n = 0; n < XR_HDR_HIST_BINS / 4; n++)
  {
    const uint4 a = g_TestIn[i * 32 + n], b = g_TestIn[i * 32 + 16 + n];
    vLuma[n * 4] = a.x;
    vLuma[n * 4 + 1] = a.y;
    vLuma[n * 4 + 2] = a.z;
    vLuma[n * 4 + 3] = a.w;
    vSky[n * 4] = b.x;
    vSky[n * 4 + 1] = b.y;
    vSky[n * 4 + 2] = b.z;
    vSky[n * 4 + 3] = b.w;
  }
  TestOut(i * 2, ExposureTarget(vLuma, vSky, g_Test.nTest.y, g_Test.vTest, g_Test.vTest2.x, g_Test.vTest2.yz));
  TestOut(i * 2 + 1, float4(ExposurePercentile(vLuma, g_Test.vTest2.w), ExposureWindowMean(vLuma, g_Test.vTest.y, g_Test.vTest.z),
                            ExposurePercentile(vSky, g_Test.vTest.w), 0));
}

//! The HDR statistics' maths (HdrShared.hlsli, HdrLight.hlsli). g_Test.nTest.y 0: of g_TestIn[i] = (light, u, v, 0) out[i] =
//! (HdrHistogramBin of the light as a word, MeterWeight at (u, v) of a 1920 x 1080 screen, 0, 0); 1: LayerRatio of the frame's
//! colour g_TestIn[2i].rgb and the layer g_TestIn[2i + 1]: out[i] = (the ratio, 1 where the layer has the scene); 2: g_TestIn[i].x
//! added to a sum in two words as CSOutput adds a frame's nits (HdrSumCarries): out[0] = (the low word, the high word, 0, 0).
[numthreads(64, 1, 1)]
void CSHdrStats(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  if (g_Test.nTest.y == 2)
  {
    const uint n = g_TestIn[i].x;
    uint nWas;
    InterlockedAdd(g_TestOut[0].x, n, nWas);
    if (HdrSumCarries(nWas, n))
      InterlockedAdd(g_TestOut[0].y, 1);
    return;
  }
  if (g_Test.nTest.y == 0)
  {
    const float4 a = TestIn(i);
    TestOut(i, float4(asfloat(HdrHistogramBin(a.x)), MeterWeight(a.yz, float2(1 / 1920.0, 1 / 1080.0)), 0, 0));
    return;
  }
  bool bScene;
  const float3 vRatio = LayerRatio(TestIn(i * 2).rgb, TestIn(i * 2 + 1), bScene);
  TestOut(i, float4(vRatio, bScene ? 1 : 0));
}

//! PathCacheCellOf and PathCacheCellSize of the point g_TestIn[2i].xyz with the normal g_TestIn[2i + 1].xyz, vCache g_Test.vTest, the
//! camera g_Test.vTest2.xyz: out[i] = (slot, key, the cell's size, 0).
[numthreads(64, 1, 1)]
void CSCacheCell(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float3 p = TestIn(i * 2).xyz, n = TestIn(i * 2 + 1).xyz;
  const SPathCacheCell c = PathCacheCellOf(p, n, g_Test.vTest, g_Test.vTest2.xyz);
  g_TestOut[i] = uint4(c.nSlot, c.nKey, asuint(PathCacheCellSize(p, g_Test.vTest, g_Test.vTest2.xyz)), 0);
}

//! PathCacheInsert (g_Test.nTest.y 0) or PathCacheFind (1) of the cells of CSCacheCell's input: out[i] = (found, the entry, the key, 0).
[numthreads(64, 1, 1)]
void CSCacheLookup(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const SPathCacheCell c = PathCacheCellOf(TestIn(i * 2).xyz, TestIn(i * 2 + 1).xyz, g_Test.vTest, g_Test.vTest2.xyz);
  uint e;
  const bool b = g_Test.nTest.y ? PathCacheFind(c, e) : PathCacheInsert(c, e);
  g_TestOut[i] = uint4(b ? 1 : 0, e, c.nKey, 0);
}

//! g_Test.nTest.x samples g_Test.vTest.rgb added to the entry g_Test.nTest.y (PathCacheAdd), a thread each.
[numthreads(64, 1, 1)]
void CSCacheAdd(uint i : SV_DispatchThreadID)
{
  if (i < g_Test.nTest.x)
    PathCacheAdd(g_Test.nTest.y, g_Test.vTest.rgb);
}

//! An entry as the buffer has it, the entry g_TestIn[i].x: out[3i] = (key, the samples' count, the light's two words),
//! out[3i + 1] the samples' sums, out[3i + 2] PathCacheLight.
[numthreads(64, 1, 1)]
void CSCacheRead(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const uint e = g_TestIn[i].x;
  const uint4 s = g_PathCache.Load4(PATH_CACHE_SAMPLES + e * 16);
  const uint2 l = g_PathCache.Load2(PATH_CACHE_LIGHT + e * 8);
  g_TestOut[i * 3] = uint4(g_PathCache.Load(e * 4), s.w, l);
  g_TestOut[i * 3 + 1] = uint4(s.xyz, 0);
  TestOut(i * 3 + 2, PathCacheLight(e));
}

//! Writes the cache's words: g_TestIn[i].x the byte offset, .y the value, a thread each (a state to start from).
[numthreads(64, 1, 1)]
void CSCachePoke(uint i : SV_DispatchThreadID)
{
  if (i < g_Test.nTest.x)
    g_PathCache.Store(g_TestIn[i].x, g_TestIn[i].y);
}
