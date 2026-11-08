// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CascadeCullTests.cpp
//	Description: Unit tests of what the sun's cascades leave out early
//	(d3d12_CascadeCull, CascadeCull.h): Reaches, by which DrawCascadeItems
//	draws a part into a slice and CScene::AddEf leaves out the parts that
//	reach none, and CasterFar, the far plane of the camera towards the
//	sun: no caster beyond it can shade a receiver.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "CascadeCull.h"

#include <cmath>
#include <cstdint>

namespace
{
  //! The fields of a cascade that Reaches reads (Renderer.h's SCascade has them).
  struct SSlice
  {
    float fCenter[2] = {};
    float fHalfSize = 0;
    float fZMin = 0;
    float Edges[8][3] = {};
    int nEdges = 0;
    float fMargin = 0;
  };

  //! A square of half size 50 about (10, 20) from z = -40 on, its receivers' outline the square x in [-20, 40],
  //! y in [-10, 50] (four edges, outward normals), with a margin of 2 m.
  SSlice MakeSlice()
  {
    SSlice s;
    s.fCenter[0] = 10;
    s.fCenter[1] = 20;
    s.fHalfSize = 50;
    s.fZMin = -40;
    const float E[4][3] = { { 1, 0, 40 }, { -1, 0, 20 }, { 0, 1, 50 }, { 0, -1, 10 } };
    for (int e = 0; e < 4; e++)
      for (int k = 0; k < 3; k++)
        s.Edges[e][k] = E[e][k];
    s.nEdges = 4;
    s.fMargin = 2;
    return s;
  }

  //! A uniform number in [0, 1) from a 64 bit LCG.
  float Next(uint64_t &n)
  {
    n = n * 6364136223846793005ull + 1442695040888963407ull;
    return (float)((n >> 40) & 0xffffff) / (float)0x1000000;
  }
}

// Why: AddEf leaves out what Reaches would not draw into any slice, so Reaches must keep a part wherever its shadow can
// fall on the slice's receivers, and drop it only off the square, farther from the sun than the slice, or past the
// receivers' outline by more than its radius and the margin; a part without a box reaches every slice.
XR_TEST(CascadeCull_Reaches)
{
  const SSlice s = MakeSlice();
  CHECK(CascadeCull::Reaches(s, 10, 20, 0, 1));
  // Any height towards the sun: the depth is clamped at the map's near end, so it casts.
  CHECK(CascadeCull::Reaches(s, 10, 20, 1000, 1));
  // Farther from the sun than the slice's range, all of the sphere: no.
  CHECK(!CascadeCull::Reaches(s, 10, 20, -42, 1));
  CHECK(CascadeCull::Reaches(s, 10, 20, -40.5f, 1));
  // Off the square.
  CHECK(!CascadeCull::Reaches(s, 62, 20, 0, 1));
  CHECK(!CascadeCull::Reaches(s, 10, -32, 0, 1));
  // Past an edge of the outline: by more than radius + margin out, in by up to that.
  CHECK(!CascadeCull::Reaches(s, 43.5f, 20, 0, 1));
  CHECK(CascadeCull::Reaches(s, 42.5f, 20, 0, 1));
  CHECK(!CascadeCull::Reaches(s, 10, -14, 0, 1));
  CHECK(CascadeCull::Reaches(s, 10, -14, 0, 3));
  // No box: everywhere.
  CHECK(CascadeCull::Reaches(s, 1000, 1000, -1000, -1));
}

// Why: the camera towards the sun ended at the main view's far plane (Training 700 m), though nothing that far along
// the sun can shade the cascades' receivers unless it stands that much higher; a low sun (long shadows) keeps it.
XR_TEST(CascadeCull_FarPlane)
{
  // A sun at 30 degrees: the receivers to 200 m along it, as low as -50, the casters up to 150: 200 + 200 / 0.5 + 10.
  CHECK_NEAR(CascadeCull::CasterFar(2000, 0.5f, 200, -50, 150), 610, 1e-3);
  // Never beyond the view's far plane, never below 1 m.
  CHECK_NEAR(CascadeCull::CasterFar(500, 0.5f, 200, -50, 150), 500, 0);
  CHECK_NEAR(CascadeCull::CasterFar(2000, 1, -100, 0, 0), 1, 0);
  CHECK_NEAR(CascadeCull::CasterFar(0, 1, 50, 0, 10), 1, 0);
  // The casters all below the receivers: the receivers' own depth and the margin.
  CHECK_NEAR(CascadeCull::CasterFar(2000, 0.8f, 100, 50, 20), 110, 1e-3);
  // A sun below 10 degrees, or none: the view's far plane.
  CHECK_NEAR(CascadeCull::CasterFar(700, 0.17f, 100, 0, 100), 700, 0);
  CHECK_NEAR(CascadeCull::CasterFar(700, -0.5f, 100, 0, 100), 700, 0);
  CHECK_NEAR(CascadeCull::CasterFar(700, NAN, 100, 0, 100), 700, 0);
  // A higher sun or lower casters: nearer.
  CHECK(CascadeCull::CasterFar(5000, 0.9f, 100, 0, 200) < CascadeCull::CasterFar(5000, 0.6f, 100, 0, 200));
  CHECK(CascadeCull::CasterFar(5000, 0.6f, 100, 0, 100) < CascadeCull::CasterFar(5000, 0.6f, 100, 0, 200));
}

// Why: the bound must hold for every caster that shades a receiver: receivers anywhere in a sphere, casters on the line
// from each towards the sun no higher than the top; each such caster's depth along the sun is within CasterFar.
XR_TEST(CascadeCull_FarPlaneHolds)
{
  uint64_t n = 330;
  int nChecked = 0, nOver = 0;
  for (int k = 0; k < 2000; k++)
  {
    // The sun: from 10 to 90 degrees high, any way round.
    const float fElev = (10.0f + 80.0f * Next(n)) * 3.14159265f / 180.0f, fTurn = 6.2831853f * Next(n);
    const float Z[3] = { cosf(fElev) * cosf(fTurn), cosf(fElev) * sinf(fTurn), sinf(fElev) };
    // The camera at 0, the receivers' sphere about c (radius r), the casters' top.
    const float c[3] = { 200 * (Next(n) - 0.5f), 200 * (Next(n) - 0.5f), 60 * (Next(n) - 0.5f) }, r = 10 + 140 * Next(n);
    const float fTop = c[2] + 300 * Next(n);
    const float fDepth = c[0] * Z[0] + c[1] * Z[1] + c[2] * Z[2] + r, fLow = c[2] - r;
    const float fFar = CascadeCull::CasterFar(1e6f, Z[2], fDepth, fLow, fTop);
    for (int j = 0; j < 20; j++)
    {
      // A receiver in the sphere.
      float d[3], l2;
      do
      {
        for (float &v : d)
          v = 2 * Next(n) - 1;
        l2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
      } while (l2 > 1 || l2 == 0);
      const float R[3] = { c[0] + r * d[0], c[1] + r * d[1], c[2] + r * d[2] };
      // A caster along the sun from it, as high as the top lets it.
      const float tMax = (fTop - R[2]) / Z[2];
      if (tMax < 0)
        continue;
      const float t = tMax * Next(n);
      const float P[3] = { R[0] + t * Z[0], R[1] + t * Z[1], R[2] + t * Z[2] };
      nChecked++;
      if (P[0] * Z[0] + P[1] * Z[1] + P[2] * Z[2] > fFar)
        nOver++;
    }
  }
  CHECK(nChecked > 10000);
  CHECK(nOver == 0);
}
