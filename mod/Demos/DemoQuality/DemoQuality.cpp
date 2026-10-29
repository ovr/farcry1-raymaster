// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoQuality.cpp
//	Description: A level at the quality presets of the game.
//
//	The demo loads a level the way DemoLevel does and views it from a
//	fixed camera. It sets the console variables of the presets of the
//	game's Advanced Video Options (Scripts/MenuScreens/Options/
//	VideoAdv.lua: SetToLowSpec ... SetToVeryHighSpec): texture quality
//	and filtering, particles, special effects, environment (decals,
//	vegetation, object detail), shadows, water and lighting (bump
//	mapping, light maps). The presets low, medium, high and very high
//	are set one after the other between two frames, each held for step
//	frames; the last frame of each is saved: 100 low, 200 medium, 300
//	high, 400 very high. The log has every variable a preset changes.
//	The menu asks for a restart of the game after a change of some of
//	them (UI.cvarsNeedingRelaunch, UISystemUtils.lua; marked below), and
//	the log names them. preset = <name> sets that one preset only, before
//	the level is loaded, a run per preset; all its frames show it.
//	The demo does not clear the screen: the frames are the game's.
//
//	Scene file keys (Demos\DemoQuality.ini):
//	  level, mission, settle     as for DemoLevel
//	  exec = <commands>          console commands once the level is there,
//	                             separated by ';' or '|' (e.g. cl_display_hud 0)
//	  camera = x y z ax ay az    the fixed camera: position, angles in degrees
//	                             (ax 0 looks level, 90 straight down, az turns
//	                             about z; default: the game's camera)
//	  step = <frames>            frames per preset (default 100; another value
//	                             needs --frames)
//	  preset = <name>            low, medium, high or veryhigh only
//	  exec_low, exec_medium, exec_high, exec_veryhigh = <commands>
//	                             console commands after the variables of the
//	                             preset, separated by ';' or '|' (e.g.
//	                             r_HDRRendering 7)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

static const char *s_szPresets[4] = { "low", "medium", "high", "veryhigh" };

//! A console variable of the presets and its values at low, medium, high and very high; NULL
//! where the preset leaves it.
struct SQualityVar
{
  const char *szName;
  const char *szValues[4];
  //! The menu asks for a restart after a change.
  bool bRestart;
};

static const SQualityVar s_Vars[] =
{
  // Texture quality
  { "r_TexResolution", { "2", "1", "0", "0" } },
  { "r_TexSkyResolution", { "2", "1", "0", "0" } },
  { "r_TexBumpResolution", { "2", "1", "0", "0" } },
  { "e_detail_texture_quality", { "0", "1", "1", "1" }, true },
  { "r_DetailTextures", { "0", "1", "1", "1" } },
  { "r_DetailNumLayers", { "0", "1", "1", "2" } },
  { "r_DetailDistance", { "0", "4", "8", "16" } },
  // Texture filter quality and anisotropic filtering level
  { "d3d9_TextureFilter", { "BILINEAR", "TRILINEAR", "TRILINEAR", "TRILINEAR" } },
  { "GL_TextureFilter", { "GL_LINEAR_MIPMAP_NEAREST", "GL_LINEAR_MIPMAP_LINEAR", "GL_LINEAR_MIPMAP_LINEAR", "GL_LINEAR_MIPMAP_LINEAR" } },
  { "r_Texture_Anisotropic_Level", { "1", "1", "1", "1" } },
  // Particle count
  { "e_particles_max_count", { "512", "2048", "4096", "8192" } },
  { "e_particles_lod", { "0.5", "0.75", "1.0", "1.0" } },
  // Special effects quality
  { "r_MotionBlur", { "0", "0", "1", "1" } },
  { "r_Glare", { "0", "0", "1", "1" } },
  { "r_RenderMode", { "0", "0", NULL, NULL } },
  { "r_GlareQuality", { "0", "1", "2", "2" } },
  { "r_Flares", { "0", "1", "1", "1" } },
  { "r_checkSunVis", { "0", "1", "2", "2" } },
  { "r_Coronas", { "0", "1", "1", "1" } },
  { "r_CoronaFade", { "0.0", "0.2", "0.1625", "0.125" } },
  { "r_HeatHaze", { "0", "0", "1", "1" } },
  { "r_ScopeLens_fx", { "0", "0", "1", "1" } },
  { "r_ProcFlares", { "0", "1", "1", "1" } },
  { "r_CryvisionType", { "2", "1", "0", "0" } },
  { "r_DisableSfx", { "1", "0", "0", "0" } },
  { "r_Beams", { "0", "1", "1", "1" } },
  { "es_EnableCloth", { "0", "1", "1", "1" }, true },
  // Environment quality
  { "r_VolumetricFog", { "0", "1", "1", "1" } },
  { "r_EnvCMResolution", { "1", "1", "2", "2" } },
  { "r_EnvCMupdateInterval", { "0.2", "0.15", "0.1", "0.05" } },
  { "r_EnvTexResolution", { "1", "2", "3", "3" } },
  { "r_EnvTexUpdateInterval", { "0.1", "0.075", "0.05", "0.001" } },
  { "e_decals", { "0", "1", "1", "1" } },
  { "e_decals_life_time_scale", { "0.5", "1.0", "2.0", "3.0" } },
  { "ca_EnableDecals", { "0", "1", "1", "1" } },
  { "e_overlay_geometry", { "0", "1", "1", "1" } },
  { "e_obj_lod_ratio", { "5", "5", "10", "10" } },
  { "e_vegetation_sprites_distance_ratio", { "0.9", "0.9", "1.0", "1.0" } },
  { "e_cgf_load_lods", { "1", "1", "1", "0" }, true },
  { "e_vegetation_min_size", { "2.2", "2.2", "0", "0" }, true },
  { "e_flocks", { "0", "0", "1", "1" } },
  { "e_EntitySuppressionLevel", { "2", "2", "0", "0" } },
  { "sys_skiponlowspec", { "1", "0", "0", "0" }, true },
  // Shadow quality
  { "e_stencil_shadows", { "0", "1", "1", "1" }, true },
  { "e_shadow_maps", { "0", "0", "1", "1" }, true },
  { "e_active_shadow_maps_receving", { "0", "0", "1", "2" } },
  { "r_ShadowBlur", { "0", "0", "1", "2" } },
  { "e_shadow_maps_view_dist_ratio", { "10", "10", "15", "20" } },
  // Water quality
  { "r_WaterRefractions", { "0", "0", "1", "1" } },
  { "r_WaterReflections", { "0", "1", "1", "1" } },
  { "r_WaterUpdateFactor", { "0.02", "0.015", "0.01", "0.001" } },
  { "r_Quality_Reflection", { "0", "0", "0", "0" }, true },
  { "e_beach", { "0", "1", "1", "1" }, true },
  { "e_use_global_fog_in_fog_volumes", { "1", "0", "0", "0" }, true },
  // Lighting quality
  { "r_Quality_BumpMapping", { "0", "1", "2", "3" }, true },
  { "r_Vegetation_PerpixelLight", { "0", "0", "1", "1" }, true },
  { "e_light_maps_quality", { "0", "1", "2", "2" }, true },
  { "ca_ambient_light_intensity", { "0.2", "0.2", "0.2", "0.2" } },
  { "ca_ambient_light_range", { "0", "10", "10", "10" } },
  { "e_max_entity_lights", { "2", "2", "3", "4" } },
  { "e_stencil_shadows_only_from_strongest_light", { "1", "1", "0", "0" } },
  { "p_lightrange", { "8", "10", "15", "15" } },
  { "r_EnvLightCMSize", { "8", "8", "8", "16" } },
  { "r_EnvLCMupdateInterval", { "0.1", "0.1", "0.1", "0.05" } },
  { "cl_projectile_light", { "0", "0", "1", "1" } },
  { "cl_weapon_light", { "0", "0", "1", "2" } },
};

class CDemoQuality : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoQuality"; }
  virtual const char *GetDefaultFrames() const { return "100,200,300,400"; }
  virtual bool ClearsFrame() const { return false; }

  virtual bool Init(CDemoHost &Host)
  {
    std::string Level = Host.GetScene().Get("level");
    if (Level.empty())
    {
      std::vector<std::string> Levels = Host.FindDirs("levels/");
      if (!Levels.empty())
        Level = Levels[0].substr(7, Levels[0].size() - 8);
    }
    if (Level.empty())
    {
      Host.Log("DemoQuality: no level");
      return false;
    }
    m_nStep = max(Host.GetScene().GetInt("step", 100), 1);
    if (Host.GetScene().Has("preset"))
    {
      std::string Preset = Host.GetScene().Get("preset");
      for (int i = 0; i < 4; i++)
      {
        if (!stricmp(Preset.c_str(), s_szPresets[i]))
          m_nOnly = i;
      }
      if (m_nOnly < 0)
      {
        Host.Log("DemoQuality: no preset '%s' (low, medium, high or veryhigh)", Preset.c_str());
        return false;
      }
      Host.Log("DemoQuality: preset %s only, set before the level is loaded", s_szPresets[m_nOnly]);
      SetPreset(Host, m_nOnly);
    }
    return Host.StartLevel(Level.c_str(), Host.GetScene().Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoQuality: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Host.GetScene().GetInt("settle", 300))
      return false;

    // The end of a cut scene turns the HUD back on: the commands come after it.
    Host.Exec("SkipCutScene");
    Host.ExecList(Host.GetScene().Get("exec"));
    m_Camera = Host.GetScene().GetFloats("camera");
    const CCamera &View = Host.GetSystem()->GetViewCamera();
    Host.Log("DemoQuality: the game's camera %g %g %g %g %g %g", View.GetPos().x, View.GetPos().y, View.GetPos().z,
      View.GetAngles().x, View.GetAngles().y, View.GetAngles().z);
    if (m_nOnly < 0)
      Host.Log("DemoQuality: frames 1 to %d", m_nStep);
    SetPreset(Host, m_nOnly >= 0 ? m_nOnly : 0);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    if (m_Camera.size() < 6)
      return false;
    Cam.SetPos(Vec3(m_Camera[0], m_Camera[1], m_Camera[2]));
    Cam.SetAngle(Vec3(m_Camera[3], m_Camera[4], m_Camera[5]));
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame) {}

  virtual void AfterFrame(CDemoHost &Host, int nFrame)
  {
    if (m_nOnly < 0 && nFrame % m_nStep == 0 && nFrame / m_nStep < 4)
    {
      Host.Log("DemoQuality: frames %d to %d", nFrame + 1, nFrame + m_nStep);
      SetPreset(Host, nFrame / m_nStep);
    }
  }

private:
  //! Sets the variables of preset nPreset that exist, logs those it changes, then runs the
  //! preset's exec_ commands.
  void SetPreset(CDemoHost &Host, int nPreset)
  {
    IConsole *pConsole = Host.GetConsole();
    std::string Restart;
    int nChanged = 0;
    for (size_t i = 0; i < sizeof(s_Vars) / sizeof(s_Vars[0]); i++)
    {
      const SQualityVar &Var = s_Vars[i];
      const char *szValue = Var.szValues[nPreset];
      if (!szValue)
        continue;
      ICVar *pVar = pConsole->GetCVar(Var.szName, false);
      if (!pVar)
      {
        Host.Log("DemoQuality: no variable %s", Var.szName);
        continue;
      }
      const std::string Old = pVar->GetString() ? pVar->GetString() : "";
      pVar->Set(szValue);
      const std::string New = pVar->GetString() ? pVar->GetString() : "";
      if (SameValue(Old, New))
        continue;
      nChanged++;
      Host.Log("DemoQuality: %s %s -> %s%s", Var.szName, Old.c_str(), New.c_str(), Var.bRestart ? " (restart)" : "");
      if (Var.bRestart)
        Restart += std::string(Restart.empty() ? "" : ", ") + Var.szName;
    }
    Host.Log("DemoQuality: preset %s, %d variables changed", s_szPresets[nPreset], nChanged);
    if (!Restart.empty())
      Host.Log("DemoQuality: after a change of %s the menu asks for a restart (scene key preset)", Restart.c_str());
    Host.ExecList(Host.GetScene().Get((std::string("exec_") + s_szPresets[nPreset]).c_str()));
  }

  //! Whether two values of a variable are the same: as text, or as numbers ("0.500000" and "0.5").
  static bool SameValue(const std::string &A, const std::string &B)
  {
    if (A == B)
      return true;
    char *pEndA, *pEndB;
    const double fA = strtod(A.c_str(), &pEndA), fB = strtod(B.c_str(), &pEndB);
    return pEndA != A.c_str() && !*pEndA && pEndB != B.c_str() && !*pEndB && fA == fB;
  }

  int m_nPlayerFrame = -1;
  int m_nStep = 100;
  //! The one preset of the scene key preset, -1 for all.
  int m_nOnly = -1;
  std::vector<float> m_Camera;
};

DEMO_PLUGIN(CDemoQuality)
