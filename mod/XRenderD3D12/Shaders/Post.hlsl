// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
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
//	  POST_BLEND       tex0 (point sampled) times vParams1, for a draw blended
//	                   over the target (motion blur, fade)
//	  POST_EFFECTS     tex0 the copy of the scene, tex1 its glow blur, tex2
//	                   the flash's afterimage, tex3 the screen blur's blur:
//	    POST_NIGHTVISION  the luminance of the scene plus that of its glow,
//	                      with grain, from dark cyan to white
//	    POST_SCREENBLUR   the scene towards the square of twice the colour
//	                      times the luminance of tex3 (C², not C: a strong
//	                      colour wins over a weak one), by the amount, clamped
//	                      only after that: vParams1 rgb the colour, w the amount
//	    POST_FLASHBANG    scene * vParams2.x + afterimage * vParams2.y +
//	                      vParams2.z (white), plus a spot vParams2.w *
//	                      exp(-8 r^2), r the distance from vParams3.xy (uv)
//	                      times vParams3.zw (1 / its radius in uv)
//	    POST_FADE         times 1 - vParams0.z (to black)
//	                   vParams0.w: the grain's seed (a new one every frame).
//	                   The alpha of the scene stays.
//	  POST_SUN_PATCH   the sky around the sun (Flares.cpp): 1 where the depth
//	                   copy tex0 is the far plane, else 0, at the pixel
//	                   vParams1.xy + (texel - the middle) * vParams1.z (clamped
//	                   to the screen, vParams1.w its width, vParams2.x height)
//	  POST_SUN_MASK    the sun's mask: SUN_MASK_BASE plus, for each of
//	                   SUN_MASK_STEPS squares of the patch tex0 smaller and
//	                   smaller around the middle, SUN_MASK_STEP times the patch
//	                   where the square covers the texel; in every channel
//	  POST_DEPTH_VIEW  the depth tex0 (0 near to 1 far) as grey: the distance
//	                   in log scale, black at vParams1.x (near), white at
//	                   vParams1.y (far) (d3d12_UpscaleDebug 2)
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
    float3 vTint = 2 * g_Draw.vParams1.rgb * dot(SampleClamp(g_Draw.nTex3, vUV).rgb, LUMINANCE);
    c = lerp(c, vTint * vTint, g_Draw.vParams1.w);
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

// The sun's mask: SUN_MASK_SIZE texels, the patch SUN_PATCH_SIZE; the first square is the patch
// SUN_MASK_SIZE * 2 across, each next one SUN_MASK_SHRINK texels less to each side.
static const float SUN_PATCH_SIZE = 64;
static const float SUN_MASK_SIZE = 128;
static const float SUN_MASK_BASE = 0.2;
static const int SUN_MASK_STEPS = 32;
static const float SUN_MASK_STEP = 7.0 / 255;
static const float SUN_MASK_SHRINK = 43.0 / 12;

float4 SunPatch(float2 vPixel)
{
  float2 vScreen = g_Draw.vParams1.xy + (vPixel - SUN_PATCH_SIZE / 2) * g_Draw.vParams1.z;
  int2 vLoad = clamp((int2)floor(vScreen), 0, int2(g_Draw.vParams1.w, g_Draw.vParams2.x) - 1);
  return g_Textures[g_Draw.nTex0].Load(int3(vLoad, 0)).r >= 1 ? 1 : 0;
}

float4 SunMask(float2 vPixel)
{
  float2 d = vPixel - SUN_MASK_SIZE / 2;
  float fFar = max(abs(d.x), abs(d.y));
  float m = SUN_MASK_BASE;
  for (int k = 0; k < SUN_MASK_STEPS; k++)
  {
    float s = SUN_MASK_SIZE - k * SUN_MASK_SHRINK;
    if (fFar < s)
      m += SUN_MASK_STEP * SampleClamp(g_Draw.nTex0, 0.5 + d / (2 * s)).a;
  }
  return saturate(m);
}

float4 DepthView(float2 vPixel)
{
  float fNear = g_Draw.vParams1.x, fFar = g_Draw.vParams1.y;
  float d = g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerPointClamp, vPixel * g_Draw.vParams0.xy, 0).r;
  float fDist = fNear * fFar / max(fFar - d * (fFar - fNear), 1e-6);
  return float4((float3)saturate(log2(fDist / fNear) / log2(fFar / fNear)), 1);
}

float4 PSMain(SPostOutput In) : SV_Target
{
  if (HasFlag(POST_DEPTH_VIEW))
    return DepthView(In.vPos.xy);
  if (HasFlag(POST_SUN_PATCH))
    return SunPatch(In.vPos.xy);
  if (HasFlag(POST_SUN_MASK))
    return SunMask(In.vPos.xy);
  if (HasFlag(POST_DOWNSAMPLE))
    return Downsample(In.vPos.xy);
  if (HasFlag(POST_BLUR))
    return Blur(In.vPos.xy);
  if (HasFlag(POST_BLEND))
    return g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerPointClamp, In.vPos.xy * g_Draw.vParams0.xy, 0) * g_Draw.vParams1;
  return Effects(In.vPos.xy);
}
