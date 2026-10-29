// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoCharacters.cpp
//	Description: Animated characters of the game side by side, one of
//	them holding a weapon.
//
//	The characters are made by the animation system (ICryCharManager::
//	MakeCharacter), stand in a row along x facing the camera and each
//	plays one animation from the start of the demo. A weapon object
//	(I3DEngine::MakeObject) is attached to a bone of one of them with
//	ICryCharInstance::AttachObjectToBone, the way the game binds its third
//	person weapon models to "weapon_bone", and is drawn with it (the
//	character's SRendParams with FOB_TRANS_MASK). All of them are drawn in
//	one scene like in DemoAnimation (Update, Draw between EF_StartEf and
//	EF_EndEf3D, one point light, a grey ambient colour); the fixed camera
//	frames the union of their boxes. Frames 25 and 50 are 0.48 s and
//	0.98 s after frame 1 in the animations.
//
//	Scene file keys (Demos\DemoCharacters.ini):
//	  characters = <file>, <file>  the characters, left to right (default:
//	                       the console variable mp_model, then the first
//	                       models under objects/characters/ that have an
//	                       animation list (.cal) and the bone below)
//	  count = <n>          the number of default characters (default 4)
//	  animation = <name>   the animation of every character (default: its
//	                       first animation with "idle" in the name, else its
//	                       first one)
//	  animation1 = <name>  the animation of the first character, and so on
//	  weapon = <file>      the weapon (default: the first *_bind.cgf, a third
//	                       person weapon model, under objects/weapons/)
//	  bone = <name>        the bone it is attached to (default weapon_bone)
//	  armed = <n>          the character holding it, from 1 (default: the
//	                       first one with the bone)
//	  yaw = <degrees>      where the camera looks from, around z (default 0:
//	                       from -y, the side the characters face)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <ICryAnimation.h>
#include <CryCharAnimationParams.h>
#include <ctype.h>

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

//! Whether two file names name the same file (case and slashes ignored).
static bool SameFile(const std::string &A, const std::string &B)
{
  if (A.size() != B.size())
    return false;
  for (size_t i = 0; i < A.size(); i++)
  {
    char a = (char)tolower((unsigned char)A[i]), b = (char)tolower((unsigned char)B[i]);
    if (a == '\\')
      a = '/';
    if (b == '\\')
      b = '/';
    if (a != b)
      return false;
  }
  return true;
}

class CDemoCharacters : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoCharacters"; }
  virtual const char *GetDefaultFrames() const { return "1,25,50"; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    m_pManager = Host.GetSystem()->GetIAnimationSystem();
    if (!m_pManager)
      return false;
    m_Bone = Scene.Get("bone", "weapon_bone");

    if (Scene.Has("characters"))
    {
      std::vector<std::string> Names = SplitList(Scene.Get("characters"));
      for (size_t i = 0; i < Names.size(); i++)
        AddCharacter(Host, Names[i], false);
    }
    else
    {
      const int nCount = Scene.GetInt("count", 4);
      std::string Model = Host.GetCVarString("mp_model");
      if (!Model.empty())
        AddCharacter(Host, Model, false);
      FindCharacters(Host, "objects/characters/", nCount);
    }
    if (m_Chars.empty())
    {
      Host.Log("DemoCharacters: no character made");
      return false;
    }

    for (size_t i = 0; i < m_Chars.size(); i++)
      StartAnimation(Host, (int)i);
    AttachWeapon(Host);

    // The characters stand in a row along x, centred on the origin, their boxes apart by half a box.
    float fWidth = 0;
    for (size_t i = 0; i < m_Chars.size(); i++)
    {
      SCharacter &Char = m_Chars[i];
      Char.pChar->Update();
      Char.pChar->GetBBox(Char.vMin, Char.vMax);
      fWidth = max(fWidth, Char.vMax.x - Char.vMin.x);
    }
    const float fSpacing = max(fWidth * 1.5f, 0.5f);
    for (size_t i = 0; i < m_Chars.size(); i++)
    {
      SCharacter &Char = m_Chars[i];
      Char.vPos = Vec3(((float)i - (float)(m_Chars.size() - 1) * 0.5f) * fSpacing, 0, 0);
      const Vec3 vMin = Char.vMin + Char.vPos, vMax = Char.vMax + Char.vPos;
      m_vMin = i ? Vec3(min(m_vMin.x, vMin.x), min(m_vMin.y, vMin.y), min(m_vMin.z, vMin.z)) : vMin;
      m_vMax = i ? Vec3(max(m_vMax.x, vMax.x), max(m_vMax.y, vMax.y), max(m_vMax.z, vMax.z)) : vMax;
      Host.Log("DemoCharacters: %d: %s at x %g, box (%g %g %g) - (%g %g %g)", (int)i + 1, Char.Name.c_str(), Char.vPos.x,
        Char.vMin.x, Char.vMin.y, Char.vMin.z, Char.vMax.x, Char.vMax.y, Char.vMax.z);
    }
    Host.Log("DemoCharacters: all in (%g %g %g) - (%g %g %g)", m_vMin.x, m_vMin.y, m_vMin.z, m_vMax.x, m_vMax.y, m_vMax.z);
    return true;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    const CCamera Cam = Host.FrameBox(m_vMin, m_vMax, Host.GetScene().GetFloat("yaw", 0), 10);
    const float fSize = GetLength(m_vMax - m_vMin);

    CDLight Light;
    Light.m_Flags = DLF_POINT;
    Light.m_Origin = Cam.GetPos() + Vec3(fSize, fSize, fSize);
    Light.m_fRadius = Light.m_fEndRadius = fSize * 20;
    Light.m_Color = CFColor(0.8f, 0.8f, 0.8f, 1);

    SRendParams Params;
    Params.vAmbientColor = Vec3(0.25f, 0.25f, 0.25f);
    Host.BeginScene(Cam, Params, &Light, 1);
    // One set of parameters per character, alive until the scene is drawn. The weapon, a static object
    // drawn with them at its bone, takes FOB_TRANS_MASK like any one placed by a matrix (DemoObject).
    std::vector<SRendParams> CharParams(m_Chars.size(), Params);
    for (size_t i = 0; i < m_Chars.size(); i++)
    {
      SCharacter &Char = m_Chars[i];
      CharParams[i].vPos = Char.vPos;
      CharParams[i].dwFObjFlags = FOB_TRANS_MASK;
      CharParams[i].fDistance = GetLength(Cam.GetPos() - Char.vPos);
      Char.pChar->Update();
      Char.pChar->Draw(CharParams[i], Vec3(0, 0, 0));
    }
    Host.EndScene();
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    for (size_t i = 0; i < m_Chars.size(); i++)
    {
      if (m_Chars[i].bArmed)
        m_Chars[i].pChar->AttachObjectToBone(NULL, m_Bone.c_str());
      m_pManager->RemoveCharacter(m_Chars[i].pChar);
    }
    m_Chars.clear();
    if (m_pWeapon)
      Host.Get3DEngine()->ReleaseObject(m_pWeapon);
    m_pWeapon = NULL;
  }

private:
  struct SCharacter
  {
    std::string Name;
    ICryCharInstance *pChar = NULL;
    bool bArmed = false;
    Vec3 vPos = Vec3(0, 0, 0);
    //! The box around its origin, after the first update.
    Vec3 vMin = Vec3(0, 0, 0), vMax = Vec3(0, 0, 0);
  };

  //! Makes the character Name; with bNeedBone, keeps it only if it has the bone. False if not kept.
  bool AddCharacter(CDemoHost &Host, const std::string &Name, bool bNeedBone)
  {
    for (size_t i = 0; i < m_Chars.size(); i++)
    {
      if (SameFile(m_Chars[i].Name, Name))
        return false;
    }
    ICryCharInstance *pChar = m_pManager->MakeCharacter(Name.c_str());
    if (!pChar)
    {
      Host.Log("DemoCharacters: character '%s' not made", Name.c_str());
      return false;
    }
    if (bNeedBone && !pChar->GetBoneByName(m_Bone.c_str()))
    {
      Host.Log("DemoCharacters: %s has no bone %s, left out", Name.c_str(), m_Bone.c_str());
      m_pManager->RemoveCharacter(pChar);
      return false;
    }
    SCharacter Char;
    Char.Name = Name;
    Char.pChar = pChar;
    m_Chars.push_back(Char);
    return true;
  }

  //! Adds the models in szDir and its sub folders (sorted, depth first) that have an animation list
  //! (<model>.cal) and the bone, until there are nCount characters.
  void FindCharacters(CDemoHost &Host, const char *szDir, int nCount)
  {
    std::vector<std::string> Models = Host.FindFiles(szDir, "*.cgf");
    std::vector<std::string> Lists = Host.FindFiles(szDir, "*.cal");
    for (size_t i = 0; i < Models.size() && (int)m_Chars.size() < nCount; i++)
    {
      const std::string List = Models[i].substr(0, Models[i].size() - 4) + ".cal";
      bool bList = false;
      for (size_t j = 0; j < Lists.size() && !bList; j++)
        bList = SameFile(Lists[j], List);
      if (bList)
        AddCharacter(Host, Models[i], true);
    }
    std::vector<std::string> Dirs = Host.FindDirs(szDir);
    for (size_t i = 0; i < Dirs.size() && (int)m_Chars.size() < nCount; i++)
      FindCharacters(Host, Dirs[i].c_str(), nCount);
  }

  //! Starts the animation of character n (from 0): a scene value, else its first animation with
  //! "idle" in the name, else its first one.
  void StartAnimation(CDemoHost &Host, int n)
  {
    SCharacter &Char = m_Chars[n];
    ICryAnimationSet *pAnims = Char.pChar->GetModel() ? Char.pChar->GetModel()->GetAnimationSet() : NULL;
    const int nAnims = pAnims ? pAnims->Count() : 0;
    char szKey[32];
    sprintf(szKey, "animation%d", n + 1);
    std::string Anim = Host.GetScene().Get(szKey, Host.GetScene().Get("animation").c_str());
    for (int i = 0; i < nAnims && Anim.empty(); i++)
    {
      std::string Name = pAnims->GetName(i) ? pAnims->GetName(i) : "";
      for (size_t c = 0; c < Name.size(); c++)
        Name[c] = (char)tolower((unsigned char)Name[c]);
      if (Name.find("idle") != std::string::npos)
        Anim = pAnims->GetName(i);
    }
    if (Anim.empty() && nAnims > 0)
      Anim = pAnims->GetName(0);
    CryCharAnimationParams Params(0, 0, 0, 0);
    const bool bStarted = !Anim.empty() && Char.pChar->StartAnimation(Anim.c_str(), Params);
    const int nId = pAnims && !Anim.empty() ? pAnims->Find(Anim.c_str()) : -1;
    Host.Log("DemoCharacters: %d: %s, %d animations, '%s' (%g s) %s", n + 1, Char.Name.c_str(), nAnims, Anim.c_str(),
      nId >= 0 ? pAnims->GetLength(nId) : 0.0f, bStarted ? "started" : "NOT started");
  }

  //! Attaches the weapon to the bone of the armed character.
  void AttachWeapon(CDemoHost &Host)
  {
    std::string Name = Host.GetScene().Has("weapon") ? Host.GetScene().Get("weapon")
                                                     : Host.FindFirstFileDeep("objects/weapons/", "*_bind.cgf");
    m_pWeapon = Name.empty() ? NULL : Host.Get3DEngine()->MakeObject(Name.c_str());
    if (!m_pWeapon)
    {
      Host.Log("DemoCharacters: weapon '%s' not loaded", Name.c_str());
      return;
    }
    int nArmed = Host.GetScene().GetInt("armed", 0) - 1;
    for (size_t i = 0; i < m_Chars.size() && nArmed < 0; i++)
    {
      if (m_Chars[i].pChar->GetBoneByName(m_Bone.c_str()))
        nArmed = (int)i;
    }
    if (nArmed < 0 || nArmed >= (int)m_Chars.size())
    {
      Host.Log("DemoCharacters: no character to hold %s (no bone %s)", Name.c_str(), m_Bone.c_str());
      return;
    }
    SCharacter &Char = m_Chars[nArmed];
    ICryCharInstance::ObjectBindingHandle hBinding = Char.pChar->AttachObjectToBone(m_pWeapon, m_Bone.c_str());
    Char.bArmed = hBinding != ICryCharInstance::nInvalidObjectBindingHandle;
    Host.Log("DemoCharacters: %d: %s holds %s at %s: %s", nArmed + 1, Char.Name.c_str(), Name.c_str(), m_Bone.c_str(),
      Char.bArmed ? "attached" : "NOT attached");
  }

  ICryCharManager *m_pManager = NULL;
  std::vector<SCharacter> m_Chars;
  IStatObj *m_pWeapon = NULL;
  std::string m_Bone;
  Vec3 m_vMin = Vec3(0, 0, 0), m_vMax = Vec3(0, 0, 0);
};

DEMO_PLUGIN(CDemoCharacters)
