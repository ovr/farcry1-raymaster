// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: CommandLine.h
//	Description: A console variable's value on the game's command line
//	(the unit tests), without the renderer's types. The engine sets the
//	command line's variables ("r_TexUpscaled 0") only after its start has
//	loaded the menu's and the shaders' textures; a variable that decides
//	how a texture loads is taken from the line when it is registered.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_COMMANDLINE_H
#define XRENDERD3D12_COMMANDLINE_H

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

namespace CommandLine
{
  //! The integer after the console variable Name on the command line Line ("name value" or "name=value", in quotes or
  //! not, the name in any case, after a space, a quote, '+' or '-' or at the start; the last one counts), else nDefault.
  inline int IntValue(std::string_view Line, std::string_view Name, int nDefault)
  {
    auto Lower = [](std::string_view s)
    {
      std::string r(s);
      for (char &c : r)
        c = (char)std::tolower((unsigned char)c);
      return r;
    };
    const std::string s = Lower(Line), n = Lower(Name);
    if (n.empty())
      return nDefault;
    int nValue = nDefault;
    for (size_t nAt = s.find(n); nAt != std::string::npos; nAt = s.find(n, nAt + 1))
    {
      const bool bStart = nAt == 0 || s[nAt - 1] == ' ' || s[nAt - 1] == '\t' || s[nAt - 1] == '"' || s[nAt - 1] == '+' ||
                          s[nAt - 1] == '-';
      size_t p = nAt + n.size();
      if (!bStart || p >= s.size() || (s[p] != ' ' && s[p] != '\t' && s[p] != '='))
        continue;
      while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '='))
        p++;
      const char *szFrom = s.c_str() + p;
      char *pEnd = nullptr;
      const long n2 = std::strtol(szFrom, &pEnd, 10);
      if (pEnd != szFrom)
        nValue = (int)n2;
    }
    return nValue;
  }
}

#endif
