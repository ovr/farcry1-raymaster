// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RenderModeOutdoorTests.cpp
//	Description: Unit tests of the render modes' glare between indoors
//	and outdoors (RenderModeOutdoor.h, d3d12_RenderModeOutdoor) against
//	the original's traced constants: Training's bunker held 80 frames,
//	then its bay's hill (frames 90 and 130 of that scene), the bay from
//	the level's start (frames 100, 200, 300), Swamp; the step by the time.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "RenderModeOutdoor.h"

using namespace RenderModeOutdoor;

namespace
{
  //! The share after n frames of 1/50 s outdoors from s.
  float Frames(float s, int n, float fTarget = 1)
  {
    for (int i = 0; i < n; i++)
      s = Step(s, fTarget, STEP_TIME);
    return s;
  }
}

XR_TEST(RenderModeOutdoor_Transition)
{
  // From the bunker (0) to the hill at once: frame 90 is the tenth frame outside, frame 130 the fiftieth.
  const float s90 = Frames(0, 10), s130 = Frames(0, 50);
  CHECK_NEAR(s90, 0.18305f, 2e-4);
  CHECK_NEAR(s130, 0.63589f, 2e-4);
  // The traced threshold and gain at those frames, paradise, improved and cold.
  const SGlare P90 = Blend(INDOOR[1], OUTDOOR[1], s90), P130 = Blend(INDOOR[1], OUTDOOR[1], s130);
  CHECK_NEAR(P90.fThreshold, -0.118305f, 5e-5);
  CHECK_NEAR(P90.fGain, 0.83661f, 1e-4);
  CHECK_NEAR(P130.fThreshold, -0.163589f, 5e-5);
  CHECK_NEAR(P130.fGain, 0.927177f, 1e-4);
  const SGlare I90 = Blend(INDOOR[0], OUTDOOR[0], s90), I130 = Blend(INDOOR[0], OUTDOOR[0], s130);
  CHECK_NEAR(I90.fThreshold, -0.24084f, 5e-5);
  CHECK_NEAR(I90.fGain, 0.745171f, 1e-4);
  CHECK_NEAR(I130.fThreshold, -0.218202f, 5e-5);
  CHECK_NEAR(I130.fGain, 0.609273f, 1e-4);
  const SGlare C90 = Blend(INDOOR[2], OUTDOOR[2], s90), C130 = Blend(INDOOR[2], OUTDOOR[2], s130);
  CHECK_NEAR(C90.fThreshold, -0.118305f, 5e-5);
  CHECK_NEAR(C90.fGain, 0.622559f, 1e-4);
  CHECK_NEAR(C130.fThreshold, -0.163589f, 5e-5);
  CHECK_NEAR(C130.fGain, 0.554624f, 1e-4);
}

XR_TEST(RenderModeOutdoor_Bay)
{
  // Training starts indoors; the bay's camera from the demo's frame 1: paradise at frames 100, 200, 300.
  const float vThreshold[3] = { -0.186748f, -0.198244f, -0.199767f };
  for (int i = 0; i < 3; i++)
  {
    const SGlare G = Blend(INDOOR[1], OUTDOOR[1], Frames(0, 100 * (i + 1)));
    CHECK_NEAR(G.fThreshold, vThreshold[i], 2e-4);
  }
}

XR_TEST(RenderModeOutdoor_Ends)
{
  // Long indoors: the indoor pair (the bunker's -0.1, 0.8), long outdoors the outdoor one (Swamp's -0.2, 1).
  const SGlare In = Blend(INDOOR[1], OUTDOOR[1], Frames(1, 2000, 0)), Out = Blend(INDOOR[1], OUTDOOR[1], Frames(0, 2000));
  CHECK_NEAR(In.fThreshold, -0.1f, 1e-5);
  CHECK_NEAR(In.fGain, 0.8f, 1e-5);
  CHECK_NEAR(Out.fThreshold, -0.2f, 1e-5);
  CHECK_NEAR(Out.fGain, 1.0f, 1e-5);
  // The share stays in [0, 1] and Blend clamps it.
  const SGlare Over = Blend(INDOOR[1], OUTDOOR[1], 3.0f);
  CHECK_NEAR(Over.fGain, 1.0f, 1e-6);
  CHECK(Step(0.5f, 1, 1000.0f) <= 1.0f);
  CHECK(Step(0.5f, 0, 1000.0f) >= 0.0f);
}

XR_TEST(RenderModeOutdoor_StepByTime)
{
  // Two steps of half the time make one whole step; none for no time; a longer frame steps further.
  const float s = 0.3f;
  CHECK_NEAR(Step(Step(s, 1, STEP_TIME / 2), 1, STEP_TIME / 2), Step(s, 1, STEP_TIME), 1e-6);
  CHECK_NEAR(Step(s, 1, 0), s, 1e-7);
  CHECK_NEAR(Step(s, 1, -1), s, 1e-7);
  CHECK(Step(s, 1, 2 * STEP_TIME) > Step(s, 1, STEP_TIME));
  CHECK_NEAR(Step(s, 1, STEP_TIME), s + (1 - s) * STEP_SHARE, 1e-6);
  CHECK_NEAR(Step(s, 0, STEP_TIME), s * (1 - STEP_SHARE), 1e-6);
}
