// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Fxaa.hlsli
//	Description: FXAA (d3d12_AA fxaa, PostAA.cpp): fast approximate
//	anti-aliasing of the finished picture, the algorithm of FXAA 3.11's
//	quality path (Timothy Lottes, NVIDIA) written from its published
//	description. For a pixel:
//	  - its luma and its 4 neighbours': where their range is below the
//	    edge thresholds (relative to the brightest, and absolute) there is
//	    no edge, the pixel stays;
//	  - the edge's direction from the second differences of the 3 x 3
//	    lumas across rows and down columns, and its side: the neighbour
//	    across it with the steeper step;
//	  - along the edge, both ways, the luma halfway to that side (the mean
//	    of the two rows or columns, exact) until it leaves the edge's luma
//	    by a quarter of the step: the edge's ends;
//	  - the nearer end gives the blend, 0.5 - its distance / the edge's
//	    length, where the luma there goes the other way than the pixel's
//	    (the pixel is on the end's step); the 3 x 3 mean's difference from
//	    the pixel gives a blend of its own for edges thinner than a pixel;
//	  - the pixel's colour moves towards the neighbour across the edge by
//	    the larger of the two.
//	The integer steps along the edge and the pair means are what the
//	original does with bilinear taps at half pixels, without the filtering
//	(the picture of a pixel's neighbours, the same on every GPU). The alpha
//	stays (the HUD masks through the back buffer's).
//	The includer defines float4 FxaaLoad(int2 p), the picture's pixel p
//	(clamped to it), before it includes this.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_FXAA_HLSLI
#define XRENDERD3D12_FXAA_HLSLI

//! d3d12_AAQuality 0 to 3 (low, medium, high, ultra): the relative and the absolute edge threshold, the
//! share of the blend for edges thinner than a pixel, and the steps along the edge (pixels each). High has
//! FXAA's own defaults (0.166, 0.0833, 0.75). Measured on Training's islands from the hill against the view
//! supersampled 4 x: every quality takes the error at the edges to 0.73-0.78 of none's, and the share for thin
//! edges is what softens the textures (the foliage's above all): High 0.75-0.81 of their detail, with 0.0312 as
//! its absolute threshold 0.69-0.78 for no better edges.
static const float FXAA_EDGE_THRESHOLD[4] = { 0.25, 0.166, 0.166, 0.125 };
static const float FXAA_EDGE_THRESHOLD_MIN[4] = { 0.0833, 0.0833, 0.0833, 0.0625 };
static const float FXAA_SUBPIXEL[4] = { 0.25, 0.5, 0.75, 0.75 };
static const uint FXAA_MAX_STEPS = 16;
static const uint FXAA_STEP_COUNT[4] = { 4, 7, 11, 14 };
static const float FXAA_STEPS[4][FXAA_MAX_STEPS] = {
  { 1, 2, 4, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 2, 2, 4, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 8, 0, 0, 0, 0, 0 },
  { 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 8, 0, 0 },
};

//! What FXAA does with a pixel: bEdge false leaves it as it is (vColor its colour); else its colour moves
//! towards the pixel vStep away (the neighbour across the edge) by fBlend (0 to 1): vColor.
struct SFxaaResult
{
  float4 vColor;
  int2 vStep;
  float fBlend;
  bool bEdge;
};

//! The luma FXAA goes by: of the colour as it is (gamma encoded, about perceptual).
float FxaaLuma(float3 c)
{
  return dot(c, float3(0.299, 0.587, 0.114));
}

float FxaaLumaAt(int2 p)
{
  return FxaaLuma(FxaaLoad(p).rgb);
}

//! FXAA of the pixel p at the quality nQuality (0 to 3).
SFxaaResult Fxaa(int2 p, uint nQuality)
{
  nQuality = min(nQuality, 3u);
  SFxaaResult r;
  const float4 c = FxaaLoad(p);
  r.vColor = c;
  r.vStep = int2(0, 0);
  r.fBlend = 0;
  r.bEdge = false;

  const float lC = FxaaLuma(c.rgb);
  const float lN = FxaaLumaAt(p + int2(0, -1)), lS = FxaaLumaAt(p + int2(0, 1));
  const float lW = FxaaLumaAt(p + int2(-1, 0)), lE = FxaaLumaAt(p + int2(1, 0));
  const float lMax = max(max(max(lN, lS), max(lW, lE)), lC);
  const float lMin = min(min(min(lN, lS), min(lW, lE)), lC);
  const float fRange = lMax - lMin;
  if (fRange < max(FXAA_EDGE_THRESHOLD_MIN[nQuality], lMax * FXAA_EDGE_THRESHOLD[nQuality]))
    return r;
  r.bEdge = true;

  const float lNW = FxaaLumaAt(p + int2(-1, -1)), lNE = FxaaLumaAt(p + int2(1, -1));
  const float lSW = FxaaLumaAt(p + int2(-1, 1)), lSE = FxaaLumaAt(p + int2(1, 1));

  // A horizontal edge (the luma changes down the columns) or a vertical one: the second differences down the
  // three columns against those across the three rows, the middle ones twice.
  const float fAcrossRows = abs(lNW - 2 * lW + lSW) + 2 * abs(lN - 2 * lC + lS) + abs(lNE - 2 * lE + lSE);
  const float fAcrossColumns = abs(lNW - 2 * lN + lNE) + 2 * abs(lW - 2 * lC + lE) + abs(lSW - 2 * lS + lSE);
  const bool bHorizontal = fAcrossRows >= fAcrossColumns;

  // The side of the edge: the neighbour across it with the steeper step.
  const float lBefore = bHorizontal ? lN : lW, lAfter = bHorizontal ? lS : lE;
  const float fBefore = abs(lBefore - lC), fAfter = abs(lAfter - lC);
  const bool bBefore = fBefore >= fAfter;
  const int2 vAcross = bHorizontal ? int2(0, 1) : int2(1, 0);
  const int2 vStep = bBefore ? -vAcross : vAcross;
  const float lSide = bBefore ? lBefore : lAfter;
  const float fGradient = max(fBefore, fAfter);
  const float lEdge = 0.5 * (lC + lSide);
  const float fEnd = 0.25 * fGradient;

  // Along the edge both ways: the luma halfway to the side's row (or column), until it leaves the edge's.
  const int2 vAlong = bHorizontal ? int2(1, 0) : int2(0, 1);
  float fDistNeg = 0, fDistPos = 0;
  float lEndNeg = 0, lEndPos = 0;
  bool bDoneNeg = false, bDonePos = false;
  const uint nSteps = FXAA_STEP_COUNT[nQuality];
  [loop]
  for (uint k = 0; k < nSteps && !(bDoneNeg && bDonePos); k++)
  {
    const float fStep = FXAA_STEPS[nQuality][k];
    if (!bDoneNeg)
    {
      fDistNeg += fStep;
      const int2 q = p - vAlong * (int)fDistNeg;
      lEndNeg = 0.5 * (FxaaLumaAt(q) + FxaaLumaAt(q + vStep)) - lEdge;
      bDoneNeg = abs(lEndNeg) >= fEnd;
    }
    if (!bDonePos)
    {
      fDistPos += fStep;
      const int2 q = p + vAlong * (int)fDistPos;
      lEndPos = 0.5 * (FxaaLumaAt(q) + FxaaLumaAt(q + vStep)) - lEdge;
      bDonePos = abs(lEndPos) >= fEnd;
    }
  }

  // The nearer end: the pixel is on its step where the luma there goes the other way than the pixel's.
  const bool bNegNearer = fDistNeg < fDistPos;
  const float fDist = bNegNearer ? fDistNeg : fDistPos;
  const float lEnd = bNegNearer ? lEndNeg : lEndPos;
  const bool bGood = (lEnd < 0) != (lC - lEdge < 0);
  const float fEdgeBlend = bGood ? 0.5 - fDist / (fDistNeg + fDistPos) : 0;

  // Thinner than a pixel: the 3 x 3 mean (the 4 neighbours twice) against the pixel, over the range.
  const float lMean = (2 * (lN + lS + lW + lE) + (lNW + lNE + lSW + lSE)) / 12;
  const float s = saturate(abs(lMean - lC) / fRange);
  const float s2 = (3 - 2 * s) * s * s;
  const float fSubBlend = s2 * s2 * FXAA_SUBPIXEL[nQuality];

  r.vStep = vStep;
  r.fBlend = max(fEdgeBlend, fSubBlend);
  r.vColor = float4(lerp(c.rgb, FxaaLoad(p + vStep).rgb, r.fBlend), c.a);
  return r;
}

#endif
