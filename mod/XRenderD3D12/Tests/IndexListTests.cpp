// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: IndexListTests.cpp
//	Description: Unit tests of the primitives as triangle lists
//	(IndexLists.h): the same lists as made an index at a time, for every
//	kind, with degenerate strips, short and odd counts and appends after
//	others, and never more than its bound.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "IndexLists.h"

#include <vector>

namespace
{
  //! The lists as LeafBuffer.cpp made them before IndexLists.h: an index at a time.
  void AppendSlow(std::vector<uint16_t> &Out, const uint16_t *p, int n, int nPrimType)
  {
    using namespace IndexLists;
    if (!p || n < 3)
      return;
    switch (nPrimType)
    {
    case PRIM_TRIANGLE_STRIP:
    case PRIM_MULTI_STRIPS:
      for (int i = 0; i + 2 < n; i++)
      {
        uint16_t a = p[i], b = p[i + 1];
        const uint16_t c = p[i + 2];
        if (a == b || b == c || a == c)
          continue;
        if (i & 1)
          std::swap(a, b);
        Out.push_back(a);
        Out.push_back(b);
        Out.push_back(c);
      }
      break;
    case PRIM_TRIANGLE_FAN:
      for (int i = 1; i + 1 < n; i++)
      {
        Out.push_back(p[0]);
        Out.push_back(p[i]);
        Out.push_back(p[i + 1]);
      }
      break;
    case PRIM_QUADS:
      for (int i = 0; i + 3 < n; i += 4)
      {
        const uint16_t q[6] = { p[i], p[i + 1], p[i + 2], p[i], p[i + 2], p[i + 3] };
        Out.insert(Out.end(), q, q + 6);
      }
      break;
    default:
      Out.insert(Out.end(), p, p + (n - n % 3));
      break;
    }
  }

  //! A strip of n indices with some repeated (the joins of a multi strip), from a seed.
  std::vector<uint16_t> Strip(int n, unsigned nSeed)
  {
    std::vector<uint16_t> v;
    for (int i = 0; i < n; i++)
    {
      nSeed = nSeed * 1664525u + 1013904223u;
      v.push_back((nSeed >> 28) < 3 && i ? v.back() : (uint16_t)(nSeed >> 16));
    }
    return v;
  }
}

XR_TEST(IndexLists_SameAsOneByOne)
{
  using namespace IndexLists;
  for (int nPrim : { PRIM_TRIANGLES, PRIM_TRIANGLE_STRIP, PRIM_QUADS, PRIM_TRIANGLE_FAN, PRIM_MULTI_STRIPS, PRIM_MULTI_GROUPS })
    for (int n : { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 64, 65, 1001 })
      for (unsigned nSeed : { 1u, 7u, 12345u })
      {
        const std::vector<uint16_t> In = Strip(n, nSeed);
        // After a list already there, as UpdateList appends one material's after another's.
        std::vector<uint16_t> Fast = { 9, 8, 7 }, Slow = Fast;
        Append(Fast, In.data(), n, nPrim);
        AppendSlow(Slow, In.data(), n, nPrim);
        CHECK(Fast == Slow);
        CHECK(Fast.size() - 3 <= Bound(n, nPrim));
      }
}

XR_TEST(IndexLists_StripJoins)
{
  using namespace IndexLists;
  // Two strips joined by repeated indices (1 2 3 4, 4 7, 7 8 9 10): the joins' four triangles dropped, every odd one
  // of the strips turned.
  const uint16_t p[] = { 1, 2, 3, 4, 4, 7, 7, 8, 9, 10 };
  std::vector<uint16_t> Out;
  Append(Out, p, 10, PRIM_MULTI_STRIPS);
  const std::vector<uint16_t> Want = { 1, 2, 3, 3, 2, 4, 7, 8, 9, 9, 8, 10 };
  CHECK(Out == Want);
  // No indices, a null pointer: nothing.
  Append(Out, nullptr, 10, PRIM_MULTI_STRIPS);
  Append(Out, p, 2, PRIM_TRIANGLES);
  CHECK(Out == Want);
}
