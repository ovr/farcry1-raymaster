// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Parallax.hlsli
//	Description: Parallax occlusion mapping (r_Parallax; General.hlsl's
//	ParallaxFetch): where the view meets a surface's height, the
//	RayMaster material map's alpha (PbrCompanion.h: 0 a hollow, 1 a bump,
//	the reference height flat), r_ParallaxDepth deep in the texture's
//	coordinates; the textures read there in place of the geometry's uv.
//	The ray from the height 1 down to 0 in steps (more the more slanting
//	the view), the first step under the height refined by the secant
//	between it and the step before, the height taken linear between
//	them (Tatarchuk 2006, "Dynamic parallax occlusion mapping with
//	approximate soft shadows"). The reference height is not offset: the
//	light map, the decals and the depth, all the geometry's, stay on the
//	surface's flat parts. The height read through PARALLAX_HEIGHT(uv, dx,
//	dy), which the includer defines first (the gradients of uv across
//	the pixel: the march's own uv has none); stands alone (the unit
//	tests' TestParallax.hlsl).
//
//////////////////////////////////////////////////////////////////////

#ifndef XR_PARALLAX_HLSLI
#define XR_PARALLAX_HLSLI

#ifndef PARALLAX_HEIGHT
#error "Parallax.hlsli: define PARALLAX_HEIGHT(uv, dx, dy) first"
#endif

//! The view's cosine to the surface below which the depth shrinks to none edge on (and from behind): the shift at most
//! 1 / PARALLAX_MIN_COS times the depth, no stretching to the horizon.
static const float PARALLAX_MIN_COS = 0.2;

//! The direction towards the eye vToEye (world) in the tangent frame vT, vB, vN (world, not normalised: the vertices'
//! frames interpolated; vB along growing v, TangentFrame.h), normalised: x along u, y along v, z out of the surface. (0, 0,
//! 1), none to offset, for a frame of no size.
float3 ParallaxView(float3 vToEye, float3 vT, float3 vB, float3 vN)
{
  const float t = dot(vT, vT), b = dot(vB, vB), n = dot(vN, vN), e = dot(vToEye, vToEye);
  if (t < 1e-12 || b < 1e-12 || n < 1e-12 || e < 1e-12)
    return float3(0, 0, 1);
  const float3 V = vToEye * rsqrt(e);
  return normalize(float3(dot(V, vT) * rsqrt(t), dot(V, vB) * rsqrt(b), dot(V, vN) * rsqrt(n)));
}

//! The depth's share kept by its size on the screen: the depth fDepth in uv over the pixel's size in uv (the longer of the
//! gradients vDx, vDy), in pixels seen straight on; all from fFrom pixels on, none at fTo and below (far off the shift is
//! less than a pixel: no march).
float ParallaxFade(float fDepth, float2 vDx, float2 vDy, float fFrom, float fTo)
{
  const float fPixel = max(max(length(vDx), length(vDy)), 1e-12);
  return saturate((fDepth / fPixel - fTo) / max(fFrom - fTo, 1e-6));
}

//! The uv's shift from the height 0 to the height 1 seen along vView (ParallaxView), the depth fDepth in uv: fDepth V.xy /
//! V.z (towards the eye as the height grows), V.z at least PARALLAX_MIN_COS, the depth less below it, none edge on and from
//! behind.
float2 ParallaxShift(float3 vView, float fDepth)
{
  return fDepth * saturate(vView.z / PARALLAX_MIN_COS) * vView.xy / max(vView.z, PARALLAX_MIN_COS);
}

//! A shift of the geometry's uv as the texture's uv' = M uv + t (DF_TEXMATRIX, the rows vRow0 and vRow1 of M): M's turn and
//! stretch, its scale taken out (the depth a share of the texture's side as without it; the scale's square root of
//! |det M|). As it is for a singular M.
float2 ParallaxTexMatrix(float2 vShift, float2 vRow0, float2 vRow1)
{
  const float fDet = abs(vRow0.x * vRow1.y - vRow0.y * vRow1.x);
  if (fDet < 1e-12)
    return vShift;
  return float2(dot(vRow0, vShift), dot(vRow1, vShift)) * rsqrt(fDet);
}

//! The march's steps for the view's cosine fCos: nMost edge on, a third of them straight on, at least 2.
uint ParallaxSteps(float fCos, uint nMost)
{
  return max((uint)round(lerp((float)nMost, nMost / 3.0, saturate(fCos))), 2u);
}

//! The gradients the march reads the height by with r_ParallaxFilter (nParallax.w): the pixel's, vDx and vDy, each at
//! least as long in uv as fLeast (r_ParallaxFilter texels of the height map) and the march's step (vShift / nSteps), its
//! direction kept: the height read as its mean over that (a coarser level), its noise finer than it not cut deep (at a
//! slanting view a height map's fine noise, r_ParallaxDepth deep, stood out of a flat stone as spikes), the blocks and
//! joints kept. Not stretched along the march alone: the anisotropic filter's taps cost more than the coarser level. As
//! they are where they are longer.
void ParallaxMarchGrad(float2 vShift, uint nSteps, float fLeast, inout float2 vDx, inout float2 vDy)
{
  const float fLen = max(fLeast, length(vShift) / max(nSteps, 1u));
  const float lx = length(vDx), ly = length(vDy);
  if (lx < fLen)
    vDx = lx > 1e-12 ? vDx * (fLen / lx) : float2(fLen, 0);
  if (ly < fLen)
    vDy = ly > 1e-12 ? vDy * (fLen / ly) : float2(0, fLen);
}

//! The uv where the view meets the height: along vUV + (h - fRef) vShift (ParallaxShift) from the height h = 1 down to 0 in
//! nSteps steps, the first at or under the height (PARALLAX_HEIGHT, by the gradients vDx and vDy) and the step before by
//! the secant; the top where the height is 1 there. A height fRef all over: vUV.
float2 ParallaxUV(float2 vUV, float2 vDx, float2 vDy, float2 vShift, float fRef, uint nSteps)
{
  const float fStep = 1.0 / max(nSteps, 1u);
  float2 uv = vUV + (1 - fRef) * vShift;
  float fRay = 1, fHeight = PARALLAX_HEIGHT(uv, vDx, vDy);
  if (fHeight >= fRay)
    return uv;
  float fRayBefore = fRay, fHeightBefore = fHeight;
  [loop]
  for (uint i = 1; i <= nSteps; i++)
  {
    fRay = (nSteps - i) * fStep;
    uv = vUV + (fRay - fRef) * vShift;
    fHeight = PARALLAX_HEIGHT(uv, vDx, vDy);
    if (fHeight >= fRay)
      break;
    fRayBefore = fRay;
    fHeightBefore = fHeight;
  }
  // Above the height by d0 at the step before, under it by d1 at this one: the height linear between them.
  const float d0 = fRayBefore - fHeightBefore, d1 = max(fHeight - fRay, 0);
  const float h = fRayBefore - (fRayBefore - fRay) * d0 / max(d0 + d1, 1e-6);
  return vUV + (h - fRef) * vShift;
}

#endif
