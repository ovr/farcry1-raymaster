// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoParticles.cpp
//	Description: Three particle effects of the 3D engine side by side in
//	a level: fire, smoke and sparks.
//
//	The demo loads a level as DemoLevel does, waits for the player and a
//	number of frames more, ends the level's cut scene and draws every frame
//	from a fixed camera. Rays from the camera through three points of the
//	screen, left to right, find the spots on the terrain or a static object
//	where the effects start (the log has each spot and what the ray met).
//	All three come from I3DEngine::SpawnParticles, between frames: fire
//	(additive, short lived) every 2 frames, smoke (alpha blended, growing
//	and drifting) every 10 frames, both from before frame 1 on, and sparks
//	(additive, with tails, falling and bouncing) in a burst before frame 1
//	and every 25 frames, 10 frames before frames 25, 50, 100 and 200.
//	Frame 1 shows the first particles, 25 and 50 the fire and the first
//	smoke, 100 and 200 the grown smoke column. The demo does not clear the
//	screen: the frames are the game's.
//
//	Scene file keys (Demos\DemoParticles.ini):
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
//	  row = <fraction>            height of the three rays on the screen, from the
//	                              top (default 0.7)
//	  fire = x y z                a spot of its own for the fire, likewise
//	  smoke = x y z               smoke and sparks (default: where the ray meets
//	  sparks = x y z              the world, else 10 m along it)
//	  firetexture = <file>        the textures of the effects (default: the engine's
//	  smoketexture = <file>       own particle texture, ParticleParams::nTexId 0)
//	  sparkstexture = <file>
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IPhysics.h>
#include <IScriptSystem.h>

class CDemoParticles : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoParticles"; }
  virtual const char *GetDefaultFrames() const { return "1,25,50,100,200"; }
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
      Host.Log("DemoParticles: no level");
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
      Host.Log("DemoParticles: the player is there after %d frames", nFramesWaited);
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
    Start(Host);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    Cam.SetPos(m_vCamPos);
    Cam.SetAngle(m_vCamAngles);
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame) {}

  virtual void AfterFrame(CDemoHost &Host, int nFrame)
  {
    // Fire every 0.04 s, smoke every 0.2 s.
    if (nFrame % 2 == 0)
      Spawn(Host, eFire, nFrame);
    if (nFrame % 10 == 0)
      Spawn(Host, eSmoke, nFrame);
    if (nFrame % 25 == 15)
      Spawn(Host, eSparks, nFrame);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    for (int i = 0; i < 3; i++)
    {
      if (m_pTextures[i])
        m_pTextures[i]->Release(false);
      m_pTextures[i] = NULL;
    }
  }

private:
  enum { eFire, eSmoke, eSparks };

  //! The fixed camera: the scene's, or the game's one as it is now.
  void SetupCamera(CDemoHost &Host)
  {
    const CCamera &View = Host.GetSystem()->GetViewCamera();
    m_vCamPos = View.GetPos();
    m_vCamAngles = View.GetAngles();
    m_fFov = View.GetFov();
    m_fRatio = View.GetProjRatio();
    std::vector<float> Camera = Host.GetScene().GetFloats("camera");
    std::vector<float> Target = Host.GetScene().GetFloats("target");
    if (Camera.size() >= 3)
      m_vCamPos = Vec3(Camera[0], Camera[1], Camera[2]);
    if (Target.size() >= 3)
      m_vCamAngles = ConvertVectorToCameraAngles(Vec3(Target[0], Target[1], Target[2]) - m_vCamPos);
    else if (Camera.size() >= 6)
      m_vCamAngles = Vec3(Camera[3], Camera[4], Camera[5]);
    Host.Log("DemoParticles: camera %g %g %g %g %g %g, fov %g", m_vCamPos.x, m_vCamPos.y, m_vCamPos.z, m_vCamAngles.x,
      m_vCamAngles.y, m_vCamAngles.z, RAD2DEG(m_fFov));
  }

  //! The direction of the ray through the point (fX, fY) of the screen (fractions from the top left)
  //! for the camera's angles (the forward vector of ConvertToRadAngles) and field of view (CCamera:
  //! the vertical half angle is fov * ratio / 2).
  Vec3 ScreenRay(float fX, float fY) const
  {
    const Vec3 vForward = ConvertToRadAngles(m_vCamAngles);
    Vec3 vRight = vForward.Cross(Vec3(0, 0, 1));
    if (vRight.GetLength() < 0.001f)
      vRight = Vec3(1, 0, 0);
    vRight.Normalize();
    const Vec3 vUp = vRight.Cross(vForward);
    const float fTanY = tanf(m_fFov * m_fRatio * 0.5f), fTanX = fTanY / max(m_fRatio, 0.01f);
    Vec3 vDir = vForward + vRight * ((fX * 2 - 1) * fTanX) + vUp * ((1 - fY * 2) * fTanY);
    vDir.Normalize();
    return vDir;
  }

  //! The spot of an effect: the scene value szKey, else where the ray through (fX, fRow) meets the
  //! terrain or a static object, else 10 m along the ray.
  Vec3 FindSpot(CDemoHost &Host, const char *szKey, float fX, float fRow)
  {
    std::vector<float> Spot = Host.GetScene().GetFloats(szKey);
    if (Spot.size() >= 3)
      return Vec3(Spot[0], Spot[1], Spot[2]);
    const Vec3 vDir = ScreenRay(fX, fRow);
    ray_hit Hit;
    memset(&Hit, 0, sizeof(Hit));
    IPhysicalWorld *pWorld = Host.GetSystem()->GetIPhysicalWorld();
    if (pWorld
      && pWorld->RayWorldIntersection(m_vCamPos, vDir * 100.0f, ent_terrain | ent_static, rwi_stop_at_pierceable, &Hit, 1))
    {
      Host.Log("DemoParticles: %s at %g %g %g (%s, %g m)", szKey, Hit.pt.x, Hit.pt.y, Hit.pt.z,
        Hit.bTerrain ? "terrain" : "static object", Hit.dist);
      return Hit.pt;
    }
    const Vec3 vSpot = m_vCamPos + vDir * 10.0f;
    Host.Log("DemoParticles: %s at %g %g %g (the ray meets nothing)", szKey, vSpot.x, vSpot.y, vSpot.z);
    return vSpot;
  }

  int LoadTexture(CDemoHost &Host, int nEffect, const char *szKey)
  {
    std::string Name = Host.GetScene().Get(szKey);
    if (Name.empty())
      return 0;
    ITexPic *pTexture = Host.GetRenderer()->EF_LoadTexture(Name.c_str(), FT_NOREMOVE, 0, eTT_Base);
    m_pTextures[nEffect] = pTexture;
    const int nId = pTexture && pTexture->IsTextureLoaded() ? pTexture->GetTextureID() : 0;
    Host.Log("DemoParticles: %s %s, id %d", szKey, Name.c_str(), nId);
    return nId;
  }

  //! Spots and textures, the first fire, smoke and sparks.
  void Start(CDemoHost &Host)
  {
    I3DEngine *p3DEngine = Host.Get3DEngine();
    p3DEngine->CheckPhysicalized(m_vCamPos - Vec3(100, 100, 100), m_vCamPos + Vec3(100, 100, 100));
    const float fRow = Host.GetScene().GetFloat("row", 0.7f);
    m_vSpots[eFire] = FindSpot(Host, "fire", 0.25f, fRow);
    m_vSpots[eSmoke] = FindSpot(Host, "smoke", 0.5f, fRow);
    m_vSpots[eSparks] = FindSpot(Host, "sparks", 0.75f, fRow);
    m_nTextures[eFire] = LoadTexture(Host, eFire, "firetexture");
    m_nTextures[eSmoke] = LoadTexture(Host, eSmoke, "smoketexture");
    m_nTextures[eSparks] = LoadTexture(Host, eSparks, "sparkstexture");
    for (int i = 0; i < 3; i++)
      Spawn(Host, i, 0);
  }

  //! Spawns effect nEffect after frame nFrame. The C library's rand, which SVariationValue uses, is
  //! seeded first from the frame and the effect, with the game's script function randomseed, so
  //! that a spawn starts from the same random numbers in every run.
  void Spawn(CDemoHost &Host, int nEffect, int nFrame)
  {
    char szSeed[64];
    sprintf(szSeed, "if randomseed then randomseed(%d) end", nFrame * 3 + nEffect + 1);
    Host.GetSystem()->GetIScriptSystem()->ExecuteBuffer(szSeed, strlen(szSeed));
    Host.Get3DEngine()->SpawnParticles(nEffect == eFire ? Fire() : nEffect == eSmoke ? Smoke() : Sparks());
  }

  //! Fire: a few particles, rising fast, yellow to red, shrinking at the end.
  ParticleParams Fire() const
  {
    ParticleParams Params;
    Params.vPosition = m_vSpots[eFire];
    Params.vDirection = Vec3(0, 0, 1);
    Params.fFocus = 20;
    Params.vColorStart = Vec3(1, 0.8f, 0.5f);
    Params.vColorEnd = Vec3(1, 0.25f, 0.05f);
    Params.fSpeed = 1.5f;
    Params.fSpeed.variation = 0.25f;
    Params.nCount = 3;
    Params.fSize = 0.5f;
    Params.fSizeFadeOut = 0.3f;
    Params.fLifeTime = 0.8f;
    Params.fFadeInTime = 0.1f;
    Params.fPosRandomOffset = 0.3f;
    Params.nTexId = m_nTextures[eFire];
    Params.eBlendType = ParticleBlendType_Additive;
    Params.fScale = 1;
    return Params;
  }

  //! Smoke: one particle, rising, grey to light grey, growing for 4 s and drifting along +x.
  ParticleParams Smoke() const
  {
    ParticleParams Params;
    Params.vPosition = m_vSpots[eSmoke];
    Params.vDirection = Vec3(0, 0, 1);
    Params.fFocus = 8;
    Params.vColorStart = Vec3(0.45f, 0.45f, 0.45f);
    Params.vColorEnd = Vec3(0.8f, 0.8f, 0.8f);
    Params.fSpeed = 1.2f;
    Params.fSpeed.variation = 0.25f;
    Params.nCount = 1;
    Params.fSize = 0.4f;
    Params.fSizeSpeed = 0.4f;
    Params.fLifeTime = 4;
    Params.fFadeInTime = 0.5f;
    Params.vGravity = Vec3(0.2f, 0, 0.2f);
    Params.fPosRandomOffset = 0.3f;
    Params.nTexId = m_nTextures[eSmoke];
    Params.eBlendType = ParticleBlendType_AlphaBased;
    Params.fScale = 1;
    return Params;
  }

  //! Sparks: one burst of 30 small particles in all directions upwards, with tails, falling and
  //! bouncing off the ground.
  ParticleParams Sparks() const
  {
    ParticleParams Params;
    Params.vPosition = m_vSpots[eSparks];
    Params.vDirection = Vec3(0, 0, 1);
    Params.fFocus = 1.5f;
    Params.vColorStart = Vec3(1, 0.9f, 0.6f);
    Params.vColorEnd = Vec3(1, 0.4f, 0.1f);
    Params.fSpeed = 5;
    Params.fSpeed.variation = 0.3f;
    Params.nCount = 30;
    Params.fSize = 0.04f;
    Params.fLifeTime = 1.2f;
    Params.fTailLenght = 0.1f;
    Params.nTailSteps = 4;
    Params.vGravity = Vec3(0, 0, -6);
    Params.fBouncenes = 0.5f;
    Params.nTexId = m_nTextures[eSparks];
    Params.eBlendType = ParticleBlendType_Additive;
    Params.fScale = 1;
    return Params;
  }

  int m_nPlayerFrame = -1;
  bool m_bSkipped = false;
  Vec3 m_vCamPos = Vec3(0, 0, 0);
  Vec3 m_vCamAngles = Vec3(0, 0, 0);
  float m_fFov = 1.0f;
  float m_fRatio = 0.75f;
  Vec3 m_vSpots[3];
  int m_nTextures[3] = { 0, 0, 0 };
  ITexPic *m_pTextures[3] = { NULL, NULL, NULL };
};

DEMO_PLUGIN(CDemoParticles)
