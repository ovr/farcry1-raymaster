// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestMain.cpp
//	Description: The runner of XRenderD3D12's unit tests:
//	  XRenderD3D12Tests [--warp] [--debug] [--list] [name ...]
//	runs every test (or those whose names contain one of the names given),
//	prints each one's result and the checks that failed, and exits with
//	the number of tests that failed. --warp runs the GPU tests on WARP
//	(Direct3D 12's software device) instead of the first hardware adapter,
//	--debug with the Direct3D 12 debug layer (GpuHarness.h).
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"
#include "GpuHarness.h"

#include <cstdio>
#include <cstring>
#include <exception>

namespace
{
  int s_nFailedChecks = 0;
}

std::vector<XrTest::STest> &XrTest::Tests()
{
  static std::vector<STest> s_Tests;
  return s_Tests;
}

void XrTest::Fail(const char *szFile, int nLine, const std::string &Message)
{
  // The first few failures of a test are printed, the rest only counted.
  if (s_nFailedChecks < 8)
  {
    const char *szName = strrchr(szFile, '\\');
    szName = szName ? szName + 1 : szFile;
    printf("    %s(%d): %s\n", szName, nLine, Message.c_str());
  }
  s_nFailedChecks++;
}

int main(int argc, char **argv)
{
  std::vector<const char *> Filters;
  bool bList = false;
  for (int i = 1; i < argc; i++)
  {
    if (!strcmp(argv[i], "--warp"))
      XrTest::GpuOptions().bWarp = true;
    else if (!strcmp(argv[i], "--debug"))
      XrTest::GpuOptions().bDebug = true;
    else if (!strcmp(argv[i], "--list"))
      bList = true;
    else
      Filters.push_back(argv[i]);
  }
  int nPassed = 0, nFailed = 0, nSkipped = 0;
  for (const XrTest::STest &Test : XrTest::Tests())
  {
    bool bWanted = Filters.empty();
    for (const char *szFilter : Filters)
      bWanted |= strstr(Test.szName, szFilter) != nullptr;
    if (!bWanted)
      continue;
    if (bList)
    {
      printf("%s\n", Test.szName);
      continue;
    }
    s_nFailedChecks = 0;
    printf("[ RUN  ] %s\n", Test.szName);
    fflush(stdout);
    try
    {
      Test.pFunc();
    }
    catch (const XrTest::SSkip &Skip)
    {
      printf("[ SKIP ] %s: %s\n", Test.szName, Skip.Reason.c_str());
      nSkipped++;
      continue;
    }
    catch (const std::exception &e)
    {
      XrTest::Fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
    }
    if (s_nFailedChecks)
    {
      printf("[ FAIL ] %s: %d failed checks\n", Test.szName, s_nFailedChecks);
      nFailed++;
    }
    else
    {
      printf("[  OK  ] %s\n", Test.szName);
      nPassed++;
    }
    fflush(stdout);
  }
  if (!bList)
    printf("%d passed, %d failed, %d skipped%s\n", nPassed, nFailed, nSkipped,
           XrTest::GpuReady() ? (std::string(" (GPU: ") + XrTest::Gpu().AdapterName() + (XrTest::Gpu().HasDebugLayer() ? ", debug layer" : "") + ")").c_str()
                              : "");
  return nFailed;
}
