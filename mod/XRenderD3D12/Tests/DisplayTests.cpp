// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DisplayTests.cpp
//	Description: Unit tests of when the window's display is looked up
//	again (DisplayPoll.h, CD3D12Device::GetDisplayInfo): a lookup is
//	0.14-0.18 ms on the main thread, so in the foreground only on the events
//	that change what it reads, in the background once a second.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "DisplayPoll.h"

#include <cstdint>

namespace
{
  //! A run of nFrames frames nStepMs apart from nStartMs, the display looked up whenever DisplayPoll::Needed says so
  //! (the state kept as GetDisplayInfo keeps it); returns the lookups. bCurrent, nMonitor and bActive for every frame.
  int Run(DisplayPoll::SLast &Last, int nFrames, uint64_t nStartMs, uint64_t nStepMs, bool bCurrent, uintptr_t nMonitor, bool bActive)
  {
    int nLookups = 0;
    for (int i = 0; i < nFrames; i++)
    {
      const uint64_t nNow = nStartMs + (uint64_t)i * nStepMs;
      if (!DisplayPoll::Needed(Last, bCurrent, nMonitor, bActive, nNow))
        continue;
      nLookups++;
      Last.bHave = true;
      Last.bStale = false;
      Last.nMonitor = nMonitor;
      Last.nTimeMs = nNow;
    }
    return nLookups;
  }
}

// Why: the game in the foreground must not look the display up on a timer: each lookup is a frame's spike of 0.14-0.18 ms
// on the main thread (it was once a second, in SDR too). Ten minutes at 4 ms a frame: the first lookup only.
XR_TEST(DisplayPoll_ForegroundOnce)
{
  DisplayPoll::SLast Last;
  CHECK(Run(Last, 150000, 5000, 4, true, 1, true) == 1);
  CHECK(Last.bHave && Last.nTimeMs == 5000);
}

// Why: what the lookup reads changes with Windows' displays (DXGI's factory then is no longer current: HDR switched on
// or off, a display came or went) and with the window's monitor; each must look it up again at once, and only once.
XR_TEST(DisplayPoll_Events)
{
  DisplayPoll::SLast Last;
  CHECK(Run(Last, 10, 0, 4, true, 1, true) == 1);
  // The window on another monitor: one lookup, then none.
  CHECK(Run(Last, 1000, 100, 4, true, 2, true) == 1);
  CHECK(Last.nMonitor == 2);
  // The factory not current: a lookup on every frame until GetDisplayInfo has a current one (it makes a new factory).
  CHECK(Run(Last, 3, 5000, 4, false, 2, true) == 3);
  CHECK(Run(Last, 1000, 6000, 4, true, 2, true) == 0);
  // Marked stale (WM_DISPLAYCHANGE, the focus back): one lookup.
  Last.bStale = true;
  CHECK(Run(Last, 1000, 20000, 4, true, 2, true) == 1);
  CHECK(!Last.bStale && Last.nTimeMs == 20000);
}

// Why: Windows' SDR content brightness is set in its Settings, with the game in the background: there a display beside
// the Settings window follows the slider once a second (nobody plays), and back in the foreground the timer stops.
XR_TEST(DisplayPoll_Background)
{
  DisplayPoll::SLast Last;
  CHECK(Run(Last, 1, 0, 4, true, 1, true) == 1);
  // 10 s in the background at 4 ms a frame: one lookup a second, at the second's frame, none in between.
  CHECK(Run(Last, 2500, 4, 4, true, 1, false) == 10);
  CHECK(Last.nTimeMs == 10000);
  CHECK(!DisplayPoll::Needed(Last, true, 1, false, 10999));
  CHECK(DisplayPoll::Needed(Last, true, 1, false, 11000));
  // The focus back (OnActivateApp marks it stale): one lookup, then none for a minute.
  Last.bStale = true;
  CHECK(Run(Last, 15000, 10100, 4, true, 1, true) == 1);
}
