// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: WakeBodies.h
//	Description: The bodies that make the wakes (Wake.cpp; the unit tests),
//	on the CPU without the engine: the part of an entity that crosses the
//	ocean's surface as an ellipse (Shaders/Wake.hlsli's WakeShape), which
//	of them make waves in a frame, and the constants of the wakes' kernels
//	(Shaders/Wake.hlsl) for a step.
//
//	An entity crosses the surface where its box reaches below and above the
//	water level: its ellipse the box's across (its local box turned by its
//	heading, a bit narrower: a hull's waterline is), its depth the box's
//	below the level (at most WAKE_MOST_DEPTH; a character's at most
//	WAKE_CHARACTER_DEPTH, its ellipse WAKE_CHARACTER_HALF across: what a
//	body pushes aside at the surface). The window's cells smooth an
//	ellipse narrower than a cell away, so its half axes are at least
//	WAKE_LEAST_HALF.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_WAKEBODIES_H
#define XRENDERD3D12_WAKEBODIES_H

// SWakeConstants (uint32 the includer's: the engine's or TestTypes.h's).
#include "Shaders/Constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace WakeBodies
{
  //! The waves' damping: all of them (1/s: a wake fades over a minute), the shortest (at k_N, pi / the cell, along an
  //! axis; as (k / k_N)^4: the cells' ripple that the window cannot carry).
  constexpr float WAKE_DAMPING = 0.02f;
  constexpr float WAKE_SHORT_DAMPING = 1.5f;
  //! The sponge at the window's edge: its width (cells: 12 m), its rate there (1/s; as the square towards the edge).
  constexpr float WAKE_SPONGE_CELLS = 96;
  constexpr float WAKE_SPONGE_RATE = 4;
  //! The foam: its life (s), its spread (m^2/s; a texel's share to each neighbour at most WAKE_FOAM_MOST_SPREAD a frame,
  //! below 0.25, where it would grow: so down to 50 frames a second); a body's where it is faster than WAKE_FOAM_SPEED (m/s), the most
  //! (WAKE_FOAM_MOST of the surface white) WAKE_FOAM_FULL faster; the waves' where their slope is above WAKE_BREAK_SLOPE,
  //! the most WAKE_BREAK_RAMP steeper (a wave breaks at Stokes' limit, H / L 0.142: k A 0.44; at 0.3 the short waves
  //! of a swimmer's wake broke all over it, a trail of fog; none under a body, its own hollow's sides). The foam made the share of the surface white there (not more each frame: a hull's
  //! white water a lace of bubbles, not a band white all over), the foam left behind fading and spreading.
  constexpr float WAKE_FOAM_LIFE = 4;
  constexpr float WAKE_FOAM_SPREAD = 0.18f;
  constexpr float WAKE_FOAM_MOST_SPREAD = 0.24f;
  constexpr float WAKE_FOAM_SPEED = 1.5f;
  constexpr float WAKE_FOAM_FULL = 4;
  constexpr float WAKE_FOAM_MOST = 0.7f;
  constexpr float WAKE_BREAK_SLOPE = 0.44f;
  constexpr float WAKE_BREAK_RAMP = 0.15f;
  //! The longest step (s): a longer frame takes it (the waves slow down rather than jump).
  constexpr float WAKE_LONGEST_STEP = 0.1f;
  //! The calm: after WAKE_QUIET s with nothing in the window faster than WAKE_QUIET_SPEED (m/s: a boat at its mooring
  //! bobs at 0.03) the waves have faded (0.3 of them at most) and most have run out through the sponge (waves 4 m long in
  //! 50 s): no more steps until something moves (the window cleared then), the water's wakes fading out over the last
  //! WAKE_QUIET_FADE s. A window cleared (the first frame, after a pause) is calm until something stirs it.
  constexpr double WAKE_QUIET = 60;
  constexpr double WAKE_QUIET_FADE = 15;
  constexpr float WAKE_QUIET_SPEED = 0.1f;
  //! The window's half side (m).
  constexpr float WAKE_HALF_WINDOW = 0.5f * XR_WAKE_SIZE * (float)XR_WAKE_CELL;

  //! The share of the box's half sides an ellipse's half axes are (a hull narrows to its bow and stern).
  constexpr float WAKE_HALF_SHARE = 0.85f;
  //! The least half axis (m: four cells of the window).
  constexpr float WAKE_LEAST_HALF = 0.5f;
  //! The deepest displacement (m): what is deeper pushes the surface little.
  constexpr float WAKE_MOST_DEPTH = 1.0f;
  //! A character's (a swimmer's, one wading): its half axes and its deepest displacement (m). Only the head and the
  //! shoulders cross a swimmer's surface, the rest of the body under it: as a pressure on the surface 0.4 m deep a
  //! swimmer at 1.5 m/s made waves 0.38 m high, 0.1 m deep 0.11 m high and steeper than a wave breaks at (0.67: its
  //! wake a trail of foam); a body pushing through the water raises it about U^2 / 2g (0.11 m at 1.5 m/s) in front of
  //! it, its rings a few centimetres: at 0.05 m 0.06 m high, their slope up to 0.35 (Wake_Height).
  constexpr float WAKE_CHARACTER_HALF = 0.35f;
  constexpr float WAKE_CHARACTER_DEPTH = 0.05f;
  //! The largest half side of a box (m) that may be a body: larger ones are buildings, piers, the terrain's parts.
  constexpr float WAKE_LARGEST_HALF = 25.0f;
  //! How far a body moves at most a step (m): faster ones are split into more steps (Wake.cpp), up to WAKE_MOST_STEPS.
  constexpr float WAKE_STEP_MOVE = 0.5f;
  constexpr int WAKE_MOST_STEPS = 4;

  //! A body where it crosses the surface.
  struct SBody
  {
    float fX = 0, fY = 0;         //!< the ellipse's centre (world, m)
    float fCos = 1, fSin = 0;     //!< the turn of its axes about z
    float fHalfA = 0, fHalfB = 0; //!< its half axes along them (m)
    float fDepth = 0;             //!< the depth of its displacement (m)
  };

  //! The body of an entity: its local box (vMin, vMax), its position vPos, its heading fYaw (degrees about z, as the
  //! engine's angles turn it: its local x along (cos, sin)), its world box's bottom and top, the water level fWater; a
  //! character's (bCharacter) its own size. False where the box does not cross the surface or is no body's.
  inline bool MakeBody(const float vMin[3], const float vMax[3], const float vPos[3], float fYaw, float fWorldMinZ, float fWorldMaxZ,
                       float fWater, bool bCharacter, SBody &Body)
  {
    if (!(fWorldMinZ < fWater && fWorldMaxZ > fWater))
      return false;
    const float ex = 0.5f * (vMax[0] - vMin[0]), ey = 0.5f * (vMax[1] - vMin[1]);
    if (!(ex >= 0 && ey >= 0) || std::max(ex, ey) > WAKE_LARGEST_HALF)
      return false;
    const float r = fYaw * (3.14159265f / 180.0f);
    Body.fCos = std::cos(r);
    Body.fSin = std::sin(r);
    // The box's middle across, turned with it.
    const float cx = 0.5f * (vMin[0] + vMax[0]), cy = 0.5f * (vMin[1] + vMax[1]);
    Body.fX = vPos[0] + Body.fCos * cx - Body.fSin * cy;
    Body.fY = vPos[1] + Body.fSin * cx + Body.fCos * cy;
    if (bCharacter)
    {
      Body.fHalfA = Body.fHalfB = WAKE_CHARACTER_HALF;
      Body.fDepth = std::min(fWater - fWorldMinZ, WAKE_CHARACTER_DEPTH);
    }
    else
    {
      Body.fHalfA = std::max(ex * WAKE_HALF_SHARE, WAKE_LEAST_HALF);
      Body.fHalfB = std::max(ey * WAKE_HALF_SHARE, WAKE_LEAST_HALF);
      Body.fDepth = std::min(fWater - fWorldMinZ, WAKE_MOST_DEPTH);
    }
    return Body.fDepth > 0;
  }

  //! The body between a and b at t (0 a, 1 b): the centre and the depth linearly, the turn the short way round.
  inline SBody Between(const SBody &a, const SBody &b, float t)
  {
    SBody o = b;
    o.fX = a.fX + (b.fX - a.fX) * t;
    o.fY = a.fY + (b.fY - a.fY) * t;
    o.fHalfA = a.fHalfA + (b.fHalfA - a.fHalfA) * t;
    o.fHalfB = a.fHalfB + (b.fHalfB - a.fHalfB) * t;
    o.fDepth = a.fDepth + (b.fDepth - a.fDepth) * t;
    const float fA = std::atan2(a.fSin, a.fCos);
    float fTurn = std::atan2(b.fSin, b.fCos) - fA;
    fTurn -= 6.2831853f * std::floor((fTurn + 3.14159265f) / 6.2831853f);
    o.fCos = std::cos(fA + fTurn * t);
    o.fSin = std::sin(fA + fTurn * t);
    return o;
  }

  //! Whether the body moved or changed between a and b (a body at rest makes no waves).
  inline bool Moved(const SBody &a, const SBody &b)
  {
    return std::fabs(a.fX - b.fX) + std::fabs(a.fY - b.fY) + std::fabs(a.fDepth - b.fDepth) + std::fabs(a.fCos - b.fCos) +
               std::fabs(a.fSin - b.fSin) + std::fabs(a.fHalfA - b.fHalfA) + std::fabs(a.fHalfB - b.fHalfB) > 1e-5f;
  }

  //! Whether point (x, y) lies inside the body's ellipse.
  inline bool Inside(const SBody &b, float x, float y)
  {
    const float dx = x - b.fX, dy = y - b.fY;
    const float u = (dx * b.fCos + dy * b.fSin) / b.fHalfA, v = (-dx * b.fSin + dy * b.fCos) / b.fHalfB;
    return u * u + v * v < 1;
  }

  //! A body of the frame: where it was at the last step (Before) and where it is (After).
  struct SMove
  {
    int nId = 0;
    SBody Before, After;
  };

  //! The bodies that make waves in a frame from those crossing the surface (Moves): those that moved, nearest the point (x,
  //! y) first, at most nMost of them, none whose centre lies inside a larger one's (a crew in its boat, one body's parts).
  inline std::vector<SMove> Choose(std::vector<SMove> Moves, float x, float y, size_t nMost)
  {
    std::vector<SMove> Out;
    for (const SMove &m : Moves)
    {
      bool bInside = false;
      for (const SMove &o : Moves)
        bInside = bInside || (o.nId != m.nId && o.After.fHalfA * o.After.fHalfB > m.After.fHalfA * m.After.fHalfB &&
                              Inside(o.After, m.After.fX, m.After.fY));
      if (!bInside && Moved(m.Before, m.After))
        Out.push_back(m);
    }
    std::stable_sort(Out.begin(), Out.end(), [x, y](const SMove &a, const SMove &b) {
      return std::hypot(a.After.fX - x, a.After.fY - y) < std::hypot(b.After.fX - x, b.After.fY - y);
    });
    if (Out.size() > nMost)
      Out.resize(nMost);
    return Out;
  }

  //! The steps a frame takes for the bodies' moves (each at most WAKE_STEP_MOVE a step), 1 to WAKE_MOST_STEPS.
  inline int Steps(const std::vector<SMove> &Moves)
  {
    float fMost = 0;
    for (const SMove &m : Moves)
      fMost = std::max(fMost, std::hypot(m.After.fX - m.Before.fX, m.After.fY - m.Before.fY));
    return std::clamp((int)std::ceil(fMost / WAKE_STEP_MOVE), 1, WAKE_MOST_STEPS);
  }

  //! The cell of the window's centre along an axis for the camera at x (m).
  inline int WindowCell(double x)
  {
    return (int)std::floor(x / XR_WAKE_CELL);
  }

  //! A body of a step for the kernels: where it was and where it is, its speed (m/s, for its foam).
  struct SStepBody
  {
    SBody Before, After;
    float fSpeed = 0;
  };

  //! Whether a frame's bodies stir the window (one faster than WAKE_QUIET_SPEED).
  inline bool Stirred(const std::vector<SStepBody> &Bodies)
  {
    return std::any_of(Bodies.begin(), Bodies.end(), [](const SStepBody &b) { return b.fSpeed > WAKE_QUIET_SPEED; });
  }

  //! The share of the wakes the water shows fSince s after the window was last stirred: all of them, fading out over the
  //! calm's last WAKE_QUIET_FADE s; none from WAKE_QUIET on (no steps).
  inline float CalmShare(double fSince)
  {
    return (float)std::clamp((WAKE_QUIET - fSince) / WAKE_QUIET_FADE, 0.0, 1.0);
  }

  //! The kernels' constants (Shaders/Wake.hlsl) of a step of dt (s) of the window around cell vCell from the window around
  //! vBefore (bMoved: the cells new to it cleared), the bodies moving from Before to After over it (at most XR_WAKE_BODIES
  //! of them), their positions from the window's centre point; for the view the foam made over dt (the frame's), the foam
  //! before left (bFoamBefore; none in the first frame).
  inline void StepConstants(SWakeConstants &c, const int vCell[2], const int vBefore[2], bool bMoved, float dt, bool bFoamBefore,
                            const SStepBody *pBodies, int nBodies)
  {
    std::memset(&c, 0, sizeof(c));
    const float fCell = (float)XR_WAKE_CELL;
    c.vWindow[0] = (float)vCell[0];
    c.vWindow[1] = (float)vCell[1];
    c.vWindow[2] = (float)vBefore[0];
    c.vWindow[3] = (float)vBefore[1];
    c.vStep[0] = dt;
    c.vStep[1] = std::exp(-WAKE_DAMPING * dt);
    c.vStep[2] = WAKE_SHORT_DAMPING * dt;
    c.vStep[3] = WAKE_SPONGE_RATE * dt;
    c.vSponge[0] = WAKE_SPONGE_CELLS;
    c.vSponge[1] = bFoamBefore ? std::exp(-dt / WAKE_FOAM_LIFE) : 0.0f;
    c.vSponge[2] = std::min(WAKE_FOAM_SPREAD * dt / (fCell * fCell), WAKE_FOAM_MOST_SPREAD);
    c.vSponge[3] = 1 / WAKE_FOAM_FULL;
    c.vBreak[0] = WAKE_BREAK_SLOPE;
    c.vBreak[1] = 1 / WAKE_BREAK_RAMP;
    c.vBreak[2] = WAKE_FOAM_MOST;
    nBodies = std::clamp(nBodies, 0, (int)XR_WAKE_BODIES);
    c.nCounts[0] = (uint32)nBodies;
    c.nCounts[1] = bMoved ? 1u : 0u;
    const float fCx = (float)(vCell[0] * XR_WAKE_CELL), fCy = (float)(vCell[1] * XR_WAKE_CELL);
    for (int i = 0; i < nBodies; i++)
    {
      const SStepBody &b = pBodies[i];
      const SBody *pPoses[2] = { &b.Before, &b.After };
      const float fReach = std::max(std::max(b.Before.fHalfA, b.Before.fHalfB), std::max(b.After.fHalfA, b.After.fHalfB)) + fCell;
      for (int j = 0; j < 2; j++)
      {
        const SBody &p = *pPoses[j];
        float *pPose = c.vBodies[4 * i + 2 * j], *pShape = c.vBodies[4 * i + 2 * j + 1];
        pPose[0] = p.fX - fCx;
        pPose[1] = p.fY - fCy;
        pPose[2] = p.fCos;
        pPose[3] = p.fSin;
        pShape[0] = 1 / p.fHalfA;
        pShape[1] = 1 / p.fHalfB;
        pShape[2] = p.fDepth;
        pShape[3] = j == 0 ? fReach : b.fSpeed - WAKE_FOAM_SPEED;
      }
    }
  }
}

#endif
