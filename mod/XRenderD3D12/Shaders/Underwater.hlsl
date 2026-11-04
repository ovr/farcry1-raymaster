// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Underwater.hlsl
//	Description: The view from under the ocean's surface (eD3D12P_Underwater,
//	d3d12_WaterUnder, Underwater.cpp): a triangle over the view after the
//	water surfaces of the blended list, over a copy of the scene as drawn up
//	to them (the water from below unfogged: Water.hlsl's UnderSurface). Each
//	pixel's scene, at its distance along the view's ray from the depth copy
//	(through the water at most up to the surface: the level, or the crests
//	of the ocean's grid where its depth is not in the copy), attenuated in
//	each channel along that path and along the light's way down to the
//	scene's depth (WaterVolume.hlsli's attenuation of the fog volume's
//	colour), and the light the water scatters towards the camera on the way
//	(Underwater.hlsli's UnderInscatter): the light coming down fading with
//	the depth, the sun's share of it in the sun's shadow and the FFT
//	ocean's caustics at XR_UNDER_TAPS points of the ray (the shafts of
//	light under the waves), scattered forwards most (sea water's phase
//	function: the glow round the sun).
//	tex0 the scene's colour copy, tex2 its depth copy, tex1 the caustics
//	(OceanFFT.cpp's SetOceanCaustics, an array; vParams0.w 0: none).
//	vColor rgb the ocean's fog volume's colour (the game's), w its density;
//	vParams0 x the level, y how far over it the surface may be (the
//	grid's crests: where the copy has no surface), z d3d12_WaterDebug;
//	vParams1 x the sun's share of the light the water scatters, z the depth
//	the caustics are gone by, w the most they brighten; vParams2.xy each
//	caustics cascade's 1 / tile, vParams3.xy its first depth;
//	vLightPos[2..4] xyz the view's rays (GetRayView).
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#include "WaterCaustics.hlsli"
#include "Underwater.hlsli"

struct SUnderOutput
{
  float4 vPos : SV_Position;
  float2 vNDC : TEXCOORD0;      //!< the view's x and y (-1..1, y up)
};

SUnderOutput VSMain(SVertexInput In)
{
  SUnderOutput Out;
  Out.vPos = float4(In.vPos.xy, 0, 1);
  Out.vNDC = In.vPos.xy;
  return Out;
}

float3 UnderDecode(float3 c)
{
  return float3(HdrDecode(c.r), HdrDecode(c.g), HdrDecode(c.b));
}

float3 UnderEncode(float3 v)
{
  return float3(HdrEncode(v.r), HdrEncode(v.g), HdrEncode(v.b));
}

//! The view depth of the depth buffer's value z.
float UnderViewDepth(float z)
{
  return g_Frame.vDepthParams.y / (z + g_Frame.vDepthParams.x);
}

bool UnderQuadAny(bool b)
{
  const uint u = b ? 1u : 0u;
  return (u | QuadReadAcrossX(u) | QuadReadAcrossY(u) | QuadReadAcrossDiagonal(u)) != 0;
}

//! The sun's light (1 its mean) at the point x of the view's ray fDepth metres under the level, a pixel's fFootprint metres
//! across there: in the sun's shadow where its ray refracted to x left the surface (one bilinear comparison of the cascades,
//! 1 beyond them; as WaterVolume's), times the FFT ocean's caustics there at x's depth (each cascade's mip level from the
//! footprint and the sun's disc's blur at that depth, its mean where that is finer than its last level's texel; fading to the
//! mean by the depth vParams1.z).
float UnderSun(float3 x, float fDepth, float fFootprint)
{
  const float3 E = VolumeSunEntry(x, fDepth, g_Frame.vSunDir.xyz);
  float v = CascadeTap(E);
  [branch]
  if (g_Draw.vParams0.w > 0 && v > 0 && fDepth < g_Draw.vParams1.z)
  {
    const Texture2DArray Caustics = g_TextureArrays[g_Draw.nTex1];
    const float fSize = max(fFootprint, fDepth * CAUSTICS_SUN_ANGLE);
    float fLight = 1;
    [unroll]
    for (uint i = 0; i < XR_CAUSTICS_CASCADES; i++)
    {
      const float fTexels = fSize * g_Draw.vParams2[i] * XR_OCEAN_SIZE;
      const float4 c = Caustics.SampleLevel(g_SamplerLinearWrap, float3(E.xy * g_Draw.vParams2[i], i), log2(max(fTexels, 1)));
      fLight *= lerp(1, CausticsAtDepth(c, g_Draw.vParams3[i], fDepth), CausticsFar(fTexels));
    }
    v *= CausticsFactor(fLight, saturate(1 - fDepth / g_Draw.vParams1.z), g_Draw.vParams1.w);
  }
  return v;
}

float4 PSMain(SUnderOutput In) : SV_Target
{
  const int3 vAt = int3(In.vPos.xy, 0);
  // The pixel's view ray (the upscaler's jitter, which moved the scene, taken off) and the angle between neighbouring
  // pixels' rays (before any branch).
  const float2 v = In.vNDC - g_Frame.vJitter.xy;
  const float3 vRay = g_Draw.vLightPos[4].xyz + v.x * g_Draw.vLightPos[2].xyz + v.y * g_Draw.vLightPos[3].xyz;
  const float fRayLength = length(vRay);
  const float3 D = vRay / fRayLength;
  const float fPixelAngle = max(length(ddx(D)), length(ddy(D)));

  const float3 vCamera = g_Frame.vCameraPos.xyz;
  const float fLevel = g_Draw.vParams0.x;
  const float fCameraDepth = max(fLevel - vCamera.z, 0);
  const float fDistance = UnderViewDepth(g_Textures[g_Draw.nTex2].Load(vAt).r) * fRayLength;
  const float fPath = UnderPath(fDistance, D.z, fCameraDepth, g_Draw.vParams0.y);
  const float3 P = vCamera + D * fDistance;
  const float3 L = g_Frame.vSunDir.xyz;

  const float3 vDeep = UnderDecode(g_Draw.vColor.rgb);
  const float3 c = VolumeAttenuation(vDeep, g_Draw.vColor.w);
  const float fMu = -D.z;
  const float3 k = c * (1 + fMu);
  const float fReach = min(fPath, VOLUME_REACH / max(k.g, 1e-4));

  // The sun's light at the ray's segments, XR_UNDER_TAPS / 4 a pixel, the quad's shared: lane q (its place in the 2 x 2
  // pixels) the segments 4 i + q of its own ray, at a point of each moved by the pixel's noise over the frames (the
  // temporal anti-aliasing's) and by the golden ratio from one to the next; where any of the quad's pixels needs them.
  const float fShare = g_Draw.vParams1.x;
  const bool bTaps = fShare > 0 && fReach > 0.01;
  const uint q = (uint)In.vPos.x % 2 + 2 * ((uint)In.vPos.y % 2);
  float4 vTaps = 1;
  if (UnderQuadAny(bTaps))
  {
    const float fNoise = PictureNoise(In.vPos.xy, g_Frame.vTime.y);
    [unroll]
    for (uint i = 0; i < XR_UNDER_TAPS / 4; i++)
    {
      const uint j = 4 * i + q;
      const float t = UnderSegmentAt((j + frac(fNoise + 0.618034 * i)) / XR_UNDER_TAPS, k.g, fReach);
      const float3 x = vCamera + D * t;
      vTaps[i] = UnderSun(x, max(fLevel - x.z, 0), t * fPixelAngle);
    }
  }
  float vSun[XR_UNDER_TAPS];
  [unroll]
  for (uint i = 0; i < XR_UNDER_TAPS / 4; i++)
  {
    [unroll]
    for (uint l = 0; l < 4; l++)
      vSun[4 * i + l] = QuadReadLaneAt(vTaps[i], l);
  }

  // Scattered forwards most: towards the camera from the sun's refracted rays.
  const float fPhase = UnderSunPhase(L, D);
  float fLit;
  const float3 vInscatter = UnderInscatter(vDeep, c, fCameraDepth, fMu, fPath, fReach, bTaps ? fShare : 0, fPhase, vSun, fLit);
  const float fDepth = max(fLevel - P.z, 0);
  const float3 T = exp(-c * (fPath + fDepth / VolumeRefractedCos(saturate(L.z))));
  if (g_Draw.vParams0.z == 31)
    return float4(UnderEncode(vInscatter), 1);
  if (g_Draw.vParams0.z == 32)
    return float4(saturate(fLit * 0.5).xxx, 1);
  if (g_Draw.vParams0.z == 33)
    return float4(T, 1);
  const float3 vScene = UnderDecode(g_Textures[g_Draw.nTex0].Load(vAt).rgb);
  return SceneOutput(float4(UnderEncode(vScene * T + vInscatter), 1));
}
