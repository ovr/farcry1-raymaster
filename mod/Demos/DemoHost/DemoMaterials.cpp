// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoMaterials.cpp
//	Description: Materials of a shader template (DemoMaterials.h).
//
//////////////////////////////////////////////////////////////////////

#include "DemoMaterials.h"

//! Sub materials of each material, for the chunk ids 1 to nSubMaterials.
static const int nSubMaterials = 16;

std::vector<std::string> SplitList(const std::string &List, char cDelim)
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

bool HasText(const std::string &Name, const char *szText)
{
  std::string Lower = Name;
  for (size_t i = 0; i < Lower.size(); i++)
    Lower[i] = (char)tolower((unsigned char)Lower[i]);
  return Lower.find(szText) != std::string::npos;
}

CFColor SceneColor(const CSceneFile &Scene, const char *szKey, const CFColor &Default)
{
  const std::vector<float> v = Scene.GetFloats(szKey);
  return v.size() >= 3 ? CFColor(v[0], v[1], v[2], 1) : Default;
}

void SDemoTextures::FromScene(CDemoHost &Host)
{
  const CSceneFile &Scene = Host.GetScene();
  Texture = Scene.Get("texture");
  if (Texture.empty())
  {
    std::vector<std::string> Names = Host.FindFiles("textures/", "*.dds");
    for (size_t i = 0; i < Names.size() && Texture.empty(); i++)
    {
      if (!HasText(Names[i], "bump") && !HasText(Names[i], "_ddn"))
        Texture = Names[i];
    }
  }
  Bump = Scene.Has("bump") ? Scene.Get("bump") : Host.FindFirstFileDeep("textures/", "*_ddn.dds");
  CubeMap = Scene.Has("cubemap") ? Scene.Get("cubemap") : Host.FindFirstFileDeep("textures/", "*_posx.dds");
  if (Scene.Has("cubeslots"))
    CubeSlots = Scene.GetFloats("cubeslots");
  Gloss = Scene.Get("gloss");
}

SShaderItem SDemoMaterial::Make(CDemoHost &Host, const char *szName, const char *szTemplate, float fOpacity,
  const SDemoTextures &Textures, SLightMaterial &Light)
{
  SInputShaderResources Res;
  Res.m_LMaterial = &Light;
  Res.m_Opacity = fOpacity;
  Res.m_Textures[EFTT_DIFFUSE].m_Name = Textures.Texture.c_str();
  Res.m_Textures[EFTT_BUMP].m_Name = Textures.Bump.c_str();
  Res.m_Textures[EFTT_BUMP].m_TU.m_eTexType = (byte)eTT_Bumpmap;
  Res.m_Textures[EFTT_GLOSS].m_Name = Textures.Gloss.c_str();
  for (size_t i = 0; i < Textures.CubeSlots.size(); i++)
  {
    if (Textures.CubeSlots[i] == EFTT_CUBEMAP)
      Res.m_Textures[EFTT_CUBEMAP].m_Name = Textures.CubeMap.c_str();
    else if (Textures.CubeSlots[i] == EFTT_REFLECTION)
    {
      Res.m_Textures[EFTT_REFLECTION].m_Name = Textures.CubeMap.c_str();
      Res.m_Textures[EFTT_REFLECTION].m_TU.m_eTexType = (byte)eTT_Cubemap;
    }
  }

  SShaderItem Item = Host.GetRenderer()->EF_LoadShaderItem(szName, eSH_World, true, szTemplate, 0, &Res);
  if (!Item.m_pShader)
    return Item;

  I3DEngine *p3DEngine = Host.Get3DEngine();
  pMat = p3DEngine->CreateMatInfo();
  pSubMat = p3DEngine->CreateMatInfo();
  if (!pMat || !pSubMat)
    return Item;
  pMat->SetShaderItem(Item);
  pSubMat->SetShaderItem(Item);
  for (int i = 0; i < nSubMaterials; i++)
    pMat->AddSubMtl(pSubMat);
  return Item;
}

void SDemoMaterial::Release(CDemoHost &Host)
{
  if (pMat)
  {
    pMat->RemoveAllSubMtls();
    Host.Get3DEngine()->DeleteMatInfo(pMat);
  }
  if (pSubMat)
    Host.Get3DEngine()->DeleteMatInfo(pSubMat);
  pMat = pSubMat = NULL;
}
