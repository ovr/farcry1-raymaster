// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DisplayPoll.h
//	Description: When CD3D12Device::GetDisplayInfo looks the window's
//	display up again (its outputs' IDXGIOutput6::GetDesc1, Windows' SDR
//	white level through QueryDisplayConfig: 0.14-0.18 ms on the main
//	thread in step 329's tests, a frame's spike). On the events that
//	change what it reads: DXGI's factory no longer current (a display
//	came or went, its HDR mode changed), the window on another monitor,
//	the lookup marked stale (WM_DISPLAYCHANGE, the game getting the focus
//	back: Windows' SDR content brightness is set in its Settings, out of
//	the game). With the game in the foreground nothing else; in the
//	background once a second, so that a display beside the Settings
//	window follows the slider.
//	The unit tests (DisplayTests.cpp) hold it to that.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_DISPLAYPOLL_H
#define XRENDERD3D12_DISPLAYPOLL_H

#include <cstdint>

namespace DisplayPoll
{
  //! In the background the display is looked up again this often (ms).
  constexpr uint64_t BACKGROUND_MS = 1000;

  //! The last lookup: whether there is one, marked stale since, of which monitor, when (ms).
  struct SLast
  {
    bool bHave = false;
    bool bStale = false;
    uintptr_t nMonitor = 0;
    uint64_t nTimeMs = 0;
  };

  //! True when the display must be looked up again now: none yet or stale, the factory not current, the window on
  //! another monitor, or in the background a lookup BACKGROUND_MS old.
  inline bool Needed(const SLast &Last, bool bFactoryCurrent, uintptr_t nMonitor, bool bAppActive, uint64_t nNowMs)
  {
    if (!Last.bHave || Last.bStale || !bFactoryCurrent || nMonitor != Last.nMonitor)
      return true;
    return !bAppActive && nNowMs - Last.nTimeMs >= BACKGROUND_MS;
  }
}

#endif
