// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: VolumetricRays.hlsl
//	Description: The volumetric light's sun by rays
//	(d3d12_VolumetricLightRays, Volumetric.cpp): Shader Model 6.5 (inline
//	RayQuery), the compute root signature with rays (D3D12Compute.h), its
//	pass constants SVolumetricConstants (b2).
//
//	CSSun, a thread a froxel: at this frame's point in it (the same as
//	Volumetric.hlsl's CSInject takes: VolFroxelCell) a ray towards a point
//	of the sun's disc (d3d12_RTSunAngle wide, another for each froxel and
//	frame) among the instances of the mask (not the pieces of the sky,
//	XR_RAY_SKY: the sun is behind them), alpha tested as the sun's shadow
//	by rays (RaySun.hlsl): first over SHAFT_SURFACE, then on to the rays'
//	length; the sun's state there (VolSunState): something within
//	SHAFT_SURFACE hides the sun (the point is under a roof or inside a
//	thing: not air), something farther does (a shadow), or it reaches it.
//	The light of the froxels then takes the cascades' place with these
//	(Volumetric.hlsl's XR_VOL_RAYS), and indoors as well: what keeps the
//	sun out of a room is in the scene for rays, so only what comes through
//	its openings lights its air.
//	With d3d12_VolumetricLightRaysHalf (nRays.z) every other froxel traces
//	in a frame (half the threads across, VolSunFroxel: threads that skipped
//	theirs in a group of all of them saved a seventh), the others keep the
//	state the frame before
//	found at their points: half the rays; the injection blends the froxels
//	of the frames before in anyway, and each froxel's point moves from
//	frame to frame, so a state a frame old is one more of those.
//	  u0 the states (RWTexture3D, R8_SNORM, a texel a froxel)
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"
#include "Volumetric.hlsli"

ConstantBuffer<SVolumetricConstants> g_Vol : register(b2);
RWTexture3D<float> g_Out : register(u0);

//! Whether something among the instances of nMask hides the sun along Ray (alpha tested as RaySun.hlsl's SunHit:
//! fConeWidth the ray cone's width at the origin, fSpread its growth a metre; nCoverage RayCandidatePasses' seed).
bool SunHidden(RayDesc Ray, uint nMask, float fConeWidth, float fSpread, uint nCoverage)
{
  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, nMask, Ray);
  uint nTests = 0;
  while (Query.Proceed())
  {
    if (Query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
      continue;
    bool bHit = nTests >= XR_RAY_MAX_ALPHA_TESTS;
    if (!bHit)
    {
      nTests++;
      const float fCone = fConeWidth + Query.CandidateTriangleRayT() * fSpread;
      bHit = RayCandidatePasses(Query.CandidateInstanceID(), Query.CandidatePrimitiveIndex(), Query.CandidateTriangleBarycentrics(),
        Query.CandidateObjectToWorld3x4(), Ray.Direction, fCone, nCoverage);
    }
    if (bHit)
      Query.CommitNonOpaqueTriangleHit();
  }
  return Query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

[numthreads(XR_VOLUMETRIC_GROUP, XR_VOLUMETRIC_GROUP, 1)]
void CSSun(uint3 vThread : SV_DispatchThreadID)
{
  // d3d12_VolumetricLightRaysHalf: every other froxel a frame, the rest keep their states (u0 has the frame before's).
  const uint3 vId = VolSunFroxel(vThread, g_Vol.nRays.y, g_Vol.nRays.z != 0);
  if (any(vId >= (uint3)g_Vol.vSize.xyz))
    return;
  // The point CSInject takes this frame.
  const float fNoise = VolColumnNoise(vId.xy);
  const float3 vCell = VolFroxelCell(vId, g_Vol.vJitter.xyz, fNoise, g_Vol.vSize.xyz);
  const float d = VolSliceDepth(vCell.z, g_Vol.vSlices);
  const float3 vPos = VolFroxelPos(vCell.xy, d, g_Vol.vCameraPos.xyz, g_Vol.vRayO.xyz, g_Vol.vRayX.xyz, g_Vol.vRayY.xyz);
  // A point of the sun's disc, uniform over it: another for each froxel and frame.
  const float u = frac(fNoise + 0.618034 * vId.z + 0.7548777 * (g_Vol.nRays.y & 1023));
  const float v = frac(VolColumnNoise(vId.yx + uint2(vId.z * 7, g_Vol.nRays.y * 13)) + 0.5698403 * vId.z);
  float3 vA, vB;
  VolAcross(g_Vol.vSunDir.xyz, vA, vB);
  const float2 vDisc = sqrt(u) * g_Vol.vRays.y * float2(cos(6.2831853 * v), sin(6.2831853 * v));
  RayDesc Ray;
  Ray.Origin = vPos;
  Ray.Direction = normalize(g_Vol.vSunDir.xyz + vDisc.x * vA + vDisc.y * vB);
  Ray.TMin = 0;
  Ray.TMax = min(SHAFT_SURFACE, g_Vol.vRays.x);
  const uint nMask = g_Vol.nRays.x & 0xff;
  const float fCone = d * g_Vol.vRays.z, fSpread = 2 * g_Vol.vRays.y;
  const uint nCoverage = (g_Vol.nRays.x & XR_RAY_COVERAGE_RAYS) ? asuint(u) ^ (asuint(v) * 0x9e3779b1u) | 1 : 0;
  // Near first (not air), then the rest of the way (a shadow): each the first hit it finds.
  const bool bNear = SunHidden(Ray, nMask, fCone, fSpread, nCoverage);
  bool bFar = false;
  if (!bNear && g_Vol.vRays.x > SHAFT_SURFACE)
  {
    Ray.TMin = SHAFT_SURFACE;
    Ray.TMax = g_Vol.vRays.x;
    bFar = SunHidden(Ray, nMask, fCone, fSpread, nCoverage ? RayCoverageHash(nCoverage) | 1 : 0);
  }
  g_Out[vId] = VolSunState(bNear, bFar);
}
