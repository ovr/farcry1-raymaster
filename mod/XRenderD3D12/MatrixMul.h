// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: MatrixMul.h
//	Description: The product of two 4 x 4 matrices of floats kept by rows
//	(the SDK's Matrix44, row vectors) four elements at a time with SSE.
//	Each element is its four products added from the left, as
//	Cry_Matrix.h's operator* adds them, so the bits are the same (the
//	renderer builds with /fp:precise: no fused multiply-add). For the
//	transforms every draw makes (DrawLeafItem, the cascades' instances).
//	Columns3: a matrix's columns as rows (the scene for rays' transforms).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <emmintrin.h>

namespace MatrixMul
{
  //! out = l * r: out's row i is l's row i times r. out may be l or r.
  inline void Mul44(const float *l, const float *r, float *out)
  {
    const __m128 r0 = _mm_loadu_ps(r), r1 = _mm_loadu_ps(r + 4), r2 = _mm_loadu_ps(r + 8), r3 = _mm_loadu_ps(r + 12);
    __m128 Rows[4];
    for (int i = 0; i < 4; i++)
    {
      __m128 s = _mm_mul_ps(_mm_set1_ps(l[i * 4]), r0);
      s = _mm_add_ps(s, _mm_mul_ps(_mm_set1_ps(l[i * 4 + 1]), r1));
      s = _mm_add_ps(s, _mm_mul_ps(_mm_set1_ps(l[i * 4 + 2]), r2));
      Rows[i] = _mm_add_ps(s, _mm_mul_ps(_mm_set1_ps(l[i * 4 + 3]), r3));
    }
    for (int i = 0; i < 4; i++)
      _mm_storeu_ps(out + i * 4, Rows[i]);
  }

  //! The same for a matrix type whose elements are float data[16] by rows (Matrix44).
  template <class M> inline M Mul(const M &l, const M &r)
  {
    M m;
    Mul44(l.data, r.data, m.data);
    return m;
  }

  //! out's rows 0..2 (four floats each) are m's columns 0..2, each written as one 16 byte store: whole words read back
  //! soon after (a hash) are forwarded from it, where from four 4 byte stores they wait for them to reach the cache.
  inline void Columns3(const float *m, float *out)
  {
    __m128 c0 = _mm_loadu_ps(m), c1 = _mm_loadu_ps(m + 4), c2 = _mm_loadu_ps(m + 8), c3 = _mm_loadu_ps(m + 12);
    _MM_TRANSPOSE4_PS(c0, c1, c2, c3);
    _mm_storeu_ps(out, c0);
    _mm_storeu_ps(out + 4, c1);
    _mm_storeu_ps(out + 8, c2);
  }
}
