// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LightmapLight.hlsli
//	Description: How the original's light-map bump shaders light a
//	surface with a dot3 light map (General.hlsl's DF_LIGHTMAP_DIR and
//	the lamps baked into it, RayShade.hlsli's hits). Without bindings:
//	the programs and the unit tests (Tests/Shaders/TestHeaders.hlsl)
//	include it.
//
//	The original's pixel shaders of the bump templates over a dot3 light
//	map (seen in Research's frames): the light map's colour L (rgb, a)
//	and its direction map D (rgb * 2 - 1 the light's direction in the
//	surface's tangent space, a its strength) give the diffuse light
//	4 * L.rgb * D.a * lerp(1, saturate(N'.D), L.a), N' the normal map's
//	normal; with a lamp baked into the light map in the object's lights
//	they add, after it, 2 * its specular colour times the material's *
//	falloff(d / radius) * saturate(R.V)^32 * the texture's alpha, R the
//	direction to the lamp reflected about N', V towards the eye, and the
//	sum is clamped to 1.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_LIGHTMAPLIGHT_HLSLI
#define XRENDERD3D12_LIGHTMAPLIGHT_HLSLI

//! The share of a dot3 light map's light at a texel: N'.D (fND, saturated) alone (bMix false, as before
//! d3d12_LightmapDot3), or as the original's shaders mixed towards 1 by the light map's alpha fMapAlpha (where it is 0
//! the bumps do not shade its light at all).
float LightmapDot3(float fND, float fMapAlpha, bool bMix)
{
  return bMix ? lerp(1, fND, fMapAlpha) : fND;
}

//! How a lamp baked into a light map lights its highlight at the distance x * its radius: the original's light-map bump
//! shaders look its 512 x 16 falloff texture up at v = 0.25, between its row of (1 - x)^2 and its row of
//! min(1, 1.5 (1 - x)) with a linear filter, so half of each (0 from the radius on).
float LightmapLampFalloff(float x)
{
  const float t = saturate(1 - x);
  return 0.5 * t * t + 0.5 * saturate(1.5 * t);
}

//! The highlight of such a lamp without its colour and falloff: saturate(R.V)^32, R the unit vector to the lamp vL
//! reflected about the unit normal vN, vToEye the unit vector to the eye; nothing left out where N.L < 0 (as the
//! original's).
float LightmapLampHighlight(float3 vN, float3 vL, float3 vToEye)
{
  const float fRV = saturate(dot(2 * dot(vL, vN) * vN - vL, vToEye));
  const float f2 = fRV * fRV, f4 = f2 * f2, f8 = f4 * f4, f16 = f8 * f8;
  return f16 * f16;
}

#endif
