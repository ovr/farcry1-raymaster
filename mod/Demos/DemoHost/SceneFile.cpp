//////////////////////////////////////////////////////////////////////
//
//	File: SceneFile.cpp
//	Description: The scene file of a demo.
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

static std::string Trim(const std::string &s)
{
  size_t nBegin = s.find_first_not_of(" \t\r\n");
  if (nBegin == std::string::npos)
    return std::string();
  size_t nEnd = s.find_last_not_of(" \t\r\n");
  return s.substr(nBegin, nEnd - nBegin + 1);
}

bool CSceneFile::Load(const char *szFile)
{
  m_Values.clear();
  FILE *pFile = fopen(szFile, "rt");
  if (!pFile)
    return false;
  char szLine[1024];
  while (fgets(szLine, sizeof(szLine), pFile))
  {
    std::string Line = Trim(szLine);
    if (Line.empty() || Line[0] == '#' || Line[0] == ';')
      continue;
    size_t nEq = Line.find('=');
    if (nEq == std::string::npos)
      continue;
    std::string Key = Trim(Line.substr(0, nEq));
    if (!Key.empty())
      m_Values.push_back(std::make_pair(Key, Trim(Line.substr(nEq + 1))));
  }
  fclose(pFile);
  return true;
}

bool CSceneFile::Has(const char *szKey) const
{
  for (size_t i = 0; i < m_Values.size(); i++)
  {
    if (!stricmp(m_Values[i].first.c_str(), szKey))
      return true;
  }
  return false;
}

std::string CSceneFile::Get(const char *szKey, const char *szDefault) const
{
  for (size_t i = 0; i < m_Values.size(); i++)
  {
    if (!stricmp(m_Values[i].first.c_str(), szKey))
      return m_Values[i].second;
  }
  return szDefault;
}

float CSceneFile::GetFloat(const char *szKey, float fDefault) const
{
  return Has(szKey) ? (float)atof(Get(szKey).c_str()) : fDefault;
}

int CSceneFile::GetInt(const char *szKey, int nDefault) const
{
  return Has(szKey) ? atoi(Get(szKey).c_str()) : nDefault;
}

std::vector<float> CSceneFile::GetFloats(const char *szKey) const
{
  std::vector<float> Numbers;
  std::string Value = Get(szKey);
  const char *p = Value.c_str();
  while (*p)
  {
    char *pEnd = NULL;
    float f = (float)strtod(p, &pEnd);
    if (pEnd == p)
    {
      p++;
      continue;
    }
    Numbers.push_back(f);
    p = pEnd;
  }
  return Numbers;
}

std::vector<std::string> CSceneFile::GetKeys(const char *szPrefix) const
{
  std::vector<std::string> Keys;
  size_t nLen = strlen(szPrefix);
  for (size_t i = 0; i < m_Values.size(); i++)
  {
    if (!strnicmp(m_Values[i].first.c_str(), szPrefix, nLen))
      Keys.push_back(m_Values[i].first);
  }
  return Keys;
}
