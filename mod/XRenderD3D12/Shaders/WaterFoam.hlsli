// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterFoam.hlsli
//	Description: The sea's foam (Water.hlsl; the unit tests'
//	TestHeaders.hlsl): how much of the surface is white and the light it
//	sends.
//
//	  - whitecaps: the FFT ocean's foam kernel (OceanFFT.hlsli's OceanFoam)
//	    keeps the share of each texel of the long and the middle cascades
//	    white where their crests break, fading after (a share of the sea
//	    Monahan's of the wind, OceanSpectrum.h); the middle cascade's crests
//	    break more on the long waves' crests (where their slopes add up:
//	    Longuet-Higgins and Stewart's modulation of short waves by long ones);
//	  - the shore: a wave breaks where the water is shallower than its height
//	    over the breaker index (McCowan's 0.78 on the mean, from a wave to the
//	    next and along its crest 0.55-1.0), so the foam reaches out from the
//	    water's edge as far as the waves passing break: further under a crest,
//	    a thin rim in a trough; against rocks, walls and what stands in the
//	    water (where the bottom drops steeply) the foam of the waves running up
//	    them, out to twice their height;
//	  - the surf: the swell (d3d12_WaterSurf) breaks where the water gets
//	    shallower than its height over the breaker index and runs to the
//	    shore as bores, a white roller on each front and the lace it leaves
//	    (SurfFoam);
//	  - the pattern: foam is a lace of bubbles (FoamPattern.h, its values
//	    ranked: uniform 0..1), white where it is above one less the amount
//	    (the amount is the share of the surface white), softened over the
//	    pattern's change across the pixel; where the pixel covers many of its
//	    texels the amount itself;
//	  - the light: a white diffuse layer (FOAM_ALBEDO, fresh foam on sea
//	    water reflects about half to two thirds of the light) under the sun
//	    and the sky, thin foam (a low amount) letting some of the water's
//	    light through.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WATERFOAM_HLSLI
#define XRENDERD3D12_WATERFOAM_HLSLI

//! McCowan's breaker index: a wave breaks where the water is shallower than its height over it.
static const float FOAM_BREAKER_INDEX = 0.78;
//! The albedo of foam on sea water.
static const float FOAM_ALBEDO = 0.6;
//! How far the sun's light wraps round the foam (through the thin walls of its bubbles).
static const float FOAM_WRAP = 0.2;
//! How far out from a rock, a wall or what stands in the water its foam reaches, in the waves' height there (ContactFoam).
static const float FOAM_CONTACT = 2;
//! The breaker index's spread either way about McCowan's (FoamBreakerIndex).
static const float FOAM_INDEX_SPREAD = 0.3;
//! How much of the light under it the thinnest foam hides (a single layer of bubbles: the lace a breaking wave leaves);
//! the thickest (the amount 1) all.
static const float FOAM_THIN = 0.45;

//! The share of the surface white with the foam of waves breaking on the shore: the water fDepth deep (m), the crest
//! fCrest above the sea's level (m; the long waves' recent crests' highest), the sea's significant height fHs (m), the
//! breaker index fIndex there (FoamBreakerIndex). Its height there twice its crest's, at least a quarter of fHs (the thin
//! rim the ripple keeps at the edge): all white at the water's edge, less out to where the water gets deeper than that
//! height over the breaker index, none beyond; as the square of the share of that depth left (the foam the bores carry
//! up the beach gathers at the edge: further out the lace a breaking wave leaves, a quarter at half of it).
float ShoreFoam(float fDepth, float fCrest, float fHs, float fIndex)
{
  const float fBreak = max(max(2 * fCrest, 0.25 * fHs) / max(fIndex, 0.1), 0.02);
  const float s = saturate(1 - fDepth / fBreak);
  return s * s;
}

//! The share of the surface white where the water meets a rock, a wall or what stands in it: the waves run up it and
//! fall back, and the foam they make floats against it (where the bottom drops steeply none breaks on its way there:
//! ShoreFoam is none). Out to FOAM_CONTACT times the waves' height there (as ShoreFoam's: twice the crest fCrest, at
//! least a quarter of the significant height fHs, at least 2.5 cm) along the view's path through the water to what is
//! behind it (fPath, m: across a wall seen at a slant about the distance to it), the square of the share left.
float ContactFoam(float fPath, float fCrest, float fHs)
{
  const float fReach = max(FOAM_CONTACT * max(2 * fCrest, 0.25 * fHs), 0.025);
  const float s = saturate(1 - fPath / fReach);
  return s * s;
}

//! A hash of the lattice point p: 0..1.
float FoamHash(int2 p)
{
  uint h = asuint(p.x) * 0x8da6b343u ^ asuint(p.y) * 0xd8163841u;
  h = h * 747796405u + 2891336453u;
  h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
  return (float)((h >> 22u) ^ h) * (1.0 / 4294967296.0);
}

//! How fast the white roller on a bore's front thins behind it, per period (most of it over the 0.2 of it after the
//! front: a fifth of the zone, about as wide as the bore is high on a beach of 1:10).
static const float FOAM_SURF_ROLLER = 5;
//! The lace a bore leaves behind it (its amount just behind the front) and how fast it fades, per period.
static const float FOAM_SURF_LACE = 0.5;
static const float FOAM_SURF_FADE = 2;
//! The share of the period before a bore's front over which its foam rises (no hard edge in time or across the screen).
static const float FOAM_SURF_FRONT = 0.03;
//! How far shorewards of where the swell breaks the breaking has grown full (in the travel time's share, below).
static const float FOAM_SURF_ONSET = 0.2;

//! The share of the surface white in the surf zone: the swell breaks where the water gets shallower than its height over
//! the breaker index (fBreak, m: that depth) and runs on to the shore as bores, a white roller on each front and the lace
//! it leaves behind fading till the next. The water fDepth deep (m); fCycle the swell's phase there (periods: the time
//! over XR_FOAM_SURF_PERIOD plus an offset along the shore). A front's place is its travel time from the break point,
//! sqrt(fDepth / fBreak) of the whole on a plane beach (the speed of a bore sqrt(g h)), so the fronts follow the depth's
//! lines and run shorewards (XR_FOAM_SURF_BORES of them in the zone); each bore its own strength (0.55-1: a swell's sets); none beyond where it breaks, growing
//! over the first FOAM_SURF_ONSET of the way.
float SurfFoam(float fDepth, float fBreak, float fCycle)
{
  if (fDepth >= fBreak || fBreak <= 0)
    return 0;
  const float fTau = sqrt(saturate(fDepth / fBreak));
  const float x = fCycle + XR_FOAM_SURF_BORES * fTau;
  const float s = frac(x);  // the share of the period since the last bore passed here
  const float n = floor(x);
  const float fLast = 0.55 + 0.45 * FoamHash(int2((int)n, 7));
  const float fNext = 0.55 + 0.45 * FoamHash(int2((int)n + 1, 7));
  const float fBehind = (exp(-FOAM_SURF_ROLLER * s) + FOAM_SURF_LACE * exp(-FOAM_SURF_FADE * s)) * fLast;
  const float fAhead = (1 + FOAM_SURF_LACE) * fNext * saturate((s - (1 - FOAM_SURF_FRONT)) / FOAM_SURF_FRONT);
  return saturate(max(fBehind, fAhead) * saturate((1 - fTau) / FOAM_SURF_ONSET));
}

//! Smooth value noise of p (its lattice 1 apart): 0..1, its mean 0.5.
float FoamValueNoise(float2 p)
{
  const float2 i = floor(p), f = p - i, w = f * f * (3 - 2 * f);
  const int2 q = int2(i);
  return lerp(lerp(FoamHash(q), FoamHash(q + int2(1, 0)), w.x), lerp(FoamHash(q + int2(0, 1)), FoamHash(q + int2(1, 1)), w.x), w.y);
}

//! The breaker index where a wave meets the shore from the noise n (0..1, its mean 0.5): McCowan's 0.78 the mean,
//! FOAM_INDEX_SPREAD either way (from one wave to the next and along a crest it breaks in deeper or shallower water: the
//! beach's slope and the wave's steepness change it, measured from 0.6 to 1.2).
float FoamBreakerIndex(float n)
{
  return FOAM_BREAKER_INDEX * (1 + FOAM_INDEX_SPREAD * (2 * saturate(n) - 1));
}

//! The whitecaps' share of the surface from the long cascade's foam f0 and the middle one's f1, the middle one's more on the
//! long waves' crests: times 1 + the long waves' height fHeight0 over its RMS (1 / fInvRMS0), 0..2 (its mean 1 over a sea
//! whose heights are symmetric about its level).
float WhitecapAmount(float f0, float f1, float fHeight0, float fInvRMS0)
{
  return saturate(f0 + f1 * clamp(1 + fHeight0 * fInvRMS0, 0, 2));
}

//! The pattern's two samples a and b (each uniform 0..1) as one uniform 0..1 value: the distribution of their mean, a
//! triangle, mapped back to uniform by its CDF.
float FoamUniform(float a, float b)
{
  const float v = 0.5 * (a + b);
  return v < 0.5 ? 2 * v * v : 1 - 2 * (1 - v) * (1 - v);
}

//! The share of a pixel white with foam of the amount fAmount (the share of the surface white there): where the pattern's
//! uniform value u is above 1 - fAmount, softened over fWidth (the pattern's change across the pixel); where the pattern is
//! finer than the pixel (fBlur 1) the amount itself. None at no amount, all at 1, its mean over u the amount (but for
//! amounts within half the softening of 0 or 1).
float FoamCover(float fAmount, float u, float fWidth, float fBlur)
{
  const float a = saturate(fAmount);
  const float w = max(fWidth, 0.02);
  const float fSharp = 1 - (1 - saturate((u + a - 1) / w + 0.5) * saturate(2 * a / w)) * saturate(2 * (1 - a) / w);
  return lerp(fSharp, a, saturate(fBlur));
}

//! How much of the water's light the foam over a pixel hides: its share of the pixel white fCover, thin where the amount
//! fAmount is low (FOAM_THIN), all where it is 1.
float FoamOpacity(float fCover, float fAmount)
{
  return fCover * lerp(FOAM_THIN, 1, saturate(fAmount));
}

//! The light the foam sends to the camera (linear): a white diffuse layer under the sun's light vSun (a white face's turned
//! to it) at the cosine of the normal N and the sun's direction L, wrapped a little, in its shadow fShadow, and under the
//! sky's light vSky (its radiance: what a white face under it sends).
float3 FoamLight(float3 N, float3 L, float3 vSun, float fShadow, float3 vSky)
{
  const float fCos = saturate((dot(N, L) + FOAM_WRAP) / (1 + FOAM_WRAP));
  return FOAM_ALBEDO * (vSun * (fCos * fShadow) + vSky);
}

#endif
