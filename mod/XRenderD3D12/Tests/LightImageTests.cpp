// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LightImageTests.cpp
//	Description: Unit tests of the images of projected lights
//	(LightImage.h), on the CPU: the faces of a cube made of one picture
//	that have it (+X alone, all six for FT2_REPLICATETOALLSIDES) and the
//	frame of an image's sequence a light casts at a time, against the
//	frames the original cast for Training's bunker's caustics lamps
//	(traced: 32 frames, 0.05 s a frame).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "TestMath.h"

#include "LightImage.h"

using namespace XrTest;

XR_TEST(LightImage_Faces)
{
  CHECK(LightImage::FaceHasPicture(0, false));
  CHECK(LightImage::FaceHasPicture(0, true));
  for (int nFace = 1; nFace < 6; nFace++)
  {
    CHECK(!LightImage::FaceHasPicture(nFace, false));
    CHECK(LightImage::FaceHasPicture(nFace, true));
  }
  CHECK(!LightImage::FaceHasPicture(6, true));
  CHECK(!LightImage::FaceHasPicture(-1, true));
}

XR_TEST(LightImage_SequenceFrame)
{
  // The original's frames at the traced times.
  CHECK(LightImage::SequenceFrame(10.28f, 0.05f, 32) == 13);
  CHECK(LightImage::SequenceFrame(10.38f, 0.05f, 32) == 15);
  CHECK(LightImage::SequenceFrame(10.58f, 0.05f, 32) == 19);
  // The last frame, then the first again.
  CHECK(LightImage::SequenceFrame(1.57f, 0.05f, 32) == 31);
  CHECK(LightImage::SequenceFrame(1.62f, 0.05f, 32) == 0);
  CHECK(LightImage::SequenceFrame(0.0f, 0.05f, 32) == 0);
  // Every frame is one of the sequence, however long the game runs.
  for (float t = 0; t < 4000; t += 13.37f)
  {
    const int n = LightImage::SequenceFrame(t, 0.05f, 32);
    CHECK(n >= 0 && n < 32);
  }
  // One frame, no frame time, a time before 0, a time too large to count frames: the first frame.
  CHECK(LightImage::SequenceFrame(10.28f, 0.05f, 1) == 0);
  CHECK(LightImage::SequenceFrame(10.28f, 0.05f, 0) == 0);
  CHECK(LightImage::SequenceFrame(10.28f, 0.0f, 32) == 0);
  CHECK(LightImage::SequenceFrame(10.28f, -0.05f, 32) == 0);
  CHECK(LightImage::SequenceFrame(-1.0f, 0.05f, 32) == 0);
  CHECK(LightImage::SequenceFrame(1.0e30f, 0.05f, 32) == 0);
  CHECK(LightImage::SequenceFrame(10.28f, 1.0e-30f, 32) == 0);
}
