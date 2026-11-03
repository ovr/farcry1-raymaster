// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: FarSpriteAngles.h
//	Description: The directions of the far vegetation sprites' pictures
//	(FarSprites.cpp; the unit tests). MakeSprite gets the angles 90 to 435
//	degrees in steps of 15, a picture each: slice n of the object's array
//	shows it seen in the direction (from the camera to the object, degrees
//	in the horizontal plane) 82.5 - 15 n. The original shows the picture of
//	the 15 degrees the direction to the sprite's centre falls in, so the
//	far trees change their outline at once whenever it crosses a multiple of
//	15 (d3d12_SpriteBlend 0); d3d12_SpriteBlend blends the pictures of the
//	two directions nearest it over that part of the 15 degrees about the
//	change (BandBlend; 1: over all of them, by how near each is). The
//	quad's v gives the shader the blend (Shaders/ArrayCoords.hlsli).
//	Without the renderer's types.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_FARSPRITEANGLES_H
#define XRENDERD3D12_FARSPRITEANGLES_H

#include <cmath>

namespace FarSpriteAngles
{
  constexpr int COUNT = 24;        //!< the pictures of an object
  constexpr float STEP = 15.0f;    //!< degrees between them
  constexpr float FIRST = 82.5f;   //!< the direction slice 0 shows

  //! The slice (0 to 23) of MakeSprite's angle fAngle (90 to 435 degrees in steps of 15).
  inline int SliceOfAngle(float fAngle)
  {
    const int n = (int)lroundf((fAngle - 90) / STEP) % COUNT;
    return n < 0 ? n + COUNT : n;
  }

  //! The direction (degrees, 0 to 360) slice n shows the object in: the middle of the directions SliceOfDirection gives it
  //! for.
  inline float ViewDirection(int n)
  {
    const float f = fmodf(FIRST - STEP * n, 360.0f);
    return f < 0 ? f + 360.0f : f;
  }

  //! The slice the original shows for the direction fDegrees (0 to 360) from the camera to the sprite's centre: that of
  //! MakeSprite's angle 165 - 15 floor(fDegrees / 15), brought into 90 to 435.
  inline int SliceOfDirection(float fDegrees)
  {
    int nAngle = 165 - 15 * (int)floorf(fDegrees / STEP);
    while (nAngle < 90)
      nAngle += 360;
    while (nAngle >= 450)
      nAngle -= 360;
    return (nAngle - 90) / 15;
  }

  //! Two pictures blended: slice n0 and the next one n1, n1's weight fW (0 to 1).
  struct SBlend
  {
    int n0 = 0, n1 = 0;
    float fW = 0;
  };

  //! The pictures of the two directions nearest fDegrees (0 to 360): n0's at or after it (the slices' directions go down
  //! with n), n1 = n0 + 1's before it, fW by how far it is from n0's towards n1's (0 at n0's, 0.5 where SliceOfDirection
  //! changes from n0 to n1, 1 at n1's): the weights change smoothly with the direction.
  inline SBlend BlendOfDirection(float fDegrees)
  {
    float t = (FIRST - fDegrees) / STEP;
    t -= COUNT * floorf(t / COUNT);
    SBlend b;
    b.n0 = (int)floorf(t);
    b.fW = t - (float)b.n0;
    if (b.n0 >= COUNT)
    {
      b.n0 = 0;
      b.fW = 0;
    }
    b.n1 = (b.n0 + 1) % COUNT;
    return b;
  }

  //! The blend b over the part fBand (0 to 1) of the 15 degrees about where the original changes from b.n0 to b.n1 (its
  //! weight 0.5): its weight from 0 to 1 across the band, outside it the picture of that side alone (n0 = n1, the weight
  //! 0). 1 and above: b as it is; 0 and below: the original's picture.
  inline SBlend BandBlend(SBlend b, float fBand)
  {
    if (fBand >= 1)
      return b;
    const float w = fBand > 0 ? fminf(fmaxf((b.fW - 0.5f) / fBand + 0.5f, 0.0f), 1.0f) : (b.fW < 0.5f ? 0.0f : 1.0f);
    if (w <= 0)
      b.n1 = b.n0;
    else if (w >= 1)
      b.n0 = b.n1;
    b.fW = w > 0 && w < 1 ? w : 0.0f;
    return b;
  }

  //! The weight of the next slice as the shader gets it (ArrayCoords.hlsli): m - 1 = round(fW * 255).
  inline int BlendStep(float fW)
  {
    return 1 + (int)lroundf(fminf(fmaxf(fW, 0.0f), 1.0f) * 255);
  }

  //! A quad's v (v' from 0 to 1 down the picture) with the next slice's weight fW: v' + 2 BlendStep(fW) + 0.5 (with a
  //! single slice v is v' as it is).
  inline float BlendV(float v, float fW)
  {
    return v + 2.0f * (float)BlendStep(fW) + 0.5f;
  }
}

#endif
