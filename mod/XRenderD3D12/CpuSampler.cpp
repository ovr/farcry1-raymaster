// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CpuSampler.cpp
//	Description: d3d12_CpuSamples, a sampling profiler of the thread that
//	draws the frames. While it is on, a thread of its own stops that thread
//	every N microseconds, walks its stack with the x64 unwind data of the
//	modules and keeps the return addresses (into memory taken beforehand:
//	nothing is allocated while the thread is stopped, it could hold the
//	heap's lock). When it goes back to 0, or the renderer is released,
//	XRenderD3D12_samples.txt gets the modules of the process and a line per
//	sample: the frame and the stack as module and offset, leaf first. The
//	samples are of the wall clock: a thread that waits is sampled in its
//	wait. Our functions are named by the linker's map (XRenderD3D12.map).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "CpuSampler.h"
#include <psapi.h>
#include <atomic>

namespace
{
//! The CPU's clock in milliseconds.
double NowMs()
{
  static const double fMsPerTick = []
  {
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);
    return 1000.0 / (double)Frequency.QuadPart;
  }();
  LARGE_INTEGER Now;
  QueryPerformanceCounter(&Now);
  return (double)Now.QuadPart * fMsPerTick;
}

constexpr size_t MAX_DEPTH = 64;
constexpr size_t MAX_WORDS = size_t(8) << 20;   // 64 MB: the frame, the depth and the addresses of each sample

struct SSampler
{
  HANDLE hTarget = nullptr;
  HANDLE hThread = nullptr;
  int nIntervalUs = 0;
  std::atomic<bool> bStop = false;
  std::atomic<int> nFrame = 0;
  std::vector<uint64> Words;
  size_t nWords = 0;
  uint64 nSamples = 0;
  uint64 nDropped = 0;
  double fStoppedMs = 0;   // how long the thread was held, in all
  double fStartMs = 0;
};

SSampler *g_pSampler = nullptr;

// The stack from a stopped thread's context, leaf first. Reads only that thread's stack and the
// modules' unwind data.
size_t WalkStack(CONTEXT &Ctx, uint64 *pOut, size_t nMax)
{
  size_t n = 0;
  __try
  {
    while (n < nMax && Ctx.Rip)
    {
      pOut[n++] = Ctx.Rip;
      DWORD64 nImageBase = 0;
      PRUNTIME_FUNCTION pFunction = RtlLookupFunctionEntry(Ctx.Rip, &nImageBase, nullptr);
      if (!pFunction)
      {
        // A leaf function: its return address is at the top of the stack.
        Ctx.Rip = *(const DWORD64 *)Ctx.Rsp;
        Ctx.Rsp += 8;
        continue;
      }
      void *pHandlerData = nullptr;
      DWORD64 nEstablisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, nImageBase, Ctx.Rip, pFunction, &Ctx, &pHandlerData, &nEstablisher, nullptr);
    }
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
  }
  return n;
}

DWORD WINAPI SamplerThread(void *pParam)
{
  SSampler &s = *(SSampler *)pParam;
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
  HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  uint64 Stack[MAX_DEPTH];
  while (!s.bStop)
  {
    LARGE_INTEGER Due;
    Due.QuadPart = -10ll * s.nIntervalUs;
    if (hTimer && SetWaitableTimer(hTimer, &Due, 0, nullptr, nullptr, FALSE))
      WaitForSingleObject(hTimer, INFINITE);
    else
      Sleep(1);
    const double fStart = NowMs();
    if (SuspendThread(s.hTarget) == (DWORD)-1)
      break;
    alignas(16) CONTEXT Ctx = {};
    Ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    const size_t n = GetThreadContext(s.hTarget, &Ctx) ? WalkStack(Ctx, Stack, MAX_DEPTH) : 0;
    ResumeThread(s.hTarget);
    s.fStoppedMs += NowMs() - fStart;
    if (!n || s.nWords + 2 + n > s.Words.size())
    {
      s.nDropped++;
      continue;
    }
    s.Words[s.nWords++] = (uint64)s.nFrame.load();
    s.Words[s.nWords++] = n;
    memcpy(&s.Words[s.nWords], Stack, n * sizeof(uint64));
    s.nWords += n;
    s.nSamples++;
  }
  if (hTimer)
    CloseHandle(hTimer);
  return 0;
}

struct SModule
{
  uint64 nBase, nEnd;
  std::string Name;
};

void Write(const SSampler &s, const char *szFile)
{
  FILE *f = fopen(szFile, "w");
  if (!f)
    return;
  // The modules loaded now (the sampled code is in them, unless one was unloaded since).
  std::vector<SModule> Modules;
  HMODULE hModules[1024];
  DWORD nBytes = 0;
  if (EnumProcessModules(GetCurrentProcess(), hModules, sizeof(hModules), &nBytes))
  {
    for (DWORD i = 0; i < std::min<DWORD>(nBytes / sizeof(HMODULE), 1024); i++)
    {
      MODULEINFO Info = {};
      char szName[MAX_PATH] = {};
      if (!GetModuleInformation(GetCurrentProcess(), hModules[i], &Info, sizeof(Info)) ||
          !GetModuleBaseNameA(GetCurrentProcess(), hModules[i], szName, MAX_PATH))
        continue;
      Modules.push_back({ (uint64)Info.lpBaseOfDll, (uint64)Info.lpBaseOfDll + Info.SizeOfImage, szName });
    }
  }
  std::sort(Modules.begin(), Modules.end(), [](const SModule &a, const SModule &b) { return a.nBase < b.nBase; });
  fprintf(f, "# d3d12_CpuSamples: the render thread's stack every %d us for %.0f ms; %llu samples, %llu dropped; the thread was held "
             "%.3f ms in all\n# m <index> <name> <base>: the modules; s <frame> <module>:<offset> ...: a sample, leaf first ('-' for no "
             "module)\n",
          s.nIntervalUs, NowMs() - s.fStartMs, (unsigned long long)s.nSamples, (unsigned long long)s.nDropped, s.fStoppedMs);
  for (size_t i = 0; i < Modules.size(); i++)
    fprintf(f, "m %zu %s %llx\n", i, Modules[i].Name.c_str(), (unsigned long long)Modules[i].nBase);
  for (size_t w = 0; w + 2 <= s.nWords;)
  {
    const uint64 nFrame = s.Words[w], n = s.Words[w + 1];
    w += 2;
    fprintf(f, "s %llu", (unsigned long long)nFrame);
    for (uint64 i = 0; i < n && w < s.nWords; i++, w++)
    {
      const uint64 a = s.Words[w];
      auto it = std::upper_bound(Modules.begin(), Modules.end(), a, [](uint64 v, const SModule &m) { return v < m.nBase; });
      if (it != Modules.begin() && a < (it - 1)->nEnd)
        fprintf(f, " %zu:%llx", (size_t)(it - 1 - Modules.begin()), (unsigned long long)(a - (it - 1)->nBase));
      else
        fprintf(f, " -:%llx", (unsigned long long)a);
    }
    fputc('\n', f);
  }
  fclose(f);
}

void Stop(const char *szFile)
{
  SSampler *s = g_pSampler;
  if (!s)
    return;
  s->bStop = true;
  WaitForSingleObject(s->hThread, INFINITE);
  CloseHandle(s->hThread);
  Write(*s, szFile);
  CloseHandle(s->hTarget);
  delete s;
  g_pSampler = nullptr;
}
}   // namespace

void CpuSampler::Update(int nIntervalUs, int nFrame, const char *szFile)
{
  if (nIntervalUs <= 0)
  {
    Stop(szFile);
    return;
  }
  if (!g_pSampler)
  {
    auto *s = new SSampler;
    s->nIntervalUs = std::max(nIntervalUs, 50);
    s->Words.resize(MAX_WORDS);
    s->fStartMs = NowMs();
    s->hTarget = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
    s->hThread = s->hTarget ? CreateThread(nullptr, 0, SamplerThread, s, 0, nullptr) : nullptr;
    if (!s->hThread)
    {
      if (s->hTarget)
        CloseHandle(s->hTarget);
      delete s;
      return;
    }
    g_pSampler = s;
  }
  g_pSampler->nFrame = nFrame;
}
