// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: HdrStatsMath.h
//	Description: The CPU's side of the HDR statistics' histogram
//	(HdrStats.cpp, Shaders/HdrStats.hlsl): the light of a place in it and
//	its percentiles. Without the renderer: the unit tests include it.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_HDRSTATSMATH_H
#define XRENDERD3D12_HDRSTATSMATH_H

#include <algorithm>
#include <cstdint>

// XR_HDR_HIST_* (Shaders/Constants.h).
#ifndef XR_HDR_HIST_BINS
#error "include Shaders/Constants.h first"
#endif

//! log2 of the light at fBin bins from the first bin's lower edge (HdrHistogramBin's inverse).
inline float HdrHistLog2(float fBin)
{
  return XR_HDR_HIST_MIN + fBin * (float)(XR_HDR_HIST_MAX - XR_HDR_HIST_MIN) / XR_HDR_HIST_BINS;
}

//! Where log2 of a light is in the histogram, in bins from the first bin's lower edge (not clamped).
inline float HdrHistBinOf(float fLog2)
{
  return (fLog2 - XR_HDR_HIST_MIN) * XR_HDR_HIST_BINS / (float)(XR_HDR_HIST_MAX - XR_HDR_HIST_MIN);
}

//! The samples of XR_HDR_HIST_BINS bins (of the renderer's uint32 or the tests' uint32_t).
template<class T> double HdrHistTotal(const T *pBins)
{
  double fTotal = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
    fTotal += pBins[i];
  return fTotal;
}

//! log2 of the light below which fShare (0 .. 1) of the samples are, each bin's spread evenly over
//! it; XR_HDR_HIST_MIN for an empty histogram.
template<class T> float HdrHistPercentile(const T *pBins, double fShare)
{
  const double fTotal = HdrHistTotal(pBins);
  if (fTotal <= 0)
    return (float)XR_HDR_HIST_MIN;
  const double fWant = std::clamp(fShare, 0.0, 1.0) * fTotal;
  double fSum = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
  {
    if (pBins[i] && fSum + pBins[i] >= fWant)
      return HdrHistLog2((float)(i + (fWant - fSum) / pBins[i]));
    fSum += pBins[i];
  }
  return (float)XR_HDR_HIST_MAX;
}

//! The mean of log2 of the light (each bin at its middle); XR_HDR_HIST_MIN for an empty histogram.
template<class T> float HdrHistMeanLog2(const T *pBins)
{
  double fSum = 0, fTotal = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
  {
    fSum += (double)pBins[i] * HdrHistLog2(i + 0.5f);
    fTotal += pBins[i];
  }
  return fTotal > 0 ? (float)(fSum / fTotal) : (float)XR_HDR_HIST_MIN;
}

//! The share (0 .. 1) of the samples with log2 of the light at fLog2 or above (the bin it is in
//! counted by how much of it is above).
template<class T> double HdrHistShareAbove(const T *pBins, float fLog2)
{
  const double fTotal = HdrHistTotal(pBins);
  if (fTotal <= 0)
    return 0;
  const float fBin = HdrHistBinOf(fLog2);
  double fAbove = 0;
  for (int i = 0; i < XR_HDR_HIST_BINS; i++)
    fAbove += pBins[i] * std::clamp((double)(i + 1) - fBin, 0.0, 1.0);
  return fAbove / fTotal;
}

#endif
