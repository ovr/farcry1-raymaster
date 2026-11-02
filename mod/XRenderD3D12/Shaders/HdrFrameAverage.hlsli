// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrFrameAverage.hlsli
//	Description: The HDR output's frame average limit (d3d12_HDRFrameAverage,
//	HdrOutput.cpp): the factor of the highlights' light that keeps a
//	frame's average at the display's full frame light. Without bindings:
//	FrameAverage.hlsl and the unit tests (Tests/Shaders/TestHeaders.hlsl)
//	include it.
//
//	A display (an OLED above all) holds its peak in a small part of the
//	screen only; when a frame's average is above its full frame light it
//	dims the whole picture by itself (ABL), the SDR part with it. The limit
//	takes the light away from the highlights alone instead, predictably:
//	the output's light of a pixel is its SDR colour's times 1 + (the
//	highlight layer's ratio - 1) times the factor, so the SDR picture (at
//	paper white at most) stays and the sun, fire and lamps come down
//	towards it. The frame's average is that of each pixel's brightest
//	channel, as HDR10's MaxFALL.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_HDRFRAMEAVERAGE_HLSLI
#define XRENDERD3D12_HDRFRAMEAVERAGE_HLSLI

//! The factor of the highlights' light above the SDR picture's that brings a frame's average to
//! fLimit, all in paper whites: fSdr the average of the SDR picture alone, fHighlights what the
//! highlights add to it at their full light. 1: the frame is within the limit (or has no
//! highlights); 0: the SDR picture alone is at the limit or above, which the limit leaves (it only
//! takes the highlights).
float FrameAverageFactor(float fSdr, float fHighlights, float fLimit)
{
  if (fHighlights <= 0 || fSdr + fHighlights <= fLimit)
    return 1;
  return saturate((fLimit - fSdr) / fHighlights);
}

//! This frame's factor from the last frame's fLast towards fTarget: down by fDown at most (a frame
//! getting brighter: fast), up by fUp at most (slowly back); bReset (no frame before): fTarget.
//! It reaches fTarget exactly, so a frame within the limit has the factor 1 again in the end.
float FrameAverageStep(float fLast, float fTarget, float fDown, float fUp, bool bReset)
{
  if (bReset)
    return fTarget;
  return fTarget < fLast ? max(fTarget, fLast - fDown) : min(fTarget, fLast + fUp);
}

#endif
