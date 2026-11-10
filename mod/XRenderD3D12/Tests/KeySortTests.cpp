// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: KeySortTests.cpp
//	Description: Unit tests of KeySort.h on the CPU: KeySort::Stable puts
//	a list in std::stable_sort's order (by key, equal keys as they came)
//	for lists of every length to 300 and long ones, with few and with many
//	distinct keys, in order already and backwards, the scratch reused.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"

#include "KeySort.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  //! An item as CScene::BuildLists' (its key and what tells two of one key apart), 88 bytes.
  struct SItem
  {
    uint64_t nKey;
    uint32_t nId;
    uint8_t Rest[76];
  };

  bool Same(const std::vector<SItem> &a, const std::vector<SItem> &b)
  {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); i++)
      if (a[i].nKey != b[i].nKey || a[i].nId != b[i].nId || a[i].Rest[0] != b[i].Rest[0] || a[i].Rest[75] != b[i].Rest[75])
        return false;
    return true;
  }
}

// Why: BuildLists' draw order is the lists' sort: equal keys must keep the order of EF_AddEf (the shadow volumes
// between their stencil clears), and any other order than std::stable_sort's changes what is drawn over what.
XR_TEST(KeySort_StableSortsOrder)
{
  std::vector<KeySort::SKeyPlace> Order;
  std::vector<SItem> Sorted;
  uint32_t s = 4242;
  const auto Next = [&s]() { s = s * 1664525u + 1013904223u; return s; };
  std::vector<size_t> Lengths;
  for (size_t n = 0; n <= 300; n++)
    Lengths.push_back(n);
  Lengths.push_back(4096);
  Lengths.push_back(10007);
  int nLists = 0;
  for (size_t n : Lengths)
    for (int nKind = 0; nKind < 5; nKind++)
    {
      std::vector<SItem> Items(n);
      for (size_t i = 0; i < n; i++)
      {
        SItem &it = Items[i];
        const uint32_t r = Next();
        switch (nKind)
        {
        case 0: it.nKey = r % 4; break;                                         // few keys, many ties
        case 1: it.nKey = ((uint64_t)Next() << 32) | r; break;                  // all 64 bits
        case 2: it.nKey = i / 3; break;                                         // in order already
        case 3: it.nKey = n - i / 2; break;                                     // backwards, pairs tied
        default: it.nKey = (uint64_t)(r % 16) << 60 | (r % 7); break;           // the high bits (BuildLists' pass and group)
        }
        it.nId = (uint32_t)i;
        std::fill(std::begin(it.Rest), std::end(it.Rest), (uint8_t)(i * 31 + nKind));
      }
      std::vector<SItem> Want = Items;
      std::stable_sort(Want.begin(), Want.end(), [](const SItem &a, const SItem &b) { return a.nKey < b.nKey; });
      KeySort::Stable(Items, [](const SItem &it) { return it.nKey; }, Order, Sorted);
      CHECK_MSG(Same(Items, Want), "list of " + std::to_string(n) + ", kind " + std::to_string(nKind));
      nLists++;
    }
  CHECK(nLists > 0);
}
