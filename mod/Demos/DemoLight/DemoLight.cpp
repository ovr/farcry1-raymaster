// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoLight.cpp
//	Description: One point light going round a static object of the game.
//
//	The object stands still in front of a fixed camera and is drawn like in
//	DemoObject, with a dark ambient colour and one white point light that
//	goes round it by 6 degrees per frame, half its size above its middle:
//	frames 1, 16, 31 and 46 light it from the right (+x), the back (+y),
//	the left and the front. A yellow cross of lines marks the light.
//
//	Scene file keys (Demos\DemoLight.ini):
//	  object = <file>   the object (default: the first .cgf under objects/)
//	  radius = <m>      the radius of the light (default: 3 times the size of
//	                    the object)
//	  colour = r g b    the colour of the light (default: 1 1 1)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

class CDemoLight : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoLight"; }
  virtual const char *GetDefaultFrames() const { return "1,16,31,46"; }

  virtual bool Init(CDemoHost &Host)
  {
    m_pObj = Host.LoadObject("object");
    return m_pObj != NULL;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const Vec3 vMin = m_pObj->GetBoxMin(), vMax = m_pObj->GetBoxMax();
    const Vec3 vCenter = (vMin + vMax) * 0.5f;
    const float fSize = GetLength(vMax - vMin);
    const CCamera Cam = Host.FrameBox(vMin, vMax, 0, 20);

    CDLight Light;
    const float fAround = DEG2RAD(6.0f * (nFrame - 1));
    Light.m_Flags = DLF_POINT;
    Light.m_Origin = vCenter + Vec3(cosf(fAround), sinf(fAround), 0) * fSize + Vec3(0, 0, fSize * 0.5f);
    Light.m_fRadius = Light.m_fEndRadius = Host.GetScene().GetFloat("radius", fSize * 3);
    std::vector<float> Colour = Host.GetScene().GetFloats("colour");
    Light.m_Color = Colour.size() >= 3 ? CFColor(Colour[0], Colour[1], Colour[2], 1) : CFColor(1, 1, 1, 1);

    SRendParams Params;
    Params.vAmbientColor = Vec3(0.1f, 0.1f, 0.1f);
    Params.fDistance = GetLength(Cam.GetPos() - vCenter);
    Host.RenderObject(Cam, m_pObj, Params, &Light, 1);

    // The light's place.
    const CFColor Yellow(1, 1, 0, 1);
    const float d = fSize * 0.05f;
    const Vec3 &P = Light.m_Origin;
    pRenderer->SetState(GS_NODEPTHTEST);
    pRenderer->SetWhiteTexture();
    pRenderer->DrawLineColor(P - Vec3(d, 0, 0), Yellow, P + Vec3(d, 0, 0), Yellow);
    pRenderer->DrawLineColor(P - Vec3(0, d, 0), Yellow, P + Vec3(0, d, 0), Yellow);
    pRenderer->DrawLineColor(P - Vec3(0, 0, d), Yellow, P + Vec3(0, 0, d), Yellow);
    pRenderer->SetState(GS_DEPTHWRITE);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    if (m_pObj)
      Host.Get3DEngine()->ReleaseObject(m_pObj);
    m_pObj = NULL;
  }

private:
  IStatObj *m_pObj = NULL;
};

DEMO_PLUGIN(CDemoLight)
