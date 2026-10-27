//////////////////////////////////////////////////////////////////////
//
//	File: DemoShadows.cpp
//	Description: Shadows of static objects under a moving light.
//
//	The SDK asks for shadows in two ways: shadow maps and stencil shadow
//	volumes (the CDLight flags DLF_CASTSHADOW_MAPS and DLF_CASTSHADOW_VOLUME,
//	the 3D engine's console variables e_shadow_maps and e_stencil_shadows).
//	The 3D engine makes the shadow maps of a level's objects itself (the
//	entity flags ERF_CASTSHADOWMAPS and ERF_RECVSHADOWMAPS, which the game
//	sets from the level, IStatInstGroup::bCastShadow); stencil volumes are
//	drawn with IStatObj::RenderShadowVolumes and the light in
//	SRendParams::pShadowVolumeLightSource. The demo has two scenes, both
//	with one white point light that goes round a point by 3 degrees per
//	frame from the +x side at frame 1: the default frames 31, 61, 91 and 121
//	have it on the +y, -x, -y and +x side of that point. A yellow cross marks
//	the light.
//	- The main menu (no level in the scene file): the object stands on a
//	  square floor of tiles (static objects), drawn like in DemoObject from
//	  a fixed camera, with the light (DLF_CASTSHADOW_VOLUME) going round
//	  above the object; then the object's shadow volumes for that light are
//	  drawn.
//	- A level (level = <name>): loaded like in DemoLevel and drawn by the
//	  engine, with the shadows of its objects; the light (both shadow flags)
//	  is added to the 3D engine (I3DEngine::AddDynamicLightSource) before
//	  each frame is drawn and goes round the target point. The demo only
//	  draws the cross over the game's frame.
//
//	Scene file keys (Demos\DemoShadows.ini):
//	  exec = <commands>      console commands before the object or the level
//	                         is loaded, separated by ';' or '|' (default:
//	                         e_shadow_maps 1 | e_stencil_shadows 1)
//	  radius = <m>           the radius of the light (default: 3 times the size
//	                         of the object; in a level 10, 0: no light)
//	  cast = 0|1             whether the light has its shadow flags (default 1)
//	The main menu:
//	  object = <file>        the object (default: the first .cgf under objects/)
//	  floor = <file>         the floor tile, laid out by its box (default: the
//	                         object)
//	  volumes = 0|1          draw the object's shadow volumes (default 1)
//	A level:
//	  level = <name>         the level (default: none, the main menu scene)
//	  mission = <name>       its mission (default: the level's default)
//	  settle = <frames>      frames after the player exists (default 300)
//	  exec_after = <commands> console commands once the level is there, after
//	                         its cut scene is skipped, separated like exec
//	                         (e.g. cl_display_hud 0)
//	  camera = x y z ax ay az  a fixed camera: position and angles in degrees,
//	                         as in DemoLevel (default: the game's camera, or one
//	                         from target and view)
//	  target = x y z         the point the light goes round, which the camera
//	                         looks at from view when there is no camera key
//	                         (default: 10 m in front of the camera)
//	  view = d yaw pitch     that camera: d m from the target, yaw degrees round
//	                         z (0: from -y) and pitch degrees above it
//	                         (default 15 30 30)
//	  circle = r h           the circle of the light: its radius and height
//	                         above the target (default 4 3)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

//! The light of frame nFrame: white, fCircle from vCenter and fHeight above it, going round it by
//! 3 degrees per frame from +x; a point light with nFlags.
static void MakeLight(CDLight &Light, int nFrame, const Vec3 &vCenter, float fCircle, float fHeight, float fRadius,
  unsigned int nFlags)
{
  const float fAround = DEG2RAD(3.0f * (nFrame - 1));
  Light.m_Flags = DLF_POINT | nFlags;
  Light.m_Origin = vCenter + Vec3(cosf(fAround) * fCircle, sinf(fAround) * fCircle, fHeight);
  Light.m_fRadius = Light.m_fEndRadius = fRadius;
  Light.m_Color = CFColor(1, 1, 1, 1);
  Light.m_SpecColor = CFColor(1, 1, 1, 1);
}

static void DrawCross(IRenderer *pRenderer, const Vec3 &P, float d)
{
  const CFColor Yellow(1, 1, 0, 1);
  pRenderer->SetState(GS_NODEPTHTEST);
  pRenderer->SetWhiteTexture();
  pRenderer->DrawLineColor(P - Vec3(d, 0, 0), Yellow, P + Vec3(d, 0, 0), Yellow);
  pRenderer->DrawLineColor(P - Vec3(0, d, 0), Yellow, P + Vec3(0, d, 0), Yellow);
  pRenderer->DrawLineColor(P - Vec3(0, 0, d), Yellow, P + Vec3(0, 0, d), Yellow);
  pRenderer->SetState(GS_DEPTHWRITE);
}

class CDemoShadows : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoShadows"; }
  virtual const char *GetDefaultFrames() const { return "31,61,91,121"; }
  virtual bool ClearsFrame() const { return !m_bLevel; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    Host.ExecList(Scene.Get("exec", "e_shadow_maps 1 | e_stencil_shadows 1"));
    std::string Level = Scene.Get("level");
    m_bLevel = !Level.empty();
    if (Scene.GetInt("cast", 1))
      m_nFlags = m_bLevel ? DLF_CASTSHADOW_MAPS | DLF_CASTSHADOW_VOLUME | DLF_LIGHTSOURCE : DLF_CASTSHADOW_VOLUME;
    else
      m_nFlags = m_bLevel ? DLF_LIGHTSOURCE : 0;
    if (m_bLevel)
      return Host.StartLevel(Level.c_str(), Scene.Get("mission").c_str());

    m_pObj = Host.LoadObject("object");
    if (!m_pObj)
      return false;
    if (Scene.Has("floor"))
      m_pFloor = Host.LoadObject("floor");
    m_bVolumes = Scene.GetInt("volumes", 1) != 0;
    Host.Log("DemoShadows: main menu scene, %d stencil bits, shadow volumes %s", Host.GetRenderer()->GetStencilBpp(),
      m_bVolumes ? "on" : "off");
    return true;
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    if (!m_bLevel)
      return true;
    const CSceneFile &Scene = Host.GetScene();
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoShadows: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Scene.GetInt("settle", 300))
      return false;

    // The end of a cut scene turns the HUD back on: the commands come after it.
    Host.Exec("SkipCutScene");
    Host.ExecList(Scene.Get("exec_after"));
    const CCamera &Game = Host.GetSystem()->GetViewCamera();
    std::vector<float> Camera = Scene.GetFloats("camera"), Target = Scene.GetFloats("target");
    std::vector<float> View = Scene.GetFloats("view"), Circle = Scene.GetFloats("circle");
    m_bCamera = Camera.size() >= 6 || Target.size() >= 3;
    m_vEye = Camera.size() >= 6 ? Vec3(Camera[0], Camera[1], Camera[2]) : Game.GetPos();
    m_vAngles = Camera.size() >= 6 ? Vec3(Camera[3], Camera[4], Camera[5]) : Vec3(Game.GetAngles());
    // ConvertToRadAngles: the view direction of camera angles, as the SDK's game takes it.
    m_vTarget = Target.size() >= 3 ? Vec3(Target[0], Target[1], Target[2]) : m_vEye + ConvertToRadAngles(m_vAngles) * 10.0f;
    if (Camera.size() < 6 && Target.size() >= 3)
    {
      const float fDist = View.size() >= 1 ? View[0] : 15, fYaw = DEG2RAD(View.size() >= 2 ? View[1] : 30);
      const float fPitch = DEG2RAD(View.size() >= 3 ? View[2] : 30);
      m_vEye = m_vTarget + Vec3(sinf(fYaw) * cosf(fPitch), -cosf(fYaw) * cosf(fPitch), sinf(fPitch)) * fDist;
      m_vAngles = ConvertVectorToCameraAngles(m_vTarget - m_vEye);
    }
    m_fCircle = Circle.size() >= 1 ? Circle[0] : 4;
    m_fHeight = Circle.size() >= 2 ? Circle[1] : 3;
    m_fRadius = Scene.GetFloat("radius", 10);
    Host.Log("DemoShadows: the game's camera %g %g %g %g %g %g", Game.GetPos().x, Game.GetPos().y, Game.GetPos().z,
      Game.GetAngles().x, Game.GetAngles().y, Game.GetAngles().z);
    Host.Log("DemoShadows: camera %g %g %g %g %g %g%s, target %g %g %g", m_vEye.x, m_vEye.y, m_vEye.z, m_vAngles.x,
      m_vAngles.y, m_vAngles.z, m_bCamera ? "" : " (the game's)", m_vTarget.x, m_vTarget.y, m_vTarget.z);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    if (!m_bLevel)
      return false;
    // Before the engine draws the frame, as the game's entities add their lights.
    if (m_fRadius > 0)
    {
      CDLight Light;
      MakeLight(Light, nFrame, m_vTarget, m_fCircle, m_fHeight, m_fRadius, m_nFlags);
      Host.Get3DEngine()->AddDynamicLightSource(Light, NULL);
    }
    if (!m_bCamera)
      return false;
    Cam.SetPos(m_vEye);
    Cam.SetAngle(m_vAngles);
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    if (m_bLevel)
    {
      if (m_fRadius <= 0)
        return;
      CDLight Light;
      MakeLight(Light, nFrame, m_vTarget, m_fCircle, m_fHeight, m_fRadius, m_nFlags);
      pRenderer->SetCamera(Host.GetSystem()->GetViewCamera());
      DrawCross(pRenderer, Light.m_Origin, 0.25f);
      return;
    }

    const Vec3 vMin = m_pObj->GetBoxMin(), vMax = m_pObj->GetBoxMax();
    const Vec3 vCenter = (vMin + vMax) * 0.5f;
    const float fSize = max(GetLength(vMax - vMin), 0.1f);
    // Half the side of the floor, whose top is at the bottom of the object.
    const float fHalf = fSize * 1.25f;
    const CCamera Cam = Host.FrameBox(Vec3(vCenter.x - fHalf, vCenter.y - fHalf, vMin.z),
      Vec3(vCenter.x + fHalf, vCenter.y + fHalf, vMax.z), 30, 40);

    CDLight Light;
    MakeLight(Light, nFrame, Vec3(vCenter.x, vCenter.y, vMax.z), fSize * 0.6f, fSize * 0.5f,
      Host.GetScene().GetFloat("radius", fSize * 3), m_nFlags);
    // The light in the object's own space: the object stands at the origin, unturned.
    Light.m_vObjectSpacePos = Light.m_Origin;

    SRendParams Params;
    Host.BeginScene(Cam, Params, &Light, 1);
    IStatObj *pTile = m_pFloor ? m_pFloor : m_pObj;
    const Vec3 vTileMin = pTile->GetBoxMin(), vTileMax = pTile->GetBoxMax();
    const float fTileX = max(vTileMax.x - vTileMin.x, 0.01f), fTileY = max(vTileMax.y - vTileMin.y, 0.01f);
    const int nTilesX = min(max((int)ceilf(fHalf * 2 / fTileX), 1), 16);
    const int nTilesY = min(max((int)ceilf(fHalf * 2 / fTileY), 1), 16);
    const float fX0 = vCenter.x - nTilesX * fTileX * 0.5f - vTileMin.x;
    const float fY0 = vCenter.y - nTilesY * fTileY * 0.5f - vTileMin.y;
    for (int y = 0; y < nTilesY; y++)
      for (int x = 0; x < nTilesX; x++)
        RenderAt(Cam, pTile, Vec3(fX0 + x * fTileX, fY0 + y * fTileY, vMin.z - vTileMax.z), Params.nDLightMask);
    RenderAt(Cam, m_pObj, Vec3(0, 0, 0), Params.nDLightMask);
    if (m_bVolumes)
    {
      SRendParams Volume;
      Volume.nDLightMask = Params.nDLightMask;
      Volume.pShadowVolumeLightSource = &Light;
      Volume.fShadowVolumeExtent = fSize * 4;
      Volume.fDistance = GetLength(Cam.GetPos() - vCenter);
      if (!m_bVolumeLogged)
        Host.Log("DemoShadows: shadow volume object %p before RenderShadowVolumes", (void *)m_pObj->GetShadowVolume());
      m_pObj->RenderShadowVolumes(&Volume);
      if (!m_bVolumeLogged)
        Host.Log("DemoShadows: shadow volume object %p after it", (void *)m_pObj->GetShadowVolume());
      m_bVolumeLogged = true;
    }
    Host.EndScene();
    DrawCross(pRenderer, Light.m_Origin, fSize * 0.05f);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_pFloor)
      Host.Get3DEngine()->ReleaseObject(m_pFloor);
    if (m_pObj)
      Host.Get3DEngine()->ReleaseObject(m_pObj);
    m_pFloor = m_pObj = NULL;
  }

private:
  //! Draws pObj moved by vPos (a matrix in SRendParams::pMatrix with FOB_TRANS_MASK) with the lights of nLightMask.
  static void RenderAt(const CCamera &Cam, IStatObj *pObj, const Vec3 &vPos, unsigned int nLightMask)
  {
    Matrix44 Mat;
    Mat.SetIdentity();
    Mat.SetTranslationOLD(vPos);
    SRendParams Params;
    Params.pMatrix = &Mat;
    Params.dwFObjFlags = FOB_TRANS_MASK;
    Params.nDLightMask = nLightMask;
    Params.vAmbientColor = Vec3(0.15f, 0.15f, 0.15f);
    Params.fDistance = GetLength(Cam.GetPos() - vPos);
    pObj->Render(Params, Vec3(0, 0, 0), 0);
  }

  bool m_bLevel = false;
  //! The light's flags besides DLF_POINT.
  unsigned int m_nFlags = 0;
  // The main menu scene.
  IStatObj *m_pObj = NULL;
  //! The floor tile, if not the object itself.
  IStatObj *m_pFloor = NULL;
  bool m_bVolumes = true;
  bool m_bVolumeLogged = false;
  // The level scene.
  int m_nPlayerFrame = -1;
  //! Whether the demo sets the camera (camera or target given).
  bool m_bCamera = false;
  Vec3 m_vTarget = Vec3(0, 0, 0);
  Vec3 m_vEye = Vec3(0, 0, 0);
  Vec3 m_vAngles = Vec3(0, 0, 0);
  float m_fCircle = 4;
  float m_fHeight = 3;
  float m_fRadius = 10;
};

DEMO_PLUGIN(CDemoShadows)
