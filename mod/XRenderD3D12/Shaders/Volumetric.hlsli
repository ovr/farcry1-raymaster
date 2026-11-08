// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Volumetric.hlsli
//	Description: What the volumetric light's kernels share (Volumetric.hlsl,
//	VolumetricRays.hlsl; Volumetric.cpp) and its unit tests check: the
//	froxels' points and coordinates, the shafts' disc of the air around a
//	point and the sun's state at a point from its ray towards the sun
//	(d3d12_VolumetricLightRays). Functions of their arguments alone: the
//	kernels pass their SVolumetricConstants' members. Volumetric.hlsl's
//	kernels with the cascades write the same formulas out as they were
//	before these: the compiler's fast math orders their sums otherwise when
//	the constants come as arguments, and the frames without the rays would
//	move by a last bit.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_VOLUMETRIC_HLSLI
#define XRENDERD3D12_VOLUMETRIC_HLSLI

//! The shafts: the points of the air around (a disc across the sun's direction).
#define SHAFT_TAPS 8
//! The shafts: a point less than this (m) under a surface towards the sun is not air (the ground, inside a thing).
#define SHAFT_SURFACE 1.5

//! The sun's states at a point (d3d12_VolumetricLightRays, VolSunState): it reaches it, it is in a shadow cast from
//! farther than SHAFT_SURFACE, the point is under a surface nearer than that (not air).
#define VOL_SUN_LIT 1.0
#define VOL_SUN_SHADOW 0.0
#define VOL_SUN_NOT_AIR -1.0

//! The view depth at w (0 the near end, 1 the far end of the volume) of slices vSlices (x near, y log2(far / near)).
float VolSliceDepth(float w, float4 vSlices)
{
  return vSlices.x * exp2(w * vSlices.y);
}

//! The world position at x, y (0..1 across and down the view) and the view depth d: vCamera + d (vRayO + x' vRayX +
//! y' vRayY), x' and y' -1..1 (y' up).
float3 VolFroxelPos(float2 vXY, float d, float3 vCamera, float3 vRayO, float3 vRayX, float3 vRayY)
{
  const float2 v = float2(2 * vXY.x - 1, 1 - 2 * vXY.y);
  return vCamera + d * (vRayO + v.x * vRayX + v.y * vRayY);
}

//! The noise of a column of froxels (0..1): its points' depths move by it.
float VolColumnNoise(uint2 vColumn)
{
  return frac(52.9829189 * frac(dot((float2)vColumn, float2(0.06711056, 0.00583715))));
}

//! The point of froxel vId this frame (0..1 each way through the volume of vSize's x y z froxels): vJitter's xy
//! across, its z moved by the column's noise fNoise in depth.
float3 VolFroxelCell(uint3 vId, float3 vJitter, float fNoise, float3 vSize)
{
  const float3 t = float3(vJitter.xy, frac(vJitter.z + fNoise));
  return ((float3)vId + t) / vSize;
}

//! The coordinates in the volume (0..1 across, down, deep) of world position vPos, mViewProj the view's world to clip
//! (its w the view depth), vSlices as VolSliceDepth's; any outside 0..1 out of it (behind the camera all -1).
float3 VolFroxelCoord(float3 vPos, float4x4 mViewProj, float4 vSlices)
{
  const float4 c = mul(float4(vPos, 1), mViewProj);
  if (c.w <= 0)
    return -1;
  return float3(0.5 + 0.5 * c.x / c.w, 0.5 - 0.5 * c.y / c.w, log2(c.w / vSlices.x) * vSlices.z);
}

//! Two directions across vSun (normalised), at right angles to it and each other.
void VolAcross(float3 vSun, out float3 vA, out float3 vB)
{
  vA = normalize(cross(vSun, abs(vSun.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
  vB = cross(vSun, vA);
}

//! The shafts' point k of SHAFT_TAPS of the disc of radius fRadius across the sun's direction (vA, vB: VolAcross),
//! turned by fTurn (0..1): a Vogel disc, the points spread evenly over it. The offset from the disc's centre.
float3 VolShaftTap(uint k, float fRadius, float fTurn, float3 vA, float3 vB)
{
  const float r = fRadius * sqrt((k + 0.5) / SHAFT_TAPS);
  const float a = 2.39996323 * k + 6.28318531 * fTurn;
  return r * (cos(a) * vA + sin(a) * vB);
}

//! The sun's state at a point from its rays towards the sun: bNear something hides the sun within SHAFT_SURFACE of the
//! point (VOL_SUN_NOT_AIR), else bFar something farther does (VOL_SUN_SHADOW), else VOL_SUN_LIT.
float VolSunState(bool bNear, bool bFar)
{
  return bNear ? VOL_SUN_NOT_AIR : bFar ? VOL_SUN_SHADOW : VOL_SUN_LIT;
}

//! The part of the air around that is in the sun's shadow from the states s[k] of its SHAFT_TAPS points (VolSunState;
//! any other value, a point out of the volume, is left out): 0 none, or no air around.
float VolAirInShadow(float s[SHAFT_TAPS])
{
  float fLit = 0, fAir = 0;
  [unroll]
  for (uint k = 0; k < SHAFT_TAPS; k++)
  {
    if (s[k] >= VOL_SUN_SHADOW && s[k] <= VOL_SUN_LIT)
    {
      fLit += s[k];
      fAir += 1;
    }
  }
  return fAir > 0 ? 1 - fLit / fAir : 0;
}

#endif
