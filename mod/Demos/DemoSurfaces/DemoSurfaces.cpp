// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoSurfaces.cpp
//	Description: Surfaces of different materials, glass among them, at the
//	water of a level under a moving light.
//
//	A row of static objects stands on the ground of a level and in its
//	shallow water, each with a material of its own: a shader template with
//	the demo's textures (DemoMaterials.h, as in DemoShaders: matt, glossy
//	with a bump map, chrome, reflecting the scene round it, glass) or the
//	object's own materials. The engine draws them with the level: the demo
//	hands them over from the 3D engine's render call back
//	(I3DEngine::SetRenderCallback, the editor's way to draw its objects),
//	so they get the level's lights and fog and are in the renderer's lists
//	like the level's own objects, and in the reflections the engine calls
//	it back for (the host log says for which cameras it is called in the
//	first frames). Their light mask and ambient colour are the 3D engine's
//	for their place (GetLightMaskFromPosition, GetAmbientColorFromPosition).
//	A coloured point light goes round the middle of the row by 3 degrees per
//	frame from the +x side at frame 1, as in DemoShadows; a yellow cross
//	marks it. The objects turn about z and are labelled with their template.
//	It is a scene to look at how the renderer's reflections and lights work
//	on those surfaces.
//
//	Scene file keys (Demos\DemoSurfaces.ini):
//	  level = <name>          the level (default: training)
//	  mission = <name>        its mission (default: the level's default)
//	  settle = <frames>       frames after the player exists (default 300)
//	  exec = <commands>       console commands before the level is loaded,
//	                          separated by ';' or '|'
//	  exec_after = <commands> console commands once the level is there, after
//	                          its cut scene is skipped, separated like exec
//	                          (e.g. cl_display_hud 0)
//	  object = <file>         the object of the row (default: the first .cgf
//	                          under objects/)
//	  shaders = <t> [<o>], ... one copy of the object per template, each with
//	                          an optional opacity; "-" is the object's own
//	                          materials (default: TemplLightDiffuse,
//	                          TemplBumpSpec_HP, TemplReflCM, TemplReflEnvCM,
//	                          TemplGlassCM 0.5, TemplGlassCM 0.2,
//	                          TemplDecalAdd); "none": no copies
//	  item<n> = <file> [<t> [<o>]]  more objects after the copies, in the order
//	                          of the keys, each with a template and an opacity
//	                          (default: its own materials)
//	  texture, bump, cubemap, cubeslots, gloss  the textures of the materials,
//	                          as in DemoShaders
//	  specular = <r g b>      the materials' specular colour (default: 0.8 0.8 0.8)
//	  shininess = <n>         their specular power (default: 32)
//	  row = x y dx dy         where the first object stands and the step to the
//	                          next one (default: 10 m in front of the game's
//	                          camera, 2 m apart to its right)
//	  size = <m>              the copies of the object scaled so that its
//	                          largest side is that long (default 0: as it is;
//	                          the other items are as they are)
//	  wade = <m>              how deep an object stands in the water at most:
//	                          its bottom is on the ground, or that far under
//	                          the water's surface where the water is deeper
//	                          (default 0.3)
//	  turn = <degrees>        each object's turn about z per frame (default 1)
//	  light = <r g b>         the colour of the point light (default: 1 0.8 0.5)
//	  radius = <m>            its radius (default 8, 0: no light)
//	  circle = r h            its circle round the middle of the row: radius
//	                          and height above it (default: 3 2)
//	  cast = 0|1              whether it casts shadows (default 0)
//	  camera = x y z ax ay az  a fixed camera: position and angles in degrees,
//	                          as in DemoLevel (default: one from view)
//	  view = d yaw pitch      that camera: d m from the middle of the row, yaw
//	                          degrees round z (0: from -y) and pitch degrees
//	                          above it (default: 12 0 15)
//	  orbit = <degrees>       the turn of the view camera round the row per
//	                          frame (default 0)
//	  labels = 0|1            the template over each object (default 1)
//
//////////////////////////////////////////////////////////////////////

#include "DemoMaterials.h"
#include <IFont.h>

static const char s_szDefaultShaders[] =
  "TemplLightDiffuse, TemplBumpSpec_HP, TemplReflCM, TemplReflEnvCM, TemplGlassCM 0.5, TemplGlassCM 0.2, TemplDecalAdd";
static const int nMaxItems = 32;
//! The cameras of the render call backs logged per frame, for the first frames.
static const int nLoggedFrames = 3;
static const int nLoggedCalls = 8;

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

class CDemoSurfaces : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoSurfaces"; }
  virtual const char *GetDefaultFrames() const { return "1,31,61,91,121"; }
  virtual bool ClearsFrame() const { return false; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    m_pHost = &Host;
    Host.ExecList(Scene.Get("exec"));
    return Host.StartLevel(Scene.Get("level", "training").c_str(), Scene.Get("mission").c_str());
  }

  virtual bool IsReady(CDemoHost &Host, int nFramesWaited)
  {
    const CSceneFile &Scene = Host.GetScene();
    if (m_nPlayerFrame < 0)
    {
      if (!Host.HasPlayer())
        return false;
      m_nPlayerFrame = nFramesWaited;
      Host.Log("DemoSurfaces: the player is there after %d frames", nFramesWaited);
    }
    if (nFramesWaited - m_nPlayerFrame < Scene.GetInt("settle", 300))
      return false;

    // The end of a cut scene turns the HUD back on: the commands come after it.
    Host.Exec("SkipCutScene");
    Host.ExecList(Scene.Get("exec_after"));
    MakeItems(Host);
    PlaceItems(Host);
    if (!m_Items.empty())
      Host.Get3DEngine()->SetRenderCallback(RenderCallback, this);
    return true;
  }

  virtual bool GetCamera(CDemoHost &Host, int nFrame, CCamera &Cam)
  {
    m_nFrame = nFrame;
    m_nCalls = 0;
    // Before the engine draws the frame, as the game's entities add their lights.
    if (m_fRadius > 0)
    {
      CDLight Light;
      MakeLight(Light, nFrame);
      Host.Get3DEngine()->AddDynamicLightSource(Light, NULL);
    }
    Vec3 vEye = m_vEye, vAngles = m_vAngles;
    if (!m_bFixedCamera)
    {
      const float fYaw = DEG2RAD(m_fYaw + m_fOrbit * (nFrame - 1)), fPitch = DEG2RAD(m_fPitch);
      vEye = m_vCenter + Vec3(sinf(fYaw) * cosf(fPitch), -cosf(fYaw) * cosf(fPitch), sinf(fPitch)) * m_fDist;
      vAngles = ConvertVectorToCameraAngles(m_vCenter - vEye);
    }
    Cam.SetPos(vEye);
    Cam.SetAngle(vAngles);
    Cam.Update();
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    if (nFrame <= nLoggedFrames)
    {
      std::string Cameras;
      for (int i = 0; i < m_nCalls && i < nLoggedCalls; i++)
      {
        char szCam[96];
        _snprintf(szCam, sizeof(szCam) - 1, " (%.1f %.1f %.1f)", m_vCallCams[i].x, m_vCallCams[i].y, m_vCallCams[i].z);
        szCam[sizeof(szCam) - 1] = 0;
        Cameras += szCam;
      }
      Host.Log("DemoSurfaces: frame %d: %d render call backs, cameras%s", nFrame, m_nCalls, Cameras.c_str());
    }

    IRenderer *pRenderer = Host.GetRenderer();
    pRenderer->SetCamera(Host.GetSystem()->GetViewCamera());
    if (m_fRadius > 0)
    {
      CDLight Light;
      MakeLight(Light, nFrame);
      DrawCross(pRenderer, Light.m_Origin, 0.2f);
    }
    if (m_bLabels)
      DrawLabels(Host);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (!m_Items.empty())
      Host.Get3DEngine()->SetRenderCallback(NULL, NULL);
    for (size_t i = 0; i < m_Items.size(); i++)
      m_Items[i].Mat.Release(Host);
    m_Items.clear();
    for (size_t i = 0; i < m_Objects.size(); i++)
      Host.Get3DEngine()->ReleaseObject(m_Objects[i]);
    m_Objects.clear();
  }

private:
  struct SItem
  {
    IStatObj *pObj = NULL;
    std::string Template;
    float fOpacity = 1.0f;
    SDemoMaterial Mat;
    //! The scale and where the object stands: the middle of the bottom of its box.
    float fScale = 1.0f;
    Vec3 vBase = Vec3(0, 0, 0);
  };

  static void RenderCallback(void *pParams) { ((CDemoSurfaces *)pParams)->Render(); }

  //! Hands the objects over to the scene the engine is drawing, from the renderer's camera.
  void Render()
  {
    I3DEngine *p3DEngine = m_pHost->Get3DEngine();
    const Vec3 vCam = m_pHost->GetRenderer()->GetCamera().GetPos();
    if (m_nCalls < nLoggedCalls)
      m_vCallCams[m_nCalls] = vCam;
    m_nCalls++;

    const float fTurn = DEG2RAD(m_fTurn * (m_nFrame - 1));
    const float c = cosf(fTurn), s = sinf(fTurn);
    for (size_t i = 0; i < m_Items.size(); i++)
    {
      const SItem &Item = m_Items[i];
      const Vec3 vMin = Item.pObj->GetBoxMin(), vMax = Item.pObj->GetBoxMax();
      // Turned about the middle of its box and scaled, its bottom at vBase.
      const float f = Item.fScale;
      const float cx = (vMin.x + vMax.x) * 0.5f * f, cy = (vMin.y + vMax.y) * 0.5f * f;
      const Vec3 vPos = Item.vBase - Vec3(c * cx - s * cy, s * cx + c * cy, vMin.z * f);
      // As in DemoObject: the turned and scaled axes in the rows, the position in the last row.
      Matrix44 Mat;
      Mat.SetIdentity();
      float *m = Mat.GetData();
      m[0] = c * f;
      m[1] = s * f;
      m[4] = -s * f;
      m[5] = c * f;
      m[10] = f;
      Mat.SetTranslationOLD(vPos);

      const Vec3 vMiddle = Item.vBase + Vec3(0, 0, (vMax.z - vMin.z) * 0.5f * f);
      const float fRound = GetLength(vMax - vMin) * 0.5f * f;
      SRendParams Params;
      Params.pMatrix = &Mat;
      Params.dwFObjFlags = FOB_TRANS_MASK;
      Params.pMaterial = Item.Mat.pMat;
      Params.nDLightMask = p3DEngine->GetLightMaskFromPosition(vMiddle, fRound);
      Params.vAmbientColor = p3DEngine->GetAmbientColorFromPosition(vMiddle, fRound);
      Params.fDistance = GetLength(vMiddle - vCam);
      Item.pObj->Render(Params, Vec3(0, 0, 0), 0);
    }
  }

  //! The row's copies of the object and the other items, with their materials.
  void MakeItems(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    m_Textures.FromScene(Host);
    Host.Log("DemoSurfaces: texture '%s', bump '%s', cube map '%s'", m_Textures.Texture.c_str(),
      m_Textures.Bump.c_str(), m_Textures.CubeMap.c_str());
    m_LightMat.Front.m_Specular = SceneColor(Scene, "specular", CFColor(0.8f, 0.8f, 0.8f, 1));
    m_LightMat.Front.m_SpecShininess = Scene.GetFloat("shininess", 32);

    const std::string Shaders = Scene.Get("shaders", s_szDefaultShaders);
    if (Shaders != "none")
    {
      IStatObj *pObj = Host.LoadObject("object");
      if (pObj)
      {
        m_Objects.push_back(pObj);
        std::vector<std::string> List = SplitList(Shaders, ',');
        const Vec3 vMin = pObj->GetBoxMin(), vMax = pObj->GetBoxMax();
        const float fSide = max(max(vMax.x - vMin.x, vMax.y - vMin.y), vMax.z - vMin.z);
        const float fSize = Scene.GetFloat("size", 0);
        for (size_t i = 0; i < List.size() && (int)m_Items.size() < nMaxItems; i++)
        {
          SItem Item;
          Item.pObj = pObj;
          Item.fScale = fSize > 0 && fSide > 0 ? fSize / fSide : 1.0f;
          char szTemplate[128] = "";
          sscanf(List[i].c_str(), "%127s %f", szTemplate, &Item.fOpacity);
          Item.Template = szTemplate;
          m_Items.push_back(Item);
        }
      }
    }
    std::vector<std::string> Keys = Scene.GetKeys("item");
    for (size_t i = 0; i < Keys.size() && (int)m_Items.size() < nMaxItems; i++)
    {
      char szFile[260] = "", szTemplate[128] = "-";
      SItem Item;
      sscanf(Scene.Get(Keys[i].c_str()).c_str(), "%259s %127s %f", szFile, szTemplate, &Item.fOpacity);
      Item.pObj = szFile[0] ? Host.Get3DEngine()->MakeObject(szFile) : NULL;
      if (!Item.pObj)
      {
        Host.Log("DemoSurfaces: %s '%s' not loaded", Keys[i].c_str(), szFile);
        continue;
      }
      m_Objects.push_back(Item.pObj);
      Item.Template = szTemplate;
      m_Items.push_back(Item);
    }

    for (size_t i = 0; i < m_Items.size(); i++)
    {
      SItem &Item = m_Items[i];
      if (Item.Template == "-")
        continue;
      // A material name of its own for each item: the renderer names the shader after it.
      char szName[32];
      _snprintf(szName, sizeof(szName) - 1, "DemoSurfaces%d", (int)i);
      szName[sizeof(szName) - 1] = 0;
      SShaderItem Shader = Item.Mat.Make(Host, szName, Item.Template.c_str(), Item.fOpacity, m_Textures, m_LightMat);
      IShader *pTemplate = Shader.m_pShader ? Shader.m_pShader->GetTemplate(-1) : NULL;
      Host.Log("DemoSurfaces: item %d '%s' opacity %g: shader '%s', template '%s', sort %d", (int)i,
        Item.Template.c_str(), Item.fOpacity, Shader.m_pShader ? Shader.m_pShader->GetName() : "-",
        pTemplate ? pTemplate->GetName() : "-", pTemplate ? (int)pTemplate->GetSort() : -1);
    }
  }

  //! Where the items stand, the middle of the row, the camera and the light.
  void PlaceItems(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    I3DEngine *p3DEngine = Host.Get3DEngine();
    const CCamera &Game = Host.GetSystem()->GetViewCamera();
    const Vec3 vDir = ConvertToRadAngles(Vec3(Game.GetAngles()));
    std::vector<float> Row = Scene.GetFloats("row");
    if (Row.size() < 4)
    {
      const Vec3 vFront = Game.GetPos() + vDir * 10.0f;
      Vec3 vRight(vDir.y, -vDir.x, 0);
      vRight = GetLengthSquared(vRight) > 1e-6f ? GetNormalized(vRight) * 2.0f : Vec3(2, 0, 0);
      Row = { vFront.x, vFront.y, vRight.x, vRight.y };
    }
    const float fWade = Scene.GetFloat("wade", 0.3f);
    m_fTurn = Scene.GetFloat("turn", 1);

    Vec3 vSum(0, 0, 0);
    for (size_t i = 0; i < m_Items.size(); i++)
    {
      SItem &Item = m_Items[i];
      const Vec3 vMin = Item.pObj->GetBoxMin(), vMax = Item.pObj->GetBoxMax();
      const float x = Row[0] + Row[2] * i, y = Row[1] + Row[3] * i;
      const float fGround = p3DEngine->GetTerrainElevation(x, y);
      const Vec3 vGround(x, y, fGround);
      const float fWater = p3DEngine->GetWaterLevel(&vGround);
      Item.vBase = Vec3(x, y, max(fGround, fWater - fWade));
      vSum += Item.vBase + Vec3(0, 0, (vMax.z - vMin.z) * 0.5f * Item.fScale);
      Host.Log("DemoSurfaces: item %d at %g %g %g (ground %g, water %g), scale %g", (int)i, Item.vBase.x,
        Item.vBase.y, Item.vBase.z, fGround, fWater, Item.fScale);
    }
    m_vCenter = m_Items.empty() ? Game.GetPos() + vDir * 10.0f : vSum * (1.0f / m_Items.size());

    std::vector<float> Camera = Scene.GetFloats("camera"), View = Scene.GetFloats("view");
    m_bFixedCamera = Camera.size() >= 6;
    if (m_bFixedCamera)
    {
      m_vEye = Vec3(Camera[0], Camera[1], Camera[2]);
      m_vAngles = Vec3(Camera[3], Camera[4], Camera[5]);
    }
    m_fDist = View.size() >= 1 ? View[0] : 12;
    m_fYaw = View.size() >= 2 ? View[1] : 0;
    m_fPitch = View.size() >= 3 ? View[2] : 15;
    m_fOrbit = Scene.GetFloat("orbit", 0);

    std::vector<float> Circle = Scene.GetFloats("circle");
    m_fCircle = Circle.size() >= 1 ? Circle[0] : 3;
    m_fHeight = Circle.size() >= 2 ? Circle[1] : 2;
    m_fRadius = Scene.GetFloat("radius", 8);
    m_LightColor = SceneColor(Scene, "light", m_LightColor);
    m_nLightFlags = DLF_POINT | DLF_LIGHTSOURCE;
    if (Scene.GetInt("cast", 0))
      m_nLightFlags |= DLF_CASTSHADOW_MAPS | DLF_CASTSHADOW_VOLUME;
    m_bLabels = Scene.GetInt("labels", 1) != 0;
    Host.Log("DemoSurfaces: %d items, middle %g %g %g, camera %s", (int)m_Items.size(), m_vCenter.x, m_vCenter.y,
      m_vCenter.z, m_bFixedCamera ? "fixed" : "round the middle");
  }

  //! The light of frame nFrame: m_fCircle from the middle of the row and m_fHeight above it, going
  //! round it by 3 degrees per frame from +x.
  void MakeLight(CDLight &Light, int nFrame) const
  {
    const float fAround = DEG2RAD(3.0f * (nFrame - 1));
    Light.m_Flags = m_nLightFlags;
    Light.m_Origin = m_vCenter + Vec3(cosf(fAround) * m_fCircle, sinf(fAround) * m_fCircle, m_fHeight);
    Light.m_fRadius = Light.m_fEndRadius = m_fRadius;
    Light.m_Color = m_LightColor;
    Light.m_SpecColor = m_LightColor;
  }

  //! The template (and opacity) over each item, centred on the virtual 800 x 600 screen.
  void DrawLabels(CDemoHost &Host)
  {
    if (!m_pFont)
    {
      ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
      m_pFont = pCryFont ? pCryFont->GetFont("Default") : NULL;
      if (!m_pFont)
        return;
    }
    IRenderer *pRenderer = Host.GetRenderer();
    m_pFont->Reset();
    m_pFont->SetEffect("default");
    m_pFont->SetSameSize(false);
    m_pFont->SetCharWidthScale(1.0f);
    m_pFont->SetSize(vector2f(14, 14));
    m_pFont->SetColor(color4f(1, 1, 1, 1));
    // GetTextSize is in pixels (as for the SDK's menu), DrawString takes the virtual screen.
    const float fToVirtual = 800.0f / max(pRenderer->GetWidth(), 1);
    for (size_t i = 0; i < m_Items.size(); i++)
    {
      const SItem &Item = m_Items[i];
      const Vec3 vMin = Item.pObj->GetBoxMin(), vMax = Item.pObj->GetBoxMax();
      const Vec3 vTop = Item.vBase + Vec3(0, 0, (vMax.z - vMin.z) * Item.fScale + 0.15f);
      float sx, sy, sz;
      pRenderer->ProjectToScreen(vTop.x, vTop.y, vTop.z, &sx, &sy, &sz);
      if (sz <= 0 || sz >= 1 || sx < 0 || sx > 100 || sy < 0 || sy > 100)
        continue;
      char szLabel[160];
      const char *szName = Item.Template == "-" ? "own material" : Item.Template.c_str();
      if (Item.fOpacity != 1.0f)
        _snprintf(szLabel, sizeof(szLabel) - 1, "%s %g", szName, Item.fOpacity);
      else
        _snprintf(szLabel, sizeof(szLabel) - 1, "%s", szName);
      szLabel[sizeof(szLabel) - 1] = 0;
      // Every other label a line higher, so that the labels of neighbours do not overlap.
      const float fLine = 14.0f * (1 + (i & 1));
      m_pFont->DrawString(sx * 8.0f - m_pFont->GetTextSize(szLabel).x * fToVirtual * 0.5f, sy * 6.0f - fLine, szLabel);
    }
  }

  CDemoHost *m_pHost = NULL;
  IFFont *m_pFont = NULL;
  int m_nPlayerFrame = -1;
  int m_nFrame = 1;
  //! The render call backs of this frame and the cameras of the first ones.
  int m_nCalls = 0;
  Vec3 m_vCallCams[nLoggedCalls];

  SDemoTextures m_Textures;
  //! The lighting of the materials (white, specular 0.8 and shininess 32 by default).
  SLightMaterial m_LightMat;
  std::vector<SItem> m_Items;
  //! The objects loaded, released at the end.
  std::vector<IStatObj *> m_Objects;
  float m_fTurn = 1;

  Vec3 m_vCenter = Vec3(0, 0, 0);
  bool m_bFixedCamera = false;
  Vec3 m_vEye = Vec3(0, 0, 0);
  Vec3 m_vAngles = Vec3(0, 0, 0);
  float m_fDist = 12, m_fYaw = 0, m_fPitch = 15, m_fOrbit = 0;

  unsigned int m_nLightFlags = DLF_POINT | DLF_LIGHTSOURCE;
  CFColor m_LightColor = CFColor(1, 0.8f, 0.5f, 1);
  float m_fCircle = 3, m_fHeight = 2, m_fRadius = 8;
  bool m_bLabels = true;
};

DEMO_PLUGIN(CDemoSurfaces)
