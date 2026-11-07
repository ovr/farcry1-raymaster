// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawDistanceTests.cpp
//	Description: Unit tests of r_DrawDistance's table and of how it holds
//	the engine's variables (DrawDistance.h, on the CPU): the levels found
//	by their path, the vegetation's ratio from the player's own, and the
//	game's own value kept, followed and given back, against a variable that
//	spells its floats as the engine's console does ("3.000000").
//
//////////////////////////////////////////////////////////////////////

#include "TestFramework.h"

#include "DrawDistance.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace XrTest;
using namespace DrawDistance;

namespace
{
  //! A float console variable: its text is the value written as "%f".
  struct SFloatVar
  {
    std::string Text;
    explicit SFloatVar(float f) { Set(f); }
    void Set(float f)
    {
      char sz[32];
      snprintf(sz, sizeof(sz), "%f", f);
      Text = sz;
    }
    void Set(const std::string &s) { Set((float)atof(s.c_str())); }
  };

  //! One frame of DrawDistance.cpp's loop for that variable: Track, Want, the write, Written.
  void Frame(CHeldValue &Held, SFloatVar &Var, const char *szWant)
  {
    Held.Track(Var.Text);
    if (std::optional<std::string> Write = Held.Want(szWant))
    {
      Var.Set(*Write);
      Held.Written(Var.Text);
    }
  }
}

// Why: the engine names a level by its folder (GetLevelFilePath("") is "Levels/Training/", in the case of the menu or of
// the console's "map training"); a level that is not in the table (Control, a mod's) must be left as the game has it.
XR_TEST(DrawDistance_FindLevel)
{
  CHECK(LevelName("Levels/Training/") == "Training");
  CHECK(LevelName("levels\\regulator\\") == "regulator");
  CHECK(LevelName("") == "");
  const SLevel *p = FindLevel("Levels/Training/");
  CHECK(p && p->fSprites == 3);
  p = FindLevel("levels\\REGULATOR\\");
  CHECK(p && p->fSprites == 10);
  p = FindLevel("Levels/Volcano/");
  CHECK(p && p->fSprites == 1);
  CHECK(FindLevel("Levels/Control/") == nullptr);
  CHECK(FindLevel("Levels/Cooler/") == nullptr);
  CHECK(FindLevel("Levels/Train/") == nullptr);
  CHECK(FindLevel("") == nullptr);
}

// Why: the vegetation's ratio is the player's own (the menu's 0.9 or 1) times the level's factor, so that the lower
// presets stay lower; the value is written as the console takes it.
XR_TEST(DrawDistance_SpritesValue)
{
  CHECK(SpritesValue("1.000000", 3) == "3");
  CHECK(SpritesValue("0.900000", 10) == "9");
  CHECK(atof(SpritesValue("0.900000", 3).c_str()) > 2.69 && atof(SpritesValue("0.900000", 3).c_str()) < 2.71);
}

// Why: r_DrawDistance sets the variable once (not every frame), and gives back the value the game had: with 0 the
// frames are those before it (the terrain's ratio the level's own 3, not the engine's start value).
XR_TEST(DrawDistance_HoldAndGiveBack)
{
  CHeldValue Held;
  SFloatVar Terrain(3);
  Frame(Held, Terrain, TERRAIN_FULL);
  CHECK(Held.Held());
  CHECK(Terrain.Text == "0.000000");
  CHECK(Held.Own() == "3.000000");
  // The next frames: nothing to write.
  Held.Track(Terrain.Text);
  CHECK(!Held.Want(TERRAIN_FULL).has_value());
  Frame(Held, Terrain, nullptr);
  CHECK(!Held.Held());
  CHECK(Terrain.Text == "3.000000");
  // Not held: nothing written.
  Held.Track(Terrain.Text);
  CHECK(!Held.Want(nullptr).has_value());
}

// Why: a level's load sets the terrain's ratio again (its own 3) while r_DrawDistance holds it, and the menu the
// vegetation's: that is the game's own value from then on, and r_DrawDistance's is set again (the next frame).
XR_TEST(DrawDistance_GameChangesIt)
{
  CHeldValue Held;
  SFloatVar Sprites(1);
  Held.Track(Sprites.Text);
  std::string Want = SpritesValue(Held.Own(), 3);
  if (std::optional<std::string> w = Held.Want(Want.c_str()))
  {
    Sprites.Set(*w);
    Held.Written(Sprites.Text);
  }
  CHECK(Sprites.Text == "3.000000");
  // The menu's lower preset.
  Sprites.Set(0.9f);
  Held.Track(Sprites.Text);
  CHECK(Held.Own() == "0.900000");
  Want = SpritesValue(Held.Own(), 3);
  std::optional<std::string> w = Held.Want(Want.c_str());
  CHECK(w.has_value());
  if (w)
  {
    Sprites.Set(*w);
    Held.Written(Sprites.Text);
  }
  CHECK(Sprites.Text == "2.700000");
  // Given back: the menu's value, not the one before it.
  Frame(Held, Sprites, nullptr);
  CHECK(Sprites.Text == "0.900000");
}

// Why: on another level of the table the value changes (Training's 3 to Regulator's 10) with the game's own kept; when
// the game changed it after the last write, giving back leaves the game's value (a level's load set its own).
XR_TEST(DrawDistance_LevelChange)
{
  CHeldValue Held;
  SFloatVar Sprites(1);
  Frame(Held, Sprites, "3");
  Frame(Held, Sprites, "10");
  CHECK(Sprites.Text == "10.000000");
  CHECK(Held.Own() == "1.000000");
  // The game sets its own while held, and r_DrawDistance gives it back in the same frame: the game's stays.
  SFloatVar Terrain(3);
  CHeldValue HeldTerrain;
  Frame(HeldTerrain, Terrain, TERRAIN_FULL);
  Terrain.Set(2.0f);
  Frame(HeldTerrain, Terrain, nullptr);
  CHECK(Terrain.Text == "2.000000");
  CHECK(!HeldTerrain.Held());
}

// Why: a cheat variable the engine refuses outside its dev mode reads back unchanged: r_DrawDistance must not write it
// again every frame, and giving it back is harmless.
XR_TEST(DrawDistance_Refused)
{
  CHeldValue Held;
  const std::string Before = "55.000000";
  Held.Track(Before);
  CHECK(Held.Want(OBJECTS_FAR).has_value());
  Held.Written(Before);   // refused
  Held.Track(Before);
  CHECK(!Held.Want(OBJECTS_FAR).has_value());
  Held.Track(Before);
  std::optional<std::string> w = Held.Want(nullptr);
  CHECK(w.has_value() && *w == Before);
}
