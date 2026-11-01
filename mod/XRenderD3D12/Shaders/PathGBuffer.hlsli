// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PathGBuffer.hlsli
//	Description: The path tracer's G-buffer (d3d12_PathTracing,
//	PathTrace.cpp): what the main view's opaque list writes with XR_PT
//	(General.hlsl), at the scene's size.
//	  - the colour (the scene's target, the game's colour): the light the
//	    path tracer does not make, what the surfaces it lights add unlit
//	    (DF2_ADD_TEX4, DF2_ALPHA_GLOW), the unlit surfaces' colour, the fog;
//	  - the albedo (D3D12_PT_ALBEDO_FORMAT, SV_Target3): the colour of the
//	    surfaces it lights without their light, the game's colour (what the
//	    light 1 of the game's lighting makes of them; the terrain's with the
//	    light baked into the sector's texture still in it), not fogged. It
//	    blends as the colour: a decal over a surface and the terrain's
//	    layers change it as they change the colour (the layers multiply it,
//	    their factor unfogged), a fog volume over it takes it away;
//	  - the normal (D3D12_PT_NORMAL_FORMAT, SV_Target4) of the opaque
//	    surface, its side towards the camera (the normal map's with
//	    DF_BUMP): rg octahedral (PathOctEncode), b the share of the
//	    surface's colour the fog leaves (SceneFog; 1 without fog), a the
//	    path material plus 1 (XR_PT_MAT_* + 1 & 3: 0 none or unlit, 1 lit,
//	    2 the terrain, 3 the vegetation; DF2_PT_MASK). The blended draws do
//	    not write it;
//	  - the sun's visibility (D3D12_PT_SUN_FORMAT, SV_Target5, r) at the
//	    opaque surface, as the raster finds it (its shadow maps, the sun's
//	    cascades, the mask of the sun's shadow by rays; all the casters),
//	    and the game's ambient light on it / 2 (gba, the game's colour: what
//	    the draw's ambient colour adds to its albedo's light 1), which the
//	    paths fill in with (PathTrace.hlsl). The blended draws do not write
//	    it;
//	  - the highlights' colour (D3D12_PT_SPECULAR_FORMAT, SV_Target6, rgb):
//	    half of the raster's 2 * vSpecular * gloss (DF_SPECULAR lit per
//	    pixel; the fixed function's material light about as much), the game's
//	    colour, 0 where it has none; it blends as the albedo. The highlights
//	    of the sun and the lamps on it are PathTrace.hlsl's CSSpecular.
//	The picture is then the colour plus the fog's share of the albedo's
//	surfaces lit by the path tracer (PathComposite.hlsl).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PATHGBUFFER_HLSLI
#define XRENDERD3D12_PATHGBUFFER_HLSLI

//! A unit vector as the octahedral map's point, 0..1.
float2 PathOctEncode(float3 n)
{
  n /= abs(n.x) + abs(n.y) + abs(n.z);
  float2 p = n.xy;
  if (n.z < 0)
    p = (1 - abs(n.yx)) * float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1);
  return p * 0.5 + 0.5;
}

//! The unit vector of an octahedral map's point (PathOctEncode).
float3 PathOctDecode(float2 e)
{
  const float2 p = e * 2 - 1;
  float3 n = float3(p, 1 - abs(p.x) - abs(p.y));
  const float t = saturate(-n.z);
  n.x += n.x >= 0 ? -t : t;
  n.y += n.y >= 0 ? -t : t;
  return normalize(n);
}

//! The normal target's texel: vNormal (not normalised) on the side of vToCamera, the fog's share fFog, the material
//! field of DF2_PT_MASK (the path material plus 1).
float4 PathGBufferNormal(float3 vNormal, float3 vToCamera, float fFog, uint nMaterial)
{
  float3 n = dot(vNormal, vNormal) > 1e-12 ? normalize(vNormal) : float3(0, 0, 1);
  if (dot(n, vToCamera) < 0)
    n = -n;
  return float4(PathOctEncode(n), saturate(fFog), (float)(nMaterial & 3) / 3);
}

//! The path material plus 1 of a normal texel (0: no surface the path tracer lights).
uint PathGBufferMaterial(float4 vNormal)
{
  return (uint)round(vNormal.a * 3);
}

#endif
