// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawBatch.h
//	Description: The draws of one mesh for consecutive objects drawn at
//	once (d3d12_DrawBatch, Draw.cpp's DrawGeometry): a draw is held back,
//	and the next one joins it where it binds and draws all the same but
//	its header's matrices (SDrawHead's mWorld, mWorldViewProj and
//	mPrevWorldViewProj) and its header is the next 256 bytes of the
//	frame's memory. The held draw is then one instanced draw whose vertex
//	shader reads instance i's matrices from header i (XR_DRAW_OBJECTS):
//	the instances keep the draws' order. Header only (the unit tests,
//	Tests/DrawBatchTests.cpp).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_DRAWBATCH_H
#define XRENDERD3D12_DRAWBATCH_H

#include "Shaders/Constants.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace DrawBatch
{
  //! The bytes of a header after its matrices: the colour, the flags, the textures (all the draws of a batch the same).
  constexpr size_t HEAD_MATRICES = offsetof(SDrawHead, vColor);
  constexpr size_t HEAD_REST = sizeof(SDrawHead) - HEAD_MATRICES;
  static_assert(HEAD_MATRICES == 192 && sizeof(SDrawHead) == 256, "the vertex shader reads the matrices at 0, 64 and 128 of 256");

  //! What a draw binds and draws but for its header: whole words, set to zero first (it is compared as bytes).
  struct SState
  {
    const void *pPSO;
    uint64_t nFrameCB;        //!< b0's address
    uint64_t nExt;            //!< b3's
    uint64_t VB[3][2];        //!< slots 0, 2 and 3: address, size and stride (0: not bound)
    uint64_t IB[2];           //!< address, size and format
    uint64_t nTopology;
    float Viewport[6];
    int32_t Scissor[4];
    uint32_t nCount, nFirst;
    int32_t nBaseVertex;
    uint32_t nEpoch;          //!< the device's state epoch (CD3D12Device::GetStateEpoch)
    uint8_t Rest[HEAD_REST];  //!< the header after its matrices
  };
  static_assert(sizeof(SState) % 8 == 0, "SState is compared as bytes");
  static_assert(offsetof(SState, Scissor) % 8 == 0 && offsetof(SState, nCount) % 8 == 0 && offsetof(SState, nBaseVertex) % 8 == 0 &&
                offsetof(SState, Rest) == offsetof(SState, nBaseVertex) + 8 && offsetof(SState, Viewport) == 96 &&
                offsetof(SState, Scissor) == 120 && offsetof(SState, Rest) + HEAD_REST == sizeof(SState),
                "SState's 4 byte fields in pairs of whole words, no byte between its fields");

  //! Two 4 byte fields of SState written as one word (the first at the lower address): Joins compares it 8 bytes a load at
  //! once, and a load over two 4 byte stores waits for both to reach the cache, where one 8 byte store passes it on.
  inline void PutPair(void *p, uint32_t nFirst, uint32_t nSecond)
  {
    const uint64_t nWord = (uint64_t)nFirst | (uint64_t)nSecond << 32;
    memcpy(p, &nWord, 8);
  }

  //! The draw held back and the ones that joined it.
  class CBatch
  {
  public:
    //! The draws in it (0: none held).
    uint32_t Count() const { return m_nCount; }
    const SState &State() const { return m_State; }
    //! The first draw's header (the frame's memory).
    uint64_t Head() const { return m_nHead; }

    //! Whether a draw of state s, its header at nHead, can join (the same state, its header the next one).
    bool Joins(const SState &s, uint64_t nHead) const
    {
      return m_nCount && nHead == m_nHead + (uint64_t)m_nCount * sizeof(SDrawHead) && !memcmp(&s, &m_State, sizeof(SState));
    }
    //! Holds a draw back (the batch was empty).
    void Start(const SState &s, uint64_t nHead)
    {
      memcpy(&m_State, &s, sizeof(SState));
      m_nHead = nHead;
      m_nCount = 1;
    }
    void Join() { m_nCount++; }
    //! The draws taken out to be recorded (their number).
    uint32_t Take()
    {
      const uint32_t n = m_nCount;
      m_nCount = 0;
      return n;
    }

  private:
    SState m_State = {};
    uint64_t m_nHead = 0;
    uint32_t m_nCount = 0;
  };
}

#endif
