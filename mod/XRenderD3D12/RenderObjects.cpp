//////////////////////////////////////////////////////////////////////
//
//	File: RenderObjects.cpp
//	Description: CCObject: the methods and statics the SDK header declares, and the pool of render objects.
//
//	CCObject is shared with the engine with the SDK layout: the engine fills
//	its members before it hands the object to a leaf buffer or EF_AddEf, the
//	renderer derives matrices from it in EF_EndEf3D. The objects are plain
//	heap blocks of CrySystem's heap (4-byte aligned): nothing here relies on
//	a larger alignment.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Scene.h"

#include <algorithm>
#include <numbers>

// The tables of CCObject (static members of the SDK class). Their memory is freed in
// CScene::Shutdown, before the DLL is unloaded.
TArray<SWaveForm2> CCObject::m_Waves;
MatrixArray16 CCObject::m_ObjMatrices;

namespace
{
  //! The largest index a short can carry (m_NumWFX, m_VPMatrixId, ...).
  constexpr int MAX_SHORT_INDEX = 0x7FFF;

  //! Appends a matrix to CCObject::m_ObjMatrices and returns its index (the last one when full).
  int AddObjMatrix(const Matrix44 &m)
  {
    MatrixArray16 &arr = CCObject::m_ObjMatrices;
    const size_t n = arr.size();
    if (n >= MAX_SHORT_INDEX)
    {
      arr[n - 1] = m;
      return (int)n - 1;
    }
    if (n == arr.capacity())
      arr.reserve(n < 256 ? 256 : n * 2);
    arr.resize(n + 1);
    arr[n] = m;
    return (int)n;
  }

  //! Forgets the matrices of an object derived for another camera.
  void CheckMatrixSerial(CCObject *pObj)
  {
    const short nSerial = g_pRenderer && g_pRenderer->m_pScene ? g_pRenderer->m_pScene->GetMatrixSerial() : 0;
    if (pObj->m_VPMatrixFrame != nSerial || pObj->m_VPMatrixId >= (int)CCObject::m_ObjMatrices.size() ||
        pObj->m_InvMatrixId >= (int)CCObject::m_ObjMatrices.size())
    {
      pObj->m_VPMatrixId = -1;
      pObj->m_InvMatrixId = -1;
      pObj->m_VPMatrixFrame = nSerial;
    }
  }

  //! Two new waves of CCObject::m_Waves for pObj (indices from 1 in every frame; 0 is a dummy);
  //! returns the index of the first.
  int HandOutWaves(CCObject *pObj, SWaveForm2 **ppWaves)
  {
    TArray<SWaveForm2> &waves = CCObject::m_Waves;
    if (waves.Num() == 0)
    {
      waves.AddIndex(1);
      waves[0] = SWaveForm2();
    }
    int n = waves.Num();
    if (n + 2 > MAX_SHORT_INDEX)
      n -= 2;     // full: the last pair again
    else
      waves.AddIndex(2);
    waves[n] = SWaveForm2();
    waves[n + 1] = SWaveForm2();
    pObj->m_NumWFX = (short)n;
    pObj->m_NumWFY = (short)(n + 1);
    if (ppWaves)
    {
      // The engine fills both waves through these pointers right after the call.
      ppWaves[0] = &waves[n];
      ppWaves[1] = &waves[n + 1];
    }
    return n;
  }
}

//////////////////////////////////////////////////////////////////////////
// CCObject

CCObject::~CCObject()
{
  if (m_bShaderParamCreatedInRenderer)
    SAFE_DELETE(m_ShaderParams);
}

void CCObject::Init()
{
  // m_Id stays: it is the object's place in its pool.
  if (m_bShaderParamCreatedInRenderer)
    SAFE_DELETE(m_ShaderParams);
  m_ShaderParams = nullptr;     // otherwise the engine's array
  m_bShaderParamCreatedInRenderer = false;

  m_Counter = 0;
  m_VisId = 0;
  m_VPMatrixId = -1;
  m_ObjFlags = 0;
  m_Matrix.SetIdentity();
  m_nLMId = 0;
  m_nLMDirId = 0;
  m_nOcclId = 0;
  m_nHDRLMId = 0;
  m_Reserved1 = 0;
  m_DynLMMask = 0;
  for (int i = 0; i < 3; i++)
  {
    m_Trans2[i] = 0;
    m_Angs2[i] = 0;
  }
  for (float &f : m_TempVars)
    f = 0;
  m_fDistanceToCam = 0;
  m_RE = nullptr;
  m_EF = nullptr;
  m_CustomData = nullptr;
  m_RenderState = 0;
  m_SortId = 0;
  m_InvMatrixId = -1;
  m_VPMatrixFrame = -1;
  m_NumCM = -1;
  m_nLod = 0;
  m_nTemplId = 0;
  m_FrameLight = 0;
  m_LightStyle = 0;
  m_nScissorX1 = m_nScissorY1 = m_nScissorX2 = m_nScissorY2 = 0;
  m_fRefract = 0;
  m_fBump = 0;
  m_fHeatFactor = 0;
  m_pShadowCasters = nullptr;
  m_pShadowFrustum = nullptr;
  m_AmbColor = Vec3(1, 1, 1);
  m_Color = CFColor(1, 1, 1, 1);
  m_pCharInstance = nullptr;
  m_pLMTCBufferO = nullptr;
  m_pLightImage = nullptr;      // the whole union (m_fBending, m_pLight, ...)
}

Matrix44 &CCObject::GetInvMatrix()
{
  CheckMatrixSerial(this);
  if (m_InvMatrixId < 0)
    m_InvMatrixId = (short)AddObjMatrix(GetInverted44(m_Matrix));
  return m_ObjMatrices[m_InvMatrixId];
}

Matrix44 &CCObject::GetVPMatrix()
{
  CheckMatrixSerial(this);
  if (m_VPMatrixId < 0)
  {
    Matrix44 vp = m_Matrix;
    if (g_pRenderer)
      vp = m_Matrix * g_pRenderer->m_CameraView * g_pRenderer->m_ProjMatrix;
    m_VPMatrixId = (short)AddObjMatrix(vp);
  }
  return m_ObjMatrices[m_VPMatrixId];
}

void CCObject::SetScissor()
{
  if (!g_pRenderer)
    return;
  const int w = m_nScissorX2 - m_nScissorX1;
  const int h = m_nScissorY2 - m_nScissorY1;
  if ((m_ObjFlags & FOB_NOSCISSOR) || w <= 0 || h <= 0)
    g_pRenderer->SetScissor();
  else
    g_pRenderer->SetScissor(m_nScissorX1, m_nScissorY1, w, h);
}

void CCObject::SetAlphaState(CPShader *pPS, int nCurState)
{
  // The alpha state of pixel shader programs: this renderer has none of the engine's.
}

void CCObject::AddWaves(SWaveForm2 **wf)
{
  if (g_pRenderer && g_pRenderer->m_pScene)
    g_pRenderer->m_pScene->AddWaves(this, wf);
  else
    HandOutWaves(this, wf);
}

void CCObject::SetShaderFloat(const char *Name, float Val)
{
  if (!Name || !Name[0])
    return;
  if (m_ShaderParams)
  {
    for (int i = 0; i < m_ShaderParams->Num(); i++)
    {
      SShaderParam &sp = (*m_ShaderParams)[i];
      if (!stricmp(sp.m_Name, Name))
      {
        sp.m_Type = eType_FLOAT;
        sp.m_Value.m_Float = Val;
        return;
      }
    }
    if (!m_bShaderParamCreatedInRenderer)
      return;   // the engine's array: not grown by the renderer
  }
  else
  {
    m_ShaderParams = new TArray<SShaderParam>;
    m_bShaderParamCreatedInRenderer = true;
  }
  SShaderParam sp;
  strncpy(sp.m_Name, Name, sizeof(sp.m_Name) - 1);
  sp.m_Name[sizeof(sp.m_Name) - 1] = 0;
  sp.m_Type = eType_FLOAT;
  sp.m_Value.m_Float = Val;
  m_ShaderParams->AddElem(sp);
}

void CCObject::RemovePermanent()
{
  if (g_pRenderer && g_pRenderer->m_pScene)
    g_pRenderer->m_pScene->RemovePermanent(this);
}

//////////////////////////////////////////////////////////////////////////
// Waves

float EvalWave(const SWaveForm2 &wf, float t)
{
  float x = t * wf.m_Freq + wf.m_Phase;
  x -= floorf(x);     // one period, 0..1
  float f = 0;
  switch (wf.m_eWFType)
  {
  case eWF_Sin:         f = sinf(2 * std::numbers::pi_v<float> * x); break;
  case eWF_HalfSin:     f = sinf(std::numbers::pi_v<float> * x); break;
  case eWF_InvHalfSin:  f = 1 - sinf(std::numbers::pi_v<float> * x); break;
  case eWF_Square:      f = x < 0.5f ? 1.0f : -1.0f; break;
  case eWF_Triangle:    f = x < 0.5f ? 4 * x - 1 : 3 - 4 * x; break;
  case eWF_SawTooth:    f = x; break;
  case eWF_InvSawTooth: f = 1 - x; break;
  case eWF_Hill:        f = 0.5f - 0.5f * cosf(2 * std::numbers::pi_v<float> * x); break;
  case eWF_InvHill:     f = 0.5f + 0.5f * cosf(2 * std::numbers::pi_v<float> * x); break;
  default:              f = 0; break;
  }
  return wf.m_Level + wf.m_Amp * f;
}

//////////////////////////////////////////////////////////////////////////
// CScene: the object pool

CCObject *CScene::GetTempObject()
{
  CheckNewFrame();
  if (m_nTempUsed == (int)m_TempObjects.size())
  {
    m_TempObjects.push_back(std::make_unique<CCObject>());
    m_TempObjects.back()->m_Id = (short)std::min(m_nTempUsed, MAX_SHORT_INDEX);
  }
  CCObject *pObj = m_TempObjects[m_nTempUsed++].get();
  pObj->Init();
  return pObj;
}

CCObject *CScene::GetPermanentObject(int nId)
{
  if (nId >= MAX_SHORT_INDEX)
    nId = -1;
  if (nId >= 0 && nId < (int)m_Permanent.size() && m_Permanent[nId].bUsed)
    return m_Permanent[nId].pObj.get();
  if (nId < 0)
  {
    // The first free place, else a new one.
    nId = 0;
    while (nId < (int)m_Permanent.size() && m_Permanent[nId].bUsed)
      nId++;
    if (nId >= MAX_SHORT_INDEX)
      return GetTempObject();
  }
  if (nId >= (int)m_Permanent.size())
    m_Permanent.resize(nId + 1);
  SPermanent &slot = m_Permanent[nId];
  if (!slot.pObj)
    slot.pObj = std::make_unique<CCObject>();
  slot.bUsed = true;
  CCObject *pObj = slot.pObj.get();
  pObj->Init();
  pObj->m_Id = (short)nId;
  return pObj;
}

void CScene::RemovePermanent(CCObject *pObj)
{
  if (!pObj)
    return;
  int nId = pObj->m_Id;
  if (nId < 0 || nId >= (int)m_Permanent.size() || m_Permanent[nId].pObj.get() != pObj)
  {
    auto it = std::find_if(m_Permanent.begin(), m_Permanent.end(), [pObj](const SPermanent &s) { return s.pObj.get() == pObj; });
    if (it == m_Permanent.end())
      return;     // not a permanent object of this pool
    nId = (int)(it - m_Permanent.begin());
  }
  SPermanent &slot = m_Permanent[nId];
  if (!slot.bUsed)
    return;
  slot.bUsed = false;
  // A removed object can be handed out again (a light's corona): it starts anew.
  if (g_pRenderer)
    g_pRenderer->ForgetFlare(pObj);
  pObj->Init();
  pObj->m_Id = (short)nId;
}

bool CScene::IsRemovedPermanent(const CCObject *pObj) const
{
  if (!pObj)
    return false;
  const int nId = pObj->m_Id;
  return nId >= 0 && nId < (int)m_Permanent.size() && m_Permanent[nId].pObj.get() == pObj && !m_Permanent[nId].bUsed;
}

void CScene::AddWaves(CCObject *pObj, SWaveForm2 **ppWaves)
{
  if (!pObj)
    return;
  CheckNewFrame();
  const int n = HandOutWaves(pObj, ppWaves);
  if ((int)m_WaveOwners.size() < n + 2)
    m_WaveOwners.resize(n + 2, nullptr);
  m_WaveOwners[n] = pObj;
  m_WaveOwners[n + 1] = pObj;
}

bool CScene::GetWaves(const CCObject *pObj, const SWaveForm2 **ppX, const SWaveForm2 **ppY) const
{
  if (!pObj)
    return false;
  const int nX = pObj->m_NumWFX;
  const int nY = pObj->m_NumWFY;
  const int nWaves = std::min(CCObject::m_Waves.Num(), (int)m_WaveOwners.size());
  if (nX <= 0 || nY <= 0 || nX >= nWaves || nY >= nWaves || m_WaveOwners[nX] != pObj || m_WaveOwners[nY] != pObj)
    return false;
  if (ppX)
    *ppX = &CCObject::m_Waves[nX];
  if (ppY)
    *ppY = &CCObject::m_Waves[nY];
  return true;
}

void CScene::CheckNewFrame()
{
  if (m_nDrawing > 0 || !g_pRenderer || g_pRenderer->m_nFrameID == m_nResetFrame)
    return;
  m_nResetFrame = g_pRenderer->m_nFrameID;
  ResetFrameObjects();
}

void CScene::ResetFrameObjects()
{
  m_nTempUsed = 0;
  if (CCObject::m_Waves.Num() > 1)
    CCObject::m_Waves.SetUse(1);
  m_WaveOwners.assign(1, nullptr);
  CCObject::m_ObjMatrices.clear();
  m_nFrameItems = 0;
  BumpMatrixSerial();
}

void CScene::BumpMatrixSerial()
{
  // m_VPMatrixFrame of a new object is -1: never a serial.
  m_nMatrixSerial = (short)((m_nMatrixSerial + 1) & 0x7FFF);
}
