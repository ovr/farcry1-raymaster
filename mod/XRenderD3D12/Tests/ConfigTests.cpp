// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: ConfigTests.cpp
//	Description: The list of the options, CONFIGURATION.md in the
//	repository's root, against the console variables the renderer
//	registers (Renderer.cpp): each one a row of exactly one of its two
//	parts, for players or the system ones, and each row a variable that
//	exists.
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace
{
  std::vector<std::string> ReadLines(const std::filesystem::path &Path)
  {
    std::ifstream f(Path);
    std::vector<std::string> Lines;
    for (std::string s; std::getline(f, s);)
    {
      if (!s.empty() && s.back() == '\r')
        s.pop_back();
      Lines.push_back(s);
    }
    return Lines;
  }

  std::string ReadText(const std::filesystem::path &Path)
  {
    std::string Text;
    for (const std::string &s : ReadLines(Path))
      Text += s + "\n";
    return Text;
  }
} // namespace

// Why: the options were described only in mod/XRenderD3D12/README.md, among how each feature works; CONFIGURATION.md
// lists them for the players and keeps apart those better left alone. A new variable without its row there, a row of a
// variable gone, or one in both parts fails here.
XR_TEST(Config_Documented)
{
  namespace fs = std::filesystem;
  const fs::path Dir = XR_RENDERER_SOURCE_DIR;
  const fs::path Config = Dir / ".." / ".." / "CONFIGURATION.md";
  if (!fs::is_regular_file(Dir / "Renderer.cpp") || !fs::is_regular_file(Config))
    XR_SKIP("the renderer's sources are not where the tests were built from");

  // The renderer's own registrations (Renderer.cpp), and the original renderer's (ConsoleVariables.cpp's tables).
  std::set<std::string> Registered, Original;
  const std::string Code = ReadText(Dir / "Renderer.cpp");
  const std::regex Register(R"(iConsole->(Register|CreateVariable)\(\s*\"([A-Za-z0-9_]+)\")");
  for (std::sregex_iterator i(Code.begin(), Code.end(), Register), e; i != e; ++i)
    Registered.insert((*i)[2].str());
  const std::string Tables = ReadText(Dir / "ConsoleVariables.cpp");
  const std::regex Entry(R"(\{\s*\"(r_[A-Za-z0-9_]+)\"\s*,)");
  for (std::sregex_iterator i(Tables.begin(), Tables.end(), Entry), e; i != e; ++i)
    Original.insert((*i)[1].str());
  // The game's and the 3D engine's own, which the list names for the players.
  const std::set<std::string> Game = { "r_Width", "r_Height", "r_Fullscreen", "cl_fov" };

  // The rows: | `name` | ... under "## For players" or "## System (better not to touch)".
  const std::regex Row(R"(^\| `([A-Za-z0-9_]+)` \|)");
  std::map<std::string, std::vector<std::string>> Rows;  // name -> the parts it is in
  std::string Part;
  int nPlayers = 0, nSystem = 0;
  for (const std::string &s : ReadLines(Config))
  {
    if (s.rfind("## ", 0) == 0)
      Part = s.substr(3);
    std::smatch m;
    if (!std::regex_search(s, m, Row))
      continue;
    const std::string Name = m[1].str();
    CHECK_MSG(Part == "For players" || Part == "System (better not to touch)", Name + " under \"" + Part + "\"");
    Rows[Name].push_back(Part);
    (Part == "For players" ? nPlayers : nSystem)++;
    CHECK_MSG(Registered.count(Name) || Original.count(Name) || Game.count(Name), Name + ": no such variable");
  }
  for (const std::string &Name : Registered)
    CHECK_MSG(Rows.count(Name) == 1, Name + ": not in CONFIGURATION.md");
  for (const auto &r : Rows)
    CHECK_MSG(r.second.size() == 1, r.first + ": " + std::to_string(r.second.size()) + " rows");
  std::printf("  %zu variables registered, %d rows for players, %d system ones\n", Registered.size(), nPlayers, nSystem);
  CHECK(Registered.size() > 150 && nPlayers > 30 && nSystem > 50);
}
