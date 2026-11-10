// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WordHash.h
//	Description: A hash of whole 64 bit words for the renderer's tables
//	looked up every draw (the pipeline states, the parts drawn into the
//	sun's cascades, the live leaf buffers, the scene for rays' keys):
//	MurmurHash3's mixing of each word and its final mix, a multiply a
//	word on the chain where the standard library's FNV-1a takes one a
//	byte (a pipeline key of 24 bytes took 2 % of Pier's heavy frames).
//	The unit tests (WordHashTests.cpp) hold it to its low bits, which
//	the tables' buckets take.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WORDHASH_H
#define XRENDERD3D12_WORDHASH_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdlib.h>
#include <type_traits>

class CWordHash
{
public:
  //! The bytes of Data (a key without padding) as whole words.
  template <class T> void Add(const T &Data)
  {
    static_assert(sizeof(T) % 8 == 0 && std::is_trivially_copyable_v<T>, "hashed as whole words");
    const uint8_t *p = (const uint8_t *)&Data;
    for (size_t i = 0; i < sizeof(T); i += 8)
    {
      uint64_t k;
      memcpy(&k, p + i, 8);
      AddWord(k);
    }
  }
  void AddWord(uint64_t k)
  {
    k *= 0x87c37b91114253d5ull;
    k = _rotl64(k, 31);
    k *= 0x4cf5ad432745937full;
    m_nHash ^= k;
    m_nHash = _rotl64(m_nHash, 27) * 5 + 0x52dce729;
  }
  uint64_t Get() const
  {
    uint64_t h = m_nHash;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
  }

private:
  uint64_t m_nHash = 0;
};

//! The hash of a table's key of whole words (std::unordered_set<uint64, SWordHash>, a pointer's too).
struct SWordHash
{
  template <class T> size_t operator()(const T &Key) const
  {
    CWordHash Hash;
    Hash.Add(Key);
    return (size_t)Hash.Get();
  }
};

#endif
