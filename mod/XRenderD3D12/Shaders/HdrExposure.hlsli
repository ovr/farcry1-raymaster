// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrExposure.hlsli
//	Description: The automatic exposure's target from its histogram
//	(d3d12_ExposureMeter 1: Exposure.hlsl, Hdr.cpp). The key over the mean
//	of log2 of the scene's luminance between two of its percentiles (by
//	the meter's centre weight: the dark corners and the single bright spots
//	left out), lowered where the sky is in the frame so that its bright
//	part stays below the tone map's knee: the sky's brightest channel at a
//	percentile of it (d3d12_ExposureHighlights) gives the EV that puts it at
//	the knee, which counts as the sky's share of the frame grows
//	(XR_EXPOSURE_SKY_FROM .. FULL): a big sky is not pushed into the
//	shoulder, a little of it through a window may go white (a room is still
//	made brighter). The bins are those of the HDR statistics' histogram
//	(XR_HDR_HIST_*; HdrStatsMath.h is the CPU's side). Without bindings: the
//	unit tests include it.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_HDREXPOSURE_HLSLI
#define XRENDERD3D12_HDREXPOSURE_HLSLI

#include "Constants.h"

//! log2 of the light at fBin bins from the first bin's lower edge (HdrHistLog2).
float ExposureBinLog2(float fBin)
{
  return XR_HDR_HIST_MIN + fBin * (float)(XR_HDR_HIST_MAX - XR_HDR_HIST_MIN) / XR_HDR_HIST_BINS;
}

//! The samples of a histogram.
float ExposureTotal(uint vBins[XR_HDR_HIST_BINS])
{
  float fTotal = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
    fTotal += vBins[i];
  return fTotal;
}

//! log2 of the light below which fShare (0 .. 1) of the samples are, each bin's spread evenly over it (HdrHistPercentile);
//! XR_HDR_HIST_MIN for an empty histogram.
float ExposurePercentile(uint vBins[XR_HDR_HIST_BINS], float fShare)
{
  const float fTotal = ExposureTotal(vBins);
  if (fTotal <= 0)
    return XR_HDR_HIST_MIN;
  const float fWant = saturate(fShare) * fTotal;
  float fSum = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
  {
    const float n = vBins[i];
    if (n > 0 && fSum + n >= fWant)
      return ExposureBinLog2(i + (fWant - fSum) / n);
    fSum += n;
  }
  return XR_HDR_HIST_MAX;
}

//! The mean of log2 of the light of the samples from the share fLow to fHigh of them (by their light), each bin's spread
//! evenly over it: of what is between the two percentiles. When they are one, that percentile; XR_HDR_HIST_MIN for an empty
//! histogram.
float ExposureWindowMean(uint vBins[XR_HDR_HIST_BINS], float fLow, float fHigh)
{
  const float fTotal = ExposureTotal(vBins);
  if (fTotal <= 0)
    return XR_HDR_HIST_MIN;
  const float a = saturate(min(fLow, fHigh)) * fTotal, b = saturate(max(fLow, fHigh)) * fTotal;
  float fSum = 0, fMean = 0, fWeight = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
  {
    const float n = vBins[i];
    const float lo = max(fSum, a), hi = min(fSum + n, b);
    if (n > 0 && hi > lo)
    {
      // Its samples from lo to hi, at the middle of that part of the bin.
      fMean += (hi - lo) * ExposureBinLog2(i + ((lo + hi) * 0.5 - fSum) / n);
      fWeight += hi - lo;
    }
    fSum += n;
  }
  return fWeight > 0 ? fMean / fWeight : ExposurePercentile(vBins, fLow);
}

//! The exposure's target: x the EV (before d3d12_ExposureMin / Max), y the window's mean log2 luminance, z the EV that puts
//! the sky's bright part at the knee (XR_EXPOSURE_NONE: no sky, or no protection), w the sky's share of the samples.
//! vLuma: the samples by log2 of their luminance (centre weighted); vSky: the sky's samples by log2 of their brightest
//! channel; nSamples: all the samples. vParams: x log2 of the key, y and z the window's shares (d3d12_ExposureLow, High),
//! w the sky's percentile kept at the knee (d3d12_ExposureHighlights; 0: none). fKneeLog2: log2 of the tone map's knee as
//! light. vSkyShare: the sky's share of the samples where the ceiling starts to count (x) and counts whole (y).
float4 ExposureTarget(uint vLuma[XR_HDR_HIST_BINS], uint vSky[XR_HDR_HIST_BINS], uint nSamples, float4 vParams, float fKneeLog2,
                      float2 vSkyShare)
{
  if (ExposureTotal(vLuma) <= 0)
    return float4(0, vParams.x, XR_EXPOSURE_NONE, 0);
  const float fMean = ExposureWindowMean(vLuma, vParams.y, vParams.z);
  const float fTarget = vParams.x - fMean;
  const float fSky = ExposureTotal(vSky);
  const float fShare = nSamples > 0 ? fSky / nSamples : 0;
  if (vParams.w <= 0 || fSky <= 0)
    return float4(fTarget, fMean, XR_EXPOSURE_NONE, fShare);
  const float fCeiling = fKneeLog2 - ExposurePercentile(vSky, vParams.w);
  const float w = vSkyShare.y > vSkyShare.x ? smoothstep(vSkyShare.x, vSkyShare.y, fShare) : (fShare >= vSkyShare.y ? 1 : 0);
  return float4(fTarget - w * max(fTarget - fCeiling, 0), fMean, fCeiling, fShare);
}

#endif
