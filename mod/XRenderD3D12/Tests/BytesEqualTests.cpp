// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: BytesEqualTests.cpp
//	Description: Unit tests of BytesEqual.h on the CPU: each loop this CPU
//	has (AVX2, AVX-512) and Equal say what memcmp says, for every size up
//	to 600 bytes and the rest of a draw's constants (1,808), the blocks
//	equal and with one bit off at every byte, at odd addresses too, and
//	read nothing outside the blocks.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestTypes.h"

#include "BytesEqual.h"
#include "Shaders/Constants.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  //! Runs Check(name, f) for each loop this CPU has: Equal, and AVX2 and AVX-512 themselves.
  template <class TCheck> void ForEachLoop(TCheck Check)
  {
    Check("Equal", [](const void *a, const void *b, size_t n) { return BytesEqual::Equal(a, b, n); }, size_t(0));
    if (BytesEqual::g_nLevel >= BytesEqual::LEVEL_AVX2)
      Check("EqualAvx2", [](const void *a, const void *b, size_t n) { return BytesEqual::EqualAvx2(a, b, n); }, size_t(32));
    if (BytesEqual::g_nLevel >= BytesEqual::LEVEL_AVX512)
      Check("EqualAvx512", [](const void *a, const void *b, size_t n) { return BytesEqual::EqualAvx512(a, b, n); }, size_t(64));
  }
}

// Why: DrawExt.h's Needs and SLast::Find take it for memcmp: a draw that binds the default block or the last copy when
// its bytes are not those uploads the wrong constants (and one that misses a match only uploads them again).
XR_TEST(BytesEqual_SameAsMemcmp)
{
  std::vector<size_t> Sizes;
  for (size_t n = 0; n <= 600; n++)
    Sizes.push_back(n);
  Sizes.push_back(sizeof(SDrawExt));
  std::vector<uint8_t> A(2048 + 64), B(2048 + 64);
  uint32_t s = 4242;
  for (size_t i = 0; i < A.size(); i++)
  {
    s = s * 1664525u + 1013904223u;
    A[i] = B[i] = (uint8_t)(s >> 24);
  }
  int nChecked = 0;
  ForEachLoop([&](const char *szName, auto Equal, size_t nMin)
  {
    for (size_t nOffset = 0; nOffset < 2; nOffset++)
      for (size_t n : Sizes)
      {
        if (n < nMin)
          continue;
        const uint8_t *p = A.data() + 3 + nOffset;
        uint8_t *q = B.data() + 7;
        memcpy(q, p, n);
        CHECK_MSG(Equal(p, q, n), std::string(szName) + ": equal blocks of " + std::to_string(n) + " bytes");
        for (size_t k = 0; k < n; k++)
        {
          const uint8_t nBit = (uint8_t)(1u << (k % 8));
          q[k] ^= nBit;
          const bool bWant = memcmp(p, q, n) == 0;
          CHECK_MSG(Equal(p, q, n) == bWant, std::string(szName) + ": " + std::to_string(n) + " bytes, byte " + std::to_string(k) + " off");
          q[k] ^= nBit;
          nChecked++;
        }
      }
    // B's bytes after the block differ: none of them is read as part of it (nor A's).
    std::vector<uint8_t> C(A.begin(), A.end());
    for (size_t n : Sizes)
    {
      if (n < nMin)
        continue;
      memcpy(C.data() + 7, A.data() + 3, n);
      for (size_t k = n; k < n + 64; k++)
        C[7 + k] = (uint8_t)~A[3 + k];
      CHECK_MSG(Equal(A.data() + 3, C.data() + 7, n), std::string(szName) + ": the bytes after a block of " + std::to_string(n));
    }
  });
  CHECK(nChecked > 0);
}

// Why: a loop may load only inside the blocks: the last loads end at the block's end (they overlap the ones before it),
// so a block at the end of a committed page must not fault.
XR_TEST(BytesEqual_StaysInsideTheBlocks)
{
  SYSTEM_INFO Info;
  GetSystemInfo(&Info);
  const size_t nPage = Info.dwPageSize;
  uint8_t *pPages = static_cast<uint8_t *>(VirtualAlloc(nullptr, 2 * nPage, MEM_RESERVE, PAGE_NOACCESS));
  CHECK(pPages != nullptr);
  if (!pPages)
    return;
  CHECK(VirtualAlloc(pPages, nPage, MEM_COMMIT, PAGE_READWRITE) != nullptr);
  std::vector<uint8_t> Other(nPage);
  ForEachLoop([&](const char *szName, auto Equal, size_t nMin)
  {
    for (size_t n : { size_t(32), size_t(64), size_t(65), size_t(255), size_t(256), size_t(300), size_t(1808) })
    {
      if (n < nMin)
        continue;
      // The block ends where the page does; the page after it is not readable.
      uint8_t *p = pPages + nPage - n;
      for (size_t k = 0; k < n; k++)
        p[k] = Other[k] = (uint8_t)(k * 13 + 5);
      CHECK_MSG(Equal(p, Other.data(), n) && Equal(Other.data(), p, n), std::string(szName) + ": " + std::to_string(n) + " bytes at a page's end");
    }
  });
  VirtualFree(pPages, 0, MEM_RELEASE);
}
