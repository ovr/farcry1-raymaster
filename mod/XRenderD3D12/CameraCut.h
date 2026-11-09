// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CameraCut.h
//	Description: Whether the main view's camera cut to another shot
//	between two frames (d3d12_CutReset, NoteCameraCut in Upscale.cpp): a
//	cut scene's camera moved farther than a step or turned wider than a
//	turn in one frame. Then the histories of the screen (the upscaler's,
//	the rays' denoisers, the path tracer's light, the volumetric light)
//	start again instead of reprojecting the shot before onto the new one:
//	Training's opening cut scene turns 130-160 degrees at its cuts (2-180
//	m), its camera otherwise less than a degree and a few centimetres a
//	frame. The unit tests (CameraCutTests.cpp) hold it to that.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_CAMERACUT_H
#define XRENDERD3D12_CAMERACUT_H

#include <cmath>

namespace CameraCut
{
  constexpr float MAX_STEP_M = 4;      //!< farther in a frame: a cut (a vehicle at 100 km/h moves 1 m a frame at 30 frames a second)
  constexpr float MAX_TURN_DEG = 45;   //!< a wider turn in a frame: a cut (a mouse's fast flick turns some 10-20 degrees)

  //! Whether the camera at p looking along d (both directions of unit length) cut from the frame before's (p0 looking
  //! along d0): it moved more than fMaxStep m or its direction turned more than fMaxTurnDeg degrees.
  inline bool IsCut(const float p0[3], const float d0[3], const float p[3], const float d[3], float fMaxStep = MAX_STEP_M,
                    float fMaxTurnDeg = MAX_TURN_DEG)
  {
    const float dx = p[0] - p0[0], dy = p[1] - p0[1], dz = p[2] - p0[2];
    if (dx * dx + dy * dy + dz * dz > fMaxStep * fMaxStep)
      return true;
    const float fCos = d0[0] * d[0] + d0[1] * d[1] + d0[2] * d[2];
    return fCos < cosf(fMaxTurnDeg * 3.14159265f / 180);
  }
}

#endif
