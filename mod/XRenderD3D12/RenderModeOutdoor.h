// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RenderModeOutdoor.h
//	Description: The render modes' glare between indoors and outdoors
//	(d3d12_RenderModeOutdoor, PostFx.cpp), as the original's screen process
//	moves it (traced in Training's bunker and on its bay's hill, and in
//	Swamp):
//	  - an outdoor share s steps 2 % of the way to its target every 1/50 s:
//	    1 outdoors, 0 with the camera in a vis area the outdoor lights do
//	    not reach. From the bunker to the hill at once, s was 0.183 ten
//	    frames later and 0.636 fifty frames later, 1 - 0.98^n;
//	  - the glare's threshold and gain go from the mode's indoor pair to its
//	    outdoor one by s: Improved -0.25, 0.8 to -0.2, 0.5; Paradise -0.1,
//	    0.8 to -0.2, 1; Cold -0.1, 0.65 to -0.2, 0.5. The luminance's share
//	    at the end stays (0.2, 0.1, 0.425).
//	Pure, for the unit tests.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_RENDERMODEOUTDOOR_H
#define XRENDERD3D12_RENDERMODEOUTDOOR_H

#include <algorithm>
#include <cmath>

namespace RenderModeOutdoor
{

//! The share of the way the outdoor share steps every STEP_TIME (the original's frame at 50 per second).
constexpr float STEP_SHARE = 0.02f;
constexpr float STEP_TIME = 0.02f;

//! The outdoor share s after dt seconds towards fTarget (0 or 1): STEP_SHARE of the way every STEP_TIME and as
//! smoothly in between (two steps of half the time make one whole); none for a time of 0 or less.
inline float Step(float s, float fTarget, float dt)
{
  const float k = 1 - std::pow(1 - STEP_SHARE, std::max(dt, 0.0f) / STEP_TIME);
  return std::clamp(s + (fTarget - s) * k, 0.0f, 1.0f);
}

//! A render mode's glare: the threshold added to the reduced scene before it is squared, and the gain.
struct SGlare
{
  float fThreshold, fGain;
};

//! The glare at the outdoor share s: the indoor pair at 0, the outdoor one at 1.
inline SGlare Blend(const SGlare &Indoor, const SGlare &Outdoor, float s)
{
  s = std::clamp(s, 0.0f, 1.0f);
  return { Indoor.fThreshold + (Outdoor.fThreshold - Indoor.fThreshold) * s, Indoor.fGain + (Outdoor.fGain - Indoor.fGain) * s };
}

//! The original's pairs of render modes 1-3 (improved, paradise, cold), indoors and outdoors (traced).
constexpr SGlare INDOOR[3] = { { -0.25f, 0.8f }, { -0.1f, 0.8f }, { -0.1f, 0.65f } };
constexpr SGlare OUTDOOR[3] = { { -0.2f, 0.5f }, { -0.2f, 1.0f }, { -0.2f, 0.5f } };

} // namespace RenderModeOutdoor

#endif
