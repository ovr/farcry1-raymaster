//////////////////////////////////////////////////////////////////////
//
//	File: Post.hlsl
//	Description: The post program: the screen effects of the main view
//	(PostFx.cpp), drawn over the finished 3D scene.
//
//	A triangle over the whole target, POSITION in clip space. The mode is
//	in nFlags; every mode finds its pixel's uv as SV_Position times
//	vParams0.xy (1 / the target's size).
//	  POST_DOWNSAMPLE  the mean of tex0 over the footprint of the pixel:
//	                   vParams0.z x vParams0.z bilinear taps (1: a copy)
//	  POST_BLUR        a gaussian of tex0, sigma vParams1.z texels (up to
//	                   about 2.5), along vParams1.xy (one texel of tex0 in uv)
//	  POST_EFFECTS     tex0 the copy of the scene, tex1 its glow blur, tex2
//	                   the flash's afterimage, tex3 the screen blur's blur:
//	    POST_NIGHTVISION  the luminance of the scene plus that of its glow,
//	                      with grain, from dark cyan to white
//	    POST_SCREENBLUR   the scene loses sharpness for the luminance of
//	                      tex3 tinted in the colour: vParams1 rgb the colour,
//	                      w the amount
//	    POST_FLASHBANG    scene * vParams2.x + afterimage * vParams2.y +
//	                      vParams2.z (white), plus a spot vParams2.w *
//	                      exp(-8 r^2), r the distance from vParams3.xy (uv)
//	                      times vParams3.zw (1 / its radius in uv)
//	    POST_FADE         times 1 - vParams0.z (to black)
//	                   vParams0.w: the grain's seed (a new one every frame).
//	                   The alpha of the scene stays.
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"

struct SPostOutput
{
  float4 vPos : SV_Position;
};

SPostOutput VSMain(SVertexInput In)
{
  SPostOutput Out;
  Out.vPos = float4(In.vPos.xy, 0, 1);
  return Out;
}

static const float3 LUMINANCE = float3(0.299, 0.587, 0.114);

// Night vision: x = max(sharp * L + glow * L(blur) + bias, 0), colour = saturate(NV_DARK + NV_SLOPE * x):
// cyan in the mid tones, white where every channel saturates; then grey grain (it shows in the whites too).
static const float NV_SHARP = 1.57;
static const float NV_GLOW = 1.27;
static const float NV_BIAS = -0.243;
static const float3 NV_DARK = float3(0.087, 0.224, 0.211);
static const float3 NV_SLOPE = float3(0.778, 0.726, 0.755);
static const float NV_GRAIN = 0.34;           //!< the grain's range
static const float NV_GRAIN_ROWS = 576;       //!< the grain's cells across the screen's height (smooth noise)

// Screen blur, amount a, colour C: scene * max(1 - SHARP_LOSS * a, 0) + a * C * (GAIN * L(blur) + BIAS).
static const float BLUR_SHARP_LOSS = 1.39;
static const float BLUR_TINT_GAIN = 2.89;
static const float BLUR_TINT_BIAS = -0.336;

float4 SampleClamp(uint nTex, float2 vUV)
{
  return g_Textures[nTex].SampleLevel(g_SamplerLinearClamp, vUV, 0);
}

//! A value in 0..1 from a cell and a seed (an integer hash).
float Hash(uint2 vCell, uint nSeed)
{
  uint n = vCell.x * 73856093u ^ vCell.y * 19349663u ^ nSeed * 83492791u;
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n ^= n >> 4;
  n *= 0x27d4eb2du;
  n ^= n >> 15;
  return n * (1.0 / 4294967296.0);
}

//! Smooth noise in 0..1: Hash on the lattice of vPos, blended bilinearly.
float ValueNoise(float2 vPos, uint nSeed)
{
  float2 vCell = floor(vPos);
  float2 f = vPos - vCell;
  uint2 c = (uint2)vCell;
  float n00 = Hash(c, nSeed), n10 = Hash(c + uint2(1, 0), nSeed);
  float n01 = Hash(c + uint2(0, 1), nSeed), n11 = Hash(c + uint2(1, 1), nSeed);
  return lerp(lerp(n00, n10, f.x), lerp(n01, n11, f.x), f.y);
}

float4 Downsample(float2 vPixel)
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

float4 Blur(float2 vPixel)
{
  float2 vUV = vPixel * g_Draw.vParams0.xy;
  float2 vStep = g_Draw.vParams1.xy;
  float fK = -0.5 / (g_Draw.vParams1.z * g_Draw.vParams1.z);
  float4 vSum = SampleClamp(g_Draw.nTex0, vUV);
  float fSum = 1;
  [unroll]
  for (int k = 1; k <= 6; k++)
  {
    float fWeight = exp(fK * k * k);
    vSum += (SampleClamp(g_Draw.nTex0, vUV + vStep * k) + SampleClamp(g_Draw.nTex0, vUV - vStep * k)) * fWeight;
    fSum += 2 * fWeight;
  }
  return vSum / fSum;
}

float4 Effects(float2 vPixel)
{
  float2 vUV = vPixel * g_Draw.vParams0.xy;
  float4 vScene = g_Textures[g_Draw.nTex0].Load(int3(vPixel, 0));
  float3 vBlur = SampleClamp(g_Draw.nTex1, vUV).rgb;
  float3 c = vScene.rgb;
  if (HasFlag(POST_NIGHTVISION))
  {
    float fGrain = ValueNoise(vUV * float2(NV_GRAIN_ROWS * g_Draw.vParams0.y / g_Draw.vParams0.x, NV_GRAIN_ROWS), (uint)g_Draw.vParams0.w) - 0.5;
    float x = max(NV_SHARP * dot(c, LUMINANCE) + NV_GLOW * dot(vBlur, LUMINANCE) + NV_BIAS, 0);
    c = saturate(NV_DARK + NV_SLOPE * x) + NV_GRAIN * fGrain;
  }
  if (HasFlag(POST_SCREENBLUR))
  {
    float fAmount = g_Draw.vParams1.w;
    float fSoft = dot(SampleClamp(g_Draw.nTex3, vUV).rgb, LUMINANCE);
    c = c * max(1 - BLUR_SHARP_LOSS * fAmount, 0) + fAmount * g_Draw.vParams1.rgb * (BLUR_TINT_GAIN * fSoft + BLUR_TINT_BIAS);
  }
  if (HasFlag(POST_FLASHBANG))
  {
    float3 vGhost = SampleClamp(g_Draw.nTex2, vUV).rgb;
    float2 d = (vUV - g_Draw.vParams3.xy) * g_Draw.vParams3.zw;
    c = c * g_Draw.vParams2.x + vGhost * g_Draw.vParams2.y + g_Draw.vParams2.z + g_Draw.vParams2.w * exp(-8 * dot(d, d));
  }
  c = saturate(c);
  if (HasFlag(POST_FADE))
    c *= 1 - g_Draw.vParams0.z;
  return float4(c, vScene.a);
}

float4 PSMain(SPostOutput In) : SV_Target
{
  if (HasFlag(POST_DOWNSAMPLE))
    return Downsample(In.vPos.xy);
  if (HasFlag(POST_BLUR))
    return Blur(In.vPos.xy);
  return Effects(In.vPos.xy);
}
