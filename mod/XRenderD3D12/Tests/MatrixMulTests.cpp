// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: MatrixMulTests.cpp
//	Description: Unit tests of MatrixMul.h on the CPU: its product has the
//	bits of Cry_Matrix.h's operator* (each element its four products
//	added from the left), also where it writes over a factor.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"

#include "MatrixMul.h"

#include <cstdint>
#include <cstring>
#include <string>

using namespace XrTest;

namespace
{
  struct SMat
  {
    float data[16];
  };

  //! Cry_Matrix.h's operator* for Matrix44 (row major: data[i * 4 + j] is row i, column j), element by element.
  SMat Reference(const SMat &l, const SMat &r)
  {
    SMat m;
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        m.data[i * 4 + j] = l.data[i * 4] * r.data[j] + l.data[i * 4 + 1] * r.data[4 + j] + l.data[i * 4 + 2] * r.data[8 + j] +
                            l.data[i * 4 + 3] * r.data[12 + j];
    return m;
  }

  //! Floats of either sign over 2^-24 .. 2^24, zeros of both signs and ones among them (the transforms' range and more).
  float Next(uint32_t &s)
  {
    s = s * 1664525u + 1013904223u;
    const uint32_t k = s >> 28;
    if (k == 0)
      return (s & 0x100) ? -0.0f : 0.0f;
    if (k == 1)
      return (s & 0x100) ? -1.0f : 1.0f;
    const uint32_t nExp = 127 - 24 + ((s >> 8) % 49), nSign = (s >> 7) & 1;
    uint32_t u = s;
    u = u * 1664525u + 1013904223u;
    const uint32_t nBits = (nSign << 31) | (nExp << 23) | (u >> 9);
    float f;
    std::memcpy(&f, &nBits, 4);
    return f;
  }

  bool SameBits(const SMat &a, const SMat &b) { return std::memcmp(a.data, b.data, sizeof(a.data)) == 0; }
}

// Why: the draws' transforms (World * View * Proj) and the cascades' (World * WorldToMap * Proj) go to the GPU and decide which
// cascade a box is in; the SSE product must give the very bits of the SDK's, or the frames move by a rounding.
XR_TEST(MatrixMul_SameBits)
{
  uint32_t s = 12345;
  for (int n = 0; n < 20000; n++)
  {
    SMat l, r;
    for (float &f : l.data)
      f = Next(s);
    for (float &f : r.data)
      f = Next(s);
    const SMat Want = Reference(l, r);
    const SMat Got = MatrixMul::Mul(l, r);
    CHECK_MSG(SameBits(Got, Want), "pair " + std::to_string(n));
    if (!SameBits(Got, Want))
      break;
  }
}

// Why: a caller may write the product over a factor; every row of the factors is read before any of the result is written.
XR_TEST(MatrixMul_InPlace)
{
  uint32_t s = 777;
  SMat l, r;
  for (float &f : l.data)
    f = Next(s);
  for (float &f : r.data)
    f = Next(s);
  const SMat Want = Reference(l, r);
  SMat a = l;
  MatrixMul::Mul44(a.data, r.data, a.data);
  CHECK(SameBits(a, Want));
  SMat b = r;
  MatrixMul::Mul44(l.data, b.data, b.data);
  CHECK(SameBits(b, Want));
}
