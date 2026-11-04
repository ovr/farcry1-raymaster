// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Underwater.hlsli
//	Description: The light in the ocean's water seen from under its surface
//	(Underwater.hlsl with d3d12_WaterUnder; the unit tests' kernels), in
//	linear light: the view's path through the water, the phase function of
//	sea water (molecules' and particles'), and the light the water scatters
//	towards a camera under the surface, the light coming down fading with
//	the depth (WaterVolume.hlsli's model seen from inside), the sun's share
//	of it taken at the segments of the view's ray (its shadow and the
//	waves' caustics there: the shafts of light under the waves).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_UNDERWATER_HLSLI
#define XRENDERD3D12_UNDERWATER_HLSLI

#include "WaterVolume.hlsli"

//! The segments of the view's ray whose sun the light the water scatters takes (a pixel's quad's four pixels four each).
#define XR_UNDER_TAPS 16
//! The share of the light the water scatters that its molecules scatter (Morel's phase function of pure water: 0.06225
//! (1 + 0.835 cos^2) per steradian), the rest its particles' (Henyey and Greenstein's at the mean cosine of Petzold's
//! measurements, 0.924): in the clearest ocean water the molecules scatter about as much as the particles, in coastal
//! water far less.
static const float UNDER_MOLECULAR = 0.5;
static const float UNDER_PARTICLE_G = 0.924;

//! The view's path through the water (m) from a camera fDepth metres under the level to the scene fDist metres away along
//! a direction whose z is fDirZ: all of it down and level; up at most to fCap metres over the level (the surface the water
//! draws there: the crests of the waves; 0 the flat surface).
float UnderPath(float fDist, float fDirZ, float fDepth, float fCap)
{
  return fDirZ > 0 ? min(fDist, (fDepth + fCap) / fDirZ) : fDist;
}

//! The phase function of sea water at the cosine fCos between the light's way and the way it is scattered, 1 the same every
//! way (its mean over all ways is 1): UNDER_MOLECULAR of it the molecules' (Morel), the rest the particles'
//! (Henyey-Greenstein at UNDER_PARTICLE_G, strongly forwards: looking towards the sun the water glows round it).
float UnderPhase(float fCos)
{
  const float g = UNDER_PARTICLE_G, f = 1 + g * g - 2 * g * fCos;
  const float fMolecular = 0.06225 * 4 * 3.14159265 * (1 + 0.835 * fCos * fCos);
  return UNDER_MOLECULAR * fMolecular + (1 - UNDER_MOLECULAR) * (1 - g * g) / (f * sqrt(f));
}

//! The phase function towards a camera looking along D of the sun's light in the water (the sun at L in the air, its rays
//! refracted down into the water: S the way up to it there); 1 with the sun under the horizon.
float UnderSunPhase(float3 L, float3 D)
{
  const float2 h = L.xy / VOLUME_ETA;
  const float3 S = float3(h, sqrt(saturate(1 - dot(h, h))));
  return L.z > 0 ? UnderPhase(dot(S, D)) : 1;
}

//! 1 - exp(-x) for x >= 0, to a float's precision also for small x (its series below 0.01).
float UnderOneLessExp(float x)
{
  return x < 0.01 ? x * (1 - x * (0.5 - x * (1.0 / 6))) : 1 - exp(-x);
}

//! The integral of exp(-k t) from t0 to t1 (t1 - t0 as k goes to 0: the light scattered straight up the ray).
float UnderSegment1(float k, float t0, float t1)
{
  const float d = t1 - t0, x = k * d;
  return exp(-k * t0) * d * (x > 0.01 ? (1 - exp(-x)) / x : 1 - x * (0.5 - x * (1.0 / 6 - x * (1.0 / 24))));
}

float3 UnderSegment(float3 k, float t0, float t1)
{
  return float3(UnderSegment1(k.r, t0, t1), UnderSegment1(k.g, t0, t1), UnderSegment1(k.b, t0, t1));
}

//! Where along the view's ray (m) the share f (0 to 1) of the light the water scatters between the camera and fReach comes
//! from nearer, in the channel whose attenuation along the ray is k: f 1 at fReach; even where the light hardly fades along
//! the ray (k fReach under 0.001: looking up at the surface).
float UnderSegmentAt(float f, float k, float fReach)
{
  const float x = k * fReach;
  if (x < 1e-3)
    return fReach * f;
  // -log(1 - y) by its series where y is small (log's argument near 1 loses the digits).
  const float y = f * UnderOneLessExp(x);
  return min((y < 0.01 ? y * (1 + y * (0.5 + y * (1.0 / 3))) : -log(1 - y)) / k, fReach);
}

//! Where the j-th of the XR_UNDER_TAPS segments ends (j 1 to XR_UNDER_TAPS, 0 the camera): each the same share of that
//! light (the nearer ones shorter), the last at fReach.
float UnderSegmentEnd(uint j, float k, float fReach)
{
  return j >= XR_UNDER_TAPS ? fReach : UnderSegmentAt(j / (float)XR_UNDER_TAPS, k, fReach);
}

//! The light the water scatters towards a camera fDepth metres under the level along a view ray going down at fMu (the cosine
//! of its direction to the vertical down; negative up), from the camera out to fPath: the light coming down, attenuated by
//! exp(-c d) to the depth d of each point (as WaterVolume.hlsli's VolumeInscatter: deep water seen straight down from the
//! surface its colour vDeep), scattered towards the camera and taken by the water on the way back (exp(-c t)). The sun's
//! share fShare of it at each segment j (UnderSegmentEnd out to fReach, the green channel's) times the sun's light v[j] there
//! (1 its mean: 0 in its shadow, the caustics' light), beyond the last segment the last's, and times fPhase (UnderPhase
//! towards the camera); the rest the same every way. fLit the share of the sun's light left (the green channel's, 1 all of
//! it).
float3 UnderInscatter(float3 vDeep, float3 c, float fDepth, float fMu, float fPath, float fReach, float fShare, float fPhase,
                      float v[XR_UNDER_TAPS], out float fLit)
{
  const float3 k = c * (1 + fMu);
  const float3 J = 2 * c * vDeep * exp(-c * fDepth);
  float3 vAll = 0, vSun = 0;
  float fWeight = 0, fSum = 0, t0 = 0;
  [unroll]
  for (uint j = 0; j < XR_UNDER_TAPS; j++)
  {
    const float t1 = UnderSegmentEnd(j + 1, k.g, fReach);
    const float3 w = UnderSegment(k, t0, t1);
    vAll += w;
    vSun += w * v[j];
    fWeight += w.g;
    fSum += w.g * v[j];
    t0 = t1;
  }
  const float3 w = UnderSegment(k, fReach, max(fPath, fReach));
  vAll += w;
  vSun += w * v[XR_UNDER_TAPS - 1];
  fWeight += w.g;
  fSum += w.g * v[XR_UNDER_TAPS - 1];
  fLit = fWeight > 0 ? fSum / fWeight : 1;
  return J * ((1 - fShare) * vAll + fShare * fPhase * vSun);
}

//! The light in the water over a surface fDist metres along the view's ray D from a camera fCameraDepth metres under the
//! level, the surface itself fDepth metres under it (0 over it): the share T of its light left, along the view's path through
//! the water (UnderPath with fCap) and along the sun's refracted way (L the sun) down to its depth, as Underwater.hlsl's; the
//! light the water scatters towards the camera in front of it returned, as UnderInscatter's with the sun's light its mean all
//! along (v 1: the shafts are the view's taps alone).
float3 UnderFog(float3 vDeep, float3 c, float fCameraDepth, float3 D, float fDist, float fDepth, float fCap, float3 L, float fShare,
                out float3 T)
{
  const float fPath = UnderPath(fDist, D.z, fCameraDepth, fCap);
  T = exp(-c * (fPath + fDepth / VolumeRefractedCos(saturate(L.z))));
  const float3 J = 2 * c * vDeep * exp(-c * fCameraDepth);
  return J * UnderSegment(c * (1 - D.z), 0, fPath) * (1 - fShare + fShare * UnderSunPhase(L, D));
}

//! The light of the ocean's surface seen from below at the cosine fCos of the view's ray to the vertical (up), Snell's window
//! fWindow (SnellWindow) of the light from the air vAbove through it, the rest the water below it mirrors: the light deep water
//! scatters up towards the surface along the mirrored ray (VolumeInscatter's 2 / (1 + mu) of its colour vDeep).
float3 UnderSurface(float3 vDeep, float3 vAbove, float fWindow, float fCos)
{
  return lerp(vDeep * 2 / (1 + saturate(fCos)), vAbove, fWindow);
}

#endif
