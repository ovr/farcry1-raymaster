// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestHeaders.hlsl
//	Description: The unit tests' kernels over the renderer's shader
//	headers that stand alone (Shader Model 6.0): PathGBuffer.hlsli,
//	HdrLight.hlsli, HdrToneMap.hlsli and PathCache.hlsli, included as the renderer's
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
#include "PathCache.hlsli"

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

//! The HDR statistics' maths (HdrShared.hlsli, HdrLight.hlsli). g_Test.nTest.y 0: of g_TestIn[i] = (light, u, v, 0) out[i] =
//! (HdrHistogramBin of the light as a word, MeterWeight at (u, v) of a 1920 x 1080 screen, 0, 0); 1: LayerRatio of the frame's
//! colour g_TestIn[2i].rgb and the layer g_TestIn[2i + 1]: out[i] = (the ratio, 1 where the layer has the scene).
[numthreads(64, 1, 1)]
void CSHdrStats(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
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
