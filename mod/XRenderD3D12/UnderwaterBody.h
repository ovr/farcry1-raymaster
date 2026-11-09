// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: UnderwaterBody.h
//	Description: The bodies under the ocean's surface that the water
//	looks past (d3d12_UnderwaterBodies): a character (the fish, a
//	shark) under the ocean's level (a small one: its middle), the camera
//	above it. The main
//	view draws them after the rest of its opaque list without writing the
//	depth (Draw3D.cpp's DrawItems), so that the water over them takes the
//	floor under them: the shore's foam (Water.hlsl, its depth under the
//	surface from the scene behind) took a fish swimming just under the
//	surface for a shallow floor and laid white foam in its shape over the
//	water (Steam's night sea), and their colour fades by the water down to
//	that floor, as the original's fog volume pass hides them. Unit tests:
//	UnderwaterBodyTests.cpp.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_UNDERWATERBODY_H
#define XRENDERD3D12_UNDERWATERBODY_H

#include <cfloat>
#include <cstring>

namespace UnderwaterBody
{
//! A body's lowest and highest point in the world (m); none (fBottom > fTop) without vertices.
struct SSpan
{
  float fBottom = FLT_MAX, fTop = -FLT_MAX;
};

//! How far under the level a body's top must be (m): the waves' troughs leave one just under it in the air.
constexpr float MARGIN = 0.05f;
//! A small body (a fish) is shorter than this (m) from its lowest to its highest point. One with its middle under the level
//! counts as in the water though its back reaches over it (Steam's fish rise to 0.15-0.25 m over the level 42 m, under the
//! waves' crests): the water is drawn over the little in the air. A swimmer's head stays.
constexpr float SMALL = 1.0f;

//! Whether the water looks past an element: a character's (bCharacter) whose span s is under the ocean's level fLevel by
//! MARGIN, or a small one with its middle under the level; the camera (fCameraZ) above that level.
inline bool LooksPast(bool bCharacter, SSpan s, float fLevel, float fCameraZ)
{
  if (!bCharacter || !(s.fBottom <= s.fTop) || fCameraZ <= fLevel)
    return false;
  if (s.fTop < fLevel - MARGIN)
    return true;
  return s.fTop - s.fBottom < SMALL && (s.fTop + s.fBottom) * 0.5f < fLevel;
}

//! The span in the world of nVerts vertices nStride bytes apart, each starting with its position (three floats) in the
//! object's space, m the object's matrix (row vectors: z = x m[2] + y m[6] + z m[10] + m[14]); none without any (a
//! character's leaf buffer has no box once skinned: ProcessSkinning writes the vertices into its video copy).
inline SSpan ZSpan(const void *pVerts, int nVerts, int nStride, const float *m)
{
  SSpan s;
  if (!pVerts || nVerts <= 0 || nStride < (int)(3 * sizeof(float)))
    return s;
  for (int i = 0; i < nVerts; i++)
  {
    float p[3];
    memcpy(p, (const unsigned char *)pVerts + (size_t)i * nStride, sizeof(p));
    const float fZ = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
    s.fBottom = fZ < s.fBottom ? fZ : s.fBottom;
    s.fTop = fZ > s.fTop ? fZ : s.fTop;
  }
  return s;
}
}

#endif
