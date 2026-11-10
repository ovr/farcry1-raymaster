// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WordHashTests.cpp
//	Description: Unit tests of the renderer's hash of whole words
//	(WordHash.h): every bit of a key counts, the words' order too, and the
//	keys the tables hold (pipeline keys of small fields, pointers to
//	aligned blocks, parts' keys) spread over the low bits as well as
//	random numbers would.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "WordHash.h"

#include <unordered_set>
#include <vector>

namespace
{
  //! A key of three words, as the pipeline's (24 bytes).
  struct SKey3
  {
    uint64_t w[3];
  };

  uint64_t Next(uint64_t &nState)
  {
    nState = nState * 6364136223846793005ull + 1442695040888963407ull;
    return nState ^ (nState >> 29);
  }

  //! The buckets of a table of 2^nBits the hashes take (the tables mask the low bits).
  template <class T> size_t Buckets(const std::vector<T> &Keys, int nBits)
  {
    std::unordered_set<uint64_t> Used;
    for (const T &k : Keys)
      Used.insert(SWordHash()(k) & ((1ull << nBits) - 1));
    return Used.size();
  }
}

XR_TEST(WordHash_EveryBit)
{
  uint64_t nState = 1;
  for (int n = 0; n < 64; n++)
  {
    SKey3 k = { { Next(nState), Next(nState), Next(nState) } };
    const size_t h = SWordHash()(k);
    CHECK(SWordHash()(k) == h);
    for (int b = 0; b < 192; b++)
    {
      SKey3 f = k;
      f.w[b / 64] ^= 1ull << (b % 64);
      CHECK(SWordHash()(f) != h);
    }
    // The words' order counts.
    if (k.w[0] != k.w[1])
    {
      SKey3 s = { { k.w[1], k.w[0], k.w[2] } };
      CHECK(SWordHash()(s) != h);
    }
  }
}

XR_TEST(WordHash_LowBitsSpread)
{
  // 4096 keys in 8192 buckets: random hashes take 8192 (1 - e^-0.5) = 3224 of them; at least 3000 here.
  constexpr int N = 4096, BITS = 13;
  // Pipeline keys: small values in a few of their bytes (a state's bits, a program, a target).
  std::vector<SKey3> Pipelines;
  for (int i = 0; i < N; i++)
    Pipelines.push_back({ { (uint64_t)(i & 15) | (uint64_t)((i >> 4) & 7) << 32 | (uint64_t)(i >> 7) << 40, (uint64_t)(i % 3), 0 } });
  CHECK(Buckets(Pipelines, BITS) >= 3000);
  // Pointers to blocks 16, 64 and 4096 bytes apart (leaf buffers, objects).
  for (uint64_t nStride : { 16ull, 64ull, 4096ull })
  {
    std::vector<const void *> Pointers;
    for (int i = 0; i < N; i++)
      Pointers.push_back((const void *)(uintptr_t)(0x1d3a5b40000ull + nStride * i));
    CHECK(Buckets(Pointers, BITS) >= 3000);
  }
  // Parts' keys: an object's address with a material's index in the low bits.
  std::vector<uint64_t> Parts;
  for (int i = 0; i < N; i++)
    Parts.push_back((0x2a7f0000ull + 0x150ull * (i / 4)) << 8 | (uint64_t)(i % 4));
  CHECK(Buckets(Parts, BITS) >= 3000);
  // And sequential numbers.
  std::vector<uint64_t> Ids;
  for (int i = 0; i < N; i++)
    Ids.push_back((uint64_t)i);
  CHECK(Buckets(Ids, BITS) >= 3000);
}
