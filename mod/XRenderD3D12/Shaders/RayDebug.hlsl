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
//	  the path tracer's views (SPathShade, PathShade.hlsli):
//	  vParams2  xyz vSun, w fLamps
//	  vParams3  x fEmission, y fDelight, z fLightGap
//	  vAmbient  rgb vBakeLight
//	  nTex2     nLights (g_PTLights, root SRV t2 space5)
//	The views:
//	  XR_RAY_DEBUG_INSTANCES  a colour per instance, of its mesh
//	                          (SRayInstance::nPath.z) and where it is to
//	                          0.25 m (not the index of the frame's list,
//	                          which moves as the list does), the edges of
//	                          its triangles darker
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
//	  XR_RAY_DEBUG_PT_ALBEDO  the path tracer's albedo of the hits, as the
//	                          game's colour (PathSurface)
//	  XR_RAY_DEBUG_PT_EMISSION its emission, as the game's colour
//	  XR_RAY_DEBUG_PT_DIRECT  emission + albedo * the light of the sun and
//	                          the lamps (PathDirect), fogged as the picture
//	  XR_RAY_DEBUG_MATERIALS  a colour per material of the hit's instance
//	                          (its SRayInstance's words but where its
//	                          geometry is in the buffers): not the index of
//	                          the frame's instance, which moves as the
//	                          frame's list does, so a surface keeps its
//	                          colour while the rays take the same of it;
//	                          its triangles' edges darker
//	  XR_RAY_DEBUG_GB_*       the path tracer's G-buffer (PathGBuffer.hlsli:
//	                          the albedo, the normal, the fog's share, the
//	                          material, the sun's visibility, the ambient
//	                          light the paths fill in with, the
//	                          highlights' colour), the light of its paths
//	                          (or of its radiance cache) on a grey albedo
//	                          (the game's 0.5) and the
//	                          highlights, no rays: tex0 its albedo, tex1
//	                          its normal, tex2 the light or the highlights
//	                          (PathTrace.hlsl's), the visibility or the
//	                          highlights' colour, vParams2.xy their size
//	A ray that hits nothing is black (the shaded views: the fog's
//	colour). The views of the instances, the distance and the kind take
//	the alpha tested triangles as opaque.
//
//////////////////////////////////////////////////////////////////////

#include "PathShade.hlsli"
#include "PathGBuffer.hlsli"

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

//! XR_RAY_DEBUG_INSTANCES: the instances' places in their colours to this many metres.
static const float INSTANCE_CELL = 0.25;

//! A number for the material the rays take of an instance (XR_RAY_DEBUG_MATERIALS): all of its SRayInstance but the
//! buffers and the bytes its geometry is at (this frame's geometry is in other pages of the upload each frame).
uint MaterialHash(SRayInstance I)
{
  const uint4 Words[] = { uint4(I.nLayout.xyz, 0), I.nMaterial, I.nMaterial2, asuint(I.vColor), asuint(I.vAmbient),
                          asuint(I.vTexGenU), asuint(I.vTexGenV), asuint(I.vTexMat0), asuint(I.vTexMat1), asuint(I.vAddColor),
                          asuint(I.vAlbedo), asuint(I.vSpecular), I.nPath };
  uint n = 2166136261u;
  [unroll]
  for (uint i = 0; i < 13; i++)
  {
    n = (n ^ Words[i].x) * 16777619u;
    n = (n ^ Words[i].y) * 16777619u;
    n = (n ^ Words[i].z) * 16777619u;
    n = (n ^ Words[i].w) * 16777619u;
  }
  return n;
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

//! The G-buffer's views (XR_RAY_DEBUG_GB_*) at the target's point vPixel.
float4 GBufferView(uint nView, float2 vPixel)
{
  const int3 vAt = int3(min(vPixel * g_Draw.vParams0.xy * g_Draw.vParams2.xy, g_Draw.vParams2.xy - 1), 0);
  const float3 vAlbedo = g_Textures[g_Draw.nTex0].Load(vAt).rgb;
  const float4 vNormal = g_Textures[g_Draw.nTex1].Load(vAt);
  const uint nMaterial = PathGBufferMaterial(vNormal);
  if (nView == XR_RAY_DEBUG_GB_ALBEDO)
    return float4(vAlbedo, 1);
  if (nView == XR_RAY_DEBUG_GB_FOG)
    return float4(vNormal.bbb, 1);
  if (nView == XR_RAY_DEBUG_GB_SUN)
    return float4(g_Textures[g_Draw.nTex2].Load(vAt).rrr, 1);
  if (nView == XR_RAY_DEBUG_GB_AMBIENT)
    return float4(nMaterial ? saturate(2 * g_Textures[g_Draw.nTex2].Load(vAt).gba) : 0, 1);
  if (nView == XR_RAY_DEBUG_GB_SPECULAR)
    return float4(nMaterial ? saturate(2 * g_Textures[g_Draw.nTex2].Load(vAt).rgb) : 0, 1);
  if (nView == XR_RAY_DEBUG_GB_HIGHLIGHTS)
    return float4(nMaterial ? saturate(g_Textures[g_Draw.nTex2].Load(vAt).rgb) : 0, 1);
  if (nMaterial == 0)
    return float4(0, 0, 0, 1);
  if (nView == XR_RAY_DEBUG_GB_NORMAL)
    return float4(PathOctDecode(vNormal.rg) * 0.5 + 0.5, 1);
  if (nView == XR_RAY_DEBUG_GB_TERRAIN)
    return float4(nMaterial == XR_PT_MAT_TERRAIN + 1 ? saturate(g_Textures[g_Draw.nTex2].Load(vAt).www) : 0, 1);
  if (nView == XR_RAY_DEBUG_GB_LIGHT || nView == XR_RAY_DEBUG_GB_RAYS || nView == XR_RAY_DEBUG_GB_CACHE)
    return float4(pow(saturate(pow(0.5, HDR_GAMMA) * g_Textures[g_Draw.nTex2].Load(vAt).rgb), 1 / HDR_GAMMA), 1);
  const float3 vColor = nMaterial == 2 ? float3(0.3, 0.9, 0.3) : nMaterial == 3 ? float3(0.9, 0.9, 0.2) : float3(0.8, 0.8, 0.8);
  return float4(vColor * (0.4 + 0.6 * saturate(dot(vAlbedo, float3(0.299, 0.587, 0.114)))), 1);
}

float4 PSMain(SRayDebugOutput In) : SV_Target
{
  const uint nView = (uint)g_Draw.vParams0.z;
  if (nView >= XR_RAY_DEBUG_GB_ALBEDO)
    return GBufferView(nView, In.vPos.xy);
  if (nView == XR_RAY_DEBUG_SHADED && In.vPos.x * g_Draw.vParams0.x < 0.5)
    discard;
  RayDesc Ray;
  Ray.Origin = g_Draw.vParams1.xyz;
  Ray.Direction = PixelRay(In.vPos.xy);
  Ray.TMin = 0.01;
  Ray.TMax = g_Draw.vParams0.w;
  const uint nMask = asuint(g_Draw.vParams1.w) & 0xff;

  if (nView >= XR_RAY_DEBUG_PT_ALBEDO && nView <= XR_RAY_DEBUG_PT_DIRECT)
  {
    const float fSpread = length(PixelRay(In.vPos.xy + float2(1, 0)) - Ray.Direction);
    const SRayHit Hit = TraceScene(Ray, nMask, 0, fSpread);
    if (!Hit.bHit)
      return nView == XR_RAY_DEBUG_PT_DIRECT ? SceneOutput(float4(g_Frame.vFogColor.rgb, 1)) : float4(0, 0, 0, 1);
    SPathShade P;
    P.vSun = g_Draw.vParams2.xyz;
    P.fLamps = g_Draw.vParams2.w;
    P.fEmission = g_Draw.vParams3.x;
    P.fDelight = g_Draw.vParams3.y;
    P.vBakeLight = g_Draw.vAmbient.rgb;
    P.fLightGap = g_Draw.vParams3.z;
    P.nLights = g_Draw.nTex2;
    P.nMask = nMask;
    P.nMaxTests = XR_RAY_MAX_ALPHA_TESTS;
    const float fWidth = Hit.fT * fSpread;
    const SPathSurface H = PathSurface(Hit, Ray.Direction, fWidth, P);
    float3 vLight;
    if (nView == XR_RAY_DEBUG_PT_ALBEDO)
      vLight = H.vAlbedo;
    else if (nView == XR_RAY_DEBUG_PT_EMISSION)
      vLight = H.vEmission;
    else
      vLight = H.vEmission + H.vAlbedo * PathDirect(H, P, fWidth);
    float3 vColor = float3(HdrEncode(vLight.r), HdrEncode(vLight.g), HdrEncode(vLight.b));
    if (nView == XR_RAY_DEBUG_PT_DIRECT && H.bFog && g_Frame.vFogParams.w != 0)
      vColor = lerp(g_Frame.vFogColor.rgb, vColor, SceneFog(Ray.Origin + Ray.Direction * Hit.fT));
    return SceneOutput(float4(vColor, 1));
  }

  if (nView == XR_RAY_DEBUG_MATERIALS)
  {
    // As the paths see the scene: the alpha tests on the way.
    const float fSpread = length(PixelRay(In.vPos.xy + float2(1, 0)) - Ray.Direction);
    const SRayHit Hit = TraceScene(Ray, nMask, 0, fSpread);
    if (!Hit.bHit)
      return float4(0, 0, 0, 1);
    const float fEdge = min(min(Hit.vBary.x, Hit.vBary.y), 1 - Hit.vBary.x - Hit.vBary.y);
    return float4(HashColor(MaterialHash(g_RayInstances[Hit.nInstance])) * (fEdge < 0.03 ? 0.4 : 1), 1);
  }

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
    // The mesh (SRayInstance::nPath.z) and where it is to INSTANCE_CELL m: the same from frame to frame, not as the
    // index of the frame's list.
    const float3x4 m = Query.CommittedObjectToWorld3x4();
    const int3 c = (int3)floor(float3(m._m03, m._m13, m._m23) / INSTANCE_CELL);
    uint n = g_RayInstances[Query.CommittedInstanceID()].nPath.z;
    n = (n ^ (uint)c.x) * 16777619u;
    n = (n ^ (uint)c.y) * 16777619u;
    n = (n ^ (uint)c.z) * 16777619u;
    vColor = HashColor(n) * (fEdge < 0.03 ? 0.4 : 1);
  }
  return float4(vColor, 1);
}
