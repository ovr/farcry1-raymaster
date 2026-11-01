// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TestFramework.h
//	Description: The unit tests' own minimal framework (no third party
//	one): XR_TEST(name) { ... } registers a test, CHECK(cond) and
//	CHECK_NEAR(a, b, eps) record a failure with its file and line and go
//	on, XR_SKIP(reason) ends a test as skipped (a device without what it
//	needs). TestMain.cpp runs them: the exit code is the number of tests
//	that failed (0: all passed or skipped).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_TESTS_TESTFRAMEWORK_H
#define XRENDERD3D12_TESTS_TESTFRAMEWORK_H

#include <cmath>
#include <string>
#include <vector>

namespace XrTest
{
  using TestFunc = void (*)();

  struct STest
  {
    const char *szName;
    TestFunc pFunc;
  };

  //! The registered tests, in the order of registration (a function's static: no global object with a destructor that
  //! runs at an order of its own).
  std::vector<STest> &Tests();

  struct CRegister
  {
    CRegister(const char *szName, TestFunc pFunc) { Tests().push_back({ szName, pFunc }); }
  };

  //! A test's end as skipped (thrown by XR_SKIP, caught by the runner).
  struct SSkip
  {
    std::string Reason;
  };

  //! Records a failed check of the running test (only the first few are printed: a check in a loop over thousands of
  //! items prints a few and counts the rest).
  void Fail(const char *szFile, int nLine, const std::string &Message);

  inline bool Check(bool b, const char *szExpr, const char *szFile, int nLine)
  {
    if (!b)
      Fail(szFile, nLine, szExpr);
    return b;
  }

  inline bool CheckNear(double a, double b, double fEps, const char *szA, const char *szB, const char *szFile, int nLine)
  {
    const bool bOk = std::fabs(a - b) <= fEps;
    if (!bOk)
      Fail(szFile, nLine, std::string(szA) + " = " + std::to_string(a) + ", " + szB + " = " + std::to_string(b) + ", more apart than " +
                              std::to_string(fEps));
    return bOk;
  }
}

#define XR_TEST(name)                                                    \
  static void name();                                                    \
  static const XrTest::CRegister s_XrTestRegister_##name(#name, &name); \
  static void name()

#define CHECK(cond) XrTest::Check(!!(cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) XrTest::CheckNear((double)(a), (double)(b), (double)(eps), #a, #b, __FILE__, __LINE__)
//! A check with a message of its own (std::string) where the expression alone says too little.
#define CHECK_MSG(cond, msg) ((cond) ? true : (XrTest::Fail(__FILE__, __LINE__, std::string(#cond) + ": " + (msg)), false))
#define XR_SKIP(reason) throw XrTest::SSkip{ reason }

#endif
