// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CpuSampler.h
//	Description: d3d12_CpuSamples, a sampling profiler of the thread that
//	draws the frames (CpuSampler.cpp).
//
//////////////////////////////////////////////////////////////////////

#pragma once

namespace CpuSampler
{
//! Once a frame, from the thread that draws: nIntervalUs > 0 samples this thread's stack every
//! nIntervalUs microseconds (started on the first call with it), 0 stops and writes the samples
//! to szFile; nFrame goes with the samples taken until the next call.
void Update(int nIntervalUs, int nFrame, const char *szFile);
}
