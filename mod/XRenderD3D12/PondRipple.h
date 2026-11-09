// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PondRipple.h
//	Description: The ripple of the ponds and lakes the level's terrain
//	cuts off from the sea (d3d12_WaterPondRipple; WaterBodies.h finds
//	them): the original's in place of the FFT ocean's waves. The game's
//	terrain water (TerrainWater_OnlySky, terrainWater.csl) bends its
//	reflection by a du/dv map, a sequence of 32 frames
//	(textures/animated/water/caust##, 64 x 64, made from the grey height
//	maps caust00 to caust31), 0.05 s a frame looping, laid on the water
//	at a tile of 8 m (u = x / 8, v = y / 8) drifting 0.06 and 0.12 of a
//	tile a second: a large slow ripple with dark crests on a still pond,
//	where the FFT ocean's small wind waves stood on it. Here the height
//	maps' slopes in the wave texture's form (Water.cpp's MakeWaveTexture:
//	red dH/du, green dH/dv, blue the height, 128 for 0 and 32 the RMS;
//	alpha the square of the slopes, SLOPE_SQ_RANGE at 255, whose mip
//	levels give the variance they smooth out), a texture array of the
//	frames; Water.hlsl's PondRipple. Without the renderer's types, for the
//	unit tests (PondTests.cpp).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PONDRIPPLE_H
#define XRENDERD3D12_PONDRIPPLE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace PondRipple
{
  constexpr int FRAMES = 32;              //!< caust00 to caust31
  constexpr int SIZE = 64;                //!< their texels a side
  constexpr double FRAME_TIME = 0.05;     //!< s a frame (the sequence's Time)
  constexpr float TILE = 8;               //!< m a tile (the texgen's 0.125)
  constexpr double DRIFT[2] = { 0.06, 0.12 };  //!< tiles a second along u and v (TexShiftRipple's 'time 0.06', 'time 0.12')
  constexpr float SLOPE_SQ_RANGE = 16;    //!< the alpha's square of the slopes at 255 (Water.cpp's WAVE_SLOPE_SQ_RANGE)

  //! The frames' texels, B, G, R, A bytes each (frame after frame, rows from the top), from their heights (nFrames
  //! frames of N x N, any scale): the slopes by central differences across the tile's wrap, dH/du in red and dH/dv in
  //! green, the height in blue, scaled together over all frames so that the slopes' RMS along an axis and the height's
  //! RMS are 32 about 128; alpha (dH/du^2 + dH/dv^2) of the stored slopes, SLOPE_SQ_RANGE at 255. Flat heights: flat.
  inline std::vector<uint8_t> MakeSlopes(const std::vector<float> &Heights, int N, int nFrames)
  {
    const size_t nPlane = (size_t)N * N;
    std::vector<float> Field(nPlane * nFrames * 3);
    double fSlopeSq = 0, fMean = 0;
    for (size_t i = 0; i < nPlane * nFrames; i++)
      fMean += Heights[i];
    fMean /= (double)(nPlane * nFrames);
    double fHeightSq = 0;
    for (int f = 0; f < nFrames; f++)
      for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
        {
          const float *h = &Heights[f * nPlane];
          const float sx = 0.5f * (h[(size_t)y * N + (x + 1) % N] - h[(size_t)y * N + (x + N - 1) % N]);
          const float sy = 0.5f * (h[(size_t)((y + 1) % N) * N + x] - h[(size_t)((y + N - 1) % N) * N + x]);
          const float fH = (float)(h[(size_t)y * N + x] - fMean);
          float *pOut = &Field[(f * nPlane + (size_t)y * N + x) * 3];
          pOut[0] = sx;
          pOut[1] = sy;
          pOut[2] = fH;
          fSlopeSq += 0.5 * ((double)sx * sx + (double)sy * sy);
          fHeightSq += (double)fH * fH;
        }
    const double n = (double)(nPlane * nFrames);
    const float fSlopeScale = fSlopeSq > 0 ? (float)(32 / std::sqrt(fSlopeSq / n)) : 0.0f;
    const float fHeightScale = fHeightSq > 0 ? (float)(32 / std::sqrt(fHeightSq / n)) : 0.0f;
    const auto Encode = [](float v) { return (uint8_t)std::clamp((int)std::lrint(128 + v), 1, 255); };
    std::vector<uint8_t> Pixels(nPlane * nFrames * 4);
    for (size_t i = 0; i < nPlane * nFrames; i++)
    {
      Pixels[i * 4 + 0] = Encode(Field[i * 3 + 2] * fHeightScale);
      Pixels[i * 4 + 1] = Encode(Field[i * 3 + 1] * fSlopeScale);
      Pixels[i * 4 + 2] = Encode(Field[i * 3 + 0] * fSlopeScale);
      const float dx = (Pixels[i * 4 + 2] - 128) / 32.0f, dy = (Pixels[i * 4 + 1] - 128) / 32.0f;
      Pixels[i * 4 + 3] = (uint8_t)std::clamp((int)std::lrint((dx * dx + dy * dy) * (255 / SLOPE_SQ_RANGE)), 0, 255);
    }
    return Pixels;
  }

  //! The sequence's place at fTime (s): the frame shown (the original's (int)(fTime / FRAME_TIME) modulo FRAMES) plus
  //! the share of the way to the next one (the shader blends the two, so that the ripple does not step 20 times a
  //! second), in [0, FRAMES).
  inline double Frame(double fTime)
  {
    const double f = std::fmod(std::max(fTime, 0.0) / FRAME_TIME, (double)FRAMES);
    return f < FRAMES ? f : 0.0;
  }

  //! The tile's drift at fTime (s) along u or v (nAxis 0, 1), in tiles, wrapped to [0, 1).
  inline double Drift(double fTime, int nAxis)
  {
    const double d = std::max(fTime, 0.0) * DRIFT[nAxis];
    return d - std::floor(d);
  }
}

#endif
