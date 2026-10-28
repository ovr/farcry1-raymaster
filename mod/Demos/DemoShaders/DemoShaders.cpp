//////////////////////////////////////////////////////////////////////
//
//	File: DemoShaders.cpp
//	Description: One static object of the game with a different material
//	in each cell of a grid.
//
//	The object is drawn six times, in a grid of 3 x 2, each copy with a
//	material of its own: an IMatInfo of the 3D engine (CreateMatInfo)
//	whose shader item comes from IRenderer::EF_LoadShaderItem with a
//	shader template, the way the engine makes the materials of its
//	objects (MakMatInfoFromMAT_ENTITY.h). SRendParams::pMaterial puts it
//	in place of the object's own materials: chunk id 0 takes the
//	material's shader item, id n its sub material n - 1, so the material
//	has sub materials with the same item. All materials get the same
//	textures (diffuse, bump map, cube map); each template uses the ones it
//	needs. The copies are in one 3D scene with one white point light above
//	and left of the camera and a grey ambient colour, turn about z by 2
//	degrees per frame and are labelled with their template (and opacity).
//	Frames 1, 30 and 60 show them turned by 0, 58 and 118 degrees.
//
//	Scene file keys (Demos\DemoShaders.ini):
//	  object = <file>         the object (default: the first .cgf under objects/)
//	  shaders = <t> [<o>], ...  up to 9 templates, row by row, each with an
//	                          optional opacity (default: TemplLightDiffuse,
//	                          TemplBumpDiffuse, TemplBumpSpec_HP, TemplReflCM,
//	                          TemplGlassCM 0.5, TemplDecalAdd)
//	  texture = <file>        the diffuse texture (default: the first
//	                          textures/*.dds that is no bump map)
//	  bump = <file>           the bump map (default: the first *_ddn.dds under
//	                          textures/)
//	  cubemap = <file>        the cube map, its +x face (default: the first
//	                          *_posx.dds under textures/)
//	  gloss = <file>          the gloss map (default: none)
//	  light = <r g b>         the light's colour (default: 0.9 0.9 0.9)
//	  lightpos = <x y z>      the light's position (default: above and left of
//	                          the camera); the grid is in the plane y = 0
//	  lightspec = <r g b>     the light's specular colour (default: 0 0 0, as a
//	                          CDLight is made)
//	  specular = <r g b>      the materials' specular colour (default: 0.8 0.8 0.8)
//	  shininess = <n>         the materials' specular power (default: 32)
//	  exec = <commands>       console commands before the materials are made,
//	                          separated by ';' or '|'
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>

static const char s_szDefaultShaders[] =
  "TemplLightDiffuse, TemplBumpDiffuse, TemplBumpSpec_HP, TemplReflCM, TemplGlassCM 0.5, TemplDecalAdd";
static const int nMaxCells = 9;
//! Sub materials of each material, for the chunk ids 1 to nSubMaterials.
static const int nSubMaterials = 16;

//! The items of a list separated by cDelim, without the spaces around them; empty ones are left out.
static std::vector<std::string> SplitList(const std::string &List, char cDelim)
{
  std::vector<std::string> Items;
  for (size_t nBegin = 0; nBegin <= List.size();)
  {
    size_t nEnd = List.find(cDelim, nBegin);
    if (nEnd == std::string::npos)
      nEnd = List.size();
    std::string Item = List.substr(nBegin, nEnd - nBegin);
    Item.erase(0, Item.find_first_not_of(" \t"));
    Item.erase(Item.find_last_not_of(" \t") + 1);
    if (!Item.empty())
      Items.push_back(Item);
    nBegin = nEnd + 1;
  }
  return Items;
}

//! Whether szText is part of Name, in any case.
static bool HasText(const std::string &Name, const char *szText)
{
  std::string Lower = Name;
  for (size_t i = 0; i < Lower.size(); i++)
    Lower[i] = (char)tolower((unsigned char)Lower[i]);
  return Lower.find(szText) != std::string::npos;
}

class CDemoShaders : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoShaders"; }
  virtual const char *GetDefaultFrames() const { return "1,30,60"; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    Host.ExecList(Scene.Get("exec"));

    m_pObj = Host.LoadObject("object");
    if (!m_pObj)
      return false;

    m_Texture = Scene.Get("texture");
    if (m_Texture.empty())
    {
      std::vector<std::string> Names = Host.FindFiles("textures/", "*.dds");
      for (size_t i = 0; i < Names.size() && m_Texture.empty(); i++)
      {
        if (!HasText(Names[i], "bump") && !HasText(Names[i], "_ddn"))
          m_Texture = Names[i];
      }
    }
    m_Bump = Scene.Has("bump") ? Scene.Get("bump") : Host.FindFirstFileDeep("textures/", "*_ddn.dds");
    m_CubeMap = Scene.Has("cubemap") ? Scene.Get("cubemap") : Host.FindFirstFileDeep("textures/", "*_posx.dds");
    Host.Log("DemoShaders: texture '%s', bump '%s', cube map '%s'", m_Texture.c_str(), m_Bump.c_str(), m_CubeMap.c_str());
    m_Gloss = Scene.Get("gloss");
    m_LightColor = ColorOf(Scene, "light", m_LightColor);
    m_LightPos = Scene.GetFloats("lightpos");
    m_LightSpecColor = ColorOf(Scene, "lightspec", m_LightSpecColor);
    m_LightMat.Front.m_Specular = ColorOf(Scene, "specular", CFColor(0.8f, 0.8f, 0.8f, 1));
    m_LightMat.Front.m_SpecShininess = Scene.GetFloat("shininess", 32);

    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont("Default") : NULL;

    std::vector<std::string> Shaders = SplitList(Scene.Get("shaders", s_szDefaultShaders), ',');
    for (size_t i = 0; i < Shaders.size() && (int)m_Cells.size() < nMaxCells; i++)
    {
      SCell Cell;
      char szTemplate[128] = "";
      sscanf(Shaders[i].c_str(), "%127s %f", szTemplate, &Cell.fOpacity);
      Cell.Template = szTemplate;
      MakeMaterial(Host, Cell, (int)m_Cells.size());
      m_Cells.push_back(Cell);
    }
    return !m_Cells.empty();
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();

    // The grid of the copies in the plane y = 0, x to the right, z up; each cell has room for the
    // object turned in any way about z, and for its label below it.
    const Vec3 vMin = m_pObj->GetBoxMin(), vMax = m_pObj->GetBoxMax();
    const float fRound = max(max(fabsf(vMin.x), fabsf(vMax.x)), max(fabsf(vMin.y), fabsf(vMax.y)));
    const float fHeight = max(vMax.z - vMin.z, 0.01f);
    const float fCellX = max(fRound * 2, fHeight) * 1.2f, fCellZ = fHeight * 1.5f;
    const int nCells = (int)m_Cells.size();
    const int nCols = min(nCells, 3), nRows = (nCells + 2) / 3;

    // A level camera looking along +y, far enough away for the whole grid.
    const float fFov = 40.0f;
    const float fRatio = (float)pRenderer->GetHeight() / max(pRenderer->GetWidth(), 1);
    const float fTanX = tanf(DEG2RAD(fFov) * 0.5f), fTanZ = tanf(DEG2RAD(fFov) * fRatio * 0.5f);
    const float fHalfX = fCellX * nCols * 0.5f, fHalfZ = fCellZ * nRows * 0.5f;
    const float fDist = max(fHalfX / fTanX, fHalfZ / fTanZ) * 1.05f + fRound;
    const Vec3 vEye(0, -fDist, 0);
    const CCamera Cam = Host.LookAt(vEye, Vec3(0, 0, 0), fFov);

    CDLight Light;
    Light.m_Flags = DLF_POINT;
    Light.m_Origin = m_LightPos.size() >= 3 ? Vec3(m_LightPos[0], m_LightPos[1], m_LightPos[2]) : vEye + Vec3(-fHalfX * 0.5f, fDist * 0.2f, fHalfZ * 1.5f);
    Light.m_fRadius = Light.m_fEndRadius = fDist * 10;
    Light.m_Color = m_LightColor;
    Light.m_SpecColor = m_LightSpecColor;

    SRendParams Base;
    Base.vAmbientColor = Vec3(0.25f, 0.25f, 0.25f);
    Host.BeginScene(Cam, Base, &Light, 1);
    const float fTurn = DEG2RAD(2.0f * (nFrame - 1));
    Matrix44 Mats[nMaxCells];
    Vec3 vCenters[nMaxCells];
    for (int i = 0; i < nCells; i++)
    {
      const int nCol = i % 3, nRow = i / 3;
      vCenters[i] = Vec3((nCol - (nCols - 1) * 0.5f) * fCellX, 0, ((nRows - 1) * 0.5f - nRow) * fCellZ);
      // The object's box from 0.4 of its height below the middle of the cell to 0.6 above.
      const Vec3 vPos = vCenters[i] + Vec3(0, 0, fHeight * 0.1f - (vMin.z + vMax.z) * 0.5f);
      // As in DemoObject: the turned axes in the rows, the position in the last row.
      Matrix44 &Mat = Mats[i];
      Mat.SetIdentity();
      float *m = Mat.GetData();
      m[0] = cosf(fTurn);
      m[1] = sinf(fTurn);
      m[4] = -sinf(fTurn);
      m[5] = cosf(fTurn);
      Mat.SetTranslationOLD(vPos);
      SRendParams Params(Base);
      Params.pMatrix = &Mat;
      Params.dwFObjFlags = FOB_TRANS_MASK;
      Params.pMaterial = m_Cells[i].pMat;
      Params.fDistance = GetLength(vPos - vEye);
      m_pObj->Render(Params, Vec3(0, 0, 0), 0);
    }
    Host.EndScene();

    // The labels, centred under the cells on the virtual 800 x 600 screen.
    if (!m_pFont)
      return;
    m_pFont->Reset();
    m_pFont->SetEffect("default");
    m_pFont->SetSameSize(false);
    m_pFont->SetCharWidthScale(1.0f);
    m_pFont->SetSize(vector2f(16, 16));
    m_pFont->SetColor(color4f(1, 1, 1, 1));
    // GetTextSize is in pixels (as for the SDK's menu), DrawString takes the virtual screen.
    const float fToVirtual = 800.0f / max(pRenderer->GetWidth(), 1);
    for (int i = 0; i < nCells; i++)
    {
      char szLabel[160];
      if (m_Cells[i].fOpacity != 1.0f)
        _snprintf(szLabel, sizeof(szLabel) - 1, "%s  opacity %g", m_Cells[i].Template.c_str(), m_Cells[i].fOpacity);
      else
        _snprintf(szLabel, sizeof(szLabel) - 1, "%s", m_Cells[i].Template.c_str());
      szLabel[sizeof(szLabel) - 1] = 0;
      const Vec3 vLabel = vCenters[i] - Vec3(0, 0, fHeight * 0.45f) - vEye;
      const float x = 400.0f + 400.0f * vLabel.x / (vLabel.y * fTanX);
      const float y = 300.0f - 300.0f * vLabel.z / (vLabel.y * fTanZ);
      m_pFont->DrawString(x - m_pFont->GetTextSize(szLabel).x * fToVirtual * 0.5f, y, szLabel);
    }
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    for (size_t i = 0; i < m_Cells.size(); i++)
    {
      if (m_Cells[i].pMat)
      {
        m_Cells[i].pMat->RemoveAllSubMtls();
        Host.Get3DEngine()->DeleteMatInfo(m_Cells[i].pMat);
      }
      if (m_Cells[i].pSubMat)
        Host.Get3DEngine()->DeleteMatInfo(m_Cells[i].pSubMat);
    }
    m_Cells.clear();
    if (m_pObj)
      Host.Get3DEngine()->ReleaseObject(m_pObj);
    m_pObj = NULL;
  }

private:
  struct SCell
  {
    std::string Template;
    float fOpacity = 1.0f;
    IMatInfo *pMat = NULL;
    IMatInfo *pSubMat = NULL;
  };

  //! The material of cell nCell: a shader item of the cell's template with the demo's textures, as
  //! CIndexedMesh__LoadMaterial makes it (MakMatInfoFromMAT_ENTITY.h).
  void MakeMaterial(CDemoHost &Host, SCell &Cell, int nCell)
  {
    SInputShaderResources Res;
    Res.m_LMaterial = &m_LightMat;
    Res.m_Opacity = Cell.fOpacity;
    Res.m_Textures[EFTT_DIFFUSE].m_Name = m_Texture.c_str();
    Res.m_Textures[EFTT_BUMP].m_Name = m_Bump.c_str();
    Res.m_Textures[EFTT_BUMP].m_TU.m_eTexType = (byte)eTT_Bumpmap;
    Res.m_Textures[EFTT_GLOSS].m_Name = m_Gloss.c_str();
    Res.m_Textures[EFTT_CUBEMAP].m_Name = m_CubeMap.c_str();
    Res.m_Textures[EFTT_REFLECTION].m_Name = m_CubeMap.c_str();
    Res.m_Textures[EFTT_REFLECTION].m_TU.m_eTexType = (byte)eTT_Cubemap;

    // A material name of its own for each cell: the renderer names the shader after it.
    char szName[32];
    _snprintf(szName, sizeof(szName) - 1, "DemoShaders%d", nCell);
    szName[sizeof(szName) - 1] = 0;
    SShaderItem Item = Host.GetRenderer()->EF_LoadShaderItem(szName, eSH_World, true, Cell.Template.c_str(), 0, &Res);
    IShader *pTemplate = Item.m_pShader ? Item.m_pShader->GetTemplate(-1) : NULL;
    Host.Log("DemoShaders: cell %d '%s' opacity %g: shader '%s', template '%s', sort %d, flags2 0x%x", nCell,
      Cell.Template.c_str(), Cell.fOpacity, Item.m_pShader ? Item.m_pShader->GetName() : "-",
      pTemplate ? pTemplate->GetName() : "-", pTemplate ? (int)pTemplate->GetSort() : -1, pTemplate ? pTemplate->GetFlags2() : 0);
    if (!Item.m_pShader)
      return;

    I3DEngine *p3DEngine = Host.Get3DEngine();
    Cell.pMat = p3DEngine->CreateMatInfo();
    Cell.pSubMat = p3DEngine->CreateMatInfo();
    if (!Cell.pMat || !Cell.pSubMat)
      return;
    Cell.pMat->SetShaderItem(Item);
    Cell.pSubMat->SetShaderItem(Item);
    for (int i = 0; i < nSubMaterials; i++)
      Cell.pMat->AddSubMtl(Cell.pSubMat);
  }

  IStatObj *m_pObj = NULL;
  IFFont *m_pFont = NULL;
  std::string m_Texture, m_Bump, m_CubeMap, m_Gloss;
  CFColor m_LightColor = CFColor(0.9f, 0.9f, 0.9f, 1);
  CFColor m_LightSpecColor = CFColor(0, 0, 0, 1);
  std::vector<float> m_LightPos;
  //! The lighting of the materials (white, specular 0.8 and shininess 32 by default).
  SLightMaterial m_LightMat;
  std::vector<SCell> m_Cells;

  //! The colour "r g b" of szKey, or Default.
  static CFColor ColorOf(const CSceneFile &Scene, const char *szKey, const CFColor &Default)
  {
    const std::vector<float> v = Scene.GetFloats(szKey);
    return v.size() >= 3 ? CFColor(v[0], v[1], v[2], 1) : Default;
  }
};

DEMO_PLUGIN(CDemoShaders)
