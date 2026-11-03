// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: BeamTests.cpp
//	Description: Unit tests of the lights' beams (LightBeam.h, Beams.cpp),
//	on the CPU: the vertices of the cone model as the original's vertex
//	shader of the beams makes them, with the numbers of Treehouse's
//	corridor lamps (LightBeamHangingYellow_Flare: radius 0.2 to 7 over 12
//	m, colour (0.99 0.95 0.42 0.4) to (0.59 0.58 0.42 -0.3); the model
//	10 long, of radius 1).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"

#include "LightBeam.h"

#include <cmath>

using namespace XrTest;

namespace
{
  LightBeam::SParams Yellow()
  {
    LightBeam::SParams b;
    b.fStartRadius = 0.2f;
    b.fEndRadius = 7;
    b.fLength = 12;
    const float s[4] = { 0.99f, 0.95f, 0.42f, 0.4f }, e[4] = { 0.59f, 0.58f, 0.42f, -0.3f };
    for (int c = 0; c < 4; c++)
    {
      b.StartColor[c] = s[c];
      b.EndColor[c] = e[c];
    }
    b.fModelLength = 10;
    b.fModelRadius = 1;
    return b;
  }

  //! The vertex of the model's point (x, y, z) with the normal n, the camera at c (the light's axes).
  LightBeam::SVertex At(const LightBeam::SParams &b, float x, float y, float z, const float n[3], const float c[3])
  {
    const float p[3] = { x, y, z };
    return LightBeam::Transform(b, p, n, c);
  }
}

//! The cone: the model's ring at x = 0 at the light with StartRadius, its ring at its length Length away with
//! EndRadius, a ring between them in proportion. Why: Treehouse's corridors had no yellow cones under their lamps
//! (2026-11-03): the beams of the lights' shaders were not drawn at all.
XR_TEST(LightBeam_Cone)
{
  const LightBeam::SParams b = Yellow();
  const float n[3] = { 0, 1, 0 }, c[3] = { 5, 20, 0 };
  for (float x : { 0.0f, 2.5f, 5.0f, 10.0f })
  {
    const float t = x / 10, R = 0.2f + t * (7 - 0.2f);
    const LightBeam::SVertex v = At(b, x, 0.6f, -0.8f, n, c);
    CHECK_NEAR(v.p[0], t * 12, 1e-5);
    CHECK_NEAR(v.p[1], 0.6f * R, 1e-5);
    CHECK_NEAR(v.p[2], -0.8f * R, 1e-5);
  }
}

//! The colour: rgb from StartColor to EndColor along the cone; the alpha the colours' alpha times d^2 (d the cosine
//! between the normal and the way to the camera, the normal not scaled with the cone) times min(10 t, 1), clamped: 0 at
//! the light, full a tenth of the way, ending where the colours' alpha goes below 0 (t 4/7 here).
XR_TEST(LightBeam_Colour)
{
  const LightBeam::SParams b = Yellow();
  const float n[3] = { 0, 2, 0 };
  // The camera straight along the normal from each point: d = 1.
  for (float t : { 0.0f, 0.05f, 0.1f, 0.3f, 0.5f, 4.0f / 7, 0.8f, 1.0f })
  {
    const float R = 0.2f + t * 6.8f;
    const float c[3] = { t * 12, R + 30, 0 };
    const LightBeam::SVertex v = At(b, t * 10, 1, 0, n, c);
    CHECK_NEAR(v.Color[0], 0.99f + t * (0.59f - 0.99f), 1e-5);
    CHECK_NEAR(v.Color[1], 0.95f + t * (0.58f - 0.95f), 1e-5);
    CHECK_NEAR(v.Color[2], 0.42f, 1e-5);
    const float a = 0.4f + t * (-0.3f - 0.4f);
    CHECK_NEAR(v.Color[3], std::fmax(0.0f, a * std::fmin(10 * t, 1.0f)), 1e-5);
  }
  // A colour's alpha above 1 (LightBeamHangingYellow05: 2.5) clamped to 1.
  LightBeam::SParams Bright = b;
  Bright.StartColor[3] = 2.5f;
  const float c[3] = { 6, 40, 0 };
  CHECK_NEAR(At(Bright, 5, 1, 0, n, c).Color[3], 1, 1e-6);
}

//! The sides fade where the camera sees them edge on: alpha times cos^2 of the angle between the normal and the way to
//! the camera, whichever side the normal faces.
XR_TEST(LightBeam_EdgeOn)
{
  const LightBeam::SParams b = Yellow();
  const float t = 0.3f, R = 0.2f + t * 6.8f, a = 0.4f - t * 0.7f;
  const float vPoint[3] = { t * 12, R, 0 };
  for (float fDeg : { 0.0f, 30.0f, 60.0f, 90.0f, 120.0f, 180.0f })
  {
    // The camera 10 m from the point, fDeg from the normal (0 1 0) towards the light's axis x.
    const float r = fDeg * 3.14159265f / 180;
    const float c[3] = { vPoint[0] + 10 * std::sin(r), vPoint[1] + 10 * std::cos(r), 0 };
    const float n[3] = { 0, 1, 0 };
    const float d = std::cos(r);
    CHECK_NEAR(At(b, t * 10, 1, 0, n, c).Color[3], a * d * d, 1e-5);
  }
}
