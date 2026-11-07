// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawDistance.h
//	Description: The levels' view distances made farther (r_DrawDistance,
//	DrawDistance.cpp; the unit tests): the engine's variables it sets for
//	each level, and what it keeps to give them back. Without the renderer's
//	types.
//
//	For a level of the table the terrain is drawn at its full detail as far
//	as it is seen (e_terrain_lod_ratio 0: no coarser levels of detail far
//	away, the cliffs and the rocks in the sea keep their shape) and the
//	vegetation stays a model that many times farther before it turns into
//	its flat sprite (e_vegetation_sprites_distance_ratio, the player's own
//	value times the level's factor: 0.9 or 1 from the menu's presets). The
//	factors are each level's limit, found level by level by a draw distance
//	fix of the game's players (the 32-bit game, its Lua scripts of the
//	levels and -DEVMODE); a level that is not in the table (Control and Cooler, indoors, a level of a mod) is
//	left as the game has it. In the game's dev mode (-DEVMODE) the objects
//	are also seen farther (e_obj_view_dist_ratio 75, the game's 55: the
//	palms on the far islets, the characters) and the characters keep their
//	detailed models farther (ca_LodBias 0.015, the game's 0.125): both are
//	cheat variables, which the game lets change only in its dev mode.
//
//	Each variable is held (CHeldValue): its value before r_DrawDistance
//	set it is the game's own, given back when r_DrawDistance is 0, at a
//	level's change and at the end; when the game sets it while it is held
//	(a level's load, the menu, the console), that becomes its own value and
//	r_DrawDistance sets its own again.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_DRAWDISTANCE_H
#define XRENDERD3D12_DRAWDISTANCE_H

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace DrawDistance
{
  //! A level of the table: its folder's name and the factor of its vegetation's sprite distance.
  struct SLevel
  {
    const char *szName;
    float fSprites;
  };

  //! The levels whose distances r_DrawDistance makes farther and the factors of their vegetation's sprite distance (each
  //! level's limit); Volcano's vegetation keeps its distance (1), its terrain is drawn in full.
  constexpr SLevel LEVELS[] = {
    { "Archive", 10 }, { "Boat", 10 }, { "Bunker", 10 }, { "Carrier", 3 }, { "Catacombs", 5 }, { "Dam", 4 },
    { "Factory", 10 }, { "Fort", 3 }, { "Pier", 3 }, { "Rebellion", 10 }, { "Regulator", 10 }, { "Research", 4 },
    { "River", 3 }, { "Steam", 3 }, { "Swamp", 10 }, { "Training", 3 }, { "Treehouse", 10 }, { "Volcano", 1 },
  };

  // The variables and the values r_DrawDistance gives them.
  constexpr const char *TERRAIN_LOD = "e_terrain_lod_ratio";
  constexpr const char *TERRAIN_FULL = "0";
  constexpr const char *SPRITES = "e_vegetation_sprites_distance_ratio";
  constexpr const char *OBJECTS = "e_obj_view_dist_ratio";
  constexpr const char *OBJECTS_FAR = "75";
  constexpr const char *CHARACTER_LOD = "ca_LodBias";
  constexpr const char *CHARACTER_LOD_FAR = "0.015";

  //! The folder's name of a level's path (I3DEngine::GetLevelFilePath(""): "Levels/Training/"), "" without one.
  inline std::string_view LevelName(std::string_view Path)
  {
    while (!Path.empty() && (Path.back() == '/' || Path.back() == '\\'))
      Path.remove_suffix(1);
    const size_t n = Path.find_last_of("/\\");
    return n == std::string_view::npos ? Path : Path.substr(n + 1);
  }

  //! The table's level of that path (its folder's name in any case), nullptr for one not in it.
  inline const SLevel *FindLevel(std::string_view Path)
  {
    const std::string_view Name = LevelName(Path);
    for (const SLevel &Level : LEVELS)
    {
      const std::string_view s = Level.szName;
      if (s.size() != Name.size())
        continue;
      bool bSame = true;
      for (size_t i = 0; i < s.size() && bSame; i++)
        bSame = (s[i] | 0x20) == (Name[i] | 0x20);
      if (bSame)
        return &Level;
    }
    return nullptr;
  }

  //! The vegetation's sprite distance ratio for a level of factor fFactor from the player's own value (the variable's text).
  inline std::string SpritesValue(const std::string &Own, float fFactor)
  {
    char sz[32];
    snprintf(sz, sizeof(sz), "%g", (float)atof(Own.c_str()) * fFactor);
    return sz;
  }

  //! One variable r_DrawDistance holds, by its text (as the engine spells it). Each frame: Track with the variable's text,
  //! then Want with the value it should have (nullptr: none); what it returns is written into the variable, whose text
  //! then goes to Written.
  class CHeldValue
  {
  public:
    //! The variable's text now. Not held, or changed since it was written (by the game: a level's load, the menu, the
    //! console): that is the game's own value.
    void Track(std::string_view Now)
    {
      m_Now = Now;
      if (!m_bHeld || m_Now != m_Written)
        m_Own = m_Now;
    }
    //! The text to write this frame, for the value szWant (nullptr: given back the game's own), or none.
    std::optional<std::string> Want(const char *szWant)
    {
      if (szWant)
      {
        if (m_bHeld && m_Want == szWant && m_Now == m_Written)
          return std::nullopt;
        m_bHeld = true;
        m_Want = szWant;
        return m_Want;
      }
      if (!m_bHeld)
        return std::nullopt;
      m_bHeld = false;
      // Changed by the game since it was written: its value stays.
      if (m_Now != m_Written)
        return std::nullopt;
      return m_Own;
    }
    //! The variable's text after the write (the engine's spelling: "0.000000" for "0"; unchanged when it refused it).
    void Written(std::string_view Now)
    {
      m_Now = Now;
      m_Written = Now;
    }
    bool Held() const { return m_bHeld; }
    //! The game's own value (given back).
    const std::string &Own() const { return m_Own; }

  private:
    bool m_bHeld = false;
    std::string m_Now, m_Own, m_Want, m_Written;
  };
}

#endif
