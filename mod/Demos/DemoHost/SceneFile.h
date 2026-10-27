//////////////////////////////////////////////////////////////////////
//
//	File: SceneFile.h
//	Description: The scene file of a demo.
//
//	"key = value" lines; a line starting with '#' or ';' is a comment.
//	The scene files name the game's files a demo uses (textures, models,
//	levels) and stay in the game folder (Demos\<demo>.ini), never in the
//	repository.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>

class CSceneFile
{
public:
  //! False if the file can't be read (the scene then has no values).
  bool Load(const char *szFile);

  bool Has(const char *szKey) const;
  //! The value of szKey, or szDefault.
  std::string Get(const char *szKey, const char *szDefault = "") const;
  float GetFloat(const char *szKey, float fDefault) const;
  int GetInt(const char *szKey, int nDefault) const;
  //! The numbers of a value like "1 2 3" or "1, 2, 3"; empty if the key is missing.
  std::vector<float> GetFloats(const char *szKey) const;
  //! The keys starting with szPrefix (e.g. "texture" for texture1, texture2, ...), in file order.
  std::vector<std::string> GetKeys(const char *szPrefix) const;

private:
  std::vector<std::pair<std::string, std::string>> m_Values;
};
