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
//	  POST_AO_TAKEN    the light the ambient occlusion takes from the scene,
//	                   for a draw that subtracts it (AmbientOcclusion.cpp): the
//	                   ambient light tex2 times 1 - the visibility tex0 to the
//	                   power vParams1.x, 0 where the depth tex1 is the far
//	                   plane; at float16 precision, as AoResolve.hlsl's.
//	                   vParams1.y: the rays' GI (d3d12_RTGI), tex0 the factor
//	                   of the ambient light (rgb): 1 the light taken where
//	                   it is under 1 (for the draw that subtracts it), 2 the
//	                   light given where it is over 1 (for one that adds it)
//	  POST_RM_*        the render modes (r_RenderMode, PostFx.cpp): the
//	                   adapted luminance's target, the glare's source, its
//	                   blur, the scene with its glare, the cartoon
//	                   (RenderMode* below, their colour math in
//	                   RenderMode.hlsli); with the highlight layer the
//	                   composite and the cartoon keep the ratio the layer's
//	                   copy tex4 had for the scene copy's colour
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#include "RenderMode.hlsli"

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

float4 AoTaken(float2 vPixel)
{
  float2 vUV = vPixel * g_Draw.vParams0.xy;
  float fDepth = g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerPointClamp, vUV, 0).r;
  if (g_Draw.vParams1.y != 0)
  {
    const float3 f = fDepth < 1 ? pow(max(g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerPointClamp, vUV, 0).rgb, 0), g_Draw.vParams1.x) : 1;
    const float3 d = g_Draw.vParams1.y < 1.5 ? saturate(1 - f) : max(f - 1, 0);
    // (nothing to blend: the pixel left as it is)
    if (all(d <= 0))
      discard;
    return float4(f16tof32(f32tof16(d * g_Textures[g_Draw.nTex2].SampleLevel(g_SamplerPointClamp, vUV, 0).rgb)), 0);
  }
  float v = fDepth < 1 ? pow(saturate(g_Textures[g_Draw.nTex0].SampleLevel(g_SamplerPointClamp, vUV, 0).r), g_Draw.vParams1.x) : 1;
  return float4(f16tof32(f32tof16((1 - v) * g_Textures[g_Draw.nTex2].SampleLevel(g_SamplerPointClamp, vUV, 0).rgb)), 0);
}

// The render modes (r_RenderMode, PostFx.cpp), as the original's screen process draws them on the frame; their colour
// math in RenderMode.hlsli.

//! The adapted luminance's target (RenderModeAdaptTarget), m the scene's colour at the four spots the original's 2 x 2
//! target takes (the D3D9 pixel centres of its pass over its 256 x 256 copy: the top left corner, the top and the left
//! edges' middles, the middle of the screen), each about 2 of those texels across, clamped to the screen.
float4 RenderModeAdapt()
{
  float3 m = 0;
  [unroll]
  for (int s = 0; s < 4; s++)
  {
    const float2 c = float2(s & 1, s >> 1) * 0.5;
    [unroll]
    for (int y = 0; y < 4; y++)
    {
      [unroll]
      for (int x = 0; x < 4; x++)
        m += SampleClamp(g_Draw.nTex0, c + ((float2(x, y) + 0.5) / 4 - 0.5) * (2.0 / 256)).rgb;
    }
  }
  return float4((float3)RenderModeAdaptTarget(m / 64), g_Draw.vParams1.x);
}

//! The glare's source at 128 x 128 (RenderModeGlareSource) from the 256 x 256 copy tex0 (bilinear: the mean of its
//! 2 x 2 texels) and the adapted luminance tex1 (1 x 1); vParams1.xy the threshold and the gain.
float4 RenderModeBright(float2 vPixel)
{
  const float3 d = SampleClamp(g_Draw.nTex0, vPixel * g_Draw.vParams0.xy).rgb;
  const float3 a = saturate(g_Textures[g_Draw.nTex1].Load(int3(0, 0, 0)).rgb);
  return float4(RenderModeGlareSource(d, a, g_Draw.vParams1.x, g_Draw.vParams1.y), a.b);
}

//! One of the glare's blur passes: 4 bilinear taps at (1, 2), (-2, 1), (-1, -2), (2, -1) times vParams1.x texels.
float4 RenderModeBlur(float2 vPixel)
{
  const float2 vUV = vPixel * g_Draw.vParams0.xy, s = g_Draw.vParams0.xy * g_Draw.vParams1.x;
  return 0.25 * (SampleClamp(g_Draw.nTex0, vUV + s * float2(1, 2)) + SampleClamp(g_Draw.nTex0, vUV + s * float2(-2, 1)) +
                 SampleClamp(g_Draw.nTex0, vUV + s * float2(-1, -2)) + SampleClamp(g_Draw.nTex0, vUV + s * float2(2, -1)));
}

//! The scene tex0 with its glare tex1 (RenderModeGlared), towards its luminance by vParams1.x. Cold (vParams1.y):
//! the scene first through RenderModeCold with the 256 x 256 copy tex2 (the original's back buffer).
float4 RenderModeComposite(float2 vPixel)
{
  const float2 vUV = vPixel * g_Draw.vParams0.xy;
  float3 f = g_Textures[g_Draw.nTex0].Load(int3(vPixel, 0)).rgb;
  if (g_Draw.vParams1.y > 0)
    f = RenderModeCold(f, SampleClamp(g_Draw.nTex2, vUV).rgb);
  return float4(RenderModeGlared(f, SampleClamp(g_Draw.nTex1, vUV).rgb, g_Draw.vParams1.x), 1);
}

//! The cartoon: its colours (RenderModeCartoonColor) of the pixel times RenderModeEdge of those above, below, left and
//! right of it (clamped to the screen), as the original's: dark where the colour falls downwards or to the right.
float4 RenderModeCartoon(float2 vPixel)
{
  const int2 p = (int2)vPixel, vLast = (int2)round(1 / g_Draw.vParams0.xy) - 1;
  const float3 c = RenderModeCartoonColor(g_Textures[g_Draw.nTex0].Load(int3(p, 0)).rgb);
  const float3 u = RenderModeCartoonColor(g_Textures[g_Draw.nTex0].Load(int3(clamp(p + int2(0, -1), 0, vLast), 0)).rgb);
  const float3 d = RenderModeCartoonColor(g_Textures[g_Draw.nTex0].Load(int3(clamp(p + int2(0, 1), 0, vLast), 0)).rgb);
  const float3 l = RenderModeCartoonColor(g_Textures[g_Draw.nTex0].Load(int3(clamp(p + int2(-1, 0), 0, vLast), 0)).rgb);
  const float3 r = RenderModeCartoonColor(g_Textures[g_Draw.nTex0].Load(int3(clamp(p + int2(1, 0), 0, vLast), 0)).rgb);
  return float4(c * RenderModeEdge(u, d, l, r), 1);
}

float4 PSMain(SPostOutput In) : SV_Target
{
  if (HasFlag(POST_RM_ADAPT))
    return RenderModeAdapt();
  if (HasFlag(POST_RM_BRIGHT))
    return RenderModeBright(In.vPos.xy);
  if (HasFlag(POST_RM_BLUR))
    return RenderModeBlur(In.vPos.xy);
  if (HasFlag(POST_RM_COMPOSITE))
    return RenderModeComposite(In.vPos.xy);
  if (HasFlag(POST_RM_CARTOON))
    return RenderModeCartoon(In.vPos.xy);
  if (HasFlag(POST_AO_TAKEN))
    return AoTaken(In.vPos.xy);
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

//! The upscaler's picture over the back buffer (POST_BLEND, Upscale.cpp) writing the highlight
//! layer too (Common.hlsli, HdrOutput.cpp): tex1 the layer's ratio of the light at the scene's
//! size, bilinear, but 1 in the channels at or below the tone map's knee vParams2.x, where the
//! resolve's ratio is 1 (the ratio of a highlight does not spill over the dark pixels next to it).
SHighlightOutput PSHighlight(SPostOutput In)
{
  SHighlightOutput Out;
  Out.vColor = PSMain(In);
  float3 vRatio;
  if (HasFlag(POST_RM_COMPOSITE | POST_RM_CARTOON))
  {
    // A render mode over the scene: the ratio the layer's copy tex4 had where it had the scene copy's colour.
    bool bScene;
    vRatio = LayerRatio(g_Textures[g_Draw.nTex0].Load(int3(In.vPos.xy, 0)).rgb, g_Textures[g_Draw.nTex4].Load(int3(In.vPos.xy, 0)), bScene);
  }
  else
  {
    vRatio = g_Textures[g_Draw.nTex1].SampleLevel(g_SamplerLinearClamp, In.vPos.xy * g_Draw.vParams0.xy, 0).rgb;
    vRatio = lerp(float3(1, 1, 1), max(vRatio, 1), step(g_Draw.vParams2.x, Out.vColor.rgb));
  }
  Out.vHighlight = float4(vRatio, HighlightFingerprint(Out.vColor.rgb));
  return Out;
}
