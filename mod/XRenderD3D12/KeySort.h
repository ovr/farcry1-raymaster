// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: KeySort.h
//	Description: A list in the ascending order of its items' keys,
//	equal keys in the order they came: std::stable_sort of the keys with
//	the items' places (16 bytes each), the items then moved once, where
//	the merge sort moved them (CScene::BuildLists' items, 88 bytes) at
//	each of its passes. A list already in order stays
//	as it is. Header only (the unit tests, Tests/KeySortTests.cpp,
//	compare it with std::stable_sort).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_KEYSORT_H
#define XRENDERD3D12_KEYSORT_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace KeySort
{
  //! An item's key and its place in the list.
  struct SKeyPlace
  {
    uint64_t nKey;
    uint32_t nPlace;
  };

  //! Items by Key(item) ascending, equal keys in their order; Order and Sorted are the caller's to reuse (Sorted takes
  //! the items' old storage).
  template <class T, class TKey> void Stable(std::vector<T> &Items, TKey Key, std::vector<SKeyPlace> &Order, std::vector<T> &Sorted)
  {
    const size_t n = Items.size();
    if (n < 2)
      return;
    Order.resize(n);
    bool bInOrder = true;
    for (size_t i = 0; i < n; i++)
    {
      Order[i] = { (uint64_t)Key(Items[i]), (uint32_t)i };
      bInOrder = bInOrder && (i == 0 || Order[i - 1].nKey <= Order[i].nKey);
    }
    if (bInOrder)
      return;
    // The merge sort as before (BuildLists' lists come almost in order, where it is near linear), on the pairs.
    std::stable_sort(Order.begin(), Order.end(), [](const SKeyPlace &a, const SKeyPlace &b) { return a.nKey < b.nKey; });
    Sorted.resize(n);
    for (size_t i = 0; i < n; i++)
      Sorted[i] = Items[Order[i].nPlace];
    Items.swap(Sorted);
  }
}

#endif
