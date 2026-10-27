//////////////////////////////////////////////////////////////////////
//
//	File: DemoMultiLight.cpp
//	Description: Several coloured lights on a static object standing on
//	a floor.
//
//	The object stands on a square floor of tiles, which are static objects
//	too (geometry drawn with DrawDynVB is not lit by CDLights), all drawn
//	like in DemoObject (a matrix in SRendParams::pMatrix) with a dark
//	ambient colour, from a fixed camera. Point lights of different colours
//	go round the object in a circle, half its size above the floor, by 2
//	degrees per frame, spread evenly over the circle: frames 1-30 have one
//	of them (red), 31-60 two (red, green), 61-90 four (red, green, blue,
//	yellow) and 91-120 all of them (then cyan, magenta, orange, white).
//	Frames 121-150 have one warm white directional light (DLF_DIRECTIONAL)
//	45 degrees above the floor, coming from the camera's side (-y) at frame
//	121 and turning by 3 degrees per frame towards +x; frames 151-180 one
//	white projected light (DLF_PROJECT) above the object, aimed down, that
//	casts its picture onto the object and the floor, turned by 3 degrees per
//	frame. A cross in its colour marks each point light; a line points to
//	the directional light and a line down from a white cross marks the
//	projected light. After frame 180 the sequence starts again. The default
//	frames 15, 45, 75, 105, 135 and 165 show one frame of each part.
//
//	Scene file keys (Demos\DemoMultiLight.ini):
//	  object = <file>   the object (default: the first .cgf under objects/)
//	  floor = <file>    the floor tile, laid out by its box (default: the object)
//	  lights = <n>      the number of point lights, 1 to 16 (default 6)
//	  image = <file>    the picture of the projected light (default: the first
//	                    .dds under textures/lights/)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

static const int nMaxLights = 16;
static const int nPartFrames = 30;

//! The colours of the point lights, in turn.
static const CFColor s_Colours[8] = {
  CFColor(1, 0, 0, 1), CFColor(0, 1, 0, 1), CFColor(0, 0, 1, 1), CFColor(1, 1, 0, 1),
  CFColor(0, 1, 1, 1), CFColor(1, 0, 1, 1), CFColor(1, 0.5f, 0, 1), CFColor(1, 1, 1, 1),
};

class CDemoMultiLight : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoMultiLight"; }
  virtual const char *GetDefaultFrames() const { return "15,45,75,105,135,165"; }

  virtual bool Init(CDemoHost &Host)
  {
    m_pObj = Host.LoadObject("object");
    if (!m_pObj)
      return false;
    if (Host.GetScene().Has("floor"))
      m_pFloor = Host.LoadObject("floor");
    m_nLights = min(max(Host.GetScene().GetInt("lights", 6), 1), nMaxLights);
    std::string Image = Host.SceneFileOrFirst("image", "textures/lights/", "*.dds");
    // Loaded as the SDK's game loads the images of its projected lights (CPlayer::InitLight).
    if (!Image.empty())
      m_pImage = Host.GetRenderer()->EF_LoadTexture(Image.c_str(), 0, FT2_FORCECUBEMAP, eTT_Cubemap);
    Host.Log("DemoMultiLight: %d point lights, image '%s': %s", m_nLights, Image.c_str(),
      !m_pImage ? "not loaded" : m_pImage->IsTextureLoaded() ? "loaded" : "not found");
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const Vec3 vMin = m_pObj->GetBoxMin(), vMax = m_pObj->GetBoxMax();
    const Vec3 vCenter = (vMin + vMax) * 0.5f;
    const float fSize = max(GetLength(vMax - vMin), 0.1f);
    const float fCircle = fSize * 0.75f;
    const float fLightZ = vMin.z + fSize * 0.5f;
    // Half the side of the floor, whose top is at the bottom of the object.
    const float fHalf = fCircle + fSize * 0.5f;
    const CCamera Cam = Host.FrameBox(Vec3(vCenter.x - fHalf, vCenter.y - fHalf, vMin.z),
      Vec3(vCenter.x + fHalf, vCenter.y + fHalf, max(vMax.z, fLightZ)), 30, 35);

    const int nPart = (nFrame - 1) / nPartFrames % 6;
    const int nPartFrame = (nFrame - 1) % nPartFrames;
    CDLight Lights[nMaxLights];
    int nCount = 0;
    if (nPart < 4)
    {
      static const int PartLights[4] = { 1, 2, 4, nMaxLights };
      nCount = min(PartLights[nPart], m_nLights);
      for (int i = 0; i < nCount; i++)
      {
        const float fAround = DEG2RAD(360.0f * i / nCount + 2.0f * (nFrame - 1));
        CDLight &Light = Lights[i];
        Light.m_Flags = DLF_POINT;
        Light.m_Origin = Vec3(vCenter.x + cosf(fAround) * fCircle, vCenter.y + sinf(fAround) * fCircle, fLightZ);
        Light.m_fRadius = Light.m_fEndRadius = fSize * 2;
        Light.m_Color = s_Colours[i % 8];
      }
    }
    else if (nPart == 4)
    {
      // Far away in the direction of the light, like the sun of a level.
      const float fAround = DEG2RAD(-90.0f + 3.0f * nPartFrame);
      const Vec3 vDir(cosf(fAround) * 0.7071f, sinf(fAround) * 0.7071f, 0.7071f);
      CDLight &Light = Lights[nCount++];
      Light.m_Flags = DLF_DIRECTIONAL;
      Light.m_Origin = vCenter + vDir * 10000.0f;
      Light.m_fRadius = Light.m_fEndRadius = 1e8f;
      Light.m_Color = CFColor(1, 0.9f, 0.75f, 1);
    }
    else if (m_pImage)
    {
      CDLight &Light = Lights[nCount++];
      Light.m_Flags = DLF_PROJECT;
      Light.m_Origin = Vec3(vCenter.x, vCenter.y, vMax.z + fSize);
      Light.m_fRadius = Light.m_fEndRadius = (vMax.z + fSize - vMin.z) * 2;
      Light.m_fLightFrustumAngle = 30;
      Light.m_ProjAngles = Vec3(0, 90, 3.0f * nPartFrame);
      Light.m_Color = CFColor(1, 1, 1, 1);
      // The light releases its image when it is destroyed.
      Light.m_pLightImage = m_pImage;
      m_pImage->AddRef();
    }

    SRendParams Params;
    Host.BeginScene(Cam, Params, Lights, nCount);
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
    Host.EndScene();

    // The lights' places.
    const float d = fSize * 0.04f;
    pRenderer->SetState(GS_NODEPTHTEST);
    pRenderer->SetWhiteTexture();
    for (int i = 0; i < nCount; i++)
    {
      const CDLight &Light = Lights[i];
      const CFColor &C = Light.m_Color;
      Vec3 P = Light.m_Origin;
      if (Light.m_Flags & DLF_DIRECTIONAL)
      {
        P = vCenter + GetNormalized(Light.m_Origin - vCenter) * fSize;
        pRenderer->DrawLineColor(P, C, vCenter + (P - vCenter) * 0.6f, C);
      }
      else if (Light.m_Flags & DLF_PROJECT)
        pRenderer->DrawLineColor(P, C, P - Vec3(0, 0, fSize * 0.5f), C);
      pRenderer->DrawLineColor(P - Vec3(d, 0, 0), C, P + Vec3(d, 0, 0), C);
      pRenderer->DrawLineColor(P - Vec3(0, d, 0), C, P + Vec3(0, d, 0), C);
      pRenderer->DrawLineColor(P - Vec3(0, 0, d), C, P + Vec3(0, 0, d), C);
    }
    pRenderer->SetState(GS_DEPTHWRITE);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_pFloor)
      Host.Get3DEngine()->ReleaseObject(m_pFloor);
    if (m_pObj)
      Host.Get3DEngine()->ReleaseObject(m_pObj);
    if (m_pImage)
      m_pImage->Release();
    m_pFloor = m_pObj = NULL;
    m_pImage = NULL;
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
    Params.vAmbientColor = Vec3(0.1f, 0.1f, 0.1f);
    Params.fDistance = GetLength(Cam.GetPos() - vPos);
    pObj->Render(Params, Vec3(0, 0, 0), 0);
  }

  IStatObj *m_pObj = NULL;
  //! The floor tile, if not the object itself.
  IStatObj *m_pFloor = NULL;
  //! The image of the projected light.
  ITexPic *m_pImage = NULL;
  int m_nLights = 6;
};

DEMO_PLUGIN(CDemoMultiLight)
