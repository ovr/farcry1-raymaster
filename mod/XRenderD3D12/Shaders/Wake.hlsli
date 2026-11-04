// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Wake.hlsli
//	Description: The wakes' math (Wake.hlsl's kernels; the unit tests'
//	TestHeaders.hlsl): the window's cells, the bodies' displacement, the
//	waves' step in the wave numbers' space and the FFT of a row in group
//	shared memory.
//
//	The waves (Tessendorf's eWave, "eWave: Using an Exponential Solver on
//	the iWave Problem"): linear waves of deep water, the height h and the
//	velocity potential phi at the surface; in the wave numbers' space
//	dh/dt = |k| phi, dphi/dt = -g h, so a = h + i sqrt(|k| / g) phi turns
//	as exp(-i w t), w = sqrt(g |k|): the step exact for any length. The
//	window holds q, the inverse FFT of a: its real part is h (the parts of
//	a for k and -k give h's hermitian spectrum), its imaginary part the
//	potential filtered by sqrt(|k| / g) (real too). A step: the bodies'
//	displacement added to h (the real part), the sponge at the window's
//	edge, the FFT, the turn of each wave number with its damping, the
//	inverse FFT.
//
//	A body moving through the surface pushes the water aside: its depth's
//	displacement S (an ellipse's, deepest in its middle) moves with it, and
//	each step adds the change of -S to the height (the volume it frees
//	behind, the volume it takes ahead): a body at rest makes no waves, one
//	moving at U the wake whose waves move with it, w = k.U (at those waves
//	the source as the hydrostatic pressure of its depth, rho g S: Michell's
//	thin ship's sources at the surface), in deep water Kelvin's wedge of
//	19.47 degrees whatever its speed.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WAKE_HLSLI
#define XRENDERD3D12_WAKE_HLSLI

#include "Constants.h"

static const uint WAKE_N = XR_WAKE_SIZE;
static const float WAKE_PI = 3.14159265358979;
static const float WAKE_G = 9.81;

float2 WakeMul(float2 a, float2 b)
{
  return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

//! The world cell of texel t (along an axis) in the window around cell c: the one of its index modulo the window's size.
int WakeCell(uint t, int c)
{
  const int nLow = c - (int)(WAKE_N / 2);
  return nLow + (int)((uint)((int)t - nLow) & (WAKE_N - 1));
}

//! The middle of texel t's cell (along an axis) from the window's centre point (m) in the window around cell c.
float WakeOffset(uint t, int c)
{
  return (WakeCell(t, c) - c + 0.5) * XR_WAKE_CELL;
}

//! The cells from texel p's cell in to the window's edge (around cell c).
float WakeEdgeCells(uint2 p, int2 c)
{
  const float2 d = abs(float2(WakeCell(p.x, c.x) - c.x, WakeCell(p.y, c.y) - c.y) + 0.5);
  return WAKE_N / 2 - max(d.x, d.y);
}

//! The share the sponge leaves of the waves at fEdge cells in from the window's edge (fWidth its width, fMost its most rate
//! times the step): its rate grows as the square towards the edge, so that the waves fade before they wrap (hardly
//! reflected by the slow rise).
float WakeSponge(float fEdge, float fWidth, float fMost)
{
  const float f = saturate(1 - fEdge / fWidth);
  return exp(-fMost * f * f);
}

//! The shape of a body's displacement at X (m from the window's centre point, as the pose's): vPose xy its centre, zw the
//! cos and sin of its axes' turn; vShape xy 1 / its half axes. An ellipse, 1 in its middle and smooth to its edge,
//! (1 - r^2)^2.
float WakeShape(float2 X, float4 vPose, float4 vShape)
{
  const float2 d = X - vPose.xy;
  const float2 l = float2(dot(d, vPose.zw), dot(d, float2(-vPose.w, vPose.z))) * vShape.xy;
  const float r2 = dot(l, l);
  return r2 < 1 ? (1 - r2) * (1 - r2) : 0;
}

//! The depth of its displacement there (m; vShape.z its depth).
float WakeDepth(float2 X, float4 vPose, float4 vShape)
{
  return vShape.z * WakeShape(X, vPose, vShape);
}

//! The wave vector (rad / m) of texel p of the window's spectrum: the frequencies in the FFT's order.
float2 WakeWaveVector(uint2 p)
{
  const int2 m = int2(p) - int2(p >= WAKE_N / 2) * (int)WAKE_N;
  return (2 * WAKE_PI / (WAKE_N * XR_WAKE_CELL)) * float2(m);
}

//! The turn and damping of a wave number over a step (the header's a(t + dt) = a(t) exp(-i w dt)): dt the step, fKeep the
//! share the uniform damping leaves, fShortRate the shortest waves' damping's rate times the step (as (k / k_N)^4, k_N =
//! pi / the cell, along an axis). None for k = 0 (the window's mean height stays 0).
float2 WakeTurn(float2 k, float dt, float fKeep, float fShortRate)
{
  const float fK = length(k);
  if (fK <= 0)
    return 0;
  const float fW = sqrt(WAKE_G * fK);
  const float n = fK * (XR_WAKE_CELL / WAKE_PI);
  float s, c;
  sincos(fW * dt, s, c);
  return float2(c, -s) * (fKeep * exp(-fShortRate * n * n * n * n));
}

//! The row being transformed, two buffers taking turns.
groupshared float2 gs_Wake[2][XR_WAKE_SIZE];

//! The FFT (radix 2, Stockham's: no reordering) of the row in gs_Wake[nSrc], thread j of WAKE_N / 2 (each a butterfly a
//! stage; after a barrier the caller has put it there): fSign -1 forward (exp(-i ...)), 1 inverse, no 1 / N. The buffer
//! that holds the result.
uint WakeFFT(uint j, float fSign, uint nSrc)
{
  [unroll]
  for (uint p = 1; p < WAKE_N; p <<= 1)
  {
    const uint k = j & (p - 1);
    const uint i0 = ((j - k) << 1) + k;
    float s, c;
    sincos(WAKE_PI * k / p, s, c);
    const float2 w = float2(c, fSign * s);
    const float2 u0 = gs_Wake[nSrc][j], u1 = WakeMul(w, gs_Wake[nSrc][j + WAKE_N / 2]);
    gs_Wake[1 - nSrc][i0] = u0 + u1;
    gs_Wake[1 - nSrc][i0 + p] = u0 - u1;
    GroupMemoryBarrierWithGroupSync();
    nSrc = 1 - nSrc;
  }
  return nSrc;
}

//! The foam's texel of the view: fBefore the frame before's there (fAround its four neighbours' sum), fKeep the share left of
//! it, fSpread the share spread to each neighbour; fMade the share white the frame makes there (the bodies', the waves
//! breaking): that much at least, not more each frame (a hull's white water a lace of bubbles, not white all over).
float WakeFoam(float fBefore, float fAround, float fKeep, float fSpread, float fMade)
{
  const float f = (fBefore + fSpread * (fAround - 4 * fBefore)) * fKeep;
  return saturate(max(f, fMade));
}

#endif
