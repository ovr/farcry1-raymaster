// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LightBeam.h
//	Description: The vertices of a light's beam (Beams.cpp; the unit
//	tests), as the original's vertex shader of the beams makes them. A
//	beam is its shader's cone model (objects/indoor/lights/beam.cgf: along
//	x from 0 to its length, of radius 1 across y and z) put at the light,
//	in its axes: x forward (where the light points), y up, z right. With
//	t = x / the model's length:
//	  - the point (t Length, y R / r, z R / r), R = StartRadius + t
//	    (EndRadius - StartRadius), r the model's radius: the cone from
//	    StartRadius at the light to EndRadius Length away;
//	  - the colour lerp(StartColor, EndColor, t), its alpha times d^2
//	    min(10 t, 1), d the cosine between the model's normal (as the model
//	    has it, not scaled with the cone) and the way from the point to the
//	    camera: the cone's sides seen from the side are clear, its outline
//	    and its tip at the light fade out;
//	  - each component clamped to 0 .. 1 (a negative EndColor alpha ends
//	    the beam before its far end).
//	Without the engine's types: plain float arrays.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_LIGHTBEAM_H
#define XRENDERD3D12_LIGHTBEAM_H

#include <algorithm>
#include <cmath>

namespace LightBeam
{
  //! A beam (SBeam) with the size of its model.
  struct SParams
  {
    float fStartRadius = 0.1f, fEndRadius = 1, fLength = 1;
    float StartColor[4] = { 1, 1, 1, 1 }, EndColor[4] = { 1, 1, 1, 0 };
    float fModelLength = 10;            //!< the model's length along x (from 0)
    float fModelRadius = 1;             //!< the model's radius across y and z
  };

  //! A vertex of the beam: where it is in the light's axes (x forward, y up, z right, metres from
  //! the light) and its colour (r g b a, 0 .. 1).
  struct SVertex
  {
    float p[3];
    float Color[4];
  };

  //! The vertex the model's vertex at Pos with the normal Normal makes, vCamera the camera in the
  //! light's axes.
  inline SVertex Transform(const SParams &b, const float Pos[3], const float Normal[3], const float vCamera[3])
  {
    SVertex v;
    const float t = b.fModelLength != 0 ? Pos[0] / b.fModelLength : 0;
    const float fRadius = b.fStartRadius + t * (b.fEndRadius - b.fStartRadius);
    const float fScale = b.fModelRadius != 0 ? fRadius / b.fModelRadius : 0;
    v.p[0] = t * b.fLength;
    v.p[1] = Pos[1] * fScale;
    v.p[2] = Pos[2] * fScale;

    float vTo[3] = { vCamera[0] - v.p[0], vCamera[1] - v.p[1], vCamera[2] - v.p[2] };
    const float fTo = std::sqrt(vTo[0] * vTo[0] + vTo[1] * vTo[1] + vTo[2] * vTo[2]);
    const float fN = std::sqrt(Normal[0] * Normal[0] + Normal[1] * Normal[1] + Normal[2] * Normal[2]);
    const float d = fTo > 0 && fN > 0 ? (vTo[0] * Normal[0] + vTo[1] * Normal[1] + vTo[2] * Normal[2]) / (fTo * fN) : 0;

    for (int c = 0; c < 4; c++)
      v.Color[c] = b.StartColor[c] + t * (b.EndColor[c] - b.StartColor[c]);
    v.Color[3] *= d * d * std::min(10 * t, 1.0f);
    for (float &c : v.Color)
      c = std::clamp(c, 0.0f, 1.0f);
    return v;
  }
}

#endif
