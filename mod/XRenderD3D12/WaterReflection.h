// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WaterReflection.h
//	Description: When the water's planar reflection is drawn again (Water.cpp). The water
//	reads it through the matrix it was drawn with, so a floor on its interval only delays what
//	moves in it; at the water the engine's rule alone redraws it every frame.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WATERREFLECTION_H
#define XRENDERD3D12_WATERREFLECTION_H

#include <algorithm>

namespace WaterReflection
{
  constexpr float MAX_INTERVAL_S = 0.3f;   //!< the engine's rule waits at most this long far from the water

  //! Due after fFactor seconds per metre to the water (at most MAX_INTERVAL_S, at least fMinInterval), or at once
  //! when the camera moved more than fMaxMove or turned so that its directions' cosine fell under fMinCos.
  inline bool IsDue(float fAge, float fToWater, float fMoved, float fCos, float fFactor, float fMinInterval, float fMaxMove,
                    float fMinCos)
  {
    const float fInterval = std::max(std::min(fFactor * fToWater, MAX_INTERVAL_S), fMinInterval);
    return fAge > fInterval || fMoved > fMaxMove || fCos < fMinCos;
  }
}

#endif
