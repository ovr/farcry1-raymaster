// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WakeTests.cpp
//	Description: Unit tests of the wakes (d3d12_WaterWakes): the bodies on
//	the CPU (WakeBodies.h: the ellipse of an entity's box, which bodies
//	make waves, the steps of a frame) and the window's waves on the GPU
//	(the renderer's Wake.hlsl kernels as Wake.cpp runs them, GpuHarness.h's
//	RunWake): deep water's dispersion whatever the step, a body at rest
//	making none, Kelvin's wedge behind a moving one, the window following
//	the camera by cells, the sponge taking the waves at its edge, a boat's
//	and a swimmer's waves as high as theirs are, the view's slopes, mip
//	levels and foam.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"
#include "TestMath.h"
#include "WakeBodies.h"

#include "Test_WakeRows.h"
#include "Test_WakeColumns.h"
#include "Test_WakeRowsInverse.h"
#include "Test_WakeView.h"
#include "Test_WakeHeight.h"
#include "Test_WakeShape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace XrTest;

namespace
{
  using namespace WakeBodies;
  constexpr int N = XR_WAKE_SIZE;
  constexpr float CELL = (float)XR_WAKE_CELL;
  constexpr double PI = 3.14159265358979, G = 9.81;
  const int ORIGIN[2] = { 0, 0 };

  SWakeKernels Kernels()
  {
    return { XR_BYTECODE(Test_WakeRows), XR_BYTECODE(Test_WakeColumns), XR_BYTECODE(Test_WakeRowsInverse), XR_BYTECODE(Test_WakeView) };
  }

  //! A step's constants in the window around cell (0, 0) as UpdateWakes makes them; bDamped false: no damping, no sponge.
  SWakeConstants Constants(float dt, const std::vector<SStepBody> &Bodies, bool bDamped, bool bFoamBefore = false)
  {
    SWakeConstants c;
    StepConstants(c, ORIGIN, ORIGIN, false, dt, bFoamBefore, Bodies.data(), (int)Bodies.size());
    if (!bDamped)
    {
      c.vStep[1] = 1;
      c.vStep[2] = 0;
      c.vStep[3] = 0;
    }
    return c;
  }

  //! The middle of texel t's cell along an axis from the window's centre point, around cell c (Wake.hlsli's WakeOffset).
  float Offset(int t, int c = 0)
  {
    const int nLow = c - N / 2;
    return (float)(nLow + ((t - nLow) & (N - 1)) - c) * CELL + 0.5f * CELL;
  }

  //! The texel of the cell at x (m from the window's centre point around cell 0).
  int Texel(float x)
  {
    return (int)std::floor(x / CELL) & (N - 1);
  }

  float Height(const SWakeOutput &o, int x, int y) { return o.State[((size_t)y * N + x) * 2]; }

  //! A single wave A cos(k.X) travelling along k (the window's state: the height and the filtered potential, A sin(k.X)).
  std::vector<float> Wave(double A, int mx, int my)
  {
    std::vector<float> q((size_t)N * N * 2);
    const double k0 = 2 * PI / (N * CELL);
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
      {
        const double t = k0 * (mx * Offset(x) + my * Offset(y));
        q[((size_t)y * N + x) * 2] = (float)(A * std::cos(t));
        q[((size_t)y * N + x) * 2 + 1] = (float)(A * std::sin(t));
      }
    return q;
  }

  //! The bodies of a run: a body at rest (Shape) moving from From by V (m/s) for the steps' time, each step dt.
  std::vector<SWakeFrame> Moving(const SBody &Shape, float fFromX, float fFromY, float vx, float vy, float dt, int nSteps, bool bDamped = true)
  {
    SWakeFrame f;
    const float fSpeed = std::hypot(vx, vy);
    for (int s = 0; s < nSteps; s++)
    {
      SStepBody b;
      b.Before = b.After = Shape;
      b.Before.fX = fFromX + vx * dt * s;
      b.Before.fY = fFromY + vy * dt * s;
      b.After.fX = fFromX + vx * dt * (s + 1);
      b.After.fY = fFromY + vy * dt * (s + 1);
      b.fSpeed = fSpeed;
      f.Steps.push_back(Constants(dt, { b }, bDamped));
    }
    f.View = Constants(dt, {}, bDamped);
    return { f };
  }

  SBody Ellipse(float fHalfA, float fHalfB, float fDepth, float fYaw = 0)
  {
    SBody b;
    b.fHalfA = fHalfA;
    b.fHalfB = fHalfB;
    b.fDepth = fDepth;
    b.fCos = std::cos(fYaw);
    b.fSin = std::sin(fYaw);
    return b;
  }

  //! The window's energy (the sum of |q|^2: Parseval's of the height's and the potential's) over the texels whose middle's x
  //! (m from the centre point) lies in [x0, x1).
  double Energy(const std::vector<float> &q, float x0 = -1e9f, float x1 = 1e9f)
  {
    double e = 0;
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
        if (Offset(x) >= x0 && Offset(x) < x1)
        {
          const double h = q[((size_t)y * N + x) * 2], p = q[((size_t)y * N + x) * 2 + 1];
          e += h * h + p * p;
        }
    return e;
  }
}

// Why: an entity's part in the water is the ellipse of its box across (turned with its heading, its middle where the box's
// is), as deep as the box reaches below the level, a character a swimmer's size; a box not crossing the surface, or as
// large as a building, is none. Only what moved makes waves, the nearest first, none inside a larger one (the crew in its
// boat), the steps a frame takes as many as keep each body's move short; the kernels' ellipse must be the same as the
// CPU's (WakeShape against Inside), the window's texels its cells modulo its size.
XR_TEST(Wake_Bodies)
{
  const float vMin[3] = { -2.5f, -1.0f, -0.5f }, vMax[3] = { 3.5f, 1.0f, 1.5f }, vPos[3] = { 100, 200, 10 };
  SBody b;
  CHECK(MakeBody(vMin, vMax, vPos, 30, 9.5f, 11.5f, 10.2f, false, b));
  const float c = std::cos(30 * (float)PI / 180), s = std::sin(30 * (float)PI / 180);
  CHECK_NEAR(b.fX, 100 + 0.5f * c, 1e-4);
  CHECK_NEAR(b.fY, 200 + 0.5f * s, 1e-4);
  CHECK_NEAR(b.fHalfA, 3 * WAKE_HALF_SHARE, 1e-5);
  CHECK_NEAR(b.fHalfB, std::max(1 * WAKE_HALF_SHARE, WAKE_LEAST_HALF), 1e-5);
  CHECK_NEAR(b.fDepth, 0.7f, 1e-5);
  CHECK_NEAR(b.fCos, c, 1e-6);
  CHECK(!MakeBody(vMin, vMax, vPos, 0, 10.5f, 12, 10.2f, false, b));   // above the water
  CHECK(!MakeBody(vMin, vMax, vPos, 0, 5, 9, 10.2f, false, b));        // under it
  const float vHuge[3] = { 60, 1, 1 };
  CHECK(!MakeBody(vMin, vHuge, vPos, 0, 9, 11, 10.2f, false, b));
  CHECK(MakeBody(vMin, vMax, vPos, 0, 8, 11, 10.2f, false, b) && b.fDepth == WAKE_MOST_DEPTH);
  CHECK(MakeBody(vMin, vMax, vPos, 0, 8, 11, 10.2f, true, b) && b.fDepth == WAKE_CHARACTER_DEPTH && b.fHalfA == WAKE_CHARACTER_HALF);

  // Which make waves.
  const SBody Boat = Ellipse(3, 1.2f, 0.5f), Crew = Ellipse(0.4f, 0.4f, 0.4f);
  auto Placed = [](SBody x, float px, float py) {
    x.fX = px;
    x.fY = py;
    return x;
  };
  std::vector<SMove> Moves = {
    { 1, Placed(Boat, 10, 0), Placed(Boat, 10.2f, 0) },   // a boat moving
    { 2, Placed(Crew, 10.5f, 0), Placed(Crew, 10.7f, 0) },   // its crew
    { 3, Placed(Boat, 5, 5), Placed(Boat, 5, 5) },         // a boat at rest
    { 4, Placed(Crew, 3, 0), Placed(Crew, 3, 0.3f) },     // a swimmer
    { 5, Placed(Boat, 40, 0), Placed(Boat, 40.1f, 0) },   // a boat far off
  };
  std::vector<SMove> Chosen = Choose(Moves, 0, 0, 8);
  CHECK(Chosen.size() == 3 && Chosen[0].nId == 4 && Chosen[1].nId == 1 && Chosen[2].nId == 5);
  CHECK(Choose(Moves, 0, 0, 2).size() == 2);
  CHECK(Steps({ { 1, Placed(Boat, 0, 0), Placed(Boat, 1.2f, 0) } }) == 3);
  CHECK(Steps({ { 1, Placed(Boat, 0, 0), Placed(Boat, 10, 0) } }) == WAKE_MOST_STEPS);
  CHECK(Steps({}) == 1);
  // The turn the short way round: from 170 to -170 degrees through 180.
  const SBody t = Between(Ellipse(1, 1, 1, 170 * (float)PI / 180), Ellipse(1, 1, 1, -170 * (float)PI / 180), 0.5f);
  CHECK_NEAR(t.fCos, -1, 1e-5);

  // The kernels' ellipse and the window's cells.
  CRandom Random(292);
  SDispatch d;
  d.Shader = XR_BYTECODE(Test_WakeShape);
  struct SCase
  {
    SBody Body;
    float x, y;
    int t, c;
  };
  std::vector<SCase> Cases;
  for (int i = 0; i < 512; i++)
  {
    SCase k;
    k.Body = Placed(Ellipse(Random.Range(0.5f, 4), Random.Range(0.5f, 2), Random.Range(0.1f, 1), Random.Range(-3.1f, 3.1f)), Random.Range(-5, 5),
                Random.Range(-5, 5));
    k.x = Random.Range(-9, 9);
    k.y = Random.Range(-9, 9);
    k.t = (int)(Random.Unit() * N) & (N - 1);
    k.c = (int)Random.Range(-3000, 3000);
    Cases.push_back(k);
    SWakeConstants wc;
    SStepBody sb;
    sb.Before = sb.After = k.Body;
    StepConstants(wc, ORIGIN, ORIGIN, false, 0.02f, false, &sb, 1);
    Push(d.In, k.x, k.y, (float)k.t, (float)k.c);
    Push(d.In, wc.vBodies[0][0], wc.vBodies[0][1], wc.vBodies[0][2], wc.vBodies[0][3]);
    Push(d.In, wc.vBodies[1][0], wc.vBodies[1][1], wc.vBodies[1][2], wc.vBodies[1][3]);
  }
  d.Test.nTest[0] = (uint32)Cases.size();
  d.nGroups[0] = Groups64(Cases.size());
  d.nOut = Cases.size();
  const std::vector<uint32> Out = Gpu().Run(d);
  int nWrong = 0, nCells = 0;
  for (size_t i = 0; i < Cases.size(); i++)
  {
    const SCase &k = Cases[i];
    const float fShape = At(Out, i, 0);
    // (1 - r^2)^2 inside the ellipse Inside finds the point in, 0 outside.
    const float dx = k.x - k.Body.fX, dy = k.y - k.Body.fY;
    const float u = (dx * k.Body.fCos + dy * k.Body.fSin) / k.Body.fHalfA, v = (-dx * k.Body.fSin + dy * k.Body.fCos) / k.Body.fHalfB;
    const float r2 = u * u + v * v;
    if (std::fabs(fShape - (Inside(k.Body, k.x, k.y) ? (1 - r2) * (1 - r2) : 0.0f)) > 1e-4f)
      nWrong++;
    CHECK_NEAR(At(Out, i, 1), fShape * k.Body.fDepth, 1e-5);
    const int nCell = (int)At(Out, i, 2);
    nCells += ((nCell - k.t) & (N - 1)) == 0 && nCell >= k.c - N / 2 && nCell < k.c + N / 2;
    CHECK_NEAR(At(Out, i, 3), (nCell - k.c + 0.5f) * CELL, 1e-4);
  }
  CHECK_MSG(nWrong == 0, std::to_string(nWrong) + " points on the wrong side");
  CHECK(nCells == (int)Cases.size());
}

// Why: the window's waves are deep water's, w = sqrt(g k), each running at its own speed (a long wave and a short one
// moved by w t from their start); the step turns each wave number exactly (eWave), so its length does not change the
// waves (a frame of 0.02 s or of 0.25 s, as the game's frames vary).
XR_TEST(Wake_Dispersion)
{
  struct SCase
  {
    int mx, my;
    float dt;
    int nSteps;
  };
  for (const SCase &k : { SCase{ 8, 0, 0.02f, 50 }, SCase{ 20, -12, 0.02f, 50 }, SCase{ 20, -12, 0.25f, 4 }, SCase{ 64, 32, 0.1f, 10 } })
  {
    const double A = 0.1;
    SWakeFrame f;
    for (int s = 0; s < k.nSteps; s++)
      f.Steps.push_back(Constants(k.dt, {}, false));
    f.View = Constants(k.dt, {}, false);
    const SWakeOutput o = Gpu().RunWake(Wave(A, k.mx, k.my), { f }, Kernels());
    const double k0 = 2 * PI / (N * CELL), kx = k0 * k.mx, ky = k0 * k.my, w = std::sqrt(G * std::hypot(kx, ky));
    const double T = k.dt * k.nSteps;
    double fWorst = 0;
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
        fWorst = std::max(fWorst, std::fabs(Height(o, x, y) - A * std::cos(kx * Offset(x) + ky * Offset(y) - w * T)));
    std::printf("  wave (%d, %d), %d steps of %.2f s: worst %.2e m of %.2f\n", k.mx, k.my, k.nSteps, k.dt, fWorst, A);
    CHECK(fWorst < 2e-4);
  }
}

// Why: a body at rest in the water makes no waves (its displacement does not change), whatever its depth; one that moved
// and stopped leaves waves that run off (none stays where it rests: no hump nor hole).
XR_TEST(Wake_Still)
{
  SBody b = Ellipse(3, 1.2f, 0.8f, 0.4f);
  const SWakeOutput o = Gpu().RunWake({}, Moving(b, 2, -1, 0, 0, 0.05f, 40), Kernels());
  double fMost = 0;
  for (float h : o.State)
    fMost = std::max(fMost, (double)std::fabs(h));
  // The two shapes the same numbers, their difference 0 but for how the GPU rounds each (a fused multiply-add): below a
  // micrometre.
  std::printf("  the most height %.3e m\n", fMost);
  CHECK(fMost < 1e-6);
}

// Why: a body moving through deep water leaves Kelvin's wake, its waves within a wedge of 19.47 degrees whatever its speed
// (at a low Froude number the highest along its edge, the cusps): the window's waves must be deep water's and the body's
// source move with it.
XR_TEST(Wake_Kelvin)
{
  const float U = 2.5f, dt = 0.05f, T = 16;
  const int nSteps = (int)(T / dt + 0.5f);
  const SBody b = Ellipse(2, 0.6f, 0.3f);
  const SWakeOutput o = Gpu().RunWake({}, Moving(b, -U * T, 0, U, 0, dt, nSteps), Kernels());
  // Behind the body (at x 0 now), the strip a metre wide D behind: its waves' envelope across, the highest each side.
  double fSum = 0;
  int nAngles = 0;
  for (float D : { 8.0f, 11.0f, 14.0f, 17.0f })
  {
    float fMost[2] = {}, fAt[2] = {};
    for (int y = 0; y < N; y++)
    {
      const float fy = Offset(y);
      if (std::fabs(fy) < 1.0f || std::fabs(fy) > 0.8f * D)
        continue;
      float e = 0;
      for (int x = 0; x < N; x++)
        if (std::fabs(Offset(x) + D) < 0.6f)
          e = std::max(e, std::fabs(Height(o, x, y)));
      const int side = fy > 0;
      if (e > fMost[side])
        fMost[side] = e, fAt[side] = std::fabs(fy);
    }
    for (int side = 0; side < 2; side++)
    {
      const double a = std::atan(fAt[side] / D) * 180 / PI;
      std::printf("  %g m behind, %s: highest %.4f m at %.2f m across, %.1f degrees\n", D, side ? "left" : "right", fMost[side], fAt[side], a);
      fSum += a;
      nAngles++;
    }
  }
  const double fMean = fSum / nAngles;
  std::printf("  the wedge's mean half angle %.1f degrees (Kelvin's 19.47)\n", fMean);
  CHECK(fMean > 16.5 && fMean < 22.5);
}

// Why: the window follows the camera by whole cells with nothing copied: each texel stays its cell's where the cell is in
// both windows, the cells new to it start calm (the far side's waves would else appear there), the window's mean stays 0.
XR_TEST(Wake_Window)
{
  std::vector<float> q((size_t)N * N * 2);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
    {
      const double u = 2 * PI * (Offset(x) + 64) / 128, v = 2 * PI * (Offset(y) + 64) / 128;
      q[((size_t)y * N + x) * 2] = (float)(0.1 * std::sin(5 * u) * std::cos(3 * v));
      q[((size_t)y * N + x) * 2 + 1] = (float)(0.05 * std::cos(2 * u + v));
    }
  const int vCell[2] = { 3, -2 };
  SWakeFrame f;
  SWakeConstants c;
  StepConstants(c, vCell, ORIGIN, true, 0, false, nullptr, 0);
  c.vStep[1] = 1;
  c.vStep[2] = c.vStep[3] = 0;
  f.Steps.push_back(c);
  f.View = c;
  const SWakeOutput o = Gpu().RunWake(q, { f }, Kernels());
  // The cleared start, less its mean (the step keeps the mean at 0).
  std::vector<double> e((size_t)N * N * 2);
  double fMean[2] = {};
  int nNew = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
    {
      const bool bNew = Offset(x, vCell[0]) != Offset(x, 0) - vCell[0] * CELL || Offset(y, vCell[1]) != Offset(y, 0) - vCell[1] * CELL;
      nNew += bNew;
      for (int j = 0; j < 2; j++)
      {
        e[((size_t)y * N + x) * 2 + j] = bNew ? 0 : q[((size_t)y * N + x) * 2 + j];
        fMean[j] += e[((size_t)y * N + x) * 2 + j] / ((double)N * N);
      }
    }
  CHECK(nNew == 3 * N + 2 * N - 3 * 2);
  double fWorst = 0, fWorstNew = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
    {
      const bool bNew = e[((size_t)y * N + x) * 2] == 0 && e[((size_t)y * N + x) * 2 + 1] == 0;
      for (int j = 0; j < 2; j++)
      {
        const double d = std::fabs(o.State[((size_t)y * N + x) * 2 + j] - (e[((size_t)y * N + x) * 2 + j] - fMean[j]));
        (bNew ? fWorstNew : fWorst) = std::max(bNew ? fWorstNew : fWorst, d);
      }
    }
  std::printf("  kept cells within %.2e, new ones %.2e (the means %.2e, %.2e)\n", fWorst, fWorstNew, fMean[0], fMean[1]);
  CHECK(fWorst < 1e-5 && fWorstNew < 1e-5);
}

// Why: the sponge at the window's edge takes the waves running out of it before they wrap round to the far side (they
// would come back in from there), without sending them back.
XR_TEST(Wake_Sponge)
{
  // A wave packet 8 m long running towards the edge at the speed of its group (1.8 m/s).
  const double k0 = 2 * PI / 8, s = 5, x0 = 38, A = 0.1;
  std::vector<float> q((size_t)N * N * 2);
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
    {
      const double fx = Offset(x), fy = Offset(y), g = A * std::exp(-((fx - x0) * (fx - x0) + fy * fy * 0.25) / (2 * s * s));
      q[((size_t)y * N + x) * 2] = (float)(g * std::cos(k0 * fx));
      q[((size_t)y * N + x) * 2 + 1] = (float)(g * std::sin(k0 * fx));
    }
  const float dt = 0.1f, T = 30;
  const double E0 = Energy(q);
  double fLeft[2] = {};
  for (int bSponge = 0; bSponge < 2; bSponge++)
  {
    SWakeFrame f;
    for (int i = 0; i < (int)(T / dt + 0.5f); i++)
    {
      SWakeConstants c = Constants(dt, {}, true);
      if (!bSponge)
        c.vStep[3] = 0;
      f.Steps.push_back(c);
    }
    f.View = f.Steps.back();
    const SWakeOutput o = Gpu().RunWake(q, { f }, Kernels());
    fLeft[bSponge] = Energy(o.State) / E0;
    const double fWrapped = Energy(o.State, -64, -30) / E0;
    std::printf("  %s: energy left %.4f, on the far side %.5f\n", bSponge ? "the sponge" : "no sponge", fLeft[bSponge], fWrapped);
    if (bSponge)
      CHECK(fLeft[1] < 0.02 && fWrapped < 0.002);
    else
      CHECK(fWrapped > 0.05);   // without it the packet wraps round
  }
  // The uniform damping alone keeps exp(-2 * 0.02 * 30) of it.
  CHECK_NEAR(fLeft[0], std::exp(-2 * WAKE_DAMPING * T), 0.03);
}

// Why: a boat's wake and a swimmer's rings must be as high as real ones (a planing boat's 0.1-0.5 m some metres behind
// it, a swimmer's a few centimetres) from the displacement of their part in the water alone; a swimmer's short waves
// below the slope they break at (steeper, its wake was a trail of foam).
XR_TEST(Wake_Height)
{
  struct SCase
  {
    const char *szName;
    SBody Body;
    float fSpeed, fLeast, fMost;
  };
  for (const SCase &k : { SCase{ "boat", Ellipse(2.5f, 1.0f, 0.5f), 8, 0.08f, 0.6f }, SCase{ "swimmer", Ellipse(WAKE_CHARACTER_HALF, WAKE_CHARACTER_HALF, WAKE_CHARACTER_DEPTH), 1.5f, 0.005f, 0.12f } })
  {
    const float dt = 0.05f, T = 5;
    const int nSteps = (int)(T / dt + 0.5f);
    const SWakeOutput o = Gpu().RunWake({}, Moving(k.Body, -k.fSpeed * T, 0, k.fSpeed, 0, dt, nSteps), Kernels());
    // Behind it, beyond its own length (where it now is, its displacement is still a hollow under it).
    float fMost = 0, fSteepest = 0;
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
        if (Offset(x) < -2.5f * k.Body.fHalfA && Offset(x) > -k.fSpeed * T)
        {
          fMost = std::max(fMost, std::fabs(Height(o, x, y)));
          const float *v = &o.View[0][((size_t)y * N + x) * 4];
          fSteepest = std::max(fSteepest, std::hypot(v[0], v[1]));
        }
    std::printf("  %s at %.1f m/s: its waves up to %.3f m, their slope up to %.3f\n", k.szName, k.fSpeed, fMost, fSteepest);
    CHECK(fMost > k.fLeast && fMost < k.fMost);
    if (k.fSpeed < WAKE_FOAM_SPEED + 1)
      CHECK(fSteepest < WAKE_BREAK_SLOPE);
  }
}

// Why: the water reads the view: the slopes the height's (dh/dx, dh/dy), their square for the variance the mip levels
// smooth out (LEAN: each level's means of 2 x 2), the foam where the waves break (steeper than WAKE_BREAK_SLOPE), fading
// over its life; a fast body's foam where it is, at most WAKE_FOAM_MOST of the surface white (the foam's lace shows).
XR_TEST(Wake_View)
{
  // A steep wave 4 m long (slope 0.4 * 1.57 = 0.63) and a gentle one (0.05: none breaking).
  for (double A : { 0.05, 0.4 })
  {
    const int mx = 32;
    const double kx = 2 * PI / (N * CELL) * mx;
    SWakeFrame f;
    f.View = Constants(0.02f, {}, false);
    const SWakeOutput o = Gpu().RunWake(Wave(A, mx, 0), { f }, Kernels());
    double fWorst = 0, fFoam = 0, fFoamWorst = 0;
    for (int y = 0; y < N; y += 7)
      for (int x = 0; x < N; x++)
      {
        const float *v = &o.View[0][((size_t)y * N + x) * 4];
        // The central difference of A cos(kx): -A sin(kx) sin(k cell) / cell.
        const double s = -A * std::sin(kx * Offset(x)) * std::sin(kx * CELL) / CELL;
        fWorst = std::max(fWorst, std::fabs(v[0] - s) + std::fabs(v[1]) + std::fabs(v[2] - s * s));
        const double fMade = std::clamp((std::fabs(s) - WAKE_BREAK_SLOPE) / WAKE_BREAK_RAMP, 0.0, 1.0) * WAKE_FOAM_MOST;
        // None in the window's two outer cells.
        if (std::abs(Offset(x)) < 60 && std::abs(Offset(y)) < 60)
          fFoamWorst = std::max(fFoamWorst, std::fabs(v[3] - fMade));
        fFoam += v[3];
      }
    std::printf("  wave of %.2f m: slopes within %.2e, foam within %.2e (sum %.3f)\n", A, fWorst, fFoamWorst, fFoam);
    CHECK(fWorst < 2e-3 * (1 + A * 10));
    CHECK(fFoamWorst < 2e-3);
    CHECK(A > 0.1 ? fFoam > 0 : fFoam == 0);
    // Each level the mean of 2 x 2 of the one before.
    double fMips = 0;
    for (int m = 1; m < XR_WAKE_MIPS; m++)
    {
      const int w = N >> m;
      for (int y = 0; y < w; y += 3)
        for (int x = 0; x < w; x += 5)
          for (int j = 0; j < 4; j++)
          {
            const std::vector<float> &a = o.View[m - 1];
            const double fMean = 0.25 * (a[((size_t)(2 * y) * 2 * w + 2 * x) * 4 + j] + a[((size_t)(2 * y) * 2 * w + 2 * x + 1) * 4 + j] +
                                         a[((size_t)(2 * y + 1) * 2 * w + 2 * x) * 4 + j] + a[((size_t)(2 * y + 1) * 2 * w + 2 * x + 1) * 4 + j]);
            fMips = std::max(fMips, std::fabs(o.View[m][((size_t)y * w + x) * 4 + j] - fMean) / (std::fabs(fMean) + 0.01));
          }
    }
    CHECK(fMips < 2e-3);
  }

  // The foam fading: made by a steep wave at the first view, then none made (its breaking off) over 3 s of 0.05 s frames;
  // its mean left as its life says, away from the window's edge (the spread keeps it there; into the outer cells it is
  // lost: over 3 s it spreads 1.2 m).
  {
    const float dt = 0.05f;
    std::vector<SWakeFrame> Frames(61);
    for (size_t i = 0; i < Frames.size(); i++)
    {
      Frames[i].View = Constants(dt, {}, false, i > 0);
      if (i > 0)
        Frames[i].View.vBreak[2] = 0;
    }
    std::vector<SWakeFrame> First(Frames.begin(), Frames.begin() + 1);
    const SWakeOutput o0 = Gpu().RunWake(Wave(0.4, 32, 0), First, Kernels());
    const SWakeOutput o = Gpu().RunWake(Wave(0.4, 32, 0), Frames, Kernels());
    double f0 = 0, f1 = 0;
    for (int y = 0; y < N; y++)
      for (int x = 0; x < N; x++)
        if (std::abs(Offset(x)) < 50 && std::abs(Offset(y)) < 50)
          f0 += o0.Foam[(size_t)y * N + x], f1 += o.Foam[(size_t)y * N + x];
    const double fExpected = std::exp(-dt * (Frames.size() - 1) / WAKE_FOAM_LIFE);
    std::printf("  foam left after %.1f s: %.4f (its life's %.4f)\n", dt * (Frames.size() - 1), f1 / f0, fExpected);
    CHECK(f0 > 0);
    CHECK_NEAR(f1 / f0, fExpected, 0.01);
  }

  // A fast body's foam where it is (faster than WAKE_FOAM_SPEED), none from a slow one.
  for (float fSpeed : { 1.0f, 6.0f })
  {
    SBody b = Ellipse(2, 1, 0.4f);
    SStepBody sb;
    sb.Before = sb.After = b;
    sb.fSpeed = fSpeed;
    SWakeFrame f;
    f.View = Constants(0.05f, { sb }, false);
    const SWakeOutput o = Gpu().RunWake({}, { f }, Kernels());
    const float fMiddle = o.View[0][((size_t)Texel(0.1f) * N + Texel(0.1f)) * 4 + 3];
    const float fOutside = o.View[0][((size_t)Texel(0.1f) * N + Texel(3)) * 4 + 3];
    // At the middle of its cell, half a cell from the body's along each axis: its shape (1 - r^2)^2 there.
    const float h = 0.5f * CELL, r2 = (h / 2) * (h / 2) + h * h;
    const float fExpected = std::clamp((fSpeed - WAKE_FOAM_SPEED) / WAKE_FOAM_FULL, 0.0f, 1.0f) * WAKE_FOAM_MOST * (1 - r2) * (1 - r2);
    std::printf("  a body at %.1f m/s: foam %.4f in its middle, %.4f beside it\n", fSpeed, fMiddle, fOutside);
    CHECK(fOutside == 0);
    CHECK_NEAR(fMiddle, fExpected, 2e-3);
  }

  // Under a body no wave breaks (the slopes there are its hollow's sides): a slow body on the steep wave, the wave's foam
  // beside it, none under it.
  {
    const int mx = 32;
    const double A = 0.4, kx = 2 * PI / (N * CELL) * mx;
    SStepBody sb;
    sb.Before = sb.After = Ellipse(2, 1, 0.4f);
    sb.fSpeed = 1;
    SWakeFrame f;
    f.View = Constants(0.05f, { sb }, false);
    const SWakeOutput o = Gpu().RunWake(Wave(A, mx, 0), { f }, Kernels());
    float fUnder = 0;
    for (float u : { -1.5f, -0.5f, 0.1f, 0.6f, 1.4f })
      fUnder = std::max(fUnder, o.View[0][((size_t)Texel(0.1f) * N + Texel(u)) * 4 + 3]);
    const int xBeside = Texel(3);
    const float fBeside = o.View[0][((size_t)Texel(0.1f) * N + xBeside) * 4 + 3];
    const double s = -A * std::sin(kx * Offset(xBeside)) * std::sin(kx * CELL) / CELL;
    const double fExpected = std::clamp((std::fabs(s) - WAKE_BREAK_SLOPE) / WAKE_BREAK_RAMP, 0.0, 1.0) * WAKE_FOAM_MOST;
    std::printf("  a slow body on the steep wave: foam under it at most %.4f, beside it %.4f (the wave's %.4f)\n", fUnder, fBeside,
                fExpected);
    CHECK(fUnder == 0);
    CHECK_NEAR(fBeside, fExpected, 2e-3);
  }
}

// Why: the window's steps cost about 0.1 ms a frame wherever the ocean is seen; once nothing has stirred it for long
// (WAKE_QUIET: its waves faded and mostly gone through the sponge) it rests, the water's wakes fading out first (no pop),
// while a boat bobbing at its mooring does not keep it awake and a swimmer does.
XR_TEST(Wake_Calm)
{
  CHECK_NEAR(CalmShare(0), 1, 0);
  CHECK_NEAR(CalmShare(WAKE_QUIET - WAKE_QUIET_FADE), 1, 0);
  CHECK_NEAR(CalmShare(WAKE_QUIET - 0.5 * WAKE_QUIET_FADE), 0.5, 1e-6);
  CHECK(CalmShare(WAKE_QUIET) == 0 && CalmShare(WAKE_QUIET + 100) == 0);
  // Along the fade the share never rises.
  float fLast = 2;
  bool bFalls = true;
  for (double t = 0; t <= WAKE_QUIET + 1; t += 0.25)
  {
    bFalls = bFalls && CalmShare(t) <= fLast;
    fLast = CalmShare(t);
  }
  CHECK(bFalls);
  SStepBody Moored, Swimmer;
  Moored.fSpeed = 0.03f;
  Swimmer.fSpeed = 1.0f;
  CHECK(!Stirred({}));
  CHECK(!Stirred({ Moored, Moored }));
  CHECK(Stirred({ Moored, Swimmer }));
}

// Why: the wakes' barriers given in batches (D3D12Barriers.h) leave the window's state, the foam, the view and the
// heights as the barriers given one by one do, bit for bit, in fewer calls: UpdateWakes' steps and frames on the same
// textures (the state's read round trip between a frame's view and the next step's rows dropped).
XR_TEST(Wake_BarrierBatches)
{
  const SBody Body = Ellipse(3, 1.2f, 0.8f, 0.4f);
  const std::vector<SWakeFrame> Frames = Moving(Body, 2, -1, 0, 0, 0.05f, 12);
  SWakeKernels k = Kernels();
  k.Height = XR_BYTECODE(Test_WakeHeight);
  SWakeOutput a, b;
  const Barriers::SStats Immediate = InBarrierMode(Barriers::MODE_IMMEDIATE, [&] { a = Gpu().RunWake({}, Frames, k); });
  const Barriers::SStats Batched = InBarrierMode(Barriers::MODE_BATCHED, [&] { b = Gpu().RunWake({}, Frames, k); });
  CHECK(!a.State.empty() && SameBits(a.State, b.State));
  CHECK(!a.Foam.empty() && SameBits(a.Foam, b.Foam));
  for (int m = 0; m < XR_WAKE_MIPS; m++)
  {
    CHECK_MSG(SameBits(a.View[m], b.View[m]), "the view's mip " + std::to_string(m));
    CHECK_MSG(!a.Heights[m].empty() && SameBits(a.Heights[m], b.Heights[m]), "the heights' mip " + std::to_string(m));
  }
  CheckBatchedStats("wakes", Immediate, Batched);
}
