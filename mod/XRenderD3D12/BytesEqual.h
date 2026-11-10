// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: BytesEqual.h
//	Description: Whether two blocks of bytes are the same, as memcmp(a,
//	b, n) == 0, 64 bytes a load with AVX-512 or 32 with AVX2 where the
//	CPU and the OS have them (CPUID and XGETBV, once at load): the CRT's
//	memcmp compares 8 bytes at a time, and the rest of a draw's constants
//	(1,808 bytes, DrawExt.h) is compared so for nearly every draw. Header
//	only (the unit tests, Tests/BytesEqualTests.cpp, compare each loop the
//	CPU has with memcmp).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_BYTESEQUAL_H
#define XRENDERD3D12_BYTESEQUAL_H

#include <immintrin.h>
#include <intrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BytesEqual
{
  //! The loops, the widest last.
  enum ELevel
  {
    LEVEL_MEMCMP,
    LEVEL_AVX2,
    LEVEL_AVX512,
  };

  //! The widest loop the CPU has and the OS saves the registers of (XCR0: YMM; opmask and ZMM).
  inline int DetectLevel()
  {
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7)
      return LEVEL_MEMCMP;
    __cpuid(r, 1);
    const bool bOsXSave = (r[2] & (1 << 27)) != 0, bAvx = (r[2] & (1 << 28)) != 0;
    if (!bOsXSave || !bAvx)
      return LEVEL_MEMCMP;
    const unsigned long long nXcr0 = _xgetbv(0);
    __cpuidex(r, 7, 0);
    const bool bAvx2 = (nXcr0 & 0x6) == 0x6 && (r[1] & (1 << 5));
    const bool bAvx512 = bAvx2 && (nXcr0 & 0xE6) == 0xE6 && (r[1] & (1 << 16));
    return bAvx512 ? LEVEL_AVX512 : bAvx2 ? LEVEL_AVX2 : LEVEL_MEMCMP;
  }

  //! This CPU's (DetectLevel).
  inline const int g_nLevel = DetectLevel();

  //! AVX-512 (F): n at least 64. Blocks of 256 bytes, then 64 at a time, the last 64 over the bytes before them.
  inline bool EqualAvx512(const void *a, const void *b, size_t n)
  {
    const uint8_t *p = static_cast<const uint8_t *>(a), *q = static_cast<const uint8_t *>(b);
    size_t i = 0;
    bool bSame = true;
    for (; bSame && i + 256 <= n; i += 256)
    {
      __m512i x0 = _mm512_xor_si512(_mm512_loadu_si512(p + i), _mm512_loadu_si512(q + i));
      __m512i x1 = _mm512_xor_si512(_mm512_loadu_si512(p + i + 64), _mm512_loadu_si512(q + i + 64));
      // 0xF6: x | (y ^ z)
      x0 = _mm512_ternarylogic_epi64(x0, _mm512_loadu_si512(p + i + 128), _mm512_loadu_si512(q + i + 128), 0xF6);
      x1 = _mm512_ternarylogic_epi64(x1, _mm512_loadu_si512(p + i + 192), _mm512_loadu_si512(q + i + 192), 0xF6);
      const __m512i x = _mm512_or_si512(x0, x1);
      bSame = _mm512_test_epi64_mask(x, x) == 0;
    }
    if (bSame)
    {
      __m512i x = _mm512_setzero_si512();
      for (; i + 64 <= n; i += 64)
        x = _mm512_ternarylogic_epi64(x, _mm512_loadu_si512(p + i), _mm512_loadu_si512(q + i), 0xF6);
      if (i < n)
        x = _mm512_ternarylogic_epi64(x, _mm512_loadu_si512(p + n - 64), _mm512_loadu_si512(q + n - 64), 0xF6);
      bSame = _mm512_test_epi64_mask(x, x) == 0;
    }
    _mm256_zeroupper();
    return bSame;
  }

  //! AVX2: n at least 32. Blocks of 128 bytes, then 32 at a time, the last 32 over the bytes before them.
  inline bool EqualAvx2(const void *a, const void *b, size_t n)
  {
    const uint8_t *p = static_cast<const uint8_t *>(a), *q = static_cast<const uint8_t *>(b);
    auto Diff = [&](size_t k) { return _mm256_xor_si256(_mm256_loadu_si256((const __m256i *)(p + k)), _mm256_loadu_si256((const __m256i *)(q + k))); };
    size_t i = 0;
    bool bSame = true;
    for (; bSame && i + 128 <= n; i += 128)
    {
      const __m256i x = _mm256_or_si256(_mm256_or_si256(Diff(i), Diff(i + 32)), _mm256_or_si256(Diff(i + 64), Diff(i + 96)));
      bSame = _mm256_testz_si256(x, x) != 0;
    }
    if (bSame)
    {
      __m256i x = _mm256_setzero_si256();
      for (; i + 32 <= n; i += 32)
        x = _mm256_or_si256(x, Diff(i));
      if (i < n)
        x = _mm256_or_si256(x, Diff(n - 32));
      bSame = _mm256_testz_si256(x, x) != 0;
    }
    _mm256_zeroupper();
    return bSame;
  }

  //! memcmp(a, b, n) == 0 by the widest loop there is.
  inline bool Equal(const void *a, const void *b, size_t n)
  {
    if (n >= 64 && g_nLevel >= LEVEL_AVX512)
      return EqualAvx512(a, b, n);
    if (n >= 32 && g_nLevel >= LEVEL_AVX2)
      return EqualAvx2(a, b, n);
    return memcmp(a, b, n) == 0;
  }
}

#endif
