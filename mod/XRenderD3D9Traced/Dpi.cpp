// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Dpi.cpp
//	Description: DPI awareness of the game's process.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "Dpi.h"

// The user32 functions are looked up at run time: they are newer than the Windows version the
// SDK headers are set for (SetProcessDpiAwarenessContext: Windows 10 1703).
typedef HANDLE DpiContext;
typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DpiContext);
typedef DpiContext (WINAPI *PFN_SetThreadDpiAwarenessContext)(DpiContext);
typedef DpiContext (WINAPI *PFN_GetWindowDpiAwarenessContext)(HWND);
typedef int (WINAPI *PFN_GetAwarenessFromDpiAwarenessContext)(DpiContext);
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
typedef BOOL (WINAPI *PFN_SetProcessDPIAware)();

//! DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 of WinUser.h.
static const DpiContext PerMonitorAwareV2 = (DpiContext)-4;

static FARPROC GetUser32Proc(const char *szName)
{
  HMODULE hUser32 = GetModuleHandleA("user32.dll");
  return hUser32 ? GetProcAddress(hUser32, szName) : NULL;
}

void Dpi_MakeAware()
{
  char szEnv[8] = "";
  if (GetEnvironmentVariableA("XRENDERTRACED_DPI", szEnv, sizeof(szEnv)) && szEnv[0] == '0')
  {
    Trace_Write("DPI awareness: left as the game starts (XRENDERTRACED_DPI=0)");
    return;
  }
  PFN_SetProcessDpiAwarenessContext pfnSetProcess = (PFN_SetProcessDpiAwarenessContext)GetUser32Proc("SetProcessDpiAwarenessContext");
  if (pfnSetProcess)
  {
    if (pfnSetProcess(PerMonitorAwareV2))
    {
      Trace_Write("DPI awareness: the process is per monitor aware (v2)");
      return;
    }
    // The process has an awareness already: the thread that makes the window gets it instead.
    const DWORD nError = GetLastError();
    PFN_SetThreadDpiAwarenessContext pfnSetThread = (PFN_SetThreadDpiAwarenessContext)GetUser32Proc("SetThreadDpiAwarenessContext");
    const bool bThread = pfnSetThread && pfnSetThread(PerMonitorAwareV2);
    Trace_Write("DPI awareness: the process has one already (error %u); this thread %s", nError,
      bThread ? "is per monitor aware (v2)" : "is left as it is");
    return;
  }
  PFN_SetProcessDPIAware pfnSetAware = (PFN_SetProcessDPIAware)GetUser32Proc("SetProcessDPIAware");
  const bool bAware = pfnSetAware && pfnSetAware();
  Trace_Write("DPI awareness: %s", bAware ? "the process is system aware" : "not supported");
}

void Dpi_Describe(HWND hWnd, char *szText, size_t nSize)
{
  PFN_GetDpiForWindow pfnGetDpi = (PFN_GetDpiForWindow)GetUser32Proc("GetDpiForWindow");
  PFN_GetWindowDpiAwarenessContext pfnGetContext = (PFN_GetWindowDpiAwarenessContext)GetUser32Proc("GetWindowDpiAwarenessContext");
  PFN_GetAwarenessFromDpiAwarenessContext pfnGetAwareness =
    (PFN_GetAwarenessFromDpiAwarenessContext)GetUser32Proc("GetAwarenessFromDpiAwarenessContext");
  if (!hWnd || !pfnGetDpi || !pfnGetContext || !pfnGetAwareness)
  {
    _snprintf_s(szText, nSize, _TRUNCATE, "DPI unknown");
    return;
  }
  // DPI_AWARENESS of WinUser.h.
  static const char *szAwareness[] = { "unaware", "system aware", "per monitor aware" };
  const UINT nDpi = pfnGetDpi(hWnd);
  const int nAwareness = pfnGetAwareness(pfnGetContext(hWnd));
  _snprintf_s(szText, nSize, _TRUNCATE, "DPI %u (%u %%), %s", nDpi, nDpi * 100 / 96,
    nAwareness >= 0 && nAwareness < 3 ? szAwareness[nAwareness] : "awareness unknown");
}
