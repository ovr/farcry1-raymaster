// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanFFT.hlsli
//	Description: The FFT ocean's math (OceanFFT.hlsl's kernels; the unit
//	tests' TestOcean.hlsl): the sea's spectra at a time, the inverse FFT of
//	a row of them in group shared memory, and a texel of the waves from the
//	fields.
//
//	The spectra (OceanSpectrum.h makes h0 on the CPU): H(k, t) = h0(k)
//	e^(-i theta) + conj(h0(-k)) e^(i theta), theta = 2 pi frac(n tau) (n the
//	texel's steps of 2 pi / the sea's period, tau the time's fraction of
//	it). Eight real fields of it, in four complex ones whose inverse FFTs
//	give two each as their real and imaginary parts (each real field's
//	spectrum is hermitian):
//	  0  Dx + i Dy        the horizontal displacement D = i k / |k| H
//	  1  h + i dDx/dy     the height
//	  2  dh/dx + i dh/dy  its slopes, i k H
//	  3  dDx/dx + i dDy/dy
//	all of them sum F(k) e^(i k.x) over the tile (no 1 / N^2).
//
//	A texel of the waves (OceanWaves): with the displacement (factor
//	lambda) the surface's point x0 goes to X = x0 + lambda D(x0) (a wave A
//	cos(k x0) gives D = -A sin(k x0): Gerstner's trochoid), so the crests,
//	where D converges, get narrow and steep and the troughs wide and
//	flat. The texel X takes the fields at the x0 that goes there (X -
//	lambda D(x0), three steps from x0 = X; bilinear, the tile wrapping) and
//	the slopes of the moved surface, J^-T grad h with J = I + lambda dD/dx0
//	(its determinant at least the least given: where the crest would fold
//	over). It holds (dh/dX, dh/dY, the height times its weight, the
//	squared slope): the layout of the old wave texture's (Water.cpp), in
//	metres per metre; the mean of the last over a mip texel less the square
//	of the mean slope is the variance its waves lost there (LEAN).
//
//	The includer defines OCEAN_FIELDS(c, s, p), field pair s (0: Dx, Dy, h,
//	dDx/dy; 1: dh/dx, dh/dy, dDx/dx, dDy/dy) of cascade c at texel p (0 ..
//	XR_OCEAN_SIZE - 1), before it includes this when it calls OceanWaves.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_OCEANFFT_HLSLI
#define XRENDERD3D12_OCEANFFT_HLSLI

#include "Constants.h"

static const uint OCEAN_N = XR_OCEAN_SIZE;
static const float OCEAN_PI = 3.14159265358979;

float2 OceanMul(float2 a, float2 b)
{
  return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

//! i a.
float2 OceanTimesI(float2 a)
{
  return float2(-a.y, a.x);
}

//! The tile of cascade c (m).
float OceanTile(uint c)
{
  return c == 0 ? XR_OCEAN_TILE0 : c == 1 ? XR_OCEAN_TILE1 : XR_OCEAN_TILE2;
}

//! The wave vector (rad / m) of texel p of a tile fTile metres a side: its frequencies in the FFT's order.
float2 OceanWaveVector(uint2 p, float fTile)
{
  const int2 m = int2(p) - int2(p >= OCEAN_N / 2) * (int)OCEAN_N;
  return (2 * OCEAN_PI / fTile) * float2(m);
}

//! The texel of -k, as the tile wraps.
uint2 OceanOpposite(uint2 p)
{
  return (OCEAN_N - p) & (OCEAN_N - 1);
}

//! The four spectra (the header's) at wave vector k from h0 (its texel: re, im, steps) and h0 of -k, at tau.
void OceanSpectra(float3 h0, float3 h0m, float2 k, float fTau, out float2 C[4])
{
  // frac(n tau) first: n up to some thousands, the phase kept to a fraction of a degree.
  const float fTheta = 2 * OCEAN_PI * frac(h0.z * fTau);
  float s, c;
  sincos(fTheta, s, c);
  const float2 e = float2(c, s);
  const float2 H = OceanMul(h0.xy, float2(e.x, -e.y)) + OceanMul(float2(h0m.x, -h0m.y), e);
  const float fK = length(k);
  if (fK <= 0)
  {
    C[0] = C[1] = C[2] = C[3] = 0;
    return;
  }
  const float2 n = k / fK;
  const float2 iH = OceanTimesI(H);
  C[0] = n.x * iH - n.y * H;                               // i nx H + i (i ny H)
  C[1] = H - (k.x * n.y) * iH;                             // H + i (-kx ky / |k| H)
  C[2] = k.x * iH - k.y * H;                               // i kx H + i (i ky H)
  C[3] = -(k.x * n.x) * H - (k.y * n.y) * iH;              // -kx^2 / |k| H + i (-ky^2 / |k| H)
}

//! The rows being transformed: [buffer][field][element], two buffers taking turns.
groupshared float2 gs_Ocean[2][4][XR_OCEAN_SIZE];

//! The inverse FFT (radix 2, Stockham's: no reordering) of the four rows in gs_Ocean[0], thread j of OCEAN_N / 2 (each a
//! butterfly of each row a stage; after a barrier the caller has put them there). The buffer that holds the result.
uint OceanInverseFFT(uint j)
{
  uint nSrc = 0;
  [unroll]
  for (uint p = 1; p < OCEAN_N; p <<= 1)
  {
    const uint k = j & (p - 1);
    const uint i0 = ((j - k) << 1) + k;
    float s, c;
    sincos(OCEAN_PI * k / p, s, c);
    const float2 w = float2(c, s);
    [unroll]
    for (uint f = 0; f < 4; f++)
    {
      const float2 u0 = gs_Ocean[nSrc][f][j], u1 = OceanMul(w, gs_Ocean[nSrc][f][j + OCEAN_N / 2]);
      gs_Ocean[1 - nSrc][f][i0] = u0 + u1;
      gs_Ocean[1 - nSrc][f][i0 + p] = u0 - u1;
    }
    GroupMemoryBarrierWithGroupSync();
    nSrc = 1 - nSrc;
  }
  return nSrc;
}

//! The caustics: 1 over the water's refractive index (air to water).
static const float CAUSTICS_ETA = 1 / 1.333;

//! The caustics: the sun's ray that crosses the surface where its slopes are s (dh/dx, dh/dy; vSun towards the sun, z > 0),
//! its move along the surface per metre it goes down in the water (Snell's law at the normal (-s, 1)); fLight the sun's
//! light the surface lets in there for its area seen from above, as a share of a level surface's: N.L |(-s, 1)| / L.z = 1
//! - s.L.xy / L.z, a face turned towards the sun more (0 for one turned away; its mean over a tile 1, the slopes' 0).
float2 CausticsRay(float2 s, float3 vSun, out float fLight)
{
  fLight = max(1 - dot(s, vSun.xy) / max(vSun.z, 1e-3), 0);
  const float3 T = refract(-vSun, normalize(float3(-s, 1)), CAUSTICS_ETA);
  return T.z < -1e-3 ? T.xy / -T.z : 0;
}

#ifdef OCEAN_FIELDS

//! Field pair s of cascade c at texel position x (bilinear, the tile wrapping).
float4 OceanFieldsAt(uint c, uint s, float2 x)
{
  const float2 f = floor(x), t = x - f;
  const int2 p = int2(f);
  const uint m = OCEAN_N - 1;
  const float4 a = OCEAN_FIELDS(c, s, uint2(p) & m), b = OCEAN_FIELDS(c, s, uint2(p + int2(1, 0)) & m);
  const float4 d = OCEAN_FIELDS(c, s, uint2(p + int2(0, 1)) & m), e = OCEAN_FIELDS(c, s, uint2(p + int2(1, 1)) & m);
  return lerp(lerp(a, b, t.x), lerp(d, e, t.x), t.y);
}

//! The point x0 of cascade c (texels) that the displacement moves to texel X: fChop the displacement's factor (0: none, X),
//! fLeastJ the least determinant of its Jacobian, fTexel the cascade's texel (m).
float2 OceanSource(uint c, uint2 X, float fChop, float fLeastJ, float fTexel)
{
  float2 x0 = float2(X);
  if (fChop > 0)
  {
    // Newton's steps towards the x0 that the displacement moves to X: r = x0 + fChop D(x0) - X (in texels), its
    // Jacobian J = I + fChop dD/dx0 (symmetric: D is a gradient), its determinant at least fLeastJ.
    [unroll]
    for (int i = 0; i < 3; i++)
    {
      const float4 a = OceanFieldsAt(c, 0, x0), b = OceanFieldsAt(c, 1, x0);
      const float jxx = 1 + fChop * b.z, jyy = 1 + fChop * b.w, jxy = fChop * a.w;
      const float fDet = max(jxx * jyy - jxy * jxy, fLeastJ);
      const float2 r = x0 + (fChop / fTexel) * a.xy - float2(X);
      x0 -= float2(jyy * r.x - jxy * r.y, jxx * r.y - jxy * r.x) / fDet;
    }
  }
  return x0;
}

//! The waves' texel X of cascade c (the header's): fChop the displacement's factor (0: none, the fields at X), fLeastJ the
//! least determinant of its Jacobian, fTexel the cascade's texel (m), fHeightWeight its height's weight.
float4 OceanWaves(uint c, uint2 X, float fChop, float fLeastJ, float fTexel, float fHeightWeight)
{
  const float2 x0 = OceanSource(c, X, fChop, fLeastJ, fTexel);
  const float4 a = OceanFieldsAt(c, 0, x0), b = OceanFieldsAt(c, 1, x0);
  float2 vSlope = b.xy;
  if (fChop > 0)
  {
    const float jxx = 1 + fChop * b.z, jyy = 1 + fChop * b.w, jxy = fChop * a.w;
    const float fDet = max(jxx * jyy - jxy * jxy, fLeastJ);
    vSlope = float2(jyy * vSlope.x - jxy * vSlope.y, jxx * vSlope.y - jxy * vSlope.x) / fDet;
  }
  return float4(vSlope, a.z * fHeightWeight, dot(vSlope, vSlope));
}

//! The whitecaps' foam of a texel fCentre spread over the sea by the share d of it (at most 0.25) to each of its four
//! neighbours and theirs (fAround their sum) to it: the patches a breaking crest leaves grow as they fade (the foam's
//! diffusion, FTCS: none made or lost, the sea's share white kept).
float FoamSpread(float fCentre, float fAround, float d)
{
  return fCentre + d * (fAround - 4 * fCentre);
}

//! The foam's texel X of cascade c (the header's arguments as OceanWaves'): x the share of it white with the whitecaps'
//! foam: where the surface at the point moved there is squeezed below fBreak (the first order of the Jacobian's
//! determinant, 1 + fChop div D: a gaussian, so that its threshold sets the share breaking exactly; the determinant's
//! own second order lifts its lowest values, at 12 m/s only 0.009 % of the long cascade below the threshold of 0.23 %;
//! fInvRamp 1 / the width of the ramp to all white below it) the crest breaks, else the foam of the frame before
//! (fBefore) fades (fKeep of it left; the texel stays at its place on the sea, so the foam stays behind the crest that
//! made it); yz the horizontal displacement there (m: X less it is the point moved there, so that what is drawn there
//! moves with the water), w the height (m).
float4 OceanFoam(uint c, uint2 X, float fChop, float fLeastJ, float fTexel, float fBefore, float fKeep, float fBreak, float fInvRamp)
{
  const float2 x0 = OceanSource(c, X, fChop, fLeastJ, fTexel);
  const float4 a = OceanFieldsAt(c, 0, x0), b = OceanFieldsAt(c, 1, x0);
  const float fBreaking = saturate((fBreak - (1 + fChop * (b.z + b.w))) * fInvRamp);
  return float4(max(fBefore * fKeep, fBreaking), fChop * a.xy, a.z);
}

#endif

#endif
