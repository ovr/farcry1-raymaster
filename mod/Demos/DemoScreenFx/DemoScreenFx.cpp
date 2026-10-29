//////////////////////////////////////////////////////////////////////
//
//	File: DemoScreenFx.cpp
//	Description: The screen effects of the 3D engine, one after the other,
//	over a level.
//
//	The demo loads a level as DemoLevel does, waits for the player and a
//	number of frames more, ends the level's cut scene and draws every frame
//	from a fixed camera. Every 50 frames, between frames, it turns the
//	effect before off and the next one on, with the effect and parameter
//	names of the SDK's CScriptObjectSystem reference (I3DEngine::SetScreenFx,
//	SetScreenFxParam) and the values the game's scripts use; its name is
//	written in the top left corner of the frame. Effects and saved frames:
//	  1-50     none                                            25
//	  51-100   NightVision                                     75
//	  101-150  ScreenBlur, amount 0.5, colour (1, 0.5, 0.25)    125
//	           (or those of the blur key)
//	  151-200  FlashBang, time scale 1                          155, 175
//	  201-250  ScreenFade, time 1 (out, as CXGame does)         210, 235
//	  251-300  ScreenFade, time -1 (in, as CXClient does)       260, 285
//	  301-350  heat vision (I3DEngine::EnableHeatVision)        325
//	  351-400  none, after I3DEngine::ResetScreenFx             375
//	  401-450  under water, the camera of the underwater key    425
//	and, only with its scene key and --frames reaching it:
//	  451-500  a screen shader (I3DEngine::SetScreenShader), last as
//	           nothing here turns it off
//	The demo does not clear the screen: the frames are the game's.
//
//	Scene file keys (Demos\DemoScreenFx.ini):
//	  level = <name>              the level (default: the first folder in levels/)
//	  mission = <name>            its mission (default: the level's default)
//	  settle = <frames>           frames after the player exists (default 300)
//	  exec = <commands>           console commands once the level is there,
//	                              separated by ';' or '|' (e.g. cl_display_hud 0)
//	  camera = x y z [ax ay az]   the fixed camera: position and angles in degrees as
//	                              CCamera::SetAngle takes them (ax 0 looks level, 90
//	                              straight down, az turns about z; default: the
//	                              game's camera when the level is there)
//	  target = x y z              the point the camera looks at, instead of angles
//	  underwater = x y z tx ty tz the camera of the step under water: its position
//	                              and the point it looks at
//	  blur = amount r g b         the amount and colour of the ScreenBlur step
//	  screenshader = <name>       the shader of the screen shader step
//	  font = <name>               the font of the names (default: Default)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>

class CDemoScreenFx : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoScreenFx"; }
  virtual const char *GetDefaultFrames() const { return "25,75,125,155,175,210,235,260,285,325,375,425"; }
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
      Host.Log("DemoScreenFx: no level");
      return false;
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
      Host.Log("DemoScreenFx: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Host.GetScene().GetInt("settle", 300))
      return false;
    // The end of a cut scene turns the HUD back on: the commands come after it. The game's camera
    // is taken a frame later, once the cut scene has ended.
    if (!m_bSkipped)
    {
      m_bSkipped = true;
      Host.Exec("SkipCutScene");
      Host.ExecList(Host.GetScene().Get("exec"));
      return false;
    }
    SetupCamera(Host);
    m_Underwater = Host.GetScene().GetFloats("underwater");
    std::vector<float> Blur = Host.GetScene().GetFloats("blur");
    if (Blur.size() >= 4)
      std::copy_n(Blur.begin(), 4, m_Blur);
    m_ScreenShader = Host.GetScene().Get("screenshader");
    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont(Host.GetScene().Get("font", "Default").c_str()) : NULL;
    Host.Log("DemoScreenFx: font %s", m_pFont ? "found" : "not found");
    SetStep(Host, eNone);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    if (m_nStep == eUnderwater && m_Underwater.size() >= 6)
    {
      const Vec3 vPos(m_Underwater[0], m_Underwater[1], m_Underwater[2]);
      Cam.SetPos(vPos);
      Cam.SetAngle(ConvertVectorToCameraAngles(Vec3(m_Underwater[3], m_Underwater[4], m_Underwater[5]) - vPos));
    }
    else
    {
      Cam.SetPos(m_vCamPos);
      Cam.SetAngle(m_vCamAngles);
    }
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    if (!m_pFont)
      return;
    // White on a black shadow, on the virtual 800 x 600 screen.
    m_pFont->Reset();
    m_pFont->SetEffect("default");
    m_pFont->SetSameSize(false);
    m_pFont->SetCharWidthScale(1.0f);
    m_pFont->SetSize(vector2f(20, 20));
    m_pFont->SetColor(color4f(0, 0, 0, 1));
    m_pFont->DrawString(12, 12, m_Label.c_str());
    m_pFont->SetColor(color4f(1, 1, 1, 1));
    m_pFont->DrawString(10, 10, m_Label.c_str());
  }

  virtual void AfterFrame(CDemoHost &Host, int nFrame)
  {
    if (nFrame % 50 == 0 && nFrame / 50 < eSteps)
      SetStep(Host, nFrame / 50);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    Host.Get3DEngine()->EnableHeatVision(false);
    Host.Get3DEngine()->ResetScreenFx();
  }

private:
  enum
  {
    eNone, eNightVision, eScreenBlur, eFlashBang, eFadeOut, eFadeIn, eHeatVision, eReset, eUnderwater, eScreenShader,
    eSteps
  };

  //! The fixed camera: the scene's, or the game's one as it is now.
  void SetupCamera(CDemoHost &Host)
  {
    const CCamera &View = Host.GetSystem()->GetViewCamera();
    m_vCamPos = View.GetPos();
    m_vCamAngles = View.GetAngles();
    std::vector<float> Camera = Host.GetScene().GetFloats("camera");
    std::vector<float> Target = Host.GetScene().GetFloats("target");
    if (Camera.size() >= 3)
      m_vCamPos = Vec3(Camera[0], Camera[1], Camera[2]);
    if (Target.size() >= 3)
      m_vCamAngles = ConvertVectorToCameraAngles(Vec3(Target[0], Target[1], Target[2]) - m_vCamPos);
    else if (Camera.size() >= 6)
      m_vCamAngles = Vec3(Camera[3], Camera[4], Camera[5]);
    Host.Log("DemoScreenFx: camera %g %g %g %g %g %g", m_vCamPos.x, m_vCamPos.y, m_vCamPos.z, m_vCamAngles.x, m_vCamAngles.y,
      m_vCamAngles.z);
  }

  static void SetParam(I3DEngine *p3DEngine, const char *szEffect, const char *szParam, float fValue)
  {
    p3DEngine->SetScreenFxParam(szEffect, szParam, &fValue);
  }

  //! Turns the effect of the step before off and the one of nStep on.
  void SetStep(CDemoHost &Host, int nStep)
  {
    I3DEngine *p3DEngine = Host.Get3DEngine();
    switch (m_nStep)
    {
    case eNightVision: p3DEngine->SetScreenFx("NightVision", 0); break;
    case eScreenBlur: p3DEngine->SetScreenFx("ScreenBlur", 0); break;
    case eFlashBang: p3DEngine->SetScreenFx("FlashBang", 0); break;
    case eFadeOut:
    case eFadeIn: p3DEngine->SetScreenFx("ScreenFade", 0); break;
    case eHeatVision: p3DEngine->EnableHeatVision(false); break;
    }
    m_nStep = nStep;
    switch (nStep)
    {
    case eNone:
      m_Label = "none";
      break;
    case eNightVision:
      m_Label = "NightVision";
      p3DEngine->SetScreenFx("NightVision", 1);
      break;
    case eScreenBlur:
    {
      char szLabel[128];
      snprintf(szLabel, sizeof(szLabel), "ScreenBlur: amount %g, colour %g %g %g", m_Blur[0], m_Blur[1], m_Blur[2], m_Blur[3]);
      m_Label = szLabel;
      p3DEngine->SetScreenFx("ScreenBlur", 1);
      SetParam(p3DEngine, "ScreenBlur", "ScreenBlurAmount", m_Blur[0]);
      SetParam(p3DEngine, "ScreenBlur", "ScreenBlurColorRed", m_Blur[1]);
      SetParam(p3DEngine, "ScreenBlur", "ScreenBlurColorGreen", m_Blur[2]);
      SetParam(p3DEngine, "ScreenBlur", "ScreenBlurColorBlue", m_Blur[3]);
      break;
    }
    case eFlashBang:
      m_Label = "FlashBang: time scale 1";
      p3DEngine->SetScreenFx("FlashBang", 1);
      SetParam(p3DEngine, "FlashBang", "FlashBangTimeScale", 1);
      break;
    case eFadeOut:
      m_Label = "ScreenFade: time 1 (out)";
      p3DEngine->SetScreenFx("ScreenFade", 1);
      SetParam(p3DEngine, "ScreenFade", "ScreenFadeTime", 1);
      break;
    case eFadeIn:
      m_Label = "ScreenFade: time -1 (in)";
      p3DEngine->SetScreenFx("ScreenFade", 1);
      SetParam(p3DEngine, "ScreenFade", "ScreenFadeTime", -1);
      break;
    case eHeatVision:
      m_Label = "heat vision";
      p3DEngine->EnableHeatVision(true);
      break;
    case eReset:
      m_Label = "none (ResetScreenFx)";
      p3DEngine->ResetScreenFx();
      break;
    case eUnderwater:
      m_Label = m_Underwater.size() >= 6 ? "under water" : "under water: no camera in the scene file";
      break;
    case eScreenShader:
      m_Label = m_ScreenShader.empty() ? "screen shader: none in the scene file" : "screen shader " + m_ScreenShader;
      if (!m_ScreenShader.empty())
        p3DEngine->SetScreenShader(m_ScreenShader.c_str());
      break;
    }
    Host.Log("DemoScreenFx: step %d, %s", nStep, m_Label.c_str());
  }

  int m_nPlayerFrame = -1;
  bool m_bSkipped = false;
  int m_nStep = eNone;
  Vec3 m_vCamPos = Vec3(0, 0, 0);
  Vec3 m_vCamAngles = Vec3(0, 0, 0);
  std::vector<float> m_Underwater;
  float m_Blur[4] = {0.5f, 1, 0.5f, 0.25f}; //!< amount, colour
  std::string m_ScreenShader;
  std::string m_Label;
  IFFont *m_pFont = NULL;
};

DEMO_PLUGIN(CDemoScreenFx)
