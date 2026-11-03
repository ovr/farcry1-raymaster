// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RenderMode.hlsli
//	Description: The colour math of the render modes of the video options
//	(r_RenderMode, PostFx.cpp), as the original's screen process does it
//	(its passes traced at 1280 x 720). Without bindings: Post.hlsl's
//	passes and the unit tests (Tests/Shaders/TestHeaders.hlsl) include it.
//
//	  - The adapted luminance steps towards lum(4 m^2), m the scene's
//	    colour at four spots.
//	  - The glare's source: 2 (saturate(D + threshold) gain)^2 (1 - A), D
//	    the scene reduced, A the adapted luminance.
//	  - The scene with its glare G: F + (1 - L) G, then towards L by k, L
//	    the luminance of F; cold first F = 2 saturate(0.75 F - 0.3 D).
//	  - The cartoon: flatter, warmer colours times a factor dark where the
//	    colour falls downwards or to the right.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RENDERMODE_HLSLI
#define XRENDERD3D12_RENDERMODE_HLSLI

//! The original's screen process's weights of the luminance.
static const float3 RM_LUMINANCE = float3(0.3, 0.59, 0.11);

//! What the adapted luminance steps towards: the luminance of 4 m^2, m the scene's mean colour at its spots (saturated).
float RenderModeAdaptTarget(float3 m)
{
  m = saturate(m);
  return dot(4 * m * m, RM_LUMINANCE);
}

//! The glare's source of the reduced scene's colour d: past the threshold (fThreshold added, negative), times fGain,
//! squared and doubled, less where the adapted luminance a is high.
float3 RenderModeGlareSource(float3 d, float3 a, float fThreshold, float fGain)
{
  const float3 t = saturate(d + fThreshold) * fGain;
  return 2 * t * t * saturate(1 - saturate(a));
}

//! The cold mode's contrast first: 2 saturate(0.75 f - 0.3 d), up to white, d the reduced scene's colour.
float3 RenderModeCold(float3 f, float3 d)
{
  return saturate(2 * saturate(0.75 * f - 0.3 * d));
}

//! The scene's colour f with its glare g: f + (1 - L) g (saturated), then towards L by fGrey, L the luminance of f.
float3 RenderModeGlared(float3 f, float3 g, float fGrey)
{
  const float l = dot(f, RM_LUMINANCE);
  const float3 c = saturate(f + (1 - l) * g);
  return saturate(lerp(c, (float3)l, fGrey));
}

//! The cartoon's colour of a pixel of the scene: less saturated, brighter, warmer.
float3 RenderModeCartoonColor(float3 f)
{
  return saturate((0.75 * f + 0.225 * saturate(dot(f, RM_LUMINANCE))) * float3(1.25, 1.24, 1.2));
}

//! The cartoon's edge factor from its colours above (u), below (d), left (l) and right (r) of the pixel:
//! saturate(1 - saturate(2 |a|^2) - 2 |b|^2), a = 2 saturate(u - d), b = 2 saturate(l - r): 1 where the colour does not
//! fall downwards or to the right, 0 at a strong fall.
float RenderModeEdge(float3 u, float3 d, float3 l, float3 r)
{
  const float3 a = 2 * saturate(u - d), b = 2 * saturate(l - r);
  return saturate(1 - saturate(2 * dot(a, a)) - 2 * dot(b, b));
}

#endif
