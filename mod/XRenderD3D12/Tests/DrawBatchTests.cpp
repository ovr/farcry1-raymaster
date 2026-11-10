// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawBatchTests.cpp
//	Description: Unit tests of when a draw joins the one held back
//	(DrawBatch.h, d3d12_DrawBatch, on the CPU): all it binds and draws the
//	same but its header's matrices, its header the next 256 bytes; and
//	the header's layout the vertex shader reads (XR_DRAW_OBJECTS).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestTypes.h"

#include "DrawBatch.h"

#include <string>

using namespace XrTest;
using namespace DrawBatch;

namespace
{
  SState Some()
  {
    SState s;
    memset(&s, 0, sizeof(s));
    s.pPSO = &s;
    s.nFrameCB = 0x10000;
    s.nExt = 0x20000;
    s.VB[0][0] = 0x30000;
    s.VB[0][1] = (4096ull << 32) | 32;
    s.IB[0] = 0x40000;
    s.IB[1] = (2048ull << 32) | 57;
    s.nTopology = 4;
    s.Viewport[2] = 1280;
    s.Viewport[3] = 720;
    s.Viewport[5] = 1;
    s.Scissor[2] = 1280;
    s.Scissor[3] = 720;
    s.nCount = 1161;
    s.nEpoch = 7;
    for (size_t i = 0; i < HEAD_REST; i++)
      s.Rest[i] = (uint8_t)(i * 37 + 1);
    return s;
  }
}

// Why: the vertex shader of a batch (General.hlsl's XR_DRAW_OBJECTS) reads instance i's mWorld, mWorldViewProj and
// mPrevWorldViewProj at bytes 0, 64 and 128 of the header 256 * i bytes after the first; the rest is the first's.
XR_TEST(DrawBatch_HeadLayout)
{
  CHECK(offsetof(SDrawHead, mWorld) == 0);
  CHECK(offsetof(SDrawHead, mWorldViewProj) == 64);
  CHECK(offsetof(SDrawHead, mPrevWorldViewProj) == 128);
  CHECK(HEAD_MATRICES == 192 && HEAD_REST == 64);
}

// Why: only a draw whose header follows the batch's last one joins (the shader finds instance i's at i * 256): the same
// state with its header elsewhere starts a batch of its own.
XR_TEST(DrawBatch_NextHeader)
{
  CBatch b;
  const SState s = Some();
  CHECK(!b.Joins(s, 0x1000));
  b.Start(s, 0x1000);
  CHECK(b.Count() == 1 && b.Head() == 0x1000);
  CHECK(b.Joins(s, 0x1100));
  CHECK(!b.Joins(s, 0x1000));
  CHECK(!b.Joins(s, 0x1200));
  b.Join();
  CHECK(b.Joins(s, 0x1200));
  CHECK(!b.Joins(s, 0x1100));
  b.Join();
  CHECK(b.Count() == 3);
  CHECK(b.Take() == 3);
  CHECK(b.Count() == 0 && !b.Joins(s, 0x1300));
}

// Why: the batch is drawn with the first draw's state and the rest of its header: a draw that differs in any byte of
// them (another mesh, range, pipeline, constants, viewport, scissor, colour, flags, textures, or the list's state
// touched since: the epoch) draws on its own.
XR_TEST(DrawBatch_AnyByteDiffers)
{
  const SState s = Some();
  for (size_t i = 0; i < sizeof(SState); i++)
  {
    CBatch b;
    b.Start(s, 0x1000);
    SState t = s;
    reinterpret_cast<uint8_t *>(&t)[i] ^= 0x01;
    CHECK_MSG(!b.Joins(t, 0x1100), "byte " + std::to_string(i));
  }
  CBatch b;
  b.Start(s, 0x1000);
  SState t = s;
  CHECK(b.Joins(t, 0x1100));
}
