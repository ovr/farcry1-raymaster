// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanGrid.hlsli
//	Description: The ocean's grid (d3d12_WaterGeometry; OceanGrid.h,
//	OceanGrid.cpp, Water.hlsl's vertex program): the spacing of its
//	vertices, the mip level of a texture of the waves that leaves none
//	shorter than the grid carries, and how much of the waves' height a
//	vertex takes: limited by the depth under the level, none under the
//	bodies in the water, none at a camera at the water, none far off. Pure
//	functions (the unit tests run them, GridTests.cpp).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_OCEANGRID_HLSLI
#define XRENDERD3D12_OCEANGRID_HLSLI

#include "Constants.h"

//! The angle of a segment (OceanGrid::SEGMENT).
#define GRID_SEGMENT (6.28318530718 / XR_GRID_SEGMENTS)
//! Beyond XR_GRID_DISPLACED each ring this much further out than the one before (OceanGrid::FAR_GROWTH).
#define GRID_FAR_GROWTH 1.25
//! The most height of a crest over the depth under the level, and of a trough under it: half McCowan's breaker index (a
//! wave H high breaks where the water is H / 0.78 deep; Foam_Shore's index).
#define GRID_BREAKER 0.39
//! Around a camera at the water the surface flat out to GRID_CAMERA_NEAR (m), all of the waves from GRID_CAMERA_FAR on (the
//! game takes the camera as under the water by the level alone: a crest over it, a trough under it, would cut the view in
//! two), the waves all there from GRID_CAMERA_HEIGHT over or under the level on, none within half of it.
#define GRID_CAMERA_NEAR 2.0
#define GRID_CAMERA_FAR 8.0
#define GRID_CAMERA_HEIGHT 1.5
//! A crest stays GRID_CAMERA_CLEARANCE (m: more than the near plane's 0.25) under a camera over the water there, a trough
//! as far over one under it, the limit rising GRID_CAMERA_RISE m a metre further out (a crest a few metres off may rise
//! over the camera: seen from a boat in a high sea).
#define GRID_CAMERA_CLEARANCE 0.3
#define GRID_CAMERA_RISE 0.5
//! The ocean's waves come back over GRID_BODY_MARGIN (m) beyond a body's box (the game floats it on the level: the waves
//! would run through its hull).
#define GRID_BODY_MARGIN 1.0
//! A crest hides all of the scene behind it from GRID_HIDDEN (m) over the depth of its height under the level on
//! (GridHiddenShare).
#define GRID_HIDDEN 0.5

//! The grid's spacing at the radius r (m), the larger of its rings' and its segments' (OceanGrid::Spacing).
float GridSpacing(float r)
{
  return r < XR_GRID_DISPLACED ? max(XR_GRID_SPACING, r * GRID_SEGMENT) : r * (GRID_FAR_GROWTH - 1);
}

//! The mip level of a texture of fTexel (m) that leaves no waves shorter than twice the grid's fSpacing: each of its texels
//! a cell of the grid at least.
float GridLod(float fSpacing, float fTexel)
{
  return max(log2(fSpacing / fTexel), 0);
}

//! The waves' height h (m) where the water is fDepth deep under the level: a crest at most GRID_BREAKER of the depth above
//! the level, a trough as far under it (a smooth limit: h itself where it is far less), none on the shore (fDepth <= 0).
float GridShore(float h, float fDepth)
{
  const float m = GRID_BREAKER * max(fDepth, 0);
  return m > 0 ? h * rsqrt(1 + (h * h) / (m * m)) : 0;
}

//! The share of the ocean's waves left at P (m) by a body: c xy its box's centre (m), zw the cos and sin of its axes' turn;
//! a xy its box's half sides (m). None in its box's ellipse, all GRID_BODY_MARGIN beyond it (the distance out of the
//! ellipse to first order, |q / a|^2 - 1 over its gradient's length).
float GridBodyShare(float2 P, float4 c, float4 a)
{
  const float2 d = P - c.xy;
  const float2 q = float2(d.x * c.z + d.y * c.w, d.y * c.z - d.x * c.w) / a.xy;
  const float f = dot(q, q) - 1;
  if (f <= 0)
    return 0;
  const float fOut = f / (2 * length(q / a.xy));
  return smoothstep(0, GRID_BODY_MARGIN, fOut);
}

//! The share of the waves left fR (m) across the water from a camera fH (m) over (or under) the level.
float GridCameraShare(float fR, float fH)
{
  return max(smoothstep(GRID_CAMERA_NEAR, GRID_CAMERA_FAR, fR), smoothstep(0.5 * GRID_CAMERA_HEIGHT, GRID_CAMERA_HEIGHT, abs(fH)));
}

//! The waves' height h (m) fR (m) across the water from a camera fH (m) over the level (under it: < 0): on the camera's
//! side of the level at most GRID_CAMERA_CLEARANCE short of it there, GRID_CAMERA_RISE m more a metre further out (a
//! smooth limit as GridShore's), so that no crest reaches over the camera, nor a trough under one below the water.
float GridCameraCap(float h, float fR, float fH)
{
  const float s = fH >= 0 ? 1 : -1;
  const float m = max(abs(fH) - GRID_CAMERA_CLEARANCE, 0) + GRID_CAMERA_RISE * fR;
  const float t = h * s;
  return t > 0 ? s * (m > 0 ? t * rsqrt(1 + (t * t) / (m * m)) : 0) : h;
}

//! The share of the scene behind the ocean's grid, at the height z (m), that the water of the surface at the height
//! fSurface (m) in front of it hides, the level fLevel (m): the view's ray refracted into a wave goes down into the water
//! (at least 41 degrees under the surface where it enters, Snell's window), under the trough beyond the crest (but at the
//! top of a steep one), not on through the crest to what stands over the water there (a boat's side, the shore, the sky:
//! drawn as seen through the water down to its height, they showed through the waves as through glass). None of the scene
//! under the level by more than the crest's height over it (none under the flat disc, none in shallow water: a crest at
//! most GRID_BREAKER of the depth), all of it from GRID_HIDDEN higher.
float GridHiddenShare(float z, float fSurface, float fLevel)
{
  return saturate((z - fLevel + max(fSurface - fLevel, 0)) * (1.0 / GRID_HIDDEN));
}

//! The share of the waves left fR (m) from the grid's centre: all to half of XR_GRID_DISPLACED, none from it on.
float GridFarShare(float fR)
{
  return 1 - smoothstep(0.5 * XR_GRID_DISPLACED, XR_GRID_DISPLACED, fR);
}

#ifdef GRID_DISC_WORD
//! The game's disc's vertex alpha (its fade of the water towards the shore and with the distance) at vLocal (m across the
//! water from its centre) for the ocean's grid, from its table of nRings rings (OceanGrid::DiscTable; GRID_DISC_WORD(i)
//! its word i as a float): the rings' radii (the first 0), then each ring's XR_GRID_DISC_SEGMENTS alphas from +x
//! counterclockwise. Bilinear between the rings and the segments around vLocal, as the disc's triangles between its
//! vertices; beyond the last ring that ring's.
float GridDiscAlpha(float2 vLocal, uint nRings)
{
  const float r = length(vLocal);
  // The last ring of all but the last with a radius not over r (the first 0): among n from k0 on.
  uint k0 = 0, n = nRings - 1;
  while (n > 1)
  {
    const uint h = n >> 1;
    k0 = GRID_DISC_WORD(k0 + h) <= r ? k0 + h : k0;
    n -= h;
  }
  const uint k1 = min(k0 + 1, nRings - 1);
  const float r0 = GRID_DISC_WORD(k0), r1 = GRID_DISC_WORD(k1);
  const float t = saturate((r - r0) / max(r1 - r0, 1e-6));
  float u = atan2(vLocal.y, vLocal.x) * (XR_GRID_DISC_SEGMENTS / 6.28318530718);
  u = u < 0 ? u + XR_GRID_DISC_SEGMENTS : u;
  const uint s0 = min((uint)u, XR_GRID_DISC_SEGMENTS - 1), s1 = (s0 + 1) % XR_GRID_DISC_SEGMENTS;
  const float f = saturate(u - s0);
  const uint a = nRings;
  const float a0 = lerp(GRID_DISC_WORD(a + k0 * XR_GRID_DISC_SEGMENTS + s0), GRID_DISC_WORD(a + k0 * XR_GRID_DISC_SEGMENTS + s1), f);
  const float a1 = lerp(GRID_DISC_WORD(a + k1 * XR_GRID_DISC_SEGMENTS + s0), GRID_DISC_WORD(a + k1 * XR_GRID_DISC_SEGMENTS + s1), f);
  return lerp(a0, a1, t);
}

#ifndef GRID_DISC_WORDS2
#define GRID_DISC_WORDS2(i) float2(GRID_DISC_WORD(i), GRID_DISC_WORD((i) + 1))
#endif

//! GridDiscAlpha from the frame's table of it by radius and segment (OceanGrid::AlphaTable at the word nFirst, fScale
//! its rows a doubling of 1 + r / XR_GRID_ALPHA_R0): bilinear between the rows and the segments around vLocal, as the
//! forward grid's between its vertices; beyond the last row its. The deferred ocean's, a pixel's (two loads; GridDiscAlpha's
//! search among the rings first cost it 0.04-0.12 ms at 4K).
float GridDiscAlphaTable(float2 vLocal, uint nFirst, float fScale)
{
  const float v = min(log2(1 + length(vLocal) * (1.0 / XR_GRID_ALPHA_R0)) * fScale, XR_GRID_ALPHA_ROWS - 1);
  const uint j = min((uint)v, XR_GRID_ALPHA_ROWS - 2);
  float u = atan2(vLocal.y, vLocal.x) * (XR_GRID_DISC_SEGMENTS / 6.28318530718);
  u = u < 0 ? u + XR_GRID_DISC_SEGMENTS : u;
  const uint s = min((uint)u, XR_GRID_DISC_SEGMENTS - 1);
  const float f = saturate(u - s);
  const uint a = nFirst + j * (XR_GRID_DISC_SEGMENTS + 1) + s;
  const float2 a0 = GRID_DISC_WORDS2(a), a1 = GRID_DISC_WORDS2(a + XR_GRID_DISC_SEGMENTS + 1);
  return lerp(lerp(a0.x, a0.y, f), lerp(a1.x, a1.y, f), saturate(v - j));
}

//! The radius (m) of the frame's ring k of the grid (OceanGrid::FrameRings; GRID_DISC_WORD(nFirst + k), nRings of them)
//! and the spacing (m) out to the next one (none from the last).
float2 GridRing(uint k, uint nFirst, uint nRings)
{
  const float r = GRID_DISC_WORD(nFirst + k);
  return float2(r, GRID_DISC_WORD(nFirst + min(k + 1, nRings - 1)) - r);
}
#endif

#endif
