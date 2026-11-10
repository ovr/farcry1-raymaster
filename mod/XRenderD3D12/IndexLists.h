// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: IndexLists.h
//	Description: The engine's primitives (R_PRIMV_*: strips, fans, quads,
//	lists) as triangle lists, the way the leaf buffers draw them
//	(LeafBuffer.cpp's UpdateList and the others). A strip's, a fan's and
//	quads' indices are written through a pointer into room made once
//	(Pier's new views make 388,000 in one frame, and a push_back an index
//	took 4 ns of them); a list is copied as it is. The unit tests
//	(IndexListTests.cpp) hold it to the lists made an index at a time.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_INDEXLISTS_H
#define XRENDERD3D12_INDEXLISTS_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace IndexLists
{
  //! IRenderer.h's R_PRIMV_* (LeafBuffer.cpp checks them).
  constexpr int PRIM_TRIANGLES = 0, PRIM_TRIANGLE_STRIP = 1, PRIM_QUADS = 2, PRIM_TRIANGLE_FAN = 3, PRIM_MULTI_STRIPS = 4, PRIM_MULTI_GROUPS = 5;

  //! The most indices Append gives for n of nPrimType.
  inline size_t Bound(int n, int nPrimType)
  {
    if (n < 3)
      return 0;
    switch (nPrimType)
    {
    case PRIM_TRIANGLE_STRIP:
    case PRIM_MULTI_STRIPS:
    case PRIM_TRIANGLE_FAN:
      return 3 * (size_t)(n - 2);
    case PRIM_QUADS:
      return 6 * (size_t)(n / 4);
    default:
      return (size_t)(n - n % 3);
    }
  }

  //! n indices p of nPrimType appended to Out as a triangle list: a strip's triangles of two equal corners dropped and
  //! every other one turned, a fan's around its first, a quad as two; lists (and the multi groups) as they are, whole
  //! triangles.
  inline void Append(std::vector<uint16_t> &Out, const uint16_t *p, int n, int nPrimType)
  {
    if (!p || n < 3)
      return;
    // Lists (and the multi groups) as they are, whole triangles: one copy (GetDraw makes one a draw).
    if (nPrimType != PRIM_TRIANGLE_STRIP && nPrimType != PRIM_MULTI_STRIPS && nPrimType != PRIM_TRIANGLE_FAN && nPrimType != PRIM_QUADS)
    {
      Out.insert(Out.end(), p, p + (n - n % 3));
      return;
    }
    const size_t nOld = Out.size();
    Out.resize(nOld + Bound(n, nPrimType));
    uint16_t *o = Out.data() + nOld;
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
        o[0] = a;
        o[1] = b;
        o[2] = c;
        o += 3;
      }
      break;
    case PRIM_TRIANGLE_FAN:
      for (int i = 1; i + 1 < n; i++, o += 3)
      {
        o[0] = p[0];
        o[1] = p[i];
        o[2] = p[i + 1];
      }
      break;
    case PRIM_QUADS:
      for (int i = 0; i + 3 < n; i += 4, o += 6)
      {
        o[0] = p[i];
        o[1] = p[i + 1];
        o[2] = p[i + 2];
        o[3] = p[i];
        o[4] = p[i + 2];
        o[5] = p[i + 3];
      }
      break;
    }
    Out.resize((size_t)(o - Out.data()));
  }
}

#endif
