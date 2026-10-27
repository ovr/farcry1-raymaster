//////////////////////////////////////////////////////////////////////
//
//	File: DemoSkyFog.cpp
//	Description: The sky and the distance fog of a level, changed by the
//	demo.
//
//	The demo loads a level the way DemoLevel does and views it from a
//	fixed camera, at best one looking into the distance. Between two frames
//	it sets the fog and the sky through I3DEngine (SetFogColor,
//	SetFogStart, SetFogEnd, SetMaxViewDistance, SetSkyBoxAlpha,
//	SetSkyBox), one setting every step frames; the last frame of each is
//	saved. Every setting starts from the level's own fog (read when frame
//	1 starts) and sky box alpha 1. The frames (step 25):
//	  25          the level's own fog and sky
//	  50          fog colour fogcolor1, the level's fog distances
//	  75          fog colour fogcolor2
//	  100         near fog: fognear
//	  125         far fog: fogfar, the view distance out to its end
//	  150         sky box alpha 0.5: the sky half in the fog colour
//	  175         sky box alpha 0: the fog colour instead of the sky
//	  200         sky box alpha 0 and fog colour fogcolor1
//	  225 250 275 the sky boxes of skyboxes
//	The demo does not clear the screen: the frames are the game's. The
//	log has the values of every setting.
//
//	Scene file keys (Demos\DemoSkyFog.ini):
//	  level, mission, settle     as for DemoLevel
//	  exec = <commands>          console commands once the level is there,
//	                             separated by ';' or '|' (e.g. cl_display_hud 0)
//	  camera = x y z ax ay az    the fixed camera: position, angles in degrees
//	                             (ax 0 looks level, 90 straight down, az turns
//	                             about z; default: the game's camera)
//	  step = <frames>            frames per setting (default 25; another value
//	                             needs --frames)
//	  fogcolor1 = r g b          (default 0.9 0.45 0.2)
//	  fogcolor2 = r g b          (default 0.2 0.5 0.3)
//	  fognear = start end        (default: the level's own halved)
//	  fogfar = start end         (default: the level's own times 2)
//	  skyboxes = <shader>, ...   three sky shaders (default: the first ones of
//	                             the shader scripts with a SkyBox whose textures
//	                             exist, other than the level's own, which is
//	                             read from the mission file)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

class CDemoSkyFog : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoSkyFog"; }
  virtual const char *GetDefaultFrames() const { return "25,50,75,100,125,150,175,200,225,250,275"; }
  virtual bool ClearsFrame() const { return false; }

  virtual bool Init(CDemoHost &Host)
  {
    m_Level = Host.GetScene().Get("level");
    if (m_Level.empty())
    {
      std::vector<std::string> Levels = Host.FindDirs("levels/");
      if (!Levels.empty())
        m_Level = Levels[0].substr(7, Levels[0].size() - 8);
    }
    if (m_Level.empty())
    {
      Host.Log("DemoSkyFog: no level");
      return false;
    }
    m_nStep = max(Host.GetScene().GetInt("step", 25), 1);
    return Host.StartLevel(m_Level.c_str(), Host.GetScene().Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoSkyFog: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Host.GetScene().GetInt("settle", 300))
      return false;

    // The end of a cut scene turns the HUD back on: the commands come after it.
    Host.Exec("SkipCutScene");
    Host.ExecList(Host.GetScene().Get("exec"));
    m_Camera = Host.GetScene().GetFloats("camera");
    const CCamera &View = Host.GetSystem()->GetViewCamera();
    Host.Log("DemoSkyFog: the game's camera %g %g %g %g %g %g", View.GetPos().x, View.GetPos().y, View.GetPos().z,
      View.GetAngles().x, View.GetAngles().y, View.GetAngles().z);

    MakeSettings(Host);
    Apply(Host, 0);
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
    if (nFrame % m_nStep == 0 && nFrame / m_nStep < (int)m_Settings.size())
      Apply(Host, nFrame / m_nStep);
  }

private:
  struct SSetting
  {
    std::string Name;
    Vec3 vFogColor;
    float fFogStart;
    float fFogEnd;
    float fViewDistance;
    float fSkyAlpha;
    //! The sky shader; "" for the level's own.
    std::string SkyBox;
  };

  void MakeSettings(CDemoHost &Host)
  {
    I3DEngine *p3DEngine = Host.Get3DEngine();
    SSetting Level;
    Level.Name = "the level's own fog and sky";
    Level.vFogColor = p3DEngine->GetFogColor();
    Level.fFogStart = p3DEngine->GetFogStart();
    Level.fFogEnd = p3DEngine->GetFogEnd();
    Level.fViewDistance = p3DEngine->GetMaxViewDistance();
    Level.fSkyAlpha = 1.0f;
    m_LevelSky = ReadLevelSky(Host);
    m_Sky = m_LevelSky;
    m_Settings.push_back(Level);

    SSetting Setting = Level;
    const Vec3 vColor1 = GetVec3(Host, "fogcolor1", Vec3(0.9f, 0.45f, 0.2f));
    Setting.Name = "fog colour fogcolor1";
    Setting.vFogColor = vColor1;
    m_Settings.push_back(Setting);

    Setting = Level;
    Setting.Name = "fog colour fogcolor2";
    Setting.vFogColor = GetVec3(Host, "fogcolor2", Vec3(0.2f, 0.5f, 0.3f));
    m_Settings.push_back(Setting);

    Setting = Level;
    Setting.Name = "near fog";
    std::vector<float> Near = Host.GetScene().GetFloats("fognear");
    Setting.fFogStart = Near.size() >= 2 ? Near[0] : Level.fFogStart * 0.5f;
    Setting.fFogEnd = Near.size() >= 2 ? Near[1] : Level.fFogEnd * 0.5f;
    m_Settings.push_back(Setting);

    Setting = Level;
    Setting.Name = "far fog";
    std::vector<float> Far = Host.GetScene().GetFloats("fogfar");
    Setting.fFogStart = Far.size() >= 2 ? Far[0] : Level.fFogStart * 2;
    Setting.fFogEnd = Far.size() >= 2 ? Far[1] : Level.fFogEnd * 2;
    Setting.fViewDistance = max(Level.fViewDistance, Setting.fFogEnd);
    m_Settings.push_back(Setting);

    Setting = Level;
    Setting.Name = "sky box alpha 0.5";
    Setting.fSkyAlpha = 0.5f;
    m_Settings.push_back(Setting);

    Setting.Name = "sky box alpha 0";
    Setting.fSkyAlpha = 0.0f;
    m_Settings.push_back(Setting);

    Setting.Name = "sky box alpha 0, fog colour fogcolor1";
    Setting.vFogColor = vColor1;
    m_Settings.push_back(Setting);

    std::vector<std::string> Skies = Split(Host.GetScene().Get("skyboxes"), ",| \t");
    if (!Host.GetScene().Has("skyboxes"))
      Skies = FindSkyShaders(Host);
    for (int i = 0; i < 3; i++)
    {
      Setting = Level;
      if (i < (int)Skies.size())
      {
        Setting.Name = "sky box " + Skies[i];
        Setting.SkyBox = Skies[i];
      }
      else
        Setting.Name = "the level's own sky (no sky box in skyboxes)";
      m_Settings.push_back(Setting);
    }
  }

  void Apply(CDemoHost &Host, int nSetting)
  {
    const SSetting &Setting = m_Settings[nSetting];
    I3DEngine *p3DEngine = Host.Get3DEngine();
    p3DEngine->SetFogColor(Setting.vFogColor);
    p3DEngine->SetFogStart(Setting.fFogStart);
    p3DEngine->SetFogEnd(Setting.fFogEnd);
    p3DEngine->SetMaxViewDistance(Setting.fViewDistance);
    p3DEngine->SetSkyBoxAlpha(Setting.fSkyAlpha);
    const std::string &Sky = Setting.SkyBox.empty() ? m_LevelSky : Setting.SkyBox;
    if (!Sky.empty() && stricmp(Sky.c_str(), m_Sky.c_str()))
    {
      p3DEngine->SetSkyBox(Sky.c_str());
      m_Sky = Sky;
    }
    Host.Log("DemoSkyFog: frames %d to %d: %s: fog colour %g %g %g, %g to %g m, view distance %g m, sky box '%s' alpha %g",
      nSetting * m_nStep + 1, (nSetting + 1) * m_nStep, Setting.Name.c_str(), Setting.vFogColor.x, Setting.vFogColor.y,
      Setting.vFogColor.z, Setting.fFogStart, Setting.fFogEnd, Setting.fViewDistance, m_Sky.c_str(), Setting.fSkyAlpha);
  }

  //! The sky shader of the level's mission file (its Environment's SkyBox), "" if not found.
  std::string ReadLevelSky(CDemoHost &Host)
  {
    std::string Dir = "levels/" + m_Level + "/";
    std::string Mission = Host.GetScene().Get("mission", m_Level.c_str());
    ICryPak *pPak = Host.GetPak();
    FILE *pFile = pPak->FOpen((Dir + "mission_" + Mission + ".xml").c_str(), "rb");
    if (!pFile)
    {
      std::vector<std::string> Files = Host.FindFiles(Dir.c_str(), "mission_*.xml");
      pFile = Files.empty() ? NULL : pPak->FOpen(Files[0].c_str(), "rb");
    }
    std::string Sky;
    char szLine[4096];
    while (pFile && Sky.empty() && pPak->FGets(szLine, sizeof(szLine), pFile))
    {
      const char *p = strstr(szLine, " SkyBox=\"");
      const char *pEnd = p ? strchr(p + 9, '"') : NULL;
      if (pEnd)
        Sky.assign(p + 9, pEnd);
    }
    if (pFile)
      pPak->FClose(pFile);
    Host.Log("DemoSkyFog: the level's sky box '%s'", Sky.c_str());
    return Sky;
  }

  //! The shaders of the shader scripts with a SkyBox (Skys/<name>/Box) whose textures exist
  //! (textures/skys/<name>/box*), other than the level's own, in the order of the scripts.
  std::vector<std::string> FindSkyShaders(CDemoHost &Host)
  {
    std::vector<std::string> Names;
    std::vector<std::string> Files = Host.FindFiles("shaders/scripts/cryshaders/", "*.csl");
    ICryPak *pPak = Host.GetPak();
    for (size_t f = 0; f < Files.size(); f++)
    {
      FILE *pFile = pPak->FOpen(Files[f].c_str(), "rb");
      if (!pFile)
        continue;
      std::string Shader;
      char szLine[1024], szName[256];
      while (pPak->FGets(szLine, sizeof(szLine), pFile))
      {
        const char *p = szLine + strspn(szLine, " \t");
        if (sscanf(p, "Shader '%255[^']'", szName) == 1)
          Shader = szName;
        else if (!Shader.empty() && !strnicmp(p, "SkyBox", 6) && strchr(" \t=", p[6]) && strchr(p, '='))
        {
          std::vector<std::string> Value = Split(strchr(p, '=') + 1, " \t\r\n");
          const std::string Box = Value.empty() ? std::string() : Value[0];
          const size_t nSlash = Box.find_last_of("/\\");
          if (nSlash != std::string::npos && stricmp(Shader.c_str(), m_LevelSky.c_str()))
          {
            const std::string Dir = "textures/" + Box.substr(0, nSlash + 1), Mask = Box.substr(nSlash + 1) + "*";
            if (!Host.FindFiles(Dir.c_str(), Mask.c_str()).empty())
              Names.push_back(Shader);
          }
          Shader.clear();
        }
      }
      pPak->FClose(pFile);
    }
    Host.Log("DemoSkyFog: %d sky shaders with textures in the shader scripts", (int)Names.size());
    return Names;
  }

  static Vec3 GetVec3(CDemoHost &Host, const char *szKey, const Vec3 &vDefault)
  {
    std::vector<float> Numbers = Host.GetScene().GetFloats(szKey);
    return Numbers.size() >= 3 ? Vec3(Numbers[0], Numbers[1], Numbers[2]) : vDefault;
  }

  //! The non-empty parts of Text between the characters of szSeparators, trimmed.
  static std::vector<std::string> Split(const std::string &Text, const char *szSeparators)
  {
    std::vector<std::string> Parts;
    for (size_t nBegin = 0; nBegin < Text.size();)
    {
      size_t nEnd = Text.find_first_of(szSeparators, nBegin);
      if (nEnd == std::string::npos)
        nEnd = Text.size();
      std::string Part = Text.substr(nBegin, nEnd - nBegin);
      Part.erase(0, Part.find_first_not_of(" \t"));
      Part.erase(Part.find_last_not_of(" \t") + 1);
      if (!Part.empty())
        Parts.push_back(Part);
      nBegin = nEnd + 1;
    }
    return Parts;
  }

  std::string m_Level;
  int m_nPlayerFrame = -1;
  int m_nStep = 25;
  std::vector<float> m_Camera;
  std::vector<SSetting> m_Settings;
  //! The level's sky shader and the one set now.
  std::string m_LevelSky;
  std::string m_Sky;
};

DEMO_PLUGIN(CDemoSkyFog)
