// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayDebug.hlsl
//	Description: The ray debug program (eD3D12P_RayDebug, d3d12_RTDebug):
//	the scene for rays (RayTracing.h) as the main view's camera sees it,
//	one ray per pixel, over the whole picture. Shader Model 6.5 (inline
//	RayQuery).
//
//	A triangle over the whole target, POSITION in clip space.
//	  mParams0  the main view's clip space to the world (at the capture)
//	  vParams0  x 1 / the target's width, y 1 / its height, z the view
//	            (XR_RAY_DEBUG_*), w the rays' length (m)
//	  vParams1  xyz the camera, w the instance mask (d3d12_RTMask)
//	The views:
//	  XR_RAY_DEBUG_INSTANCES  a colour per instance, the edges of its
//	                          triangles darker
//	  XR_RAY_DEBUG_DISTANCE   blue near, through green, to red at the
//	                          rays' length (log scale)
//	  XR_RAY_DEBUG_KIND       static BLAS green, this frame's red, alpha
//	                          tested ones lighter
//	A ray that hits nothing is black. The alpha tested triangles are opaque
//	here.
//
//////////////////////////////////////////////////////////////////////

#include "Common.hlsli"
#include "RayConstants.h"

RaytracingAccelerationStructure g_RayScene : register(t0, space5);

struct SRayDebugOutput
{
  float4 vPos : SV_Position;
};

SRayDebugOutput VSMain(SVertexInput In)
{
  SRayDebugOutput Out;
  Out.vPos = float4(In.vPos.xy, 0, 1);
  return Out;
}

//! A colour of its own for n (a PCG hash), not too dark.
float3 HashColor(uint n)
{
  n = n * 747796405u + 2891336453u;
  n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
  n = (n >> 22u) ^ n;
  return 0.25 + 0.75 * float3(n & 255, (n >> 8) & 255, (n >> 16) & 255) / 255;
}

float4 PSMain(SRayDebugOutput In) : SV_Target
{
  // The pixel's point at the middle of the depth range: in front of the camera whichever way the
  // projection's depth goes.
  const float2 vUV = In.vPos.xy * g_Draw.vParams0.xy;
  const float4 vWorld = mul(float4(vUV.x * 2 - 1, 1 - vUV.y * 2, 0.5, 1), g_Draw.mParams0);
  RayDesc Ray;
  Ray.Origin = g_Draw.vParams1.xyz;
  Ray.Direction = SafeNormalize(vWorld.xyz / vWorld.w - Ray.Origin);
  Ray.TMin = 0.01;
  Ray.TMax = g_Draw.vParams0.w;
  RayQuery<RAY_FLAG_FORCE_OPAQUE> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, asuint(g_Draw.vParams1.w) & 0xff, Ray);
  Query.Proceed();
  if (Query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    return float4(0, 0, 0, 1);

  const uint nView = (uint)g_Draw.vParams0.z;
  const uint nKind = Query.CommittedInstanceContributionToHitGroupIndex();
  float3 vColor;
  if (nView == XR_RAY_DEBUG_DISTANCE)
  {
    const float f = saturate(log2(1 + Query.CommittedRayT()) / log2(1 + Ray.TMax));
    vColor = saturate(float3(2 * f - 1, 1 - abs(2 * f - 1), 1 - 2 * f));
  }
  else if (nView == XR_RAY_DEBUG_KIND)
  {
    vColor = (nKind & XR_RAY_STATIC) ? float3(0.1, 0.8, 0.2) : float3(0.9, 0.15, 0.1);
    if (nKind & XR_RAY_ALPHATEST)
      vColor = lerp(vColor, 1, 0.5);
  }
  else
  {
    const float2 b = Query.CommittedTriangleBarycentrics();
    const float fEdge = min(min(b.x, b.y), 1 - b.x - b.y);
    vColor = HashColor(Query.CommittedInstanceIndex()) * (fEdge < 0.03 ? 0.4 : 1);
  }
  return float4(vColor, 1);
}
