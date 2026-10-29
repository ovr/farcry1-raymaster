// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoDecals.cpp
//	Description: Decals of the 3D engine on the terrain and on static
//	objects in a level: bullet holes, blood and scorch marks.
//
//	The demo sets e_decals 1 and loads a level as DemoLevel does, waits for
//	the player and a number of frames more, ends the level's cut scene and
//	draws every frame from a fixed camera. Between frames it makes decals
//	with I3DEngine::CreateDecal the way the SDK's game code does for
//	bullets and blood: a ray from the camera through a point of the screen
//	meets the terrain or a static object
//	(IPhysicalWorld::RayWorldIntersection), and the decal gets the point,
//	the surface normal, the ray as the hit direction, the part and, for an
//	object, the object as its owner. After frame 20: four bullet holes of
//	size 0.1 to 0.5, turned by 0 to 90 degrees, in a row at 0.3 of the
//	screen's height across its middle third (meant for a wall); after
//	frame 40: three blood stains of size 0.4 to 1.2 at 0.45, likewise;
//	after frame 60: three scorch marks of size 0.75 to 2.5 at 0.8 across
//	the screen (meant for the terrain). Frame 1 has no decals,
//	frame 25 the holes, 45 the blood too, 65 and 150 all of them. The log
//	has every decal with what its ray met. The demo does not clear the
//	screen: the frames are the game's.
//
//	Scene file keys (Demos\DemoDecals.ini):
//	  level = <name>              the level (default: the first folder in levels/)
//	  mission = <name>            its mission (default: the level's default)
//	  init = <commands>           console commands before the level loads (after
//	                              e_decals 1), separated by ';' or '|'
//	  settle = <frames>           frames after the player exists (default 300)
//	  exec = <commands>           console commands once the level is there,
//	                              separated by ';' or '|' (e.g. cl_display_hud 0)
//	  camera = x y z [ax ay az]   the fixed camera: position and angles in degrees as
//	                              CCamera::SetAngle takes them (ax 0 looks level, 90
//	                              straight down, az turns about z; default: the
//	                              game's camera when the level is there)
//	  target = x y z              the point the camera looks at, instead of angles
//	  bullet = <file>             the textures of the bullet holes, the blood and
//	  blood = <file>              the scorch marks (default: the first
//	  scorch = <file>             textures/decal/*.dds)
//	  lifetime = <seconds>        the life time of the decals (default 1000)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IPhysics.h>
#include <IEntitySystem.h>

class CDemoDecals : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoDecals"; }
  virtual const char *GetDefaultFrames() const { return "1,25,45,65,150"; }
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
      Host.Log("DemoDecals: no level");
      return false;
    }
    // Decals on, before the level loads.
    Host.Exec("e_decals 1");
    Host.ExecList(Host.GetScene().Get("init"));
    return Host.StartLevel(Level.c_str(), Host.GetScene().Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoDecals: the player is there after %d frames", nFramesWaited);
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
    Host.Get3DEngine()->CheckPhysicalized(m_vCamPos - Vec3(200, 200, 200), m_vCamPos + Vec3(200, 200, 200));
    static const char *Keys[eKinds] = { "bullet", "blood", "scorch" };
    for (int i = 0; i < eKinds; i++)
    {
      std::string Name = Host.SceneFileOrFirst(Keys[i], "textures/decal/", "*.dds");
      // Clamped: a decal shows its texture once.
      if (!Name.empty())
        m_pTextures[i] = Host.GetRenderer()->EF_LoadTexture(Name.c_str(), FT_NOREMOVE | FT_CLAMP, 0, eTT_Base);
      m_nTextures[i] = m_pTextures[i] && m_pTextures[i]->IsTextureLoaded() ? m_pTextures[i]->GetTextureID() : 0;
      Host.Log("DemoDecals: %s %s, id %d", Keys[i], Name.c_str(), m_nTextures[i]);
    }
    m_fLifeTime = Host.GetScene().GetFloat("lifetime", 1000);
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
    // Kind, frame after which it is made, point on the screen (fractions from the top left), size,
    // angle of rotation in degrees.
    static const struct { int nKind, nFrame; float fX, fY, fSize, fAngle; } Decals[] =
    {
      { eBullet, 20, 0.38f, 0.3f, 0.1f, 0 },
      { eBullet, 20, 0.46f, 0.3f, 0.2f, 30 },
      { eBullet, 20, 0.54f, 0.3f, 0.35f, 60 },
      { eBullet, 20, 0.62f, 0.3f, 0.5f, 90 },
      { eBlood, 40, 0.38f, 0.45f, 0.4f, 0 },
      { eBlood, 40, 0.5f, 0.45f, 0.8f, 45 },
      { eBlood, 40, 0.62f, 0.45f, 1.2f, 90 },
      { eScorch, 60, 0.2f, 0.8f, 0.75f, 0 },
      { eScorch, 60, 0.5f, 0.8f, 1.5f, 30 },
      { eScorch, 60, 0.8f, 0.8f, 2.5f, 60 },
    };
    for (size_t i = 0; i < sizeof(Decals) / sizeof(Decals[0]); i++)
    {
      if (Decals[i].nFrame == nFrame)
        MakeDecal(Host, Decals[i].nKind, Decals[i].fX, Decals[i].fY, Decals[i].fSize, Decals[i].fAngle);
    }
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    for (int i = 0; i < eKinds; i++)
    {
      if (m_pTextures[i])
        m_pTextures[i]->Release(false);
      m_pTextures[i] = NULL;
    }
  }

private:
  enum { eBullet, eBlood, eScorch, eKinds };

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
    Host.Log("DemoDecals: camera %g %g %g %g %g %g, fov %g", m_vCamPos.x, m_vCamPos.y, m_vCamPos.z, m_vCamAngles.x,
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

  //! A decal where the ray through (fX, fY) meets the terrain or a static object, filled as the
  //! SDK's CScriptObjectPlayer::GetProjectedBloodPos does.
  void MakeDecal(CDemoHost &Host, int nKind, float fX, float fY, float fSize, float fAngle)
  {
    static const char *Kinds[eKinds] = { "bullet", "blood", "scorch" };
    IPhysicalWorld *pWorld = Host.GetSystem()->GetIPhysicalWorld();
    const Vec3 vDir = ScreenRay(fX, fY);
    ray_hit Hit;
    memset(&Hit, 0, sizeof(Hit));
    if (!pWorld
      || !pWorld->RayWorldIntersection(m_vCamPos, vDir * 200.0f, ent_terrain | ent_static, rwi_stop_at_pierceable, &Hit, 1))
    {
      Host.Log("DemoDecals: %s at (%g, %g) of the screen: the ray meets nothing", Kinds[nKind], fX, fY);
      return;
    }
    CryEngineDecalInfo Decal;
    Decal.vPos = Hit.pt;
    Decal.vNormal = Hit.n;
    Decal.vHitDirection = vDir;
    Decal.fSize = fSize;
    Decal.fAngle = fAngle;
    Decal.fLifeTime = m_fLifeTime;
    Decal.nTid = m_nTextures[nKind];
    Decal.nPartID = Hit.ipart;
    // The owner is the object behind the physical entity (foreign data 0: IEntity, 1: IEntityRender).
    pe_params_foreign_data Foreign;
    if (!Hit.bTerrain && Hit.pCollider && Hit.pCollider->GetParams(&Foreign))
    {
      if (Foreign.iForeignData == 0)
        Decal.pDecalOwner = (IEntity *)Foreign.pForeignData;
      else if (Foreign.iForeignData == 1)
        Decal.pDecalOwner = (IEntityRender *)Foreign.pForeignData;
    }
    Host.Log("DemoDecals: %s size %g angle %g at %g %g %g, normal %g %g %g, %g m, %s %s", Kinds[nKind], fSize, fAngle,
      Hit.pt.x, Hit.pt.y, Hit.pt.z, Hit.n.x, Hit.n.y, Hit.n.z, Hit.dist, Hit.bTerrain ? "terrain" : "static object",
      Decal.pDecalOwner ? Decal.pDecalOwner->GetName() : "");
    if (Hit.n.Dot(vDir) < 0)
      Host.Get3DEngine()->CreateDecal(Decal);
  }

  int m_nPlayerFrame = -1;
  bool m_bSkipped = false;
  Vec3 m_vCamPos = Vec3(0, 0, 0);
  Vec3 m_vCamAngles = Vec3(0, 0, 0);
  float m_fFov = 1.0f;
  float m_fRatio = 0.75f;
  float m_fLifeTime = 1000;
  int m_nTextures[eKinds] = { 0, 0, 0 };
  ITexPic *m_pTextures[eKinds] = { NULL, NULL, NULL };
};

DEMO_PLUGIN(CDemoDecals)
