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
//	the tables' buckets take. CWordSet: a set of words for membership
//	alone in one array (the parts drawn into the cascades, the live leaf
//	buffers): std::unordered_set's nodes cost the game's allocator one
//	call each to make and to free, every frame.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WORDHASH_H
#define XRENDERD3D12_WORDHASH_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdlib.h>
#include <type_traits>
#include <vector>

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

//! A set of words (64 bit keys, pointers) for membership alone: linear probing in one array of a power of two, at most
//! half full, 0 kept aside (an empty slot), erase by shifting back the keys after it (no tombstones). Not iterable.
template <class T> class CWordSet
{
  static_assert(sizeof(T) == 8 && std::is_trivially_copyable_v<T>, "a word");

public:
  //! True when k was not in the set.
  bool insert(T k)
  {
    const uint64_t w = Word(k);
    if (!w)
    {
      if (m_bZero)
        return false;
      m_bZero = true;
      m_nSize++;
      return true;
    }
    if ((m_nSize + 1) * 2 > m_Slots.size())
      Grow(m_Slots.empty() ? MIN_SLOTS : m_Slots.size() * 2);
    for (size_t i = Slot(w);; i = (i + 1) & m_nMask)
    {
      if (m_Slots[i] == w)
        return false;
      if (!m_Slots[i])
      {
        m_Slots[i] = w;
        m_nSize++;
        return true;
      }
    }
  }
  size_t count(T k) const
  {
    const uint64_t w = Word(k);
    if (!w)
      return m_bZero ? 1 : 0;
    if (m_Slots.empty())
      return 0;
    for (size_t i = Slot(w);; i = (i + 1) & m_nMask)
    {
      if (m_Slots[i] == w)
        return 1;
      if (!m_Slots[i])
        return 0;
    }
  }
  //! True when k was in the set.
  bool erase(T k)
  {
    const uint64_t w = Word(k);
    if (!w)
    {
      if (!m_bZero)
        return false;
      m_bZero = false;
      m_nSize--;
      return true;
    }
    if (m_Slots.empty())
      return false;
    size_t i = Slot(w);
    for (; m_Slots[i] != w; i = (i + 1) & m_nMask)
      if (!m_Slots[i])
        return false;
    // The keys after it in its run that may not stay behind the hole (their home at or before it, cyclically) move in.
    for (size_t j = (i + 1) & m_nMask; m_Slots[j]; j = (j + 1) & m_nMask)
    {
      const size_t nHome = Slot(m_Slots[j]);
      if (((j - nHome) & m_nMask) >= ((j - i) & m_nMask))
      {
        m_Slots[i] = m_Slots[j];
        i = j;
      }
    }
    m_Slots[i] = 0;
    m_nSize--;
    return true;
  }
  void clear()
  {
    if (m_nSize)
      std::fill(m_Slots.begin(), m_Slots.end(), 0);
    m_bZero = false;
    m_nSize = 0;
  }
  void reserve(size_t n)
  {
    size_t nSlots = MIN_SLOTS;
    while (nSlots < n * 2)
      nSlots *= 2;
    if (nSlots > m_Slots.size())
      Grow(nSlots);
  }
  size_t size() const { return m_nSize; }
  bool empty() const { return m_nSize == 0; }

private:
  static constexpr size_t MIN_SLOTS = 64;
  static uint64_t Word(T k)
  {
    uint64_t w;
    memcpy(&w, &k, 8);
    return w;
  }
  size_t Slot(uint64_t w) const
  {
    CWordHash Hash;
    Hash.AddWord(w);
    return (size_t)Hash.Get() & m_nMask;
  }
  void Grow(size_t nSlots)
  {
    std::vector<uint64_t> Old(nSlots, 0);
    Old.swap(m_Slots);
    m_nMask = nSlots - 1;
    for (uint64_t w : Old)
    {
      if (!w)
        continue;
      size_t i = Slot(w);
      while (m_Slots[i])
        i = (i + 1) & m_nMask;
      m_Slots[i] = w;
    }
  }

  std::vector<uint64_t> m_Slots;
  size_t m_nMask = 0;
  size_t m_nSize = 0;
  bool m_bZero = false;
};

#endif
