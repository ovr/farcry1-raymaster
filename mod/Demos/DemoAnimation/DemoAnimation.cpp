// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: DemoAnimation.cpp
//	Description: An animated character of the game.
//
//	The character is made by the animation system (ICryCharManager::
//	MakeCharacter), plays one animation from the start of the demo and is
//	drawn the way the game's menu draws its player model (Update, Draw
//	between EF_StartEf and EF_EndEf3D, one point light, a grey ambient
//	colour). With the fixed time step of 0.02 s, frames 1, 11, 21, 31 and
//	41 are 0.2 s apart in the animation.
//
//	Scene file keys (Demos\DemoAnimation.ini):
//	  character = <file>   the character (default: the console variable
//	                       mp_model, the model the game's menu shows)
//	  animation = <name>   the animation (default: the first of the model)
//	  yaw = <degrees>      where the camera looks from, around z (default 0:
//	                       from -y, the side the characters face)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <ICryAnimation.h>
#include <CryCharAnimationParams.h>

class CDemoAnimation : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoAnimation"; }
  virtual const char *GetDefaultFrames() const { return "1,11,21,31,41"; }

  virtual bool Init(CDemoHost &Host)
  {
    std::string Name = Host.GetScene().Get("character", Host.GetCVarString("mp_model").c_str());
    ICryCharManager *pManager = Host.GetSystem()->GetIAnimationSystem();
    m_pChar = pManager && !Name.empty() ? pManager->MakeCharacter(Name.c_str()) : NULL;
    if (!m_pChar)
    {
      Host.Log("DemoAnimation: character '%s' not made", Name.c_str());
      return false;
    }
    ICryAnimationSet *pAnims = m_pChar->GetModel() ? m_pChar->GetModel()->GetAnimationSet() : NULL;
    const int nAnims = pAnims ? pAnims->Count() : 0;
    std::string Anim = Host.GetScene().Get("animation", nAnims > 0 ? pAnims->GetName(0) : "");
    CryCharAnimationParams Params(0, 0, 0, 0);
    const bool bStarted = !Anim.empty() && m_pChar->StartAnimation(Anim.c_str(), Params);
    const int nId = pAnims && !Anim.empty() ? pAnims->Find(Anim.c_str()) : -1;
    Host.Log("DemoAnimation: %s, %d animations, '%s' (%g s) %s", Name.c_str(), nAnims, Anim.c_str(),
      nId >= 0 ? pAnims->GetLength(nId) : 0.0f, bStarted ? "started" : "NOT started");
    m_pChar->Update();
    m_pChar->GetBBox(m_vMin, m_vMax);
    Host.Log("DemoAnimation: box (%g %g %g) - (%g %g %g)", m_vMin.x, m_vMin.y, m_vMin.z, m_vMax.x, m_vMax.y, m_vMax.z);
    return bStarted;
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
    Params.fDistance = GetLength(Cam.GetPos());
    Host.BeginScene(Cam, Params, &Light, 1);
    m_pChar->Update();
    m_pChar->Draw(Params, Vec3(0, 0, 0));
    Host.EndScene();
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_pChar)
      Host.GetSystem()->GetIAnimationSystem()->RemoveCharacter(m_pChar);
    m_pChar = NULL;
  }

private:
  ICryCharInstance *m_pChar = NULL;
  Vec3 m_vMin, m_vMax;
};

DEMO_PLUGIN(CDemoAnimation)
