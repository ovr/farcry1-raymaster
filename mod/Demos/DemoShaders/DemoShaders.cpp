// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
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
//	  cubeslots = <n> ...     the texture slots given the cube map: 4
//	                          (EFTT_CUBEMAP, a plain texture name), 10
//	                          (EFTT_REFLECTION, of type eTT_Cubemap); default
//	                          both, "none" neither
//	  gloss = <file>          the gloss map (default: none)
//	  ambient = <r g b>       the ambient colour (default: 0.25 0.25 0.25)
//	  light = <r g b>         the light's colour (default: 0.9 0.9 0.9)
//	  lightpos = <x y z>      the light's position (default: above and left of
//	                          the camera); the grid is in the plane y = 0
//	  lightspec = <r g b>     the light's specular colour (default: 0 0 0, as a
//	                          CDLight is made)
//	  sun = 1                 the light is the sun (DLF_DIRECTIONAL | DLF_SUN),
//	                          from the direction of its position
//	  specular = <r g b>      the materials' specular colour (default: 0.8 0.8 0.8)
//	  shininess = <n>         the materials' specular power (default: 32)
//	  exec = <commands>       console commands before the materials are made,
//	                          separated by ';' or '|'
//
//////////////////////////////////////////////////////////////////////

#include "DemoMaterials.h"
#include <IFont.h>

static const char s_szDefaultShaders[] =
  "TemplLightDiffuse, TemplBumpDiffuse, TemplBumpSpec_HP, TemplReflCM, TemplGlassCM 0.5, TemplDecalAdd";
static const int nMaxCells = 9;

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

    m_Textures.FromScene(Host);
    Host.Log("DemoShaders: texture '%s', bump '%s', cube map '%s'", m_Textures.Texture.c_str(), m_Textures.Bump.c_str(),
      m_Textures.CubeMap.c_str());
    m_Ambient = SceneColor(Scene, "ambient", m_Ambient);
    m_LightColor = SceneColor(Scene, "light", m_LightColor);
    m_LightPos = Scene.GetFloats("lightpos");
    m_LightSpecColor = SceneColor(Scene, "lightspec", m_LightSpecColor);
    m_bSun = Scene.GetFloat("sun", 0) != 0;
    m_LightMat.Front.m_Specular = SceneColor(Scene, "specular", CFColor(0.8f, 0.8f, 0.8f, 1));
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
    if (m_bSun)
    {
      // The sun: far away in the direction of the light's position from the grid's centre.
      Light.m_Flags = DLF_DIRECTIONAL | DLF_SUN;
      Light.m_Origin = GetNormalized(Light.m_Origin) * 10000.0f;
      Light.m_fRadius = Light.m_fEndRadius = 100000.0f;
    }
    Light.m_Color = m_LightColor;
    Light.m_SpecColor = m_LightSpecColor;

    SRendParams Base;
    Base.vAmbientColor = Vec3(m_Ambient.r, m_Ambient.g, m_Ambient.b);
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
      Params.pMaterial = m_Cells[i].Mat.pMat;
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
      m_Cells[i].Mat.Release(Host);
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
    SDemoMaterial Mat;
  };

  //! The material of cell nCell (DemoMaterials.h), with a name of its own: the renderer names the
  //! shader after it.
  void MakeMaterial(CDemoHost &Host, SCell &Cell, int nCell)
  {
    char szName[32];
    _snprintf(szName, sizeof(szName) - 1, "DemoShaders%d", nCell);
    szName[sizeof(szName) - 1] = 0;
    SShaderItem Item = Cell.Mat.Make(Host, szName, Cell.Template.c_str(), Cell.fOpacity, m_Textures, m_LightMat);
    IShader *pTemplate = Item.m_pShader ? Item.m_pShader->GetTemplate(-1) : NULL;
    Host.Log("DemoShaders: cell %d '%s' opacity %g: shader '%s', template '%s', sort %d, flags2 0x%x", nCell,
      Cell.Template.c_str(), Cell.fOpacity, Item.m_pShader ? Item.m_pShader->GetName() : "-",
      pTemplate ? pTemplate->GetName() : "-", pTemplate ? (int)pTemplate->GetSort() : -1, pTemplate ? pTemplate->GetFlags2() : 0);
  }

  IStatObj *m_pObj = NULL;
  IFFont *m_pFont = NULL;
  SDemoTextures m_Textures;
  CFColor m_Ambient = CFColor(0.25f, 0.25f, 0.25f, 1);
  CFColor m_LightColor = CFColor(0.9f, 0.9f, 0.9f, 1);
  CFColor m_LightSpecColor = CFColor(0, 0, 0, 1);
  std::vector<float> m_LightPos;
  bool m_bSun = false;
  //! The lighting of the materials (white, specular 0.8 and shininess 32 by default).
  SLightMaterial m_LightMat;
  std::vector<SCell> m_Cells;
};

DEMO_PLUGIN(CDemoShaders)
