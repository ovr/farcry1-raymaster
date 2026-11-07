// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DrawDistance.cpp
//	Description: The levels' view distances made farther (r_DrawDistance):
//	the engine's variables of the terrain's detail, the vegetation's sprites
//	and, in the game's dev mode, the objects' view distance and the
//	characters' detail, set for the level the engine has (DrawDistance.h's
//	table and values) and given back their own values.
//
//	Once a frame, from the loading screen's frames on: what the game set
//	since (a level's load sets the terrain's ratio, the menu the vegetation's)
//	becomes the variable's own value, and r_DrawDistance's is set again. A
//	variable the game saves into system.cfg (VF_DUMPTODISK: the vegetation's
//	ratio) has that flag taken off while it is held, so that the game saves
//	none of r_DrawDistance's values (it saves system.cfg when it quits, also
//	from a level): the next start, another renderer and r_DrawDistance 0
//	get the player's own (the engine's default when the game saved it while
//	held: 1, the very high preset's). A cheat variable (VF_CHEAT) is set only
//	in the game's dev mode, as the console sets it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "DrawDistance.h"

namespace
{
  //! The variables of m_DrawDistanceVars, in its order.
  constexpr const char *VAR_NAMES[] = {
    DrawDistance::TERRAIN_LOD, DrawDistance::SPRITES, DrawDistance::OBJECTS, DrawDistance::CHARACTER_LOD,
  };
}

void CD3D12Renderer::UpdateDrawDistance()
{
  I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : nullptr;
  if (!p3DEngine || !iConsole)
    return;
  const char *szPath = p3DEngine->GetLevelFilePath("");
  const std::string_view Path = szPath ? szPath : "";
  const std::string_view Level = DrawDistance::LevelName(Path);
  const DrawDistance::SLevel *pLevel = CV_r_DrawDistance && !Level.empty() ? DrawDistance::FindLevel(Path) : nullptr;
  const bool bAnyLevel = CV_r_DrawDistance && !Level.empty();
  const bool bDevMode = iSystem->IsDevMode();
  static_assert(std::size(VAR_NAMES) == DRAW_DISTANCE_VARS);
  const bool bNewLevel = Level != m_DrawDistanceLevel;
  if (bNewLevel)
    m_DrawDistanceLevel = Level;
  bool bCheatsLeft = false;
  for (int i = 0; i < DRAW_DISTANCE_VARS; i++)
  {
    SDrawDistanceVar &v = m_DrawDistanceVars[i];
    if (!v.pVar)
      v.pVar = iConsole->GetCVar(VAR_NAMES[i]);   // ca_LodBias: once CryAnimation has it
    if (!v.pVar)
      continue;
    const char *szNow = v.pVar->GetString();
    v.Held.Track(szNow ? szNow : "");
    std::string Want;
    switch (i)
    {
    case 0:
      if (pLevel)
        Want = DrawDistance::TERRAIN_FULL;
      break;
    case 1:
      if (pLevel && pLevel->fSprites != 1)
        Want = DrawDistance::SpritesValue(v.Held.Own(), pLevel->fSprites);
      break;
    case 2:
      if (bAnyLevel)
        Want = DrawDistance::OBJECTS_FAR;
      break;
    default:
      if (bAnyLevel)
        Want = DrawDistance::CHARACTER_LOD_FAR;
      break;
    }
    // A cheat variable only in the game's dev mode (the console's rule for it).
    if (!Want.empty() && (v.pVar->GetFlags() & VF_CHEAT) && !bDevMode)
    {
      Want.clear();
      bCheatsLeft = true;
    }
    if (std::optional<std::string> Write = v.Held.Want(Want.empty() ? nullptr : Want.c_str()))
    {
      // Not saved while held: off before the write, on again once the game's own value is back.
      if (v.Held.Held() && !v.bDumpCleared && (v.pVar->GetFlags() & VF_DUMPTODISK))
      {
        v.pVar->ClearFlags(VF_DUMPTODISK);
        v.bDumpCleared = true;
      }
      v.pVar->Set(Write->c_str());
      szNow = v.pVar->GetString();
      v.Held.Written(szNow ? szNow : "");
      if (v.Held.Held())
        iLog->Log("XRenderD3D12: r_DrawDistance, level %s: %s %s (the game's %s)", m_DrawDistanceLevel.c_str(), VAR_NAMES[i],
                  szNow ? szNow : "", v.Held.Own().c_str());
      else
        iLog->Log("XRenderD3D12: r_DrawDistance %d, level %s: %s %s, the game's own given back", CV_r_DrawDistance,
                  Level.empty() ? "none" : m_DrawDistanceLevel.c_str(), VAR_NAMES[i], szNow ? szNow : "");
    }
    if (!v.Held.Held() && v.bDumpCleared)
    {
      v.pVar->SetFlags(v.pVar->GetFlags() | VF_DUMPTODISK);
      v.bDumpCleared = false;
    }
  }
  if (bNewLevel && bCheatsLeft)
    iLog->Log("XRenderD3D12: r_DrawDistance, level %s: %s and %s left as they are (cheat variables: the game's dev mode, -DEVMODE, "
              "lets them change)", m_DrawDistanceLevel.c_str(), DrawDistance::OBJECTS, DrawDistance::CHARACTER_LOD);
}

void CD3D12Renderer::ReleaseDrawDistance()
{
  if (!iConsole)
    return;
  for (SDrawDistanceVar &v : m_DrawDistanceVars)
  {
    if (!v.pVar)
      continue;
    const char *szNow = v.pVar->GetString();
    v.Held.Track(szNow ? szNow : "");
    if (std::optional<std::string> Write = v.Held.Want(nullptr))
    {
      v.pVar->Set(Write->c_str());
      szNow = v.pVar->GetString();
      v.Held.Written(szNow ? szNow : "");
    }
    if (v.bDumpCleared)
    {
      v.pVar->SetFlags(v.pVar->GetFlags() | VF_DUMPTODISK);
      v.bDumpCleared = false;
    }
  }
}
