// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoWeapons.cpp
//	Description: The weapon models of the game, one at a time and then
//	all of them in a row, each labelled with its file name.
//
//	Each weapon is a static object (I3DEngine::MakeObject, as the game
//	loads its third person weapon models) drawn like in DemoObject,
//	through the renderer's 3D path with one white point light and a grey
//	ambient colour, placed by a matrix in SRendParams::pMatrix with
//	FOB_TRANS_MASK (built here). The matrix turns it by its box: its
//	longest side lies along x, its shortest one points at the camera.
//
//	Weapon n (from 0) fills the view alone in frames 30n+1 to 30n+30,
//	turning about z through its middle by 12 degrees per frame: frames
//	30n+1, 30n+11 and 30n+21 show it from the side, at 120 and at 240
//	degrees. From frame 30N+1 on (N weapons) all of them stand upright
//	side by side, their longest sides along z, each turning about it by
//	3 degrees per frame: frames 30N+1 and 30N+21 show them from the side
//	and at 60 degrees. The default frames are those of 8 weapons (the
//	row from frame 241); with another number, pass --frames. The file
//	name of the weapon is written at the top left, the one of each weapon
//	of the row under it (IFFont::DrawString, the font Default, on the
//	virtual 800 x 600 screen).
//
//	Scene file keys (Demos\DemoWeapons.ini):
//	  weapons = <file>, <file>   the weapons (default: the first
//	                       *_bind.cgf, a third person weapon model, of
//	                       each folder under objects/weapons/)
//	  count = <n>          the number of default weapons (default 8)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>

//! Frames of one weapon alone.
static const int FRAMES_PER_WEAPON = 30;
//! The field of view of the camera, across the screen.
static const float CAMERA_FOV = 45.0f;

//! The comma separated items of List, without the spaces around them.
static std::vector<std::string> SplitList(const std::string &List)
{
  std::vector<std::string> Items;
  for (size_t nBegin = 0; nBegin < List.size();)
  {
    size_t nEnd = List.find(',', nBegin);
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

class CDemoWeapons : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoWeapons"; }
  virtual const char *GetDefaultFrames() const
  {
    return "1,11,21,31,41,51,61,71,81,91,101,111,121,131,141,151,161,171,181,191,201,211,221,231,241,261";
  }

  virtual bool Init(CDemoHost &Host)
  {
    std::vector<std::string> Names;
    if (Host.GetScene().Has("weapons"))
      Names = SplitList(Host.GetScene().Get("weapons"));
    else
    {
      const int nCount = Host.GetScene().GetInt("count", 8);
      std::vector<std::string> Dirs = Host.FindDirs("objects/weapons/");
      for (size_t i = 0; i < Dirs.size() && (int)Names.size() < nCount; i++)
      {
        std::vector<std::string> Files = Host.FindFiles(Dirs[i].c_str(), "*_bind.cgf");
        if (!Files.empty())
          Names.push_back(Files[0]);
      }
    }
    for (size_t i = 0; i < Names.size(); i++)
    {
      SWeapon Weapon;
      Weapon.Name = Names[i];
      Weapon.pObj = Host.Get3DEngine()->MakeObject(Names[i].c_str());
      if (!Weapon.pObj)
      {
        Host.Log("DemoWeapons: '%s' not loaded", Names[i].c_str());
        continue;
      }
      const Vec3 vMin = Weapon.pObj->GetBoxMin(), vMax = Weapon.pObj->GetBoxMax();
      const float Min[3] = { vMin.x, vMin.y, vMin.z }, Max[3] = { vMax.x, vMax.y, vMax.z };
      for (int a = 0; a < 3; a++)
      {
        Weapon.fMid[a] = (Min[a] + Max[a]) * 0.5f;
        Weapon.fSize[a] = Max[a] - Min[a];
      }
      // The sides of the box from the longest to the shortest.
      int *n = Weapon.nSides;
      n[0] = 0, n[1] = 1, n[2] = 2;
      for (int a = 0; a < 2; a++)
      {
        for (int b = a + 1; b < 3; b++)
        {
          if (Weapon.fSize[n[b]] > Weapon.fSize[n[a]])
          {
            const int t = n[a];
            n[a] = n[b];
            n[b] = t;
          }
        }
      }
      size_t nSlash = Names[i].find_last_of("/\\");
      Weapon.Label = nSlash == std::string::npos ? Names[i] : Names[i].substr(nSlash + 1);
      Host.Log("DemoWeapons: %d: %s, box (%g %g %g) - (%g %g %g)", (int)m_Weapons.size() + 1, Names[i].c_str(), vMin.x,
        vMin.y, vMin.z, vMax.x, vMax.y, vMax.z);
      m_Weapons.push_back(Weapon);
    }
    if (m_Weapons.empty())
    {
      Host.Log("DemoWeapons: no weapon");
      return false;
    }
    m_Mats.resize(m_Weapons.size());
    Host.Log("DemoWeapons: %d weapons, the row from frame %d", (int)m_Weapons.size(),
      (int)m_Weapons.size() * FRAMES_PER_WEAPON + 1);

    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont("Default") : NULL;
    if (!m_pFont)
      Host.Log("DemoWeapons: no font Default, no labels");
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    const int nWeapon = (nFrame - 1) / FRAMES_PER_WEAPON;
    if (nWeapon < (int)m_Weapons.size())
      DrawOne(Host, nWeapon, (nFrame - 1) % FRAMES_PER_WEAPON);
    else
      DrawRow(Host, nFrame - 1 - (int)m_Weapons.size() * FRAMES_PER_WEAPON);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    for (size_t i = 0; i < m_Weapons.size(); i++)
      Host.Get3DEngine()->ReleaseObject(m_Weapons[i].pObj);
    m_Weapons.clear();
  }

private:
  struct SWeapon
  {
    std::string Name;
    //! The file name without its folder.
    std::string Label;
    IStatObj *pObj = NULL;
    //! The middle and the size of its box.
    float fMid[3] = { 0, 0, 0 };
    float fSize[3] = { 0, 0, 0 };
    //! The axes of the box from the longest side to the shortest.
    int nSides[3] = { 0, 1, 2 };
  };

  //! A camera at fPitch degrees above the front (-y) looking at vTarget, so far away that a box of
  //! the half sizes vHalf around vTarget fits into the view with a margin.
  CCamera FitCamera(CDemoHost &Host, const Vec3 &vTarget, const Vec3 &vHalf, float fPitch)
  {
    float fTanX, fTanY;
    GetTangents(Host, fTanX, fTanY);
    const float fPitchRad = DEG2RAD(fPitch);
    const float fHalfHeight = vHalf.z * cosf(fPitchRad) + vHalf.y * sinf(fPitchRad);
    const float fDist = max(vHalf.x / (fTanX * 0.85f), fHalfHeight / (fTanY * 0.8f)) + vHalf.y;
    const Vec3 vDir(0, -cosf(fPitchRad), sinf(fPitchRad));
    return Host.LookAt(vTarget + vDir * fDist, vTarget, CAMERA_FOV);
  }

  //! The tangents of half the angles of view: the vertical angle is the camera's field of view times
  //! height / width (CCamera), the projection keeps the aspect of the screen.
  void GetTangents(CDemoHost &Host, float &fTanX, float &fTanY)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const float fAspect = (float)max(pRenderer->GetWidth(), 1) / max(pRenderer->GetHeight(), 1);
    fTanY = tanf(DEG2RAD(CAMERA_FOV) / fAspect * 0.5f);
    fTanX = fTanY * fAspect;
  }

  //! Fills Mat for weapon W: its middle at vPos, its longest side along z if bUpright, else along x,
  //! its shortest one along y, then turned by fTurn degrees about z. Matrix44 takes a row vector
  //! (v * M): row a is where axis a of the object goes, the position is the last row.
  void PlaceWeapon(Matrix44 &Mat, const SWeapon &W, bool bUpright, float fTurn, const Vec3 &vPos)
  {
    float Rows[3][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
    Rows[W.nSides[0]][bUpright ? 2 : 0] = 1;
    Rows[W.nSides[1]][bUpright ? 0 : 2] = 1;
    Rows[W.nSides[2]][1] = 1;
    // A turn, not a mirror image: the longest side is flipped when the axes swap places oddly.
    const float fDet = Rows[0][0] * (Rows[1][1] * Rows[2][2] - Rows[1][2] * Rows[2][1]) -
                       Rows[0][1] * (Rows[1][0] * Rows[2][2] - Rows[1][2] * Rows[2][0]) +
                       Rows[0][2] * (Rows[1][0] * Rows[2][1] - Rows[1][1] * Rows[2][0]);
    if (fDet < 0)
    {
      for (int c = 0; c < 3; c++)
        Rows[W.nSides[0]][c] = -Rows[W.nSides[0]][c];
    }
    const float fCos = cosf(DEG2RAD(fTurn)), fSin = sinf(DEG2RAD(fTurn));
    Mat.SetIdentity();
    float *m = Mat.GetData();
    for (int r = 0; r < 3; r++)
    {
      m[r * 4 + 0] = Rows[r][0] * fCos - Rows[r][1] * fSin;
      m[r * 4 + 1] = Rows[r][0] * fSin + Rows[r][1] * fCos;
      m[r * 4 + 2] = Rows[r][2];
    }
    float Pos[3] = { vPos.x, vPos.y, vPos.z };
    for (int c = 0; c < 3; c++)
      Pos[c] -= W.fMid[0] * m[c] + W.fMid[1] * m[4 + c] + W.fMid[2] * m[8 + c];
    Mat.SetTranslationOLD(Vec3(Pos[0], Pos[1], Pos[2]));
  }

  //! Weapon n alone, nStep frames after it came.
  void DrawOne(CDemoHost &Host, int n, int nStep)
  {
    const SWeapon &W = m_Weapons[n];
    // Turning about z, it stays inside a cylinder around its middle.
    const float fRound = sqrtf(W.fSize[W.nSides[0]] * W.fSize[W.nSides[0]] + W.fSize[W.nSides[2]] * W.fSize[W.nSides[2]]) * 0.5f;
    const Vec3 vHalf(fRound, fRound, W.fSize[W.nSides[1]] * 0.5f);
    const CCamera Cam = FitCamera(Host, Vec3(0, 0, 0), vHalf, 20);
    const float fRadius = GetLength(vHalf);

    CDLight Light;
    Light.m_Flags = DLF_POINT;
    Light.m_Origin = Cam.GetPos() + Vec3(fRadius * 2, 0, fRadius * 2);
    Light.m_fRadius = Light.m_fEndRadius = fRadius * 20;
    Light.m_Color = CFColor(0.8f, 0.8f, 0.8f, 1);

    SRendParams Params;
    PlaceWeapon(m_Mats[n], W, false, 12.0f * nStep, Vec3(0, 0, 0));
    Params.pMatrix = &m_Mats[n];
    Params.dwFObjFlags = FOB_TRANS_MASK;
    Params.vAmbientColor = Vec3(0.3f, 0.3f, 0.3f);
    Params.fDistance = GetLength(Cam.GetPos());
    Host.RenderObject(Cam, W.pObj, Params, &Light, 1);

    if (m_pFont)
    {
      char szCount[32];
      sprintf(szCount, "%d / %d", n + 1, (int)m_Weapons.size());
      SetFont(20);
      m_pFont->DrawString(20, 16, W.Name.c_str());
      SetFont(14);
      m_pFont->DrawString(20, 42, szCount);
    }
  }

  //! All weapons upright side by side, nStep frames after the row came.
  void DrawRow(CDemoHost &Host, int nStep)
  {
    // Each turns inside a cylinder as wide as the diagonal of its two shorter sides.
    float fWidth = 0, fHeight = 0;
    for (size_t i = 0; i < m_Weapons.size(); i++)
    {
      const SWeapon &W = m_Weapons[i];
      const float fMid = W.fSize[W.nSides[1]], fShort = W.fSize[W.nSides[2]];
      fWidth = max(fWidth, sqrtf(fMid * fMid + fShort * fShort));
      fHeight = max(fHeight, W.fSize[W.nSides[0]]);
    }
    const float fSpacing = fWidth * 1.15f;
    const float fFirst = -(float)(m_Weapons.size() - 1) * 0.5f * fSpacing;
    const Vec3 vHalf(-fFirst + fWidth * 0.5f, fWidth * 0.5f, fHeight * 0.5f);
    const Vec3 vTarget(0, 0, fHeight * 0.5f);
    const CCamera Cam = FitCamera(Host, vTarget, vHalf, 10);
    const float fRadius = GetLength(vHalf);

    CDLight Light;
    Light.m_Flags = DLF_POINT;
    Light.m_Origin = Cam.GetPos() + Vec3(fRadius * 2, 0, fRadius * 2);
    Light.m_fRadius = Light.m_fEndRadius = fRadius * 20;
    Light.m_Color = CFColor(0.8f, 0.8f, 0.8f, 1);

    // The weapons stand on z = 0. One set of parameters per weapon, alive until the scene is drawn.
    SRendParams Params;
    Params.dwFObjFlags = FOB_TRANS_MASK;
    Params.vAmbientColor = Vec3(0.3f, 0.3f, 0.3f);
    Host.BeginScene(Cam, Params, &Light, 1);
    std::vector<SRendParams> WeaponParams(m_Weapons.size(), Params);
    for (size_t i = 0; i < m_Weapons.size(); i++)
    {
      const SWeapon &W = m_Weapons[i];
      const Vec3 vPos(fFirst + (float)i * fSpacing, 0, W.fSize[W.nSides[0]] * 0.5f);
      PlaceWeapon(m_Mats[i], W, true, 3.0f * nStep, vPos);
      WeaponParams[i].pMatrix = &m_Mats[i];
      WeaponParams[i].fDistance = GetLength(Cam.GetPos() - vPos);
      W.pObj->Render(WeaponParams[i], Vec3(0, 0, 0), 0);
    }
    Host.EndScene();

    if (!m_pFont)
      return;
    // The labels under the weapons, every other one a line lower.
    float fTanX, fTanY;
    GetTangents(Host, fTanX, fTanY);
    const Vec3 vEye = Cam.GetPos();
    const Vec3 vFront = GetNormalized(vTarget - vEye);
    const Vec3 vRight = GetNormalized(vFront.Cross(Vec3(0, 0, 1)));
    const Vec3 vUp = vRight.Cross(vFront);
    // GetTextSize is in pixels (as for the SDK's menu), DrawString takes the virtual screen.
    const float fToVirtual = 800.0f / max(Host.GetRenderer()->GetWidth(), 1);
    SetFont(12);
    for (size_t i = 0; i < m_Weapons.size(); i++)
    {
      const Vec3 vToFoot = Vec3(fFirst + (float)i * fSpacing, 0, 0) - vEye;
      const float fDepth = vToFoot.Dot(vFront);
      const float x = 400 + 400 * vToFoot.Dot(vRight) / (fDepth * fTanX);
      const float y = 300 - 300 * vToFoot.Dot(vUp) / (fDepth * fTanY);
      const float fLabelWidth = m_pFont->GetTextSize(m_Weapons[i].Label.c_str()).x * fToVirtual;
      m_pFont->DrawString(x - fLabelWidth * 0.5f, y + 6 + (float)(i % 2) * 16, m_Weapons[i].Label.c_str());
    }
    SetFont(20);
    m_pFont->DrawString(20, 16, "All weapons");
  }

  //! Resets the font to white letters of fSize.
  void SetFont(float fSize)
  {
    m_pFont->Reset();
    m_pFont->SetEffect("default");
    m_pFont->SetSameSize(false);
    m_pFont->SetCharWidthScale(1.0f);
    m_pFont->SetSize(vector2f(fSize, fSize));
    m_pFont->SetColor(color4f(1, 1, 1, 1));
  }

  std::vector<SWeapon> m_Weapons;
  //! One matrix per weapon, alive until the scene is drawn.
  std::vector<Matrix44> m_Mats;
  IFFont *m_pFont = NULL;
};

DEMO_PLUGIN(CDemoWeapons)
