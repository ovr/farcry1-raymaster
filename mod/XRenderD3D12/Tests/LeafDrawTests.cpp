// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LeafDrawTests.cpp
//	Description: Unit tests of how LeafBuffer.cpp hands a leaf buffer's
//	draw out (Geometry::GetDraw): its sources as they must stay.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using namespace XrTest;

namespace
{
  //! The text of a function of the renderer's source from its signature to its closing brace at the line's start; empty
  //! when the file or the function is not there.
  std::string FunctionText(const char *pFile, const char *pSignature)
  {
    namespace fs = std::filesystem;
    std::ifstream f(fs::path(XR_RENDERER_SOURCE_DIR) / pFile, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string Text = ss.str();
    const size_t nFrom = Text.find(pSignature);
    const size_t nTo = nFrom == std::string::npos ? nFrom : Text.find("\n}", nFrom);
    return nTo == std::string::npos ? std::string() : Text.substr(nFrom, nTo - nFrom);
  }
}

// Why: a draw made in a local and returned was copied into the caller's optional, its fields written one by one and
// read back 16 bytes at a time (a store forwarding stall each, 4 a draw); every return is the one named result, filled
// where the caller has it.
XR_TEST(LeafDraw_OneResult)
{
  if (!std::filesystem::is_regular_file(std::filesystem::path(XR_RENDERER_SOURCE_DIR) / "LeafBuffer.cpp"))
    XR_SKIP("the renderer's sources are not where the tests were built from");
  const std::string Body = FunctionText("LeafBuffer.cpp", "std::optional<SGeomDraw> Geometry::GetDraw(CLeafBuffer *pLB");
  if (!CHECK_MSG(!Body.empty(), "Geometry::GetDraw(CLeafBuffer *, const CMatInfo *) not found"))
    return;
  const std::regex Return(R"(\n[ \t]+return\b([^;]*);)");  // the code's, not the comments'
  int nReturns = 0;
  for (std::sregex_iterator it(Body.begin(), Body.end(), Return), end; it != end; ++it, nReturns++)
    CHECK_MSG((*it)[1].str() == " Draw", "GetDraw returns" + (*it)[1].str() + ", not its one result");
  CHECK(nReturns >= 5);
  CHECK(Body.find("SGeomDraw &d = Draw.emplace();") != std::string::npos);
  CHECK(Body.find("SGeomDraw d;") == std::string::npos);
}
