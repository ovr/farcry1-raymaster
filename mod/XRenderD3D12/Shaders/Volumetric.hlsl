// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Volumetric.hlsl
//	Description: The volumetric light's kernels (Volumetric.cpp): the
//	froxels of the view (SVolumetricConstants) and the light they scatter
//	towards the camera.
//
//	CSInject, a thread a froxel: at this frame's point in it (vJitter, its
//	depth also moved by a noise of the column) the extinction of the air
//	(vDensity: even up to a height, falling off above it) and the light it
//	scatters towards the camera: the sun's (vSunLight) times its phase
//	(Henyey-Greenstein, 1 for all directions alike) and its visibility from
//	the sun's cascades (one bilinear comparison in the cascade of the
//	froxel's depth; lit beyond them), and the light from all round
//	(vAmbientLight; vAmbientLight.w of it where the sun is hidden). With
//	the shafts (vShafts.x, the radius R) the sun's light only times the
//	part of the air around that is in shadow (to the power vShafts.y: a
//	narrow shaft all, the edge of a wide shadow or the dappled light under
//	the leaves little): SHAFT_TAPS points of a disc
//	of radius R across the sun's direction (turned from frame to frame and
//	froxel to froxel), each point sampled in the cascade's depth: the sun
//	reaches it, it is in a shadow cast from more than SHAFT_SURFACE m
//	nearer the sun, or it is under a surface nearer than that (the ground,
//	inside a thing: not air, left out). Open air (all lit) scatters
//	nothing, a shaft through the leaves or a window into the shade all;
//	no light from all round. Blended with the history (the froxels of the
//	frame before at the froxel's centre, vDensity.w of it) against the
//	noise of the points.
//	  t0 the history (Texture3D, RGBA16F), t1 the full set of the cascades
//	  (Texture2DArray R32_FLOAT); u0 the froxels: rgb the light scattered
//	  (per m), a the extinction (1 / m)
//	With the sun by rays (XR_VOL_RAYS, d3d12_VolumetricLightRays) the sun's
//	visibility and the air around come from the sun's states at the froxels'
//	points that VolumetricRays.hlsl's rays found this frame (t1, Texture3D
//	R8_SNORM, VolSunState): the froxel's own (lit or not), and for each point
//	of the disc the state of the froxel it is in (out of the volume left
//	out), in place of the cascades'.
//
//	CSIntegrate, a thread a column of froxels, near to far: the light
//	scattered towards the camera from it to each froxel's far end and the
//	part of the light behind that reaches the camera (the transmittance),
//	each froxel's scattering integrated over its length (energy conserving:
//	S (1 - T) / extinction, Frostbite's).
//	  t0 the froxels; u0 the integrated froxels: rgb the light, a the
//	  transmittance (1 with the shafts: the light is added, the game's fog
//	  is the air's haze)
//	The HDR resolve (Hdr.hlsl, HDR_VOLUMETRIC) takes the integrated froxels
//	at each pixel's depth: light * a + rgb.
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"
#include "Volumetric.hlsli"

cbuffer cbVolumetric : register(b0)
{
  SVolumetricConstants g_Vol;
};

Texture3D<float4> g_Input : register(t0);  // the history (CSInject), the froxels (CSIntegrate)
#ifdef XR_VOL_RAYS
Texture3D<float> g_SunStates : register(t1);  // the sun's state at each froxel's point (VolumetricRays.hlsl)
#else
Texture2DArray<float> g_Cascades : register(t1);
#endif
RWTexture3D<float4> g_Out : register(u0);

SamplerState g_SamplerLinearClamp : register(s1);
SamplerComparisonState g_SamplerShadow : register(s2);

//! The view depth at w (0 the near end, 1 the far end of the volume): Volumetric.hlsli's VolSliceDepth (written out as
//! before it: FroxelPos).
float SliceDepth(float w)
{
  return g_Vol.vSlices.x * exp2(w * g_Vol.vSlices.y);
}

//! The world position at x, y (0..1 across and down the view) and the view depth d. Volumetric.hlsli's VolFroxelPos,
//! written out here as it was before it: the compiler's fast math orders the sum otherwise when it gets the constants as
//! a function's arguments, and the points (so the frames without the rays) would move by a last bit.
float3 FroxelPos(float2 vXY, float d)
{
  float2 v = float2(2 * vXY.x - 1, 1 - 2 * vXY.y);
  return g_Vol.vCameraPos.xyz + d * (g_Vol.vRayO.xyz + v.x * g_Vol.vRayX.xyz + v.y * g_Vol.vRayY.xyz);
}

#ifdef XR_VOL_RAYS

//! The sun's state this frame at the froxel world position vPos is in (VolSunState), 2 out of the volume.
float SunStateAt(float3 vPos)
{
  const float3 c = VolFroxelCoord(vPos, g_Vol.mViewProj, g_Vol.vSlices);
  if (any(c < 0) || any(c >= 1))
    return 2;
  return g_SunStates.Load(int4(c * g_Vol.vSize.xyz, 0));
}

//! The shafts: the part of the air within g_Vol.vShafts.x m of vPos across the sun's direction that is in the sun's
//! shadow (0 none or no air around), from this frame's states; fTurn turns the disc's points (0..1).
float AirInShadow(float3 vPos, float fTurn)
{
  float3 vA, vB;
  VolAcross(g_Vol.vSunDir.xyz, vA, vB);
  float s[SHAFT_TAPS];
  [unroll]
  for (uint k = 0; k < SHAFT_TAPS; k++)
    s[k] = SunStateAt(vPos + VolShaftTap(k, g_Vol.vShafts.x, fTurn, vA, vB));
  return VolAirInShadow(s);
}

#else

//! The cascade i (out) of view depth d; false beyond the cascades (the sun reaches everywhere).
bool CascadeOf(float d, out uint i)
{
  uint n = min((uint)g_Vol.vCascadeParams.x, XR_MAX_CASCADES);
  i = 0;
  if (n == 0 || d >= g_Vol.vCascadeEnd[n - 1])
    return false;
  [unroll]
  for (uint k = 1; k < XR_MAX_CASCADES; k++)
  {
    if (k < n && d > g_Vol.vCascadeEnd[k - 1])
      i = k;
  }
  return true;
}

//! The sun's visibility at world position vPos, view depth d.
float SunVisibility(float3 vPos, float d)
{
  uint i;
  if (!CascadeOf(d, i))
    return 1;
  float4 v = mul(float4(vPos, 1), g_Vol.mCascade[i]);
  float2 vUV = float2(0.5 + 0.5 * v.x, 0.5 - 0.5 * v.y);
  return g_Cascades.SampleCmpLevelZero(g_SamplerShadow, float3(vUV, i), v.z - g_Vol.vCascadeParams.w * g_Vol.vCascadeDepth[i]);
}

//! The shafts: the point vPos as the sun sees it in cascade i (one texel): 1 the sun reaches it, 0 it is in a shadow cast
//! from more than SHAFT_SURFACE m nearer the sun, -1 it is under a surface nearer than that (not air). Outside the cascade lit.
float SunOverAir(float3 vPos, uint i)
{
  float4 v = mul(float4(vPos, 1), g_Vol.mCascade[i]);
  float2 vUV = float2(0.5 + 0.5 * v.x, 0.5 - 0.5 * v.y);
  if (any(vUV <= 0) || any(vUV >= 1))
    return 1;
  uint w, h, e;
  g_Cascades.GetDimensions(w, h, e);
  float fNearest = g_Cascades.Load(int4(vUV * float2(w, h), i, 0));
  float fBehind = (v.z - fNearest) / g_Vol.vCascadeDepth[i];
  return fBehind <= g_Vol.vCascadeParams.w ? 1 : fBehind > SHAFT_SURFACE ? 0 : -1;
}

//! The shafts: the part of the air within g_Vol.vShafts.x m of vPos (view depth d) across the sun's direction that is in
//! the sun's shadow (0 none or no air around); fTurn turns the disc's points (0..1).
float AirInShadow(float3 vPos, float d, float fTurn)
{
  uint i;
  if (!CascadeOf(d, i))
    return 0;
  // Volumetric.hlsli's VolAcross, VolShaftTap and VolAirInShadow written out as before them (FroxelPos: the frames
  // without the rays stay to the last bit).
  float3 vSun = g_Vol.vSunDir.xyz;
  float3 vA = normalize(cross(vSun, abs(vSun.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
  float3 vB = cross(vSun, vA);
  float fLit = 0, fAir = 0;
  [unroll]
  for (uint k = 0; k < SHAFT_TAPS; k++)
  {
    // A Vogel disc: the points spread evenly over it.
    float r = g_Vol.vShafts.x * sqrt((k + 0.5) / SHAFT_TAPS);
    float a = 2.39996323 * k + 6.28318531 * fTurn;
    float s = SunOverAir(vPos + r * (cos(a) * vA + sin(a) * vB), i);
    if (s >= 0)
    {
      fLit += s;
      fAir += 1;
    }
  }
  return fAir > 0 ? 1 - fLit / fAir : 0;
}

#endif

[numthreads(XR_VOLUMETRIC_GROUP, XR_VOLUMETRIC_GROUP, 1)]
void CSInject(uint3 vId : SV_DispatchThreadID)
{
  if (any(vId >= (uint3)g_Vol.vSize.xyz))
    return;
  // The point: this frame's in the froxel, its depth moved by the column's noise.
  // (Volumetric.hlsli's VolColumnNoise and VolFroxelCell, written out as before them: FroxelPos.)
  float fNoise = frac(52.9829189 * frac(dot((float2)vId.xy, float2(0.06711056, 0.00583715))));
  float3 t = float3(g_Vol.vJitter.xy, frac(g_Vol.vJitter.z + fNoise));
  float3 vCell = ((float3)vId + t) / g_Vol.vSize.xyz;
  float d = SliceDepth(vCell.z);
  float3 vPos = FroxelPos(vCell.xy, d);

  float fExtinction = g_Vol.vDensity.x * exp2(-max(vPos.z - g_Vol.vDensity.y, 0) * g_Vol.vDensity.z);
  float3 vDir = normalize(vPos - g_Vol.vCameraPos.xyz);
  float g = g_Vol.vSunDir.w;
  float fPhase = (1 - g * g) / pow(max(1 + g * g - 2 * g * dot(vDir, g_Vol.vSunDir.xyz), 1e-4), 1.5);
  // The light from all round, less in the sun's shadow: the leaves and the roofs that hide the sun
  // hide most of the sky too.
#ifdef XR_VOL_RAYS
  // The sun's state at this point by its rays: lit or not (under a surface near it, in its shadow).
  float fVisible = g_SunStates.Load(int4(vId, 0)) > 0.5 ? 1 : 0;
#else
  float fVisible = any(g_Vol.vSunLight.rgb > 0) ? SunVisibility(vPos, d) : 1;
#endif
  float3 vLight;
  if (g_Vol.vShafts.x > 0)
  {
    // The shafts: the sun's light as much as the air around is in shadow.
#ifdef XR_VOL_RAYS
    float fShade = fVisible > 0 ? AirInShadow(vPos, frac(fNoise + g_Vol.vJitter.y + 0.618034 * vId.z)) : 0;
#else
    float fShade = fVisible > 0 ? AirInShadow(vPos, d, frac(fNoise + g_Vol.vJitter.y + 0.618034 * vId.z)) : 0;
#endif
    vLight = g_Vol.vSunLight.rgb * fPhase * fVisible * (fShade > 0 ? pow(fShade, g_Vol.vShafts.y) : 0);
  }
  else
    vLight = g_Vol.vSunLight.rgb * fPhase * fVisible + g_Vol.vAmbientLight.rgb * lerp(g_Vol.vAmbientLight.w, 1, fVisible);
#ifdef XR_VOL_RAYS
  if (g_Vol.vRays.w > 0)
  {
    // d3d12_VolumetricLightRays 2: the states in colours, the sun's light: red lit, green in a shadow, blue not air.
    const float s = g_SunStates.Load(int4(vId, 0));
    vLight = max(g_Vol.vSunLight.r, 0.5) * (s > 0.5 ? float3(1, 0, 0) : s > -0.5 ? float3(0, 1, 0) : float3(0, 0, 1));
  }
#endif
  float4 vFroxel = float4(fExtinction * vLight, fExtinction);

  // The history at the froxel's centre, where it was in the frame before.
  if (g_Vol.vDensity.w > 0)
  {
    float3 vCentre = ((float3)vId + 0.5) / g_Vol.vSize.xyz;
    float4 c = mul(float4(FroxelPos(vCentre.xy, SliceDepth(vCentre.z)), 1), g_Vol.mPrevViewProj);
    if (c.w > 0)
    {
      float3 vPrev = float3(0.5 + 0.5 * c.x / c.w, 0.5 - 0.5 * c.y / c.w, log2(c.w / g_Vol.vSlices.x) * g_Vol.vSlices.z);
      if (all(vPrev >= 0) && all(vPrev <= 1))
        vFroxel = lerp(vFroxel, g_Input.SampleLevel(g_SamplerLinearClamp, vPrev, 0), g_Vol.vDensity.w);
    }
  }
  g_Out[vId] = vFroxel;
}

[numthreads(XR_VOLUMETRIC_GROUP, XR_VOLUMETRIC_GROUP, 1)]
void CSIntegrate(uint2 vId : SV_DispatchThreadID)
{
  if (any(vId >= (uint2)g_Vol.vSize.xy))
    return;
  // The length of the column's ray per metre of view depth.
  float2 vXY = ((float2)vId + 0.5) / g_Vol.vSize.xy;
  float fRay = length(FroxelPos(vXY, 1) - g_Vol.vCameraPos.xyz);
  uint nSlices = (uint)g_Vol.vSize.z;
  float3 vLight = 0;
  float fTransmittance = 1;
  float fStart = 0;
  for (uint z = 0; z < nSlices; z++)
  {
    float fEnd = SliceDepth((z + 1) * g_Vol.vSize.w);
    float4 v = g_Input.Load(int4(vId, z, 0));
    float fExtinction = max(v.a, 1e-7);
    float fSlice = exp(-fExtinction * (fEnd - fStart) * fRay);
    vLight += fTransmittance * v.rgb * ((1 - fSlice) / fExtinction);
    fTransmittance *= fSlice;
    g_Out[uint3(vId, z)] = float4(vLight, g_Vol.vShafts.x > 0 ? 1 : fTransmittance);
    fStart = fEnd;
  }
}
