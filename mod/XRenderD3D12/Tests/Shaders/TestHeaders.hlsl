// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestHeaders.hlsl
//	Description: The unit tests' kernels over the renderer's shader
//	headers that stand alone (Shader Model 6.0): PathGBuffer.hlsli,
//	HdrLight.hlsli, HdrToneMap.hlsli, HdrExposure.hlsli, HdrFrameAverage.hlsli, PathCache.hlsli, LightmapLight.hlsli, RenderMode.hlsli, ArrayCoords.hlsli, WaterLight.hlsli, WaterFoam.hlsli, WaterCaustics.hlsli, WaterVolume.hlsli, Underwater.hlsli, AlphaCoverage.hlsli and Fxaa.hlsli, included as the renderer's
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
#include "RenderMode.hlsli"
#include "ArrayCoords.hlsli"
#include "WaterLight.hlsli"
#include "WaterFoam.hlsli"
#include "WaterCaustics.hlsli"
#include "WaterVolume.hlsli"
#include "Underwater.hlsli"
#include "OceanFFT.hlsli"
#include "Wake.hlsli"
// The ocean's grid's table of the game's disc (GridDiscAlpha): the input's first words (CSGridDisc).
#define GRID_DISC_WORD(i) asfloat(g_TestIn[(i) >> 2][(i) & 3])
#include "OceanGrid.hlsli"
#include "AlphaCoverage.hlsli"

//! CoverageAlpha (AlphaCoverage.hlsli) of the alpha g_TestIn[i].x, the reference .y and the alpha's change across the
//! pixel .z: out[i].x.
[numthreads(64, 1, 1)]
void CSCoverageAlpha(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(CoverageAlpha(a.x, a.y, a.z), 0, 0, 0));
}

//! General.hlsl's alpha test, then FadeAlpha (AlphaCoverage.hlsli): g_TestIn[i].x the texture's alpha, .y the test's
//! reference (0: none), .z the draw's fade (SDrawConstants::vEdgeFade.y, 0: none): out[i].x 1 where the test keeps the
//! pixel (clip(a - ref)), .y the alpha after it.
[numthreads(64, 1, 1)]
void CSFadeAlpha(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  const bool bKept = a.y <= 0 || a.x - a.y >= 0;
  TestOut(i, float4(bKept ? 1 : 0, FadeAlpha(a.x, a.z), 0, 0));
}

//! The FXAA tests' picture (Fxaa.hlsli): g_Test.nTest.y x g_Test.nTest.z pixels, g_TestIn[y * width + x] the rgba of
//! pixel (x, y) as floats, clamped to the picture as AntiAlias.hlsl clamps its loads.
float4 FxaaLoad(int2 p)
{
  const int2 vSize = int2(g_Test.nTest.y, g_Test.nTest.z);
  p = clamp(p, int2(0, 0), vSize - 1);
  return TestIn(p.y * vSize.x + p.x);
}

#include "Fxaa.hlsli"

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

//! The render modes' colour math (RenderMode.hlsli), n = g_Test.nTest.x items: f = g_TestIn[i].rgb, its w the glare's
//! threshold; d = g_TestIn[i + n].rgb, w the gain; a = g_TestIn[i + 2n].rgb, w the share of the luminance; r =
//! g_TestIn[i + 3n].rgb. out[4i] = (RenderModeGlareSource(d, a), RenderModeAdaptTarget(f)), out[4i + 1] =
//! (RenderModeCold(f, d), RenderModeEdge(u f, d d, l a, r r)), out[4i + 2] = (RenderModeGlared(f, the glare d), 0),
//! out[4i + 3] = (RenderModeCartoonColor(f), 0).
[numthreads(64, 1, 1)]
void CSRenderMode(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x;
  if (i >= n)
    return;
  const float4 f = TestIn(i), d = TestIn(i + n), a = TestIn(i + 2 * n), r = TestIn(i + 3 * n);
  TestOut(4 * i, float4(RenderModeGlareSource(d.rgb, a.rgb, f.w, d.w), RenderModeAdaptTarget(f.rgb)));
  TestOut(4 * i + 1, float4(RenderModeCold(f.rgb, d.rgb), RenderModeEdge(f.rgb, d.rgb, a.rgb, r.rgb)));
  TestOut(4 * i + 2, float4(RenderModeGlared(f.rgb, d.rgb, a.w), 0));
  TestOut(4 * i + 3, float4(RenderModeCartoonColor(f.rgb), 0));
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

//! FXAA (Fxaa.hlsli) of every pixel of the picture (FxaaLoad above, n = g_Test.nTest.x pixels) at the quality
//! g_Test.nTest.w: out[i] the pixel's colour as AntiAlias.hlsl leaves it (the picture's where FXAA finds no edge or no
//! blend: it does not write those), out[n + i] = (the blend, the step's x and y, 1 on an edge).
[numthreads(64, 1, 1)]
void CSFxaa(uint i : SV_DispatchThreadID)
{
  const uint n = g_Test.nTest.x, w = g_Test.nTest.y;
  if (i >= n)
    return;
  const SFxaaResult r = Fxaa(int2(i % w, i / w), g_Test.nTest.w);
  TestOut(i, r.bEdge && r.fBlend > 0 ? r.vColor : TestIn(i));
  TestOut(n + i, float4(r.fBlend, r.vStep, r.bEdge ? 1 : 0));
}

//! ArrayCoords (ArrayCoords.hlsli) of the quad's coordinates g_TestIn[i].xy in an array of g_TestIn[i].z slices (a uint
//! as a float): out[2i] = (vA, the weight), out[2i + 1] = (vB, 0).
[numthreads(64, 1, 1)]
void CSArrayCoords(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  const SArrayCoords c = ArrayCoords(a.xy, (uint)a.z);
  TestOut(i * 2, float4(c.vA, c.fW));
  TestOut(i * 2 + 1, float4(c.vB, 0));
}

//! BlendPictures and DissolvePictures (ArrayCoords.hlsli) of the pictures' texels g_TestIn[3i] and g_TestIn[3i + 1] by
//! the weight g_TestIn[3i + 2].x, the noise g_TestIn[3i + 2].y: out[2i] the blend, out[2i + 1] the dissolve.
[numthreads(64, 1, 1)]
void CSBlendPictures(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i * 3), b = TestIn(i * 3 + 1), w = TestIn(i * 3 + 2);
  TestOut(i * 2, BlendPictures(a, b, w.x));
  TestOut(i * 2 + 1, DissolvePictures(a, b, w.x, w.y));
}

//! PictureNoise (ArrayCoords.hlsli) of the pixel g_TestIn[i].xy in the frame g_TestIn[i].z: out[i].x.
[numthreads(64, 1, 1)]
void CSPictureNoise(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(PictureNoise(a.xy, a.z), 0, 0, 0));
}

//! WaterGlint (WaterLight.hlsli) of the normal g_TestIn[3i].xyz with the slopes' variance g_TestIn[3i].w, the
//! direction to the camera g_TestIn[3i + 1].xyz and to the sun g_TestIn[3i + 2].xyz: out[i] = (the glint, the density
//! of the facets of the half vector, the masking of both directions, the fresnel term at the half vector).
[numthreads(64, 1, 1)]
void CSWaterGlint(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 n = TestIn(i * 3);
  const float3 V = TestIn(i * 3 + 1).xyz, L = TestIn(i * 3 + 2).xyz;
  const float3 H = normalize(V + L);
  TestOut(i, float4(WaterGlint(n.xyz, V, L, n.w), WaterSlopeDensity(H, n.xyz, n.w), WaterMasking(V, n.w) * WaterMasking(L, n.w),
                    WaterFresnel(dot(V, H))));
}

//! WaterCrestLight (WaterLight.hlsli) of the normal g_TestIn[4i].xyz with the height g_TestIn[4i].w, the direction
//! to the camera g_TestIn[4i + 1].xyz with the waves' RMS slope g_TestIn[4i + 1].w and to the sun g_TestIn[4i + 2].xyz,
//! and WaterCrestColor of the deep water's colour g_TestIn[4i + 3].rgb: out[i] = (the light, the colour).
[numthreads(64, 1, 1)]
void CSWaterCrest(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 n = TestIn(i * 4), v = TestIn(i * 4 + 1);
  TestOut(i, float4(WaterCrestLight(n.xyz, v.xyz, TestIn(i * 4 + 2).xyz, n.w, v.w), WaterCrestColor(TestIn(i * 4 + 3).rgb)));
}

//! WaterCapLight (WaterLight.hlsli) of the light g_TestIn[i].rgb at most g_TestIn[i].w: out[i].rgb.
[numthreads(64, 1, 1)]
void CSWaterCap(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(WaterCapLight(a.rgb, a.w), 0));
}

//! The foam's share of a pixel (WaterFoam.hlsli): ShoreFoam of the depth, crest, significant height and breaker index
//! g_TestIn[3i]; FoamCover of the amount, the pattern's value, its width and the blur g_TestIn[3i + 1] and FoamOpacity of
//! that cover and the amount; FoamUniform of g_TestIn[3i + 2].xy: out[i] = (shore, cover, opacity, uniform).
[numthreads(64, 1, 1)]
void CSFoam(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 s = TestIn(i * 3), c = TestIn(i * 3 + 1), u = TestIn(i * 3 + 2);
  const float fCover = FoamCover(c.x, c.y, c.z, c.w);
  TestOut(i, float4(ShoreFoam(s.x, s.y, s.z, s.w), fCover, FoamOpacity(fCover, c.x), FoamUniform(u.x, u.y)));
}

//! WhitecapAmount (WaterFoam.hlsli) of the cascades' foam and the long waves' height and its inverse RMS g_TestIn[2i];
//! FoamBreakerIndex of g_TestIn[2i + 1].x and FoamValueNoise at g_TestIn[2i + 1].yz: out[i] = (amount, index, noise, 0).
[numthreads(64, 1, 1)]
void CSFoamMix(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 w = TestIn(i * 2), n = TestIn(i * 2 + 1);
  TestOut(i, float4(WhitecapAmount(w.x, w.y, w.z, w.w), FoamBreakerIndex(n.x), FoamValueNoise(n.yz), 0));
}

//! ContactFoam (WaterFoam.hlsli) of the view's path through the water, the crest and the significant height
//! g_TestIn[i].xyz: out[i].x.
[numthreads(64, 1, 1)]
void CSFoamContact(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(ContactFoam(a.x, a.y, a.z), 0, 0, 0));
}

//! SurfFoam (WaterFoam.hlsli) of the depth, the depth where the swell breaks and its phase g_TestIn[i].xyz: out[i].x.
[numthreads(64, 1, 1)]
void CSFoamSurf(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(SurfFoam(a.x, a.y, a.z), 0, 0, 0));
}

//! SurfClumps (WaterFoam.hlsli) at the position g_TestIn[2i].xy, the depth .z, the break's depth .w, the phase
//! g_TestIn[2i + 1].x; .y WakeClumps there of the amount g_TestIn[2i + 1].y.
[numthreads(64, 1, 1)]
void CSFoamClumps(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(2 * i), b = TestIn(2 * i + 1);
  TestOut(i, float4(SurfClumps(a.xy, a.z, a.w, b.x), WakeClumps(a.xy, b.y), 0, 0));
}

//! FoamShade (WaterFoam.hlsli) of the pattern's value g_TestIn[i].x, the amount .y, the blur .z.
[numthreads(64, 1, 1)]
void CSFoamShade(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(FoamShade(a.x, a.y, a.z), 0, 0, 0));
}

//! CausticsRay (OceanFFT.hlsli) of the slopes g_TestIn[2i].xy and the sun's direction g_TestIn[2i + 1].xyz: out[i].xy the
//! ray's move per metre, .z the light let in.
[numthreads(64, 1, 1)]
void CSCausticsRay(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  float fLight;
  const float2 v = CausticsRay(TestIn(2 * i).xy, TestIn(2 * i + 1).xyz, fLight);
  TestOut(i, float4(v, fLight, 0));
}

//! CausticsAtDepth (WaterCaustics.hlsli) of the texel g_TestIn[2i] at the first depth g_TestIn[2i + 1].x and the depth .y
//! (out[i].x), CausticsFactor of it at the share .z and the most .w (out[i].y).
[numthreads(64, 1, 1)]
void CSCausticsDepth(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(2 * i + 1);
  const float I = CausticsAtDepth(TestIn(2 * i), a.x, a.y);
  TestOut(i, float4(I, CausticsFactor(I, a.z, a.w), 0, 0));
}

//! CausticsFar (WaterCaustics.hlsli) of the texels a pixel takes g_TestIn[i].x: out[i].x.
[numthreads(64, 1, 1)]
void CSCausticsFar(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  TestOut(i, float4(CausticsFar(TestIn(i).x), 0, 0, 0));
}

//! VolumeAttenuation (WaterVolume.hlsli) of the colour g_TestIn[i].rgb and the density .w: out[i].rgb.
[numthreads(64, 1, 1)]
void CSVolumeAttenuation(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  TestOut(i, float4(VolumeAttenuation(TestIn(i).rgb, TestIn(i).w), 0));
}

//! VolumeInscatter (WaterVolume.hlsli) of the deep colour g_TestIn[3i].rgb, the attenuation g_TestIn[3i + 1].rgb, the
//! light's g_TestIn[3i + 2].rgb, the cosine g_TestIn[3i + 1].w, from g_TestIn[3i].w to g_TestIn[3i + 2].w: out[i].rgb.
[numthreads(64, 1, 1)]
void CSVolumeInscatter(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(3 * i), b = TestIn(3 * i + 1), c = TestIn(3 * i + 2);
  TestOut(i, float4(VolumeInscatter(a.rgb, b.rgb, c.rgb, b.w, a.w, c.w), 0));
}

//! VolumeReach (WaterVolume.hlsli) of the attenuations g_TestIn[i].xyz and the path .w (out[3i].x); VolumeSegmentEnd of
//! each segment out to it (out[3i + 1]), VolumeSegmentPoint (out[3i + 2]).
[numthreads(64, 1, 1)]
void CSVolumeSegments(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float r = VolumeReach(TestIn(i).xyz, TestIn(i).w);
  TestOut(3 * i, float4(r, XR_VOLUME_TAPS, 0, 0));
  float4 e = 0, p = 0;
  [unroll]
  for (uint j = 0; j < XR_VOLUME_TAPS; j++)
  {
    e[j] = VolumeSegmentEnd(j, r);
    p[j] = VolumeSegmentPoint(j, r);
  }
  TestOut(3 * i + 1, e);
  TestOut(3 * i + 2, p);
}

//! VolumeRefract (WaterVolume.hlsli) of the view g_TestIn[2i].xyz and the normal g_TestIn[2i + 1].xyz (out[2i].xyz),
//! VolumeRefractedCos of the view's z (out[2i].w); VolumeSunEntry of the point g_TestIn[2i].xyz fDepth g_TestIn[2i + 1].w
//! down, the sun's direction the normal (out[2i + 1].xyz); SnellWindow of g_TestIn[2i].w (out[2i + 1].w).
[numthreads(64, 1, 1)]
void CSVolumeRay(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(2 * i), b = TestIn(2 * i + 1);
  TestOut(2 * i, float4(VolumeRefract(a.xyz, b.xyz), VolumeRefractedCos(a.z)));
  TestOut(2 * i + 1, float4(VolumeSunEntry(a.xyz, b.w, b.xyz), SnellWindow(a.w)));
}

//! UnderPath (Underwater.hlsli) of the distance g_TestIn[i].x, the direction's z .y, the camera's depth .z and the cap .w:
//! out[i].x.
[numthreads(64, 1, 1)]
void CSUnderPath(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  TestOut(i, float4(UnderPath(a.x, a.y, a.z, a.w), 0, 0, 0));
}

//! UnderPhase (Underwater.hlsli) of the cosine g_TestIn[i].x: out[i].x.
[numthreads(64, 1, 1)]
void CSUnderPhase(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  TestOut(i, float4(UnderPhase(TestIn(i).x), 0, 0, 0));
}

//! UnderSegmentEnd (Underwater.hlsli) of each of the XR_UNDER_TAPS segments at the attenuation g_TestIn[i].x out to the
//! reach .y (out[5i] to out[5i + 3], segment j + 1's end in [j / 4][j % 4]); UnderSegmentAt of the share .z (out[5i + 4].x),
//! XR_UNDER_TAPS (out[5i + 4].y).
[numthreads(64, 1, 1)]
void CSUnderSegments(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(i);
  float4 e[XR_UNDER_TAPS / 4];
  [unroll]
  for (uint j = 0; j < XR_UNDER_TAPS; j++)
    e[j / 4][j % 4] = UnderSegmentEnd(j + 1, a.x, a.y);
  [unroll]
  for (uint q = 0; q < XR_UNDER_TAPS / 4; q++)
    TestOut(5 * i + q, e[q]);
  TestOut(5 * i + 4, float4(UnderSegmentAt(a.z, a.x, a.y), XR_UNDER_TAPS, 0, 0));
}

//! UnderWindowOrder (Underwater.hlsli) of the 4 x 4 window starting at the pixel g_TestIn[i].xy, each texel (a, b) given
//! UnderBlockSegment of its pixel plus 16 (a + 4 b): out[4i + q][r] the segment 4 q + r's.
[numthreads(64, 1, 1)]
void CSUnderWindow(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const uint2 vStart = (uint2)TestIn(i).xy;
  float4 G[4];
  [unroll]
  for (uint b = 0; b < 4; b++)
  {
    [unroll]
    for (uint a = 0; a < 4; a++)
      G[b][a] = UnderBlockSegment(float2(vStart + uint2(a, b)) + 0.5) + 16 * (a + 4 * b);
  }
  float v[XR_UNDER_TAPS];
  UnderWindowOrder(G, vStart, v);
  [unroll]
  for (uint q = 0; q < XR_UNDER_TAPS / 4; q++)
    TestOut(4 * i + q, float4(v[4 * q], v[4 * q + 1], v[4 * q + 2], v[4 * q + 3]));
}

//! UnderInscatter (Underwater.hlsli) of the deep colour g_TestIn[4i].rgb (its light UnderDeepSource's; g_TestIn[4i + 3].z
//! over 0: the light the water scatters a metre at the surface itself), the camera's depth .w, the attenuation
//! g_TestIn[4i + 1].rgb, the cosine .w, the path g_TestIn[4i + 2].x, the reach .y, the sun's share .z, the phase .w, the
//! sun's light at segment j g_TestIn[4i + 3].x + j g_TestIn[4i + 3].y (at least 0): out[i] (the light, fLit).
[numthreads(64, 1, 1)]
void CSUnderInscatter(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(4 * i), b = TestIn(4 * i + 1), c = TestIn(4 * i + 2), s = TestIn(4 * i + 3);
  float v[XR_UNDER_TAPS];
  [unroll]
  for (uint j = 0; j < XR_UNDER_TAPS; j++)
    v[j] = max(s.x + j * s.y, 0);
  float fLit;
  const float3 vSource = s.z > 0 ? a.rgb : UnderDeepSource(a.rgb, b.rgb);
  const float3 L = UnderInscatter(vSource, b.rgb, a.w, b.w, c.x, c.y, c.z, c.w, v, fLit);
  TestOut(i, float4(L, fLit));
}

//! UnderInscatterClosed (Underwater.hlsli) of the same inputs as CSUnderInscatter: out[i] (the light, fLit).
[numthreads(64, 1, 1)]
void CSUnderInscatterClosed(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(4 * i), b = TestIn(4 * i + 1), c = TestIn(4 * i + 2), s = TestIn(4 * i + 3);
  float v[XR_UNDER_TAPS];
  [unroll]
  for (uint j = 0; j < XR_UNDER_TAPS; j++)
    v[j] = max(s.x + j * s.y, 0);
  float fLit;
  const float3 vSource = s.z > 0 ? a.rgb : UnderDeepSource(a.rgb, b.rgb);
  const float3 L = UnderInscatterClosed(vSource, b.rgb, a.w, b.w, c.x, c.y, c.z, c.w, v, fLit);
  TestOut(i, float4(L, fLit));
}

//! UnderFog (Underwater.hlsli) of the deep colour g_TestIn[4i].rgb (its light UnderDeepSource's), the camera's depth .w, the
//! attenuation g_TestIn[4i + 1].rgb,
//! the distance .w, the view's direction g_TestIn[4i + 2].xyz, the surface's depth .w, the sun g_TestIn[4i + 3].xyz, the sun's
//! share .w, the cap 4 (with a positive distance; 0 with a negative one, its absolute value): out[2i] (the light, the phase
//! UnderSunPhase), out[2i + 1].rgb T.
[numthreads(64, 1, 1)]
void CSUnderFog(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(4 * i), b = TestIn(4 * i + 1), d = TestIn(4 * i + 2), e = TestIn(4 * i + 3);
  float3 T;
  const float3 L = UnderFog(UnderDeepSource(a.rgb, b.rgb), b.rgb, a.w, d.xyz, abs(b.w), d.w, b.w > 0 ? 4 : 0, e.xyz, e.w, T);
  TestOut(2 * i, float4(L, UnderSunPhase(e.xyz, d.xyz)));
  TestOut(2 * i + 1, float4(T, 0));
}

//! UnderSurface (Underwater.hlsli) of the deep colour g_TestIn[2i].rgb, the window .w, the light above g_TestIn[2i + 1].rgb
//! and the cosine .w: out[i].rgb.
[numthreads(64, 1, 1)]
void CSUnderSurface(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(2 * i), b = TestIn(2 * i + 1);
  TestOut(i, float4(UnderSurface(a.rgb, b.rgb, a.w, b.w), 0));
}

//! d3d12_WaterUnderClear (Underwater.hlsli) of the attenuation g_TestIn[3i].rgb, the sun's height .w, the sun's light
//! g_TestIn[3i + 1].rgb, the cosine .w, the sky's light g_TestIn[3i + 2].rgb, the phase function's share .w: out[3i].rgb
//! UnderDownLight, out[3i + 1].rgb UnderClearSource of it, out[3i + 2].rgb UnderClearBelow of that.
[numthreads(64, 1, 1)]
void CSUnderClear(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(3 * i), b = TestIn(3 * i + 1), c = TestIn(3 * i + 2);
  const float3 vDown = UnderDownLight(b.rgb, c.rgb, a.w), vSource = UnderClearSource(a.rgb, vDown);
  TestOut(3 * i, float4(vDown, 0));
  TestOut(3 * i + 1, float4(vSource, 0));
  TestOut(3 * i + 2, float4(UnderClearBelow(vSource, a.rgb, b.w, c.w), 0));
}

//! FoamLight (WaterFoam.hlsli) of the normal g_TestIn[4i].xyz in the sun's shadow g_TestIn[4i].w, the sun's direction
//! g_TestIn[4i + 1].xyz, its light g_TestIn[4i + 2].rgb and the sky's g_TestIn[4i + 3].rgb: out[i].rgb.
[numthreads(64, 1, 1)]
void CSFoamLight(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 n = TestIn(i * 4);
  TestOut(i, float4(FoamLight(n.xyz, TestIn(i * 4 + 1).xyz, TestIn(i * 4 + 2).rgb, n.w, TestIn(i * 4 + 3).rgb), 0));
}

//! The wakes' window and bodies (Wake.hlsli): WakeShape and WakeDepth at X g_TestIn[3i].xy of the pose g_TestIn[3i + 1]
//! and the shape g_TestIn[3i + 2] (out[i].xy); WakeCell and WakeOffset of texel g_TestIn[3i].z around cell
//! g_TestIn[3i].w (out[i].zw).
[numthreads(64, 1, 1)]
void CSWakeShape(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(3 * i), vPose = TestIn(3 * i + 1), vShape = TestIn(3 * i + 2);
  const uint t = (uint)a.z;
  const int c = (int)a.w;
  TestOut(i, float4(WakeShape(a.xy, vPose, vShape), WakeDepth(a.xy, vPose, vShape), (float)WakeCell(t, c), WakeOffset(t, c)));
}

//! The ocean's grid (OceanGrid.hlsli) of g_TestIn[4i] (the radius r, a texel's size, the height h, the depth under the
//! level, all m), g_TestIn[4i + 1] a body's pose, g_TestIn[4i + 2] (its half sides xy, the point P zw) and
//! g_TestIn[4i + 3] (the camera's height over the level; the height of a scene behind, the surface's in front of it, the
//! level, all m): out[2i] GridSpacing(r), GridLod of it and the texel, GridShore, GridBodyShare at P; out[2i + 1]
//! GridCameraShare, GridCameraCap of h, GridFarShare, GridHiddenShare.
[numthreads(64, 1, 1)]
void CSGrid(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(4 * i), vPose = TestIn(4 * i + 1), b = TestIn(4 * i + 2), e = TestIn(4 * i + 3);
  const float fSpacing = GridSpacing(a.x);
  TestOut(2 * i, float4(fSpacing, GridLod(fSpacing, a.y), GridShore(a.z, a.w), GridBodyShare(b.zw, vPose, float4(b.xy, 0, 0))));
  TestOut(2 * i + 1, float4(GridCameraShare(a.x, e.x), GridCameraCap(a.z, a.x, e.x), GridFarShare(a.x), GridHiddenShare(e.y, e.z, e.w)));
}

//! GridDiscAlpha (OceanGrid.hlsli) at the points g_TestIn[g_Test.nTest.z + i].xy (m from the disc's centre) of the
//! disc of g_Test.nTest.y rings whose table (OceanGrid::DiscTable) is the input's first words, then g_Test.nTest.w
//! rings of the grid (OceanGrid::FrameRings): out[i].x; yz GridRing of the ring g_TestIn[g_Test.nTest.z + i].z; w with
//! g_Test.vTest.x (its rows a doubling, OceanGrid::AlphaScale) GridDiscAlphaTable of the table (OceanGrid::AlphaTable)
//! from the word g_Test.vTest.y.
[numthreads(64, 1, 1)]
void CSGridDisc(uint i : SV_DispatchThreadID)
{
  if (i >= g_Test.nTest.x)
    return;
  const float4 a = TestIn(g_Test.nTest.z + i);
  const float2 vRing = g_Test.nTest.w > 0 ? GridRing((uint)a.z, g_Test.nTest.y * (1 + XR_GRID_DISC_SEGMENTS), g_Test.nTest.w) : 0;
  const float fTable = g_Test.vTest.x > 0 ? GridDiscAlphaTable(a.xy, (uint)g_Test.vTest.y, g_Test.vTest.x) : 0;
  TestOut(i, float4(GridDiscAlpha(a.xy, g_Test.nTest.y), vRing, fTable));
}