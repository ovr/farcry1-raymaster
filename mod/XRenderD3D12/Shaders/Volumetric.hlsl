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
//	(vAmbientLight; vAmbientLight.w of it where the sun is hidden). Blended
//	with the history (the froxels of the frame
//	before at the froxel's centre, vDensity.w of it) against the noise of
//	the points.
//	  t0 the history (Texture3D, RGBA16F), t1 the full set of the cascades
//	  (Texture2DArray R32_FLOAT); u0 the froxels: rgb the light scattered
//	  (per m), a the extinction (1 / m)
//
//	CSIntegrate, a thread a column of froxels, near to far: the light
//	scattered towards the camera from it to each froxel's far end and the
//	part of the light behind that reaches the camera (the transmittance),
//	each froxel's scattering integrated over its length (energy conserving:
//	S (1 - T) / extinction, Frostbite's).
//	  t0 the froxels; u0 the integrated froxels: rgb the light, a the
//	  transmittance
//	The HDR resolve (Hdr.hlsl, HDR_VOLUMETRIC) takes the integrated froxels
//	at each pixel's depth: light * a + rgb.
//
//////////////////////////////////////////////////////////////////////

#include "Constants.h"

cbuffer cbVolumetric : register(b0)
{
  SVolumetricConstants g_Vol;
};

Texture3D<float4> g_Input : register(t0);  // the history (CSInject), the froxels (CSIntegrate)
Texture2DArray<float> g_Cascades : register(t1);
RWTexture3D<float4> g_Out : register(u0);

SamplerState g_SamplerLinearClamp : register(s1);
SamplerComparisonState g_SamplerShadow : register(s2);

//! The view depth at w (0 the near end, 1 the far end of the volume).
float SliceDepth(float w)
{
  return g_Vol.vSlices.x * exp2(w * g_Vol.vSlices.y);
}

//! The world position at x, y (0..1 across and down the view) and the view depth d.
float3 FroxelPos(float2 vXY, float d)
{
  float2 v = float2(2 * vXY.x - 1, 1 - 2 * vXY.y);
  return g_Vol.vCameraPos.xyz + d * (g_Vol.vRayO.xyz + v.x * g_Vol.vRayX.xyz + v.y * g_Vol.vRayY.xyz);
}

//! The sun's visibility at world position vPos, view depth d.
float SunVisibility(float3 vPos, float d)
{
  uint n = min((uint)g_Vol.vCascadeParams.x, XR_MAX_CASCADES);
  if (n == 0 || d >= g_Vol.vCascadeEnd[n - 1])
    return 1;
  uint i = 0;
  [unroll]
  for (uint k = 1; k < XR_MAX_CASCADES; k++)
  {
    if (k < n && d > g_Vol.vCascadeEnd[k - 1])
      i = k;
  }
  float4 v = mul(float4(vPos, 1), g_Vol.mCascade[i]);
  float2 vUV = float2(0.5 + 0.5 * v.x, 0.5 - 0.5 * v.y);
  return g_Cascades.SampleCmpLevelZero(g_SamplerShadow, float3(vUV, i), v.z - g_Vol.vCascadeParams.w * g_Vol.vCascadeDepth[i]);
}

[numthreads(XR_VOLUMETRIC_GROUP, XR_VOLUMETRIC_GROUP, 1)]
void CSInject(uint3 vId : SV_DispatchThreadID)
{
  if (any(vId >= (uint3)g_Vol.vSize.xyz))
    return;
  // The point: this frame's in the froxel, its depth moved by the column's noise.
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
  float fVisible = any(g_Vol.vSunLight.rgb > 0) ? SunVisibility(vPos, d) : 1;
  float3 vLight = g_Vol.vSunLight.rgb * fPhase * fVisible + g_Vol.vAmbientLight.rgb * lerp(g_Vol.vAmbientLight.w, 1, fVisible);
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
    g_Out[uint3(vId, z)] = float4(vLight, fTransmittance);
    fStart = fEnd;
  }
}
