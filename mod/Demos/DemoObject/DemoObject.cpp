// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoObject.cpp
//	Description: A static object of the game turning in front of a fixed
//	camera.
//
//	The object is drawn the way the game's menu draws its model, through
//	the renderer's 3D path (EF_StartEf, IStatObj::Render, EF_EndEf3D) with
//	one white point light above and to the right of the camera and a grey
//	ambient colour. It turns about z by 3 degrees per frame (a matrix in
//	SRendParams::pMatrix with FOB_TRANS_MASK, built here): frames 1, 31,
//	61 and 91 show it from four sides.
//
//	Scene file keys (Demos\DemoObject.ini):
//	  object = <file>   the object (default: the first .cgf under objects/)
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"

class CDemoObject : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoObject"; }
  virtual const char *GetDefaultFrames() const { return "1,31,61,91"; }

  virtual bool Init(CDemoHost &Host)
  {
    m_pObj = Host.LoadObject("object");
    return m_pObj != NULL;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    // The box of the object turned about z in any way.
    const Vec3 vMin = m_pObj->GetBoxMin(), vMax = m_pObj->GetBoxMax();
    const float fRound = max(max(fabsf(vMin.x), fabsf(vMax.x)), max(fabsf(vMin.y), fabsf(vMax.y)));
    const Vec3 vBoxMin(-fRound, -fRound, vMin.z), vBoxMax(fRound, fRound, vMax.z);
    const float fRadius = GetLength(vBoxMax - vBoxMin) * 0.5f;
    const CCamera Cam = Host.FrameBox(vBoxMin, vBoxMax, 0, 20);

    CDLight Light;
    Light.m_Flags = DLF_POINT;
    Light.m_Origin = Cam.GetPos() + Vec3(fRadius * 2, 0, fRadius * 2);
    Light.m_fRadius = Light.m_fEndRadius = fRadius * 20;
    Light.m_Color = CFColor(0.8f, 0.8f, 0.8f, 1);

    // The object is placed by a matrix in SRendParams::pMatrix, with the FOB_TRANS_* flags in
    // dwFObjFlags. Matrix44 takes a row vector (v * M): the turned axes in the rows, the position
    // in the last row (SetTranslationOLD).
    SRendParams Params;
    const float fTurn = DEG2RAD(3.0f * (nFrame - 1));
    Matrix44 Mat;
    Mat.SetIdentity();
    float *m = Mat.GetData();
    m[0] = cosf(fTurn);
    m[1] = sinf(fTurn);
    m[4] = -sinf(fTurn);
    m[5] = cosf(fTurn);
    Mat.SetTranslationOLD(Params.vPos);
    Params.pMatrix = &Mat;
    Params.dwFObjFlags = FOB_TRANS_MASK;
    Params.vAmbientColor = Vec3(0.3f, 0.3f, 0.3f);
    Params.fDistance = GetLength(Cam.GetPos());
    Host.RenderObject(Cam, m_pObj, Params, &Light, 1);
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

DEMO_PLUGIN(CDemoObject)
