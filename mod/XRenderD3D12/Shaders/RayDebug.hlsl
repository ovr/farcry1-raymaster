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
//	  vParams1  xyz the camera, w the instance mask (d3d12_RTMask, bits
//	            0-7), bit 8 the sun's shadow at the hits (d3d12_RTShadows)
//	The views:
//	  XR_RAY_DEBUG_INSTANCES  a colour per instance, the edges of its
//	                          triangles darker
//	  XR_RAY_DEBUG_SHADED     the hits shaded (RayShade.hlsli) and fogged
//	                          as the picture, on the right half (the
//	                          picture under it on the left)
//	  XR_RAY_DEBUG_DISTANCE   blue near, through green, to red at the
//	                          rays' length (log scale)
//	  XR_RAY_DEBUG_LOD        the mip level of the hit's texture 0 under
//	                          the pixel's ray cone: blue 0, through green
//	                          4, to red 8 and more
//	  XR_RAY_DEBUG_KIND       static BLAS green, this frame's red, alpha
//	                          tested ones lighter
//	  XR_RAY_DEBUG_ALPHA      the alpha tests along the ray: none dark,
//	                          then green to red at XR_RAY_MAX_ALPHA_TESTS
//	  XR_RAY_DEBUG_SHADED_ALL as XR_RAY_DEBUG_SHADED, the whole picture
//	A ray that hits nothing is black (the shaded views: the fog's
//	colour). The views of the instances, the distance and the kind take
//	the alpha tested triangles as opaque.
//
//////////////////////////////////////////////////////////////////////

#include "RayShade.hlsli"

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

//! The direction from the camera through the point vPixel of the target.
float3 PixelRay(float2 vPixel)
{
  // The pixel's point at the middle of the depth range: in front of the camera whichever way the
  // projection's depth goes.
  const float2 vUV = vPixel * g_Draw.vParams0.xy;
  const float4 vWorld = mul(float4(vUV.x * 2 - 1, 1 - vUV.y * 2, 0.5, 1), g_Draw.mParams0);
  return SafeNormalize(vWorld.xyz / vWorld.w - g_Draw.vParams1.xyz);
}

//! Blue at 0, green at 0.5, red at 1.
float3 Ramp(float f)
{
  return saturate(float3(2 * f - 1, 1 - abs(2 * f - 1), 1 - 2 * f));
}

float4 PSMain(SRayDebugOutput In) : SV_Target
{
  const uint nView = (uint)g_Draw.vParams0.z;
  if (nView == XR_RAY_DEBUG_SHADED && In.vPos.x * g_Draw.vParams0.x < 0.5)
    discard;
  RayDesc Ray;
  Ray.Origin = g_Draw.vParams1.xyz;
  Ray.Direction = PixelRay(In.vPos.xy);
  Ray.TMin = 0.01;
  Ray.TMax = g_Draw.vParams0.w;
  const uint nMask = asuint(g_Draw.vParams1.w) & 0xff;

  if (nView == XR_RAY_DEBUG_SHADED || nView == XR_RAY_DEBUG_SHADED_ALL || nView == XR_RAY_DEBUG_LOD || nView == XR_RAY_DEBUG_ALPHA)
  {
    // The pixel's cone: from a point, as wide as the angle between neighbouring pixels.
    const float fSpread = length(PixelRay(In.vPos.xy + float2(1, 0)) - Ray.Direction);
    const SRayHit Hit = TraceScene(Ray, nMask, 0, fSpread);
    if (nView == XR_RAY_DEBUG_ALPHA)
      return float4(Hit.nAlphaTests ? Ramp(Hit.nAlphaTests / (float)XR_RAY_MAX_ALPHA_TESTS) : 0.1, 1);
    if (!Hit.bHit)
      return nView == XR_RAY_DEBUG_LOD ? float4(0, 0, 0, 1) : SceneOutput(float4(g_Frame.vFogColor.rgb, 1));
    if (nView == XR_RAY_DEBUG_LOD)
    {
      const SRayInstance I = g_RayInstances[Hit.nInstance];
      const SRaySurface S = RaySurface(Hit.nInstance, Hit.nPrimitive, Hit.vBary, Hit.mObjectToWorld, Ray.Direction, Hit.fT * fSpread);
      if (!(I.nMaterial.x & (DF_TEX0 | DF_TEX0_ARRAY)) || (I.nMaterial.x & DF_CUBE0))
        return float4(0.2, 0.2, 0.2, 1);
      const float fLevel = (I.nMaterial.x & DF_TEX0_ARRAY) ? RayTextureArrayLevel(I.nMaterial.z, S.fLod) : RayTextureLevel(I.nMaterial.z, S.fLod);
      return float4(Ramp(fLevel / 8), 1);
    }
    bool bFog;
    float4 vColor = RayShade(Hit, Ray.Direction, Hit.fT * fSpread, (asuint(g_Draw.vParams1.w) & 0x100) != 0, nMask, bFog);
    if (bFog && g_Frame.vFogParams.w != 0)
      vColor.rgb = lerp(g_Frame.vFogColor.rgb, vColor.rgb, SceneFog(Ray.Origin + Ray.Direction * Hit.fT));
    return SceneOutput(float4(vColor.rgb, 1));
  }

  RayQuery<RAY_FLAG_FORCE_OPAQUE> Query;
  Query.TraceRayInline(g_RayScene, RAY_FLAG_NONE, nMask, Ray);
  Query.Proceed();
  if (Query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
    return float4(0, 0, 0, 1);

  const uint nKind = Query.CommittedInstanceContributionToHitGroupIndex();
  float3 vColor;
  if (nView == XR_RAY_DEBUG_DISTANCE)
  {
    vColor = Ramp(saturate(log2(1 + Query.CommittedRayT()) / log2(1 + Ray.TMax)));
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
