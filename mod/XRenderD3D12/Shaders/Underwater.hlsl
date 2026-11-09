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
//	With d3d12_WaterUnderShare the sun's light comes from a pass before it
//	(PSTaps, eD3D12P_UnderwaterTaps, into an R32_FLOAT target): one point of
//	each pixel's ray, in the segment of its place modulo 4, the 4 x 4 window
//	round each pixel taking the 16 as its ray's segments; the segments'
//	weights in closed form (UnderInscatterClosed; the view's pass compiled
//	with XR_UNDER_SHARE, eD3D12P_UnderwaterShare). Without it (the program
//	before it) each pixel takes four of its own ray, its 2 x 2 quad's
//	sixteen.
//	tex0 the scene's colour copy, tex2 its depth copy, tex1 the caustics
//	(OceanFFT.cpp's SetOceanCaustics, an array; vParams0.w 0: none), tex3
//	the taps pass's target; vParams3.w 0 the quad's taps, 1 the closed form
//	without the sun's share (no taps), 2 the closed form and tex3.
//	vColor rgb the ocean's fog volume's colour (the game's), w its density;
//	vParams0 x the level, y how far over it the surface may be (the
//	grid's crests: where the copy has no surface), z d3d12_WaterDebug;
//	vParams1 x the sun's share of the light the water scatters, y 1 its
//	light from the scene's sun and sky (d3d12_WaterUnderClear: vUnderColor
//	rgb, UnderwaterLight.h's on the CPU), z the depth
//	the caustics are gone by, w the most they brighten; vParams2.xy each
//	caustics cascade's 1 / tile, vParams3.xy its first depth; PSTaps:
//	vParams2.zw the main view's jitter, vParams3.z its noise's frame (an off
//	screen target's frame constants have neither); vLightPos[2..4] xyz the
//	view's rays (GetRayView), vLightPos[5].x the near plane's view depth.
//	The waterline (d3d12_WaterUnderLine): a camera over the level whose
//	near plane dips under it draws the pass too; a pixel whose ray goes
//	under the level before the near plane (the surface clipped there) takes
//	its path through the water from there (UnderLineStart), the others are
//	left as they are (the surface drawn, seen from above).
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

//! The light the water of colour vDeep and attenuation c scatters a metre at the surface: with d3d12_WaterUnderClear
//! (vParams1.y) from the scene's sun (its colour twice, a white face turned to it) and sky (the fog's colour) coming down
//! through the surface, the same for every pixel (Underwater.hlsli's UnderClearSource of UnderDownLight, worked out once a
//! frame on the CPU: vUnderColor rgb), else as the water seen from above shows it (UnderDeepSource).
float3 UnderSource(float3 vDeep, float3 c)
{
  if (g_DrawExt.vParams1.y > 0)
    return g_DrawExt.vUnderColor.rgb;
  return UnderDeepSource(vDeep, c);
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

//! The view's ray (a unit vector) of the pixel at vNDC, the jitter vJitter (the upscaler's, which moved the scene) taken off,
//! and fRayLength, the length of the ray of a unit view depth.
float3 UnderRay(float2 vNDC, float2 vJitter, out float fRayLength)
{
  const float2 v = vNDC - vJitter;
  const float3 vRay = g_DrawExt.vLightPos[4].xyz + v.x * g_DrawExt.vLightPos[2].xyz + v.y * g_DrawExt.vLightPos[3].xyz;
  fRayLength = length(vRay);
  return vRay / fRayLength;
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
  if (g_DrawExt.vParams0.w > 0 && v > 0 && fDepth < g_DrawExt.vParams1.z)
  {
    const Texture2DArray Caustics = g_TextureArrays[g_Draw.nTex1];
    const float fSize = max(fFootprint, fDepth * CAUSTICS_SUN_ANGLE);
    float fLight = 1;
    [unroll]
    for (uint i = 0; i < XR_CAUSTICS_CASCADES; i++)
    {
      const float fTexels = fSize * g_DrawExt.vParams2[i] * XR_OCEAN_SIZE;
      const float4 c = Caustics.SampleLevel(g_SamplerLinearWrap, float3(E.xy * g_DrawExt.vParams2[i], i), log2(max(fTexels, 1)));
      fLight *= lerp(1, CausticsAtDepth(c, g_DrawExt.vParams3[i], fDepth), CausticsFar(fTexels));
    }
    v *= CausticsFactor(fLight, saturate(1 - fDepth / g_DrawExt.vParams1.z), g_DrawExt.vParams1.w);
  }
  return v;
}

//! The taps pass (d3d12_WaterUnderShare): the sun's light (UnderSun) at one point of the pixel's view ray, in the segment
//! UnderBlockSegment of the UnderSegmentAt segments, moved by the pixel's noise over the frames (the temporal anti-aliasing's);
//! 1, the mean, where the pixel takes none (no sun, the scene at the camera).
float4 PSTaps(SUnderOutput In) : SV_Target
{
  const int3 vAt = int3(In.vPos.xy, 0);
  float fRayLength;
  const float3 D = UnderRay(In.vNDC, g_DrawExt.vParams2.zw, fRayLength);
  const float fPixelAngle = max(length(ddx(D)), length(ddy(D)));

  // The path through the water from the camera, or with the waterline from where the ray goes under the level.
  const float3 vEye = g_Frame.vCameraPos.xyz;
  const float fLevel = g_DrawExt.vParams0.x;
  const float fLine = UnderLineStart(vEye.z - fLevel, D.z, g_DrawExt.vLightPos[5].x * fRayLength);
  const float3 vCamera = vEye + D * max(fLine, 0);
  const float fCameraDepth = max(fLevel - vCamera.z, 0);
  const float fDistance = UnderViewDepth(g_Textures[g_Draw.nTex2].Load(vAt).r) * fRayLength - max(fLine, 0);
  const float fPath = UnderPath(fDistance, D.z, fCameraDepth, g_DrawExt.vParams0.y);
  const float3 c = VolumeAttenuation(UnderDecode(g_Draw.vColor.rgb), g_Draw.vColor.w);
  const float k = c.g * (1 - D.z);
  const float fReach = min(fPath, VOLUME_REACH / max(k, 1e-4));
  if (!(fLine >= 0 && g_DrawExt.vParams1.x > 0 && fReach > 0.01))
    return 1;
  const float fNoise = PictureNoise(In.vPos.xy, g_DrawExt.vParams3.z);
  const float t = UnderSegmentAt((UnderBlockSegment(In.vPos.xy) + fNoise) / XR_UNDER_TAPS, k, fReach);
  const float3 x = vCamera + D * t;
  return UnderSun(x, max(fLevel - x.z, 0), (fLine + t) * fPixelAngle);
}

//! The sun's light at the XR_UNDER_TAPS segments of the pixel's ray from the taps pass (tex3): the pixels' of the 4 x 4 window
//! from one left of it and one up (moved in at the view's edges), which holds each segment (UnderBlockSegment) once, in that
//! order (UnderWindowOrder); four gathers of its 2 x 2 quarters. A window around each pixel in place of its block's: the
//! blocks' sixteen apart from their neighbours' were squares in the shafts' noise.
void UnderBlockTaps(float2 vPixel, out float v[XR_UNDER_TAPS])
{
  const Texture2D Taps = g_Textures[g_Draw.nTex3];
  uint w, h;
  Taps.GetDimensions(w, h);
  const uint2 vStart = (uint2)clamp(int2(vPixel) - 1, 0, int2(w, h) - 4);
  float4 G[4];
  [unroll]
  for (uint s = 0; s < 4; s++)
  {
    // The corner shared by the quarter's four texels: Gather's w at its top left, z top right, x bottom left, y bottom right.
    const uint2 q = uint2(s % 2, s / 2);
    const float4 g = Taps.GatherRed(g_SamplerPointClamp, (float2(vStart + 2 * q) + 1) / float2(w, h));
    if (q.x == 0)
    {
      G[2 * q.y] = float4(g.w, g.z, 0, 0);
      G[2 * q.y + 1] = float4(g.x, g.y, 0, 0);
    }
    else
    {
      G[2 * q.y].zw = float2(g.w, g.z);
      G[2 * q.y + 1].zw = float2(g.x, g.y);
    }
  }
  UnderWindowOrder(G, vStart, v);
}

#ifdef XR_UNDER_SHARE
//! The view's pass with d3d12_WaterUnderShare (eD3D12P_UnderwaterShare): the sun's light at the ray's segments the taps
//! pass's (vParams3.w 2), else 1 (no sun's share), the segments' weights in closed form (UnderInscatterClosed).
float4 PSMain(SUnderOutput In) : SV_Target
{
  const int3 vAt = int3(In.vPos.xy, 0);
  // The pixel's view ray (the upscaler's jitter, which moved the scene, taken off).
  float fRayLength;
  const float3 D = UnderRay(In.vNDC, g_Frame.vJitter.xy, fRayLength);

  // The path through the water from the camera, or with the waterline from where the ray goes under the level; the pixels
  // whose surface is drawn (seen from above) left as they are.
  const float3 vEye = g_Frame.vCameraPos.xyz;
  const float fLevel = g_DrawExt.vParams0.x;
  const float fLine = UnderLineStart(vEye.z - fLevel, D.z, g_DrawExt.vLightPos[5].x * fRayLength);
  if (fLine < 0)
    discard;
  const float3 vCamera = vEye + D * fLine;
  const float fCameraDepth = max(fLevel - vCamera.z, 0);
  const float fDistance = UnderViewDepth(g_Textures[g_Draw.nTex2].Load(vAt).r) * fRayLength - fLine;
  const float fPath = UnderPath(fDistance, D.z, fCameraDepth, g_DrawExt.vParams0.y);
  const float3 P = vCamera + D * fDistance;
  const float3 L = g_Frame.vSunDir.xyz;

  const float3 vDeep = UnderDecode(g_Draw.vColor.rgb);
  const float3 c = VolumeAttenuation(vDeep, g_Draw.vColor.w);
  const float fMu = -D.z;
  const float3 k = c * (1 + fMu);
  const float fReach = min(fPath, VOLUME_REACH / max(k.g, 1e-4));

  const float fShare = g_DrawExt.vParams1.x;
  const bool bTaps = fShare > 0 && fReach > 0.01;
  float vSun[XR_UNDER_TAPS];
  [branch]
  if (g_DrawExt.vParams3.w > 1)
    UnderBlockTaps(In.vPos.xy, vSun);
  else
  {
    [unroll]
    for (uint i = 0; i < XR_UNDER_TAPS; i++)
      vSun[i] = 1;
  }

  // Scattered forwards most: towards the camera from the sun's refracted rays.
  const float fPhase = UnderSunPhase(L, D);
  float fLit;
  const float3 vInscatter = UnderInscatterClosed(UnderSource(vDeep, c), c, fCameraDepth, fMu, fPath, fReach, bTaps ? fShare : 0, fPhase,
                                                 vSun, fLit);
  const float fDepth = max(fLevel - P.z, 0);
  const float3 T = exp(-c * (fPath + fDepth / VolumeRefractedCos(saturate(L.z))));
  if (g_DrawExt.vParams0.z == 31)
    return float4(UnderEncode(vInscatter), 1);
  if (g_DrawExt.vParams0.z == 32)
    return float4(saturate(fLit * 0.5).xxx, 1);
  if (g_DrawExt.vParams0.z == 33)
    return float4(T, 1);
  const float3 vScene = UnderDecode(g_Textures[g_Draw.nTex0].Load(vAt).rgb);
  return SceneOutput(float4(UnderEncode(vScene * T + vInscatter), 1));
}
#else
//! The view's pass without it (eD3D12P_Underwater; the program before d3d12_WaterUnderShare, instruction for instruction, but
//! for the light the water scatters, UnderSource's).
float4 PSMain(SUnderOutput In) : SV_Target
{
  const int3 vAt = int3(In.vPos.xy, 0);
  // The pixel's view ray (the upscaler's jitter, which moved the scene, taken off) and the angle between neighbouring
  // pixels' rays (before any branch).
  const float2 v = In.vNDC - g_Frame.vJitter.xy;
  const float3 vRay = g_DrawExt.vLightPos[4].xyz + v.x * g_DrawExt.vLightPos[2].xyz + v.y * g_DrawExt.vLightPos[3].xyz;
  const float fRayLength = length(vRay);
  const float3 D = vRay / fRayLength;
  const float fPixelAngle = max(length(ddx(D)), length(ddy(D)));

  // The path through the water from the camera, or with the waterline from where the ray goes under the level (the pixels
  // whose surface is drawn discarded at the end: the quad's taps are shared).
  const float3 vEye = g_Frame.vCameraPos.xyz;
  const float fLevel = g_DrawExt.vParams0.x;
  const float fLine = UnderLineStart(vEye.z - fLevel, D.z, g_DrawExt.vLightPos[5].x * fRayLength);
  const float3 vCamera = vEye + D * max(fLine, 0);
  const float fCameraDepth = max(fLevel - vCamera.z, 0);
  const float fDistance = UnderViewDepth(g_Textures[g_Draw.nTex2].Load(vAt).r) * fRayLength - max(fLine, 0);
  const float fPath = UnderPath(fDistance, D.z, fCameraDepth, g_DrawExt.vParams0.y);
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
  const float fShare = g_DrawExt.vParams1.x;
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
      vTaps[i] = UnderSun(x, max(fLevel - x.z, 0), (max(fLine, 0) + t) * fPixelAngle);
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
  const float3 vInscatter = UnderInscatter(UnderSource(vDeep, c), c, fCameraDepth, fMu, fPath, fReach, bTaps ? fShare : 0, fPhase, vSun,
                                           fLit);
  if (fLine < 0)
    discard;
  const float fDepth = max(fLevel - P.z, 0);
  const float3 T = exp(-c * (fPath + fDepth / VolumeRefractedCos(saturate(L.z))));
  if (g_DrawExt.vParams0.z == 31)
    return float4(UnderEncode(vInscatter), 1);
  if (g_DrawExt.vParams0.z == 32)
    return float4(saturate(fLit * 0.5).xxx, 1);
  if (g_DrawExt.vParams0.z == 33)
    return float4(T, 1);
  const float3 vScene = UnderDecode(g_Textures[g_Draw.nTex0].Load(vAt).rgb);
  return SceneOutput(float4(UnderEncode(vScene * T + vInscatter), 1));
}
#endif
