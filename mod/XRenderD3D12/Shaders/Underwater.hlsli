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
//	waves' caustics there: the shafts of light under the waves). With
//	d3d12_WaterUnderClear the light the water scatters from the scene's sun
//	and sky (UnderClearSource: its scattering, UnderScattering, the
//	molecules' and the particles'), else as the water seen from above
//	shows it (UnderDeepSource: twice its colour, the frames before).
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
//! The scalar irradiance just under the surface of a uniform sky's light over its light on a face facing up in the air: the
//! surface lets 0.934 of it through (Fresnel's transmission over the sky's hemisphere), within Snell's window, where its
//! mean cosine is 0.83 (a uniform radiance over the cone of 48.6 degrees round the vertical).
static const float UNDER_SKY_IN = 0.934 / 0.83;
//! d3d12_WaterUnderClear: the light coming down times 1 plus this, the light that comes back up added (from the floor and
//! the water below, scattered more than once: a sandy floor a few metres down sends up about as much as comes down; in deep
//! clear water far less).
static const float UNDER_CLEAR_UP = 1;
//! Pure water's scattering per metre in the red, green and blue channels (its molecules': 0.00288 (500 / wavelength)^4.32,
//! Morel 1974, at 610, 550 and 470 nm).
static const float3 UNDER_PURE_SCATTERING = float3(0.00122, 0.00191, 0.00376);
//! The particles' scattering in each channel over its green's: as 1 / wavelength (Morel's), at 610, 550 and 470 nm.
static const float3 UNDER_PARTICLE_SPECTRUM = float3(550.0 / 610, 1, 550.0 / 470);

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

//! The light the water scatters a metre at the surface towards each way (times the phase function's share there, 1 its mean;
//! fading as exp(-c d) with the depth d) as the water seen from above shows it (d3d12_WaterUnderClear 0, the frames before):
//! 2 c vDeep, so that deep water seen straight down from the surface is its fog volume's colour vDeep (WaterVolume.hlsli's
//! VolumeInscatter).
float3 UnderDeepSource(float3 vDeep, float3 c)
{
  return 2 * c * vDeep;
}

//! The light coming down just under the surface over 4 pi (a steradian's share of its scalar irradiance, the light reaching
//! a point from every way), in the units of the light of a white face (E / pi): the sun's vSun (a white face turned to it)
//! through the flat surface at the height fSunZ of its direction (Fresnel's transmission, SnellWindow of its refracted ray;
//! its beam's light over a horizontal face's, cos in the air over cos in the water: none under the horizon), the sky's vSky
//! (a white face facing up) UNDER_SKY_IN times; the light that comes back up from below UNDER_CLEAR_UP of it.
float3 UnderDownLight(float3 vSun, float3 vSky, float fSunZ)
{
  const float fCos = saturate(fSunZ), fWater = VolumeRefractedCos(fCos);
  return (1 + UNDER_CLEAR_UP) * 0.25 * (vSun * (SnellWindow(fWater) * fCos / fWater) + UNDER_SKY_IN * vSky);
}

//! Sea water's scattering per metre in each channel from its attenuation c (VolumeAttenuation): pure water's own
//! (UNDER_PURE_SCATTERING) and the particles': in green all the water takes beyond pure water's (VOLUME_PURE_WATER's
//! absorption and its scattering: clear water's particles absorb little there), in red and blue as much as green's over
//! the wavelength (UNDER_PARTICLE_SPECTRUM), at most what each takes beyond pure water's (the rest of blue's absorbed by
//! what is dissolved in the water and the plankton).
float3 UnderScattering(float3 c)
{
  const float3 vMore = max(c - VOLUME_PURE_WATER - UNDER_PURE_SCATTERING, 0);
  return UNDER_PURE_SCATTERING + min(vMore.g * UNDER_PARTICLE_SPECTRUM, vMore);
}

//! d3d12_WaterUnderClear: the light the water scatters a metre at the surface towards each way (as UnderDeepSource's) from
//! the light coming down vDown (UnderDownLight): the water's scattering (UnderScattering) times vDown. Clear water scatters
//! little of what it takes (red hardly any of it), so the water far off is a few times darker than a floor the sun lights,
//! darker with the depth and looking down.
float3 UnderClearSource(float3 c, float3 vDown)
{
  return UnderScattering(c) * vDown;
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

//! The segment of the view's ray whose sun the pixel at vPixel (SV_Position) takes in d3d12_WaterUnderShare's taps pass
//! (Underwater.hlsl's PSTaps): its place modulo 4, x + 4 y (any 4 x 4 window holds each once).
uint UnderBlockSegment(float2 vPixel)
{
  return (uint)vPixel.x % 4 + 4 * ((uint)vPixel.y % 4);
}

//! v turned by n places: u[i] = v[(i - n) mod 4], two selects a component.
float4 UnderTurn(float4 v, uint n)
{
  v = (n & 1) ? v.wxyz : v;
  return (n & 2) ? v.zwxy : v;
}

//! The taps of a 4 x 4 window starting at the pixel vStart (G[b][a] the pixel vStart + (a, b)) in the order of their
//! segments (UnderBlockSegment): the texel (a, b) is the segment ((x + a) mod 4) + 4 ((y + b) mod 4), so each row's columns
//! turned by x mod 4 and the rows by y mod 4 (selects, no indexing by the pixel: XR_UNDER_TAPS 16).
void UnderWindowOrder(float4 G[4], uint2 vStart, out float v[XR_UNDER_TAPS])
{
  const uint2 n = vStart % 4;
  const float4 R0 = UnderTurn(G[0], n.x), R1 = UnderTurn(G[1], n.x), R2 = UnderTurn(G[2], n.x), R3 = UnderTurn(G[3], n.x);
  const bool bOne = (n.y & 1) != 0, bTwo = (n.y & 2) != 0;
  const float4 V0 = bOne ? R3 : R0, V1 = bOne ? R0 : R1, V2 = bOne ? R1 : R2, V3 = bOne ? R2 : R3;
  const float4 W0 = bTwo ? V2 : V0, W1 = bTwo ? V3 : V1, W2 = bTwo ? V0 : V2, W3 = bTwo ? V1 : V3;
  [unroll]
  for (uint i = 0; i < 4; i++)
  {
    v[i] = W0[i];
    v[4 + i] = W1[i];
    v[8 + i] = W2[i];
    v[12 + i] = W3[i];
  }
}

//! The light the water scatters towards a camera fDepth metres under the level along a view ray going down at fMu (the cosine
//! of its direction to the vertical down; negative up), from the camera out to fPath: the light it scatters a metre at the
//! surface vSource (UnderDeepSource or UnderClearSource), fading by exp(-c d) to the depth d of each point, scattered towards
//! the camera and taken by the water on the way back (exp(-c t)). The sun's
//! share fShare of it at each segment j (UnderSegmentEnd out to fReach, the green channel's) times the sun's light v[j] there
//! (1 its mean: 0 in its shadow, the caustics' light), beyond the last segment the last's, and times fPhase (UnderPhase
//! towards the camera); the rest the same every way. fLit the share of the sun's light left (the green channel's, 1 all of
//! it).
float3 UnderInscatter(float3 vSource, float3 c, float fDepth, float fMu, float fPath, float fReach, float fShare, float fPhase,
                      float v[XR_UNDER_TAPS], out float fLit)
{
  const float3 k = c * (1 + fMu);
  const float3 J = vSource * exp(-c * fDepth);
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

//! Below this attenuation along the reach (k fReach) in its least channel UnderInscatterClosed weighs the segments one by
//! one: the differences of the light at the segments' ends lose the digits there (looking up, a surface within half a metre).
static const float UNDER_CLOSED_LEAST = 0.05;

//! UnderInscatter's light (the same to a float's rounding, some 1e-5 of it) with the segments' weights in closed form: in green
//! each segment by its making the same share 1 / XR_UNDER_TAPS of that channel's light out to fReach; in red and blue the light
//! at the j-th end u^(k / k.g), u = exp(-k.g t) = 1 - (j / XR_UNDER_TAPS) (1 - exp(-k.g fReach)), a segment's weight the
//! difference of its ends' over k (one log and two exps an end in place of six exps and three divisions a segment); every way
//! the same (vAll) the closed form out to fPath. Where k fReach of a channel is under UNDER_CLOSED_LEAST red's and blue's
//! weights segment by segment, the light at a segment's start times its length times (1 - exp(-k d)) / (k d), the start's
//! light carried on by exp(-k d) (two exps a segment), green's as above or, where UnderSegmentAt's segments are the same
//! length, each the last's times exp(-k.g d).
float3 UnderInscatterClosed(float3 vSource, float3 c, float fDepth, float fMu, float fPath, float fReach, float fShare, float fPhase,
                            float v[XR_UNDER_TAPS], out float fLit)
{
  const float3 k = c * (1 + fMu);
  const float3 J = vSource * exp(-c * fDepth);
  // The segments' light times their sun's (vSeg) and green's weights (fSeg).
  float3 vSeg = 0;
  float fSeg = 0;
  [branch]
  if (min(k.r, min(k.g, k.b)) * fReach >= UNDER_CLOSED_LEAST)
  {
    const float fStep = UnderOneLessExp(k.g * fReach) / XR_UNDER_TAPS;
    const float2 vPower = k.rb / k.g;
    const float2 vLast = exp(-k.rb * fReach);
    float2 vStart = 1, vRB = 0;
    float fSum = 0;
    [unroll]
    for (uint j = 0; j < XR_UNDER_TAPS; j++)
    {
      const float2 vEnd = j + 1 < XR_UNDER_TAPS ? exp2(vPower * log2(1 - (j + 1) * fStep)) : vLast;
      vRB += (vStart - vEnd) * v[j];
      fSum += v[j];
      vStart = vEnd;
    }
    const float fGreen = fStep / k.g;
    vSeg = float3(vRB.x / k.r, fGreen * fSum, vRB.y / k.b);
    fSeg = fGreen * XR_UNDER_TAPS;
  }
  else
  {
    // Green's weights each segment's same share, or where UnderSegmentAt's segments are the same length (the light hardly
    // fading along the ray) each the last's times exp(-k.g d).
    float fGreen, fFade;
    if (k.g * fReach < 1e-3)
    {
      const float d = fReach / XR_UNDER_TAPS;
      fGreen = UnderSegment1(k.g, 0, d);
      fFade = exp(-k.g * d);
    }
    else
    {
      fGreen = UnderSegment1(k.g, 0, fReach) / XR_UNDER_TAPS;
      fFade = 1;
    }
    float2 vStart = 1;
    float t0 = 0;
    [unroll]
    for (uint j = 0; j < XR_UNDER_TAPS; j++)
    {
      const float t1 = UnderSegmentEnd(j + 1, k.g, fReach), d = t1 - t0;
      const float2 x = k.rb * d, e = exp(-x);
      const float2 s = select(x > 0.01, (1 - e) / max(x, 1e-30), 1 - x * (0.5 - x * (1.0 / 6 - x * (1.0 / 24))));
      vSeg.xz += vStart * d * s * v[j];
      vSeg.y += fGreen * v[j];
      fSeg += fGreen;
      vStart *= e;
      fGreen *= fFade;
      t0 = t1;
    }
  }
  // Beyond the last segment the last's sun.
  const float3 vTail = UnderSegment(k, fReach, max(fPath, fReach));
  const float3 vSun = vSeg + vTail * v[XR_UNDER_TAPS - 1];
  const float3 vAll = UnderSegment(k, 0, max(fPath, fReach));
  const float fWeight = fSeg + vTail.g;
  fLit = fWeight > 0 ? vSun.g / fWeight : 1;
  return J * ((1 - fShare) * vAll + fShare * fPhase * vSun);
}

//! The light in the water over a surface fDist metres along the view's ray D from a camera fCameraDepth metres under the
//! level, the surface itself fDepth metres under it (0 over it): the share T of its light left, along the view's path through
//! the water (UnderPath with fCap) and along the sun's refracted way (L the sun) down to its depth, as Underwater.hlsl's; the
//! light the water scatters towards the camera in front of it returned, as UnderInscatter's (vSource its light a metre at the
//! surface) with the sun's light its mean all along (v 1: the shafts are the view's taps alone).
float3 UnderFog(float3 vSource, float3 c, float fCameraDepth, float3 D, float fDist, float fDepth, float fCap, float3 L, float fShare,
                out float3 T)
{
  const float fPath = UnderPath(fDist, D.z, fCameraDepth, fCap);
  T = exp(-c * (fPath + fDepth / VolumeRefractedCos(saturate(L.z))));
  const float3 J = vSource * exp(-c * fCameraDepth);
  return J * UnderSegment(c * (1 - D.z), 0, fPath) * (1 - fShare + fShare * UnderSunPhase(L, D));
}

//! The light of the ocean's surface seen from below at the cosine fCos of the view's ray to the vertical (up), Snell's window
//! fWindow (SnellWindow) of the light from the air vAbove through it, the rest the water below it mirrors: the light deep water
//! scatters up towards the surface along the mirrored ray (VolumeInscatter's 2 / (1 + mu) of its colour vDeep; with
//! d3d12_WaterUnderClear 0, the frames before).
float3 UnderSurface(float3 vDeep, float3 vAbove, float fWindow, float fCos)
{
  return lerp(vDeep * 2 / (1 + saturate(fCos)), vAbove, fWindow);
}

//! d3d12_WaterUnderClear: the light the water below the surface sends up along the mirrored ray (down at fCos, the cosine of
//! the view's ray to the vertical): what it scatters towards the surface all along that ray, from the light it scatters a
//! metre at the surface vSource fading as exp(-c d) with the depth, taken by the water on the way up (exp(-c t)), fScatter
//! the phase function's share towards it (1 - fShare + fShare UnderSunPhase): vSource fScatter / (c (1 + fCos)). The same as
//! the view sees along a level ray at the surface's depth: at the horizon the surface beyond Snell's window and the water
//! below it meet.
float3 UnderClearBelow(float3 vSource, float3 c, float fCos, float fScatter)
{
  return vSource * fScatter / (c * (1 + saturate(fCos)));
}

#endif
