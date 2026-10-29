// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoFlares.cpp
//	Description: The sun's flare and the coronas of lights in a level.
//
//	The demo loads a level as DemoLevel does (with r_Coronas 1, which the
//	flares need) and draws it from two cameras:
//	  1-60     the sun part: the camera looks at the sun (I3DEngine::
//	           GetSunPosition) and turns about z from sunturn degrees to
//	           the left of it to as many to the right, so that what stands
//	           in front of the sun hides its flare and the flare fades
//	  61-120   the lamp part: two point lights with a corona (shader
//	           "flare_from_light", m_fCoronaScale 1) are added to the 3D
//	           engine before each frame (I3DEngine::AddDynamicLightSource)
//	           with the player as their owner, who is moved under the lamp
//	           camera first (the lights of an owner indoors stay out of an
//	           outdoor view): a warm one going from the left of the target
//	           point to its right, across the view, and a white one standing
//	           far behind the target (a corona grows with the distance)
//	The default frames are 5, 20, 30, 40, 55, 70, 90 and 110. The demo does
//	not clear the screen: the frames are the game's.
//
//	Scene file keys (Demos\DemoFlares.ini):
//	  level = <name>          the level (default Training)
//	  mission = <name>        its mission (default: the level's default)
//	  settle = <frames>       frames after the player exists (default 300)
//	  exec = <commands>       console commands once the level is there,
//	                          separated by ';' or '|' (default cl_display_hud 0)
//	  sun = x y z             the camera of the sun part (default: the game's
//	                          camera when the level is there)
//	  sunturn = <degrees>     how far it turns to each side (default 12)
//	  lamp = x y z tx ty tz   the camera of the lamp part and the point it looks
//	                          at (default: the sun camera, the target 10 m in
//	                          front of it, level)
//	  lampwidth = <m>         how far the moving light goes (default 8)
//	  lampcolor = r g b       its colour (default 1 0.75 0.45)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IEntitySystem.h>

static const int nPartFrames = 60;

//! The player: the first entity of class Player.
static IEntity *FindPlayer(CDemoHost &Host)
{
  IEntitySystem *pEntities = Host.GetSystem()->GetIEntitySystem();
  IEntityIt *pIt = pEntities ? pEntities->GetEntityIterator() : NULL;
  IEntity *pPlayer = NULL;
  while (pIt && !pIt->IsEnd() && !pPlayer)
  {
    IEntity *pEntity = pIt->Next();
    const char *szClass = pEntity ? pEntity->GetEntityClassName() : NULL;
    if (szClass && !strcmp(szClass, "Player"))
      pPlayer = pEntity;
  }
  if (pIt)
    pIt->Release();
  return pPlayer;
}

class CDemoFlares : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoFlares"; }
  virtual const char *GetDefaultFrames() const { return "5,20,30,40,55,70,90,110"; }
  virtual bool ClearsFrame() const { return false; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    return Host.StartLevel(Scene.Get("level", "Training").c_str(), Scene.Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    const CSceneFile &Scene = Host.GetScene();
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoFlares: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Scene.GetInt("settle", 300))
      return false;

    // The end of a cut scene turns the HUD back on: the commands come after it.
    Host.Exec("SkipCutScene");
    Host.ExecList(Scene.Get("exec", "cl_display_hud 0"));
    Host.Exec("r_Coronas 1");

    const CCamera &Game = Host.GetSystem()->GetViewCamera();
    std::vector<float> Sun = Scene.GetFloats("sun"), Lamp = Scene.GetFloats("lamp"), Color = Scene.GetFloats("lampcolor");
    m_vSunEye = Sun.size() >= 3 ? Vec3(Sun[0], Sun[1], Sun[2]) : Game.GetPos();
    m_fSunTurn = Scene.GetFloat("sunturn", 12);
    const Vec3 vToSun = Host.Get3DEngine()->GetSunPosition(false) - m_vSunEye;
    m_vSunAngles = ConvertVectorToCameraAngles(vToSun);
    if (Lamp.size() >= 6)
    {
      m_vLampEye = Vec3(Lamp[0], Lamp[1], Lamp[2]);
      m_vLampTarget = Vec3(Lamp[3], Lamp[4], Lamp[5]);
    }
    else
    {
      Vec3 vAhead(vToSun.x, vToSun.y, 0);
      vAhead = GetLength(vAhead) > 0.01f ? GetNormalized(vAhead) : Vec3(0, 1, 0);
      m_vLampEye = m_vSunEye;
      m_vLampTarget = m_vSunEye + vAhead * 10.0f;
    }
    m_fLampWidth = Scene.GetFloat("lampwidth", 8);
    if (Color.size() >= 3)
      m_LampColor = CFColor(Color[0], Color[1], Color[2], 1);
    m_pFlareShader = Host.GetRenderer()->EF_LoadShader("flare_from_light", eSH_World);
    Host.Log("DemoFlares: the game's camera %g %g %g, sun camera %g %g %g looking %g %g %g, lamp camera %g %g %g at %g %g %g, shader %s",
      Game.GetPos().x, Game.GetPos().y, Game.GetPos().z, m_vSunEye.x, m_vSunEye.y, m_vSunEye.z, m_vSunAngles.x, m_vSunAngles.y,
      m_vSunAngles.z, m_vLampEye.x, m_vLampEye.y, m_vLampEye.z, m_vLampTarget.x, m_vLampTarget.y, m_vLampTarget.z,
      m_pFlareShader ? m_pFlareShader->GetName() : "not loaded");
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    const int nPart = (nFrame - 1) / nPartFrames, nPartFrame = (nFrame - 1) % nPartFrames;
    const float t = (float)nPartFrame / (nPartFrames - 1);
    if (nPart == 0)
    {
      Cam.SetPos(m_vSunEye);
      Cam.SetAngle(m_vSunAngles + Vec3(0, 0, m_fSunTurn * (2 * t - 1)));
    }
    else
    {
      // The lights' owner (the player) out of doors, under the camera: the lights of an owner in
      // a vis area (the start of a level, indoors) stay out of the view.
      if (nPartFrame == 0)
      {
        if (IEntity *pPlayer = FindPlayer(Host))
        {
          Host.Log("DemoFlares: the player %s a vis area, moved under the lamp camera", pPlayer->GetEntityVisArea() ? "was in" : "was not in");
          pPlayer->SetPos(m_vLampEye - Vec3(0, 0, 1.7f));
        }
      }
      // Before the engine draws the frame, as the game's entities add their lights.
      const Vec3 vDir = GetNormalized(m_vLampTarget - m_vLampEye);
      Vec3 vRight = vDir.Cross(Vec3(0, 0, 1));
      vRight = GetLength(vRight) > 0.01f ? GetNormalized(vRight) : Vec3(1, 0, 0);
      AddLamp(Host, m_vLampTarget + vRight * (m_fLampWidth * (t - 0.5f)), m_LampColor, 1);
      AddLamp(Host, m_vLampTarget + vDir * 30.0f + Vec3(0, 0, 2), CFColor(0.8f, 0.85f, 1, 1), 2);
      Cam.SetPos(m_vLampEye);
      Cam.SetAngle(ConvertVectorToCameraAngles(m_vLampTarget - m_vLampEye));
    }
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame) {}

  virtual bool IsFinished(CDemoHost &Host, int nFrame)
  {
    return nFrame >= 2 * nPartFrames;
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_pFlareShader)
      m_pFlareShader->Release();
    m_pFlareShader = NULL;
  }

private:
  //! A point light with a corona at vPos, owned by the player as the game's lights have an owner
  //! entity (nId: the same light of it every frame).
  void AddLamp(CDemoHost &Host, const Vec3 &vPos, const CFColor &Color, int nId)
  {
    IEntity *pOwner = FindPlayer(Host);
    if (!m_pFlareShader || !pOwner)
      return;
    CDLight Light;
    Light.m_Flags = DLF_POINT | DLF_LIGHTSOURCE | DLF_IGNORE_OWNER;
    Light.m_Origin = vPos;
    Light.m_fRadius = Light.m_fEndRadius = 5;
    Light.m_Color = Color;
    Light.m_SpecColor = Color;
    Light.m_fCoronaScale = 1;
    // The light releases its shader when it is destroyed.
    Light.m_pShader = m_pFlareShader;
    m_pFlareShader->AddRef();
    Host.GetRenderer()->EF_UpdateDLight(&Light);
    Host.Get3DEngine()->AddDynamicLightSource(Light, pOwner, nId);
  }

  int m_nPlayerFrame = -1;
  Vec3 m_vSunEye = Vec3(0, 0, 0), m_vSunAngles = Vec3(0, 0, 0);
  float m_fSunTurn = 12;
  Vec3 m_vLampEye = Vec3(0, 0, 0), m_vLampTarget = Vec3(0, 1, 0);
  float m_fLampWidth = 8;
  CFColor m_LampColor = CFColor(1, 0.75f, 0.45f, 1);
  IShader *m_pFlareShader = NULL;
};

DEMO_PLUGIN(CDemoFlares)
