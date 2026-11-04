// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FoamPattern.h
//	Description: The pattern of the sea's foam (Water.cpp's texture; the
//	unit tests), on the CPU, without the renderer's types: where a patch
//	of foam of a given amount is white.
//
//	Foam on the sea is a lace of bubbles: white rims between dark holes of
//	all sizes, clumped into patches. The pattern tiles (SIZE x SIZE
//	texels): four octaves of Worley's cellular noise (OCTAVES cells a side,
//	a jittered point a cell, the tile wrapping) give the rims (where the
//	nearest point and the next are about as near, F2 - F1 small) and the
//	bubbles' fill (the nearest point near), and a wrapping value noise of
//	4 x 4 knots the patches. Their weighted sum is then ranked: each value
//	0..255 as often as any other, so that the share of the texels above 1 -
//	a is a (Water.hlsl's foam is white where the pattern is above one less
//	its amount: the amount is the share of the surface white).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_FOAMPATTERN_H
#define XRENDERD3D12_FOAMPATTERN_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace FoamPattern
{
  constexpr int SIZE = 512;                         //!< texels a side
  constexpr int OCTAVES[4] = { 5, 9, 17, 33 };      //!< cells a side of each octave (no common divisor: no octave lines up with another)
  constexpr float WEIGHTS[4] = { 1.0f, 0.8f, 0.6f, 0.45f };
  constexpr float PATCH_WEIGHT = 1.6f;              //!< the value noise's weight: the foam clumps into patches

  //! PCG's hash (O'Neill).
  inline uint32_t Hash(uint32_t v)
  {
    const uint32_t s = v * 747796405u + 2891336453u;
    const uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
  }

  //! 0..1 of a hash of (a, b, c).
  inline float Random(uint32_t a, uint32_t b, uint32_t c)
  {
    return (Hash(a * 0x9E3779B9u ^ Hash(b * 0x85EBCA6Bu ^ Hash(c + 0x27D4EB2Fu))) & 0xFFFFFF) / 16777216.0f;
  }

  //! The pattern's texels (row by row), 0..255, each value as often as any other (within one), tiling.
  inline std::vector<uint8_t> Make(int N = SIZE)
  {
    std::vector<float> Value((size_t)N * N, 0.0f);
    for (int o = 0; o < 4; o++)
    {
      const int n = OCTAVES[o];
      std::vector<float> PX((size_t)n * n), PY((size_t)n * n);
      for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
        {
          PX[(size_t)j * n + i] = i + Random(o, i, j * 2);
          PY[(size_t)j * n + i] = j + Random(o, i, j * 2 + 1);
        }
      for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
        {
          // The texel in cells; the nearest point and the next of the 3 x 3 cells about it (the tile wrapping).
          const float u = (x + 0.5f) * n / N, v = (y + 0.5f) * n / N;
          const int ci = (int)u, cj = (int)v;
          float f1 = 1e9f, f2 = 1e9f;
          for (int dj = -1; dj <= 1; dj++)
            for (int di = -1; di <= 1; di++)
            {
              const int i = ci + di, j = cj + dj;
              const int wi = (i + n) % n, wj = (j + n) % n;
              const float px = PX[(size_t)wj * n + wi] + (i - wi), py = PY[(size_t)wj * n + wi] + (j - wj);
              const float d = std::sqrt((px - u) * (px - u) + (py - v) * (py - v));
              if (d < f1)
              {
                f2 = f1;
                f1 = d;
              }
              else if (d < f2)
                f2 = d;
            }
          const float fRim = 1 - std::clamp((f2 - f1) / 0.3f, 0.0f, 1.0f);
          const float fFill = std::clamp(1 - f1 * 1.2f, 0.0f, 1.0f);
          Value[(size_t)y * N + x] += WEIGHTS[o] * (0.7f * fRim * fRim + 0.3f * fFill);
        }
    }
    // The patches: a value noise of 4 x 4 knots, smoothstepped between them, wrapping.
    constexpr int K = 4;
    float Knot[K][K];
    for (int j = 0; j < K; j++)
      for (int i = 0; i < K; i++)
        Knot[j][i] = Random(7, i, j);
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        const float u = (x + 0.5f) * K / N, v = (y + 0.5f) * K / N;
        const int i = (int)u, j = (int)v;
        float fu = u - i, fv = v - j;
        fu = fu * fu * (3 - 2 * fu);
        fv = fv * fv * (3 - 2 * fv);
        const float a = Knot[j % K][i % K], b = Knot[j % K][(i + 1) % K], c = Knot[(j + 1) % K][i % K], d = Knot[(j + 1) % K][(i + 1) % K];
        Value[(size_t)y * N + x] += PATCH_WEIGHT * ((a * (1 - fu) + b * fu) * (1 - fv) + (c * (1 - fu) + d * fu) * fv);
      }
    // Ranked: the texel's place among all of them, 0..255.
    std::vector<uint32_t> Order((size_t)N * N);
    std::iota(Order.begin(), Order.end(), 0u);
    std::stable_sort(Order.begin(), Order.end(), [&Value](uint32_t a, uint32_t b) { return Value[a] < Value[b]; });
    std::vector<uint8_t> Out((size_t)N * N);
    for (size_t r = 0; r < Order.size(); r++)
      Out[Order[r]] = (uint8_t)(r * 256 / Order.size());
    return Out;
  }
}

#endif
