// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestMath.h
//	Description: Small helpers of the unit tests: 3D vectors, a seeded
//	random generator (the same items every run), the words of the GPU
//	harness's uint4 items as floats and back.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_TESTS_TESTMATH_H
#define XRENDERD3D12_TESTS_TESTMATH_H

#include "TestTypes.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace XrTest
{
  struct SVec3
  {
    float x = 0, y = 0, z = 0;
  };

  inline SVec3 operator+(SVec3 a, SVec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
  inline SVec3 operator-(SVec3 a, SVec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
  inline SVec3 operator-(SVec3 a) { return { -a.x, -a.y, -a.z }; }
  inline SVec3 operator*(SVec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
  inline float Dot(SVec3 a, SVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  inline float Length(SVec3 a) { return std::sqrt(Dot(a, a)); }
  inline SVec3 Normalize(SVec3 a) { return a * (1 / Length(a)); }
  inline float Distance(SVec3 a, SVec3 b) { return Length(a - b); }

  inline uint32 Bits(float f)
  {
    uint32 n;
    memcpy(&n, &f, 4);
    return n;
  }

  inline float Float(uint32 n)
  {
    float f;
    memcpy(&f, &n, 4);
    return f;
  }

  //! An input item of four floats or four words.
  inline void Push(std::vector<uint32> &v, float a, float b, float c, float d)
  {
    v.insert(v.end(), { Bits(a), Bits(b), Bits(c), Bits(d) });
  }
  inline void Push(std::vector<uint32> &v, SVec3 a, float w) { Push(v, a.x, a.y, a.z, w); }
  inline void PushWords(std::vector<uint32> &v, uint32 a, uint32 b, uint32 c, uint32 d) { v.insert(v.end(), { a, b, c, d }); }

  //! Output item i's first three words as a vector, word k of it as a float.
  inline SVec3 Vec(const std::vector<uint32> &Out, size_t i) { return { Float(Out[i * 4]), Float(Out[i * 4 + 1]), Float(Out[i * 4 + 2]) }; }
  inline float At(const std::vector<uint32> &Out, size_t i, int k) { return Float(Out[i * 4 + k]); }

  //! A seeded generator (SplitMix64): the same numbers on every run and every machine.
  class CRandom
  {
  public:
    explicit CRandom(uint64_t nSeed) : m_nState(nSeed) {}
    uint64_t Next()
    {
      uint64_t z = (m_nState += 0x9e3779b97f4a7c15ull);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
      return z ^ (z >> 31);
    }
    //! 0..1
    float Unit() { return (float)((Next() >> 40) * (1.0 / 16777216.0)); }
    float Range(float a, float b) { return a + (b - a) * Unit(); }
    //! A direction, every one as likely.
    SVec3 Direction()
    {
      const float z = Range(-1, 1), a = Range(0, 6.2831853f), r = std::sqrt(std::fmax(0.0f, 1 - z * z));
      return { r * std::cos(a), r * std::sin(a), z };
    }

  private:
    uint64_t m_nState;
  };

  //! The groups of a one dimensional dispatch of n items, 64 threads a group.
  inline uint32 Groups64(size_t n) { return (uint32)((n + 63) / 64); }
}

#endif
