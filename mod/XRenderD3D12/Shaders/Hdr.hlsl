// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Hdr.hlsl
//	Description: The HDR program (Hdr.cpp, r_HDRRendering): the exposure,
//	the bloom and the tone mapping of the main view's scene drawn into
//	D3D12_HDR_FORMAT.
//
//	The scene's colours are the game's own (gamma encoded, as the original
//	draws them) without the clamp at 1; light is Decode(colour) = colour ^
//	2.2 up to 1 and linear above (HdrDecode, HdrLight.hlsli: what the game
//	adds up above white is light added up), the display's colour
//	Encode(light) its inverse. The
//	exposure is in EV (log2 of the factor of the light); its 1 x 1 target
//	holds the adapted part, the compensation is added at each use.
//
//	A triangle over the whole target, POSITION in clip space; the mode is
//	in nFlags, vParams0.xy is 1 / the target's size.
//	  HDR_LUMINANCE   tex0 the scene; vParams0.z taps per side. (w * log2 L,
//	                  w) of the light, L its luminance (clamped to 2^-12 ..
//	                  2^8), w the weight of the pixel (the middle of the
//	                  screen counts most)
//	  HDR_REDUCE      tex0 the level before; the mean of vParams0.z x
//	                  vParams0.z bilinear taps over the target's texel
//	  HDR_ADAPT       tex0 the meter (1 x 1: x / y the mean log2 L), tex1 the
//	                  EV before: vParams0 x log2 of the key, y the least EV,
//	                  z the most, w 1: no history (the target EV at once);
//	                  vParams1 x the step towards a smaller EV (a brighter
//	                  scene), y towards a larger one
//	  HDR_BLOOM_DOWN  tex0 the level before, vParams0.zw one of its texels in
//	                  uv: the 13 taps of a 6 x 6 footprint (a box of 4 x 4 and
//	                  four of 2 x 2). With HDR_BLOOM_PREFILTER tex0 is the
//	                  scene: its light exposed (tex1 the EV, vParams1.x the
//	                  compensation, vParams1.y 1 with the adapted EV), each
//	                  box weighted by 1 / (1 + its luminance) against single
//	                  bright pixels, then what is above the threshold
//	                  vParams1.z (soft over vParams1.w)
//	  HDR_BLOOM_UP    tex0 the smaller level, vParams0.zw one of its texels in
//	                  uv: a 3 x 3 tent, added to the target (ONE, ONE)
//	  HDR_RESOLVE     tex0 the scene, tex1 the bloom, tex2 the EV: the light
//	                  exposed (vParams1.x the compensation, vParams1.y 1 with
//	                  the adapted EV) plus the bloom times vParams1.z, tone
//	                  mapped by vParams1.w (0 clamp, 1 soft shoulder from the
//	                  knee vParams2.x, 2 ACES fitted, 3 AgX); vParams2.y 1: the
//	                  scene clamped alone (heat vision); vParams2.z the debug
//	                  view (1 the luminance in EV bands, 2 the bloom alone);
//	                  vParams2.w 1: then through the colour grading's table
//	                  tex3 (ColorGrading.cpp). The alpha is the scene's.
//	                  PSHighlight also writes the highlight layer (Common.hlsli,
//	                  HdrOutput.cpp): the tone map continued above the knee up
//	                  to the display's peak (HighlightRatio), vParams3.x the
//	                  peak's display value HdrEncode(peak / paper white), y
//	                  peak / paper white
//	  HDR_OUTPUT      tex0 the frame (the 8-bit back buffer), tex1 the
//	                  highlight layer: the frame's light times the layer's
//	                  ratio where its fingerprint matches, times paper white
//	                  (vParams0.x nits), at most the peak (vParams0.y nits),
//	                  for the swap chain: vParams0.z 1 HDR10 (PQ, Rec. 2020),
//	                  2 scRGB (linear Rec. 709, 1 = 80 nits). vParams0.w the
//	                  debug view (d3d12_HDROutputDebug: 1 the mask, 2 the test
//	                  patches), vParams1.xy 1 / the frame's size
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
// Decode, Luminance, Max3, MeterTapUV, MeterTexel, BloomDownTaps
#include "HdrShared.hlsli"

struct SHdrOutput
{
  float4 vPos : SV_Position;
};

SHdrOutput VSMain(SVertexInput In)
{
  SHdrOutput Out;
  Out.vPos = float4(In.vPos.xy, 0, 1);
  return Out;
}

float4 SampleClamp(uint nTex, float2 vUV)
{
  return g_Textures[nTex].SampleLevel(g_SamplerLinearClamp, vUV, 0);
}

float3 Encode(float3 vLight)
{
  return float3(HdrEncode(vLight.r), HdrEncode(vLight.g), HdrEncode(vLight.b));
}

//! The factor of the light: 2^(the compensation + the adapted EV in the 1 x 1 target nTex).
float Exposure(uint nTex, float fCompensation, bool bAuto)
{
  return exp2(fCompensation + (bAuto ? g_Textures[nTex].Load(int3(0, 0, 0)).r : 0));
}

//////////////////////////////////////////////////////////////////////////
// The meter and the adaptation

float4 MeterLuminance(float2 vPixel)
{
  float2 vTexel = g_Draw.vParams0.xy;
  float2 vUV = vPixel * vTexel;
  int n = max((int)g_Draw.vParams0.z, 1);
  float3 vSum = 0;
  for (int y = 0; y < n; y++)
  {
    for (int x = 0; x < n; x++)
      vSum += Decode(SampleClamp(g_Draw.nTex0, MeterTapUV(vUV, vTexel, x, y, n)).rgb);
  }
  return MeterTexel(vSum, n, vUV, vTexel);
}

float4 Reduce(float2 vPixel)
{
  float2 vTexel = g_Draw.vParams0.xy;
  float2 vUV = vPixel * vTexel;
  int n = max((int)g_Draw.vParams0.z, 1);
  float4 vSum = 0;
  for (int y = 0; y < n; y++)
  {
    for (int x = 0; x < n; x++)
      vSum += SampleClamp(g_Draw.nTex0, vUV + ((float2(x, y) + 0.5) / n - 0.5) * vTexel);
  }
  return vSum / (n * n);
}

float4 Adapt()
{
  float2 vMeter = g_Textures[g_Draw.nTex0].Load(int3(0, 0, 0)).rg;
  float fMeanLog = vMeter.y > 0 ? vMeter.x / vMeter.y : 0;
  float fTarget = clamp(g_Draw.vParams0.x - fMeanLog, g_Draw.vParams0.y, g_Draw.vParams0.z);
  float fBefore = g_Textures[g_Draw.nTex1].Load(int3(0, 0, 0)).r;
  if (g_Draw.vParams0.w > 0 || !(abs(fBefore) < 100))
    return float4(fTarget, 0, 0, 0);
  return float4(lerp(fBefore, fTarget, fTarget < fBefore ? g_Draw.vParams1.x : g_Draw.vParams1.y), 0, 0, 0);
}

//////////////////////////////////////////////////////////////////////////
// The bloom

//! The light of tex0 at vUV: of the scene (exposed) for the prefilter, else of a bloom level.
float3 BloomTap(float2 vUV, bool bScene, float fExposure)
{
  float3 c = SampleClamp(g_Draw.nTex0, vUV).rgb;
  return bScene ? Decode(c) * fExposure : c;
}

float4 BloomDown(float2 vPixel)
{
  float2 vUV = vPixel * g_Draw.vParams0.xy;
  float2 t = g_Draw.vParams0.zw;
  bool bScene = HasFlag(HDR_BLOOM_PREFILTER);
  float fExposure = bScene ? Exposure(g_Draw.nTex1, g_Draw.vParams1.x, g_Draw.vParams1.y > 0) : 1;
  float3 a = BloomTap(vUV + t * float2(-2, -2), bScene, fExposure);
  float3 b = BloomTap(vUV + t * float2(0, -2), bScene, fExposure);
  float3 c = BloomTap(vUV + t * float2(2, -2), bScene, fExposure);
  float3 d = BloomTap(vUV + t * float2(-1, -1), bScene, fExposure);
  float3 e = BloomTap(vUV + t * float2(1, -1), bScene, fExposure);
  float3 f = BloomTap(vUV + t * float2(-2, 0), bScene, fExposure);
  float3 g = BloomTap(vUV, bScene, fExposure);
  float3 h = BloomTap(vUV + t * float2(2, 0), bScene, fExposure);
  float3 i = BloomTap(vUV + t * float2(-1, 1), bScene, fExposure);
  float3 j = BloomTap(vUV + t * float2(1, 1), bScene, fExposure);
  float3 k = BloomTap(vUV + t * float2(-2, 2), bScene, fExposure);
  float3 l = BloomTap(vUV + t * float2(0, 2), bScene, fExposure);
  float3 m = BloomTap(vUV + t * float2(2, 2), bScene, fExposure);
  return float4(BloomDownTaps(a, b, c, d, e, f, g, h, i, j, k, l, m, bScene, g_Draw.vParams1.z, g_Draw.vParams1.w), 1);
}

float4 BloomUp(float2 vPixel)
{
  float2 vUV = vPixel * g_Draw.vParams0.xy;
  float2 t = g_Draw.vParams0.zw;
  float3 vSum = SampleClamp(g_Draw.nTex0, vUV).rgb * 4;
  vSum += (SampleClamp(g_Draw.nTex0, vUV + float2(-t.x, 0)).rgb + SampleClamp(g_Draw.nTex0, vUV + float2(t.x, 0)).rgb +
           SampleClamp(g_Draw.nTex0, vUV + float2(0, -t.y)).rgb + SampleClamp(g_Draw.nTex0, vUV + float2(0, t.y)).rgb) * 2;
  vSum += SampleClamp(g_Draw.nTex0, vUV - t).rgb + SampleClamp(g_Draw.nTex0, vUV + t).rgb +
          SampleClamp(g_Draw.nTex0, vUV + float2(-t.x, t.y)).rgb + SampleClamp(g_Draw.nTex0, vUV + float2(t.x, -t.y)).rgb;
  return float4(vSum / 16, 1);
}

//////////////////////////////////////////////////////////////////////////
// The tone maps: light to the display's colour (0 .. 1, gamma encoded)

//! The game's look up to the knee k (display values); above it each channel c rolls off to 1 as
//! k + (1 - k) (1 - e^(-(c - k) / (1 - k))), smooth at the knee. Per channel, as the original's
//! target clips: light far above white goes white (the sun), a bright saturated colour towards
//! the original's clipped one (fire: yellow), only without the hard edge.
float3 SoftShoulder(float3 vLight, float k)
{
  float3 c = Encode(vLight);
  k = clamp(k, 0, 0.99);
  float fRange = 1 - k;
  float3 vTop = k + fRange * (1 - exp(-(max(c, k) - k) / fRange));
  return lerp(vTop, c, step(c, k));
}

//! ACES (the RRT and an sRGB ODT), fitted (Stephen Hill): light in, light out (0 .. 1).
float3 AcesFitted(float3 vLight)
{
  static const float3x3 INPUT = { 0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777 };
  static const float3x3 OUTPUT = { 1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602 };
  float3 v = mul(INPUT, vLight);
  float3 a = v * (v + 0.0245786) - 0.000090537;
  float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return saturate(mul(OUTPUT, a / b));
}

//! AgX (Troy Sobotka), its base look, with the polynomial fit of its curve (Benjamin Wrensch):
//! light in, light out (0 .. 1).
float3 AgX(float3 vLight)
{
  static const float3x3 INSET = { 0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                  0.0784335999999992, 0.878468636469772, 0.0784336,
                                  0.0792237451477643, 0.0791661274605434, 0.879142973793104 };
  static const float3x3 OUTSET = { 1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                   -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                   -0.0990297440797205, -0.0989611768448433, 1.15107367264116 };
  static const float MIN_EV = -12.47393, MAX_EV = 4.026069;
  float3 v = mul(vLight, INSET);
  v = clamp(log2(max(v, 1e-10)), MIN_EV, MAX_EV);
  v = (v - MIN_EV) / (MAX_EV - MIN_EV);
  float3 v2 = v * v, v4 = v2 * v2;
  v = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4 - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
  v = mul(v, OUTSET);
  return pow(saturate(v), 2.2);
}

float3 ToneMap(float3 vLight, int nMode, float fKnee)
{
  if (nMode == 1)
    return SoftShoulder(vLight, fKnee);
  if (nMode == 2)
    return Encode(AcesFitted(vLight));
  if (nMode == 3)
    return Encode(AgX(vLight));
  return saturate(Encode(vLight));
}

//! d3d12_HDRDebug 1: the luminance in whole EVs from middle grey (0.18): -5 and below dark blue,
//! then blue, light blue, teal, green at middle grey, yellow, orange, red, magenta, +5 and above
//! white.
float3 EvBands(float L)
{
  static const float3 BANDS[11] = { float3(0, 0, 0.25), float3(0, 0, 0.6), float3(0, 0.3, 1), float3(0, 0.7, 1), float3(0, 0.6, 0.4),
                                    float3(0.2, 0.9, 0.2), float3(0.8, 0.9, 0), float3(1, 0.6, 0), float3(1, 0.2, 0), float3(1, 0, 0.6),
                                    float3(1, 1, 1) };
  return BANDS[(int)clamp(round(log2(max(L, 1e-6) / 0.18)), -5, 5) + 5];
}

//! The colour grading's table (ColorGrading.cpp) at the display colour c: HDR_COLOR_LUT_SIZE slices
//! of blue side by side, red across each and green down; the two slices around c's blue, each
//! looked up bilinearly, blended.
float3 ColorLut(uint nTex, float3 c)
{
  static const float N = HDR_COLOR_LUT_SIZE;
  c = saturate(c) * (N - 1);
  float b0 = min(floor(c.b), N - 2);
  float2 vUV = (c.rg + 0.5) / float2(N * N, N);
  float3 v0 = SampleClamp(nTex, vUV + float2(b0 / N, 0)).rgb;
  float3 v1 = SampleClamp(nTex, vUV + float2((b0 + 1) / N, 0)).rgb;
  return lerp(v0, v1, c.b - b0);
}

//! The highlight layer's ratio (Common.hlsli, HdrOutput.cpp): the tone map continued above the
//! knee up to the display's peak, as light, over the light of its SDR value s, per channel. Below
//! the knee k it is 1. Above it, t, how far the SDR curve went into its shoulder, goes on along a
//! shoulder of the same shape that ends at the peak's display value P = vParams3.x instead of 1:
//! k + (P - k) (1 - e^(-t / (P - k))), smooth at the knee. For the soft shoulder t is its own
//! (the encoded light above the knee); ACES and AgX keep their curve up to the knee, and t is
//! what puts a soft shoulder where they got to, -(1 - k) ln(1 - (s - k) / (1 - k)). The clamp
//! clips at the peak instead of at white.
float3 HighlightRatio(float3 vLight, float3 s, int nMode, float k)
{
  float3 h;
  if (nMode == 0)
    h = Encode(min(vLight, g_Draw.vParams3.y));
  else
  {
    k = clamp(k, 0, 0.99);
    float fRange = 1 - k, fHead = max(g_Draw.vParams3.x - k, 1e-4);
    float3 t = nMode == 1 ? max(Encode(vLight) - k, 0) : -fRange * log(max(1 - saturate((s - k) / fRange), 1e-7));
    h = lerp(k + fHead * (1 - exp(-t / fHead)), s, step(s, k));
  }
  return max(Decode(h) / max(Decode(s), 1e-6), 1);
}

//! vRatio: the highlight layer's ratio (1 for the clamped scene and the debug views).
float4 Resolve(float2 vPixel, out float3 vRatio)
{
  vRatio = 1;
  float4 vScene = g_Textures[g_Draw.nTex0].Load(int3(vPixel, 0));
  float fAlpha = saturate(vScene.a);
  if (g_Draw.vParams2.y > 0)
    return float4(saturate(vScene.rgb), fAlpha);
  float fExposure = Exposure(g_Draw.nTex2, g_Draw.vParams1.x, g_Draw.vParams1.y > 0);
  float3 vLight = Decode(vScene.rgb) * fExposure;
  float3 vBloom = g_Draw.vParams1.z > 0 ? SampleClamp(g_Draw.nTex1, vPixel * g_Draw.vParams0.xy).rgb * g_Draw.vParams1.z : 0;
  int nDebug = (int)g_Draw.vParams2.z;
  if (nDebug == 1)
    return float4(EvBands(Luminance(vLight)), fAlpha);
  if (nDebug == 2)
    vLight = vBloom;
  else
    vLight += vBloom;
  float3 c = ToneMap(vLight, (int)g_Draw.vParams1.w, g_Draw.vParams2.x);
  if (nDebug == 0)
    vRatio = HighlightRatio(vLight, c, (int)g_Draw.vParams1.w, g_Draw.vParams2.x);
  if (g_Draw.vParams2.w > 0 && nDebug == 0)
    c = ColorLut(g_Draw.nTex3, c);
  return float4(c, fAlpha);
}

//////////////////////////////////////////////////////////////////////////
// The output pass (HdrOutput.cpp)

static const float3x3 REC709_TO_REC2020 = { 0.6274040, 0.3292820, 0.0433136, 0.0690970, 0.9195400, 0.0113612, 0.0163916, 0.0880132, 0.8955950 };

//! SMPTE ST 2084 (PQ) of a light in nits.
float3 Pq(float3 vNits)
{
  float3 y = pow(saturate(vNits / 10000), 0.1593017578125);
  return pow((0.8359375 + 18.8515625 * y) / (1 + 18.6875 * y), 78.84375);
}

//! d3d12_HDROutputDebug 2: four patches across the middle of the screen, at paper white, twice and
//! four times that and at the peak (light in paper whites, fPeak the peak's); vLight elsewhere.
float3 TestPatches(float2 vUV, float3 vLight, float fPeak)
{
  if (vUV.y < 0.4 || vUV.y > 0.6 || vUV.x < 0.1 || vUV.x > 0.9)
    return vLight;
  float x = (vUV.x - 0.1) / 0.2;
  if (frac(x) < 0.1 || frac(x) > 0.9)
    return 0;
  static const float LEVELS[3] = { 1, 2, 4 };
  int n = min((int)x, 3);
  return n == 3 ? fPeak : LEVELS[n];
}

float4 Output(float2 vPixel)
{
  float3 f = g_Textures[g_Draw.nTex0].Load(int3(vPixel, 0)).rgb;
  float4 vLayer = g_Textures[g_Draw.nTex1].Load(int3(vPixel, 0));
  bool bScene = abs(HighlightFingerprint(f) - vLayer.a) <= HIGHLIGHT_TOLERANCE;
  float3 vRatio = bScene ? max(vLayer.rgb, 1) : 1;
  float fPaper = g_Draw.vParams0.x, fPeak = g_Draw.vParams0.y;
  float3 vLight = Decode(f);
  int nDebug = (int)g_Draw.vParams0.w;
  if (nDebug == 1)
  {
    // The scene's highlights from yellow (a little) to red (the peak), the rest grey, blue where
    // something was drawn over the scene.
    float L = Luminance(vLight);
    float r = Max3(vRatio);
    if (!bScene && vLayer.a >= 0)
      vLight = float3(0.25, 0.4, 1) * (0.1 + L);
    else if (r > 1.001)
      vLight = lerp(float3(1, 0.9, 0.1), float3(1, 0.05, 0), saturate(log2(r) / max(log2(fPeak / fPaper), 0.1)));
    else
      vLight = L;
    vRatio = 1;
  }
  else if (nDebug == 2)
  {
    vLight = TestPatches(vPixel * g_Draw.vParams1.xy, vLight * vRatio, fPeak / fPaper);
    vRatio = 1;
  }
  float3 vNits = min(vLight * vRatio * fPaper, fPeak);
  if (g_Draw.vParams0.z == 1)
    return float4(Pq(mul(REC709_TO_REC2020, vNits)), 1);
  return float4(vNits / 80, 1);
}

SHighlightOutput PSHighlight(SHdrOutput In)
{
  SHighlightOutput Out;
  float3 vRatio;
  Out.vColor = Resolve(In.vPos.xy, vRatio);
  Out.vHighlight = float4(vRatio, HighlightFingerprint(Out.vColor.rgb));
  return Out;
}

float4 PSMain(SHdrOutput In) : SV_Target
{
  if (HasFlag(HDR_OUTPUT))
    return Output(In.vPos.xy);
  if (HasFlag(HDR_LUMINANCE))
    return MeterLuminance(In.vPos.xy);
  if (HasFlag(HDR_REDUCE))
    return Reduce(In.vPos.xy);
  if (HasFlag(HDR_ADAPT))
    return Adapt();
  if (HasFlag(HDR_BLOOM_DOWN))
    return BloomDown(In.vPos.xy);
  if (HasFlag(HDR_BLOOM_UP))
    return BloomUp(In.vPos.xy);
  float3 vRatio;
  return Resolve(In.vPos.xy, vRatio);
}
