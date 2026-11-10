// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterDepthCopyTests.cpp
//	Description: Unit tests of the water's depth copy by a kernel
//	(d3d12_WaterDepthCopy: DepthTiles.hlsl's XR_DEPTH_COPY, Water.cpp's
//	CopyDepthByKernel): every texel of a depth buffer the same bits in
//	the copy, at a size of no whole groups.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "Test_DepthCopyKernel.h"

#include <cstring>
#include <vector>

using namespace XrTest;

// Why: the copy is read by the water's pixels and its depth tiles in place of a copy of the resource, so its words are
// the depth buffer's: 0, 1, the depths between, the least normal float, and every edge texel of a size that is no
// multiple of the kernel's 8 x 8 groups (none written past it).
XR_TEST(WaterDepthCopy_SameBits)
{
  constexpr uint32 W = 37, H = 23;
  std::vector<uint32> Bits(W * H);
  unsigned nSeed = 4242;
  for (size_t i = 0; i < Bits.size(); i++)
  {
    nSeed = nSeed * 1664525u + 1013904223u;
    float f = (float)(nSeed >> 8) / 16777216.0f;
    f = (i % 7 == 0) ? f * 1e-30f : (i % 5 == 0) ? 1 - f * 1e-6f : f;
    std::memcpy(&Bits[i], &f, 4);
  }
  const uint32 Fixed[] = { 0x00000000u, 0x3F800000u, 0x3F7FFFFFu, 0x00800000u, 0x3F000000u, 0x33D6BF95u };
  for (size_t i = 0; i < sizeof(Fixed) / sizeof(Fixed[0]); i++)
    Bits[i * 31] = Fixed[i];
  const std::vector<uint32> Copy = Gpu().RunDepthCopy({ g_Test_DepthCopyKernel, sizeof(g_Test_DepthCopyKernel) }, Bits, W, H);
  size_t nOff = 0, nFirst = 0;
  for (size_t i = 0; i < Bits.size(); i++)
    if (Copy[i] != Bits[i] && !nOff++)
      nFirst = i;
  CHECK_MSG(nOff == 0, std::to_string(nOff) + " texels differ, the first at " + std::to_string(nFirst));
}
