// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LightImage.h
//	Description: The images of projected lights (Texture.cpp; the unit
//	tests), as the engine asks for them with the texture's flags and as
//	the original casts them (d3d12_LightImages). One picture made a cube
//	has it on +X alone, or on all six faces for FT2_REPLICATETOALLSIDES
//	(Training's bunker's water caustics lamps: traced, the six faces of
//	the original's cube are the same picture). An image loaded with
//	FT2_CHECKFORALLSEQUENCES is the frame <name>00 of a sequence (the
//	digits in front of the extension counting up while the files are
//	found, causq00 to causq31): the light casts frame (int)(t / s)
//	modulo the frames at the timer's time t with s = the light's
//	m_fAnimSpeed seconds a frame (traced at t 10.28, 10.30, 10.38 and
//	10.58 with s 0.05: frames 13, 14, 15 and 19 of 32). Without the
//	renderer's types.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_LIGHTIMAGE_H
#define XRENDERD3D12_LIGHTIMAGE_H

namespace LightImage
{
  //! The faces (D3D order: +X, -X, +Y, -Y, +Z, -Z) of a cube made of one picture that have it: +X alone, all six with
  //! bAllSides; the others are transparent black.
  constexpr bool FaceHasPicture(int nFace, bool bAllSides)
  {
    return nFace == 0 || (bAllSides && nFace > 0 && nFace < 6);
  }

  //! The frame of a sequence of nFrames a light casts at the time fTime with fFrameTime seconds a frame: (int)(fTime /
  //! fFrameTime) modulo nFrames; 0 for one frame, without a frame time and before the time 0.
  inline int SequenceFrame(float fTime, float fFrameTime, int nFrames)
  {
    if (nFrames <= 1 || !(fFrameTime > 0) || !(fTime >= 0))
      return 0;
    const float fFrame = fTime / fFrameTime;
    if (!(fFrame < 9.0e18f))
      return 0;
    return (int)((long long)fFrame % nFrames);
  }
}

#endif
