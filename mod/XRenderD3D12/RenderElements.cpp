//////////////////////////////////////////////////////////////////////
//
//	File: RenderElements.cpp
//	Description: CRendElement and the CRE* classes: the methods and statics the SDK headers declare.
//
//	The engine gets its elements from EF_CreateRE and destroys them with
//	Release; the leaf buffers make one CREOcLeaf per material. The CRendElement
//	constructor and destructor of the SDK keep every element in the ring of
//	CRendElement::m_RootGlobal.
//
//	Drawing: EF_EndEf3D calls mfPrepare and mfCheckUpdate on every item
//	before it is drawn. mfDraw returns true when the element did its own
//	drawing (the CRECommon elements call back into the 3D engine, which
//	draws through IRenderer), false when the renderer has to draw it (Draw3D).
//	Its SShader argument is the item's shader (RE_ShaderArg in Scene.h).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Scene.h"

#include <CRETriMeshShadow.h>
#include <CREPolyMesh.h>

#include <algorithm>

using namespace RendererStructures;

//////////////////////////////////////////////////////////////////////////
// Statics of the SDK classes

CRendElement CRendElement::m_RootGlobal;
CREOcLeaf *CREOcLeaf::m_pLastRE = nullptr;
SPolyStat CREPolyMesh::mRS;
int CRETriMeshShadow::m_nCRETriMeshShadowRebuildsPerFrrame = 0;
int CRETriMeshShadow::m_nCRETriMeshShadowShadowsPerFrrame = 0;
int CRETriMeshShadow::m_nCRETriMeshShadowAloocatedShadows = 0;

namespace
{
  //! A bare element of a type without an SDK class.
  class CREGeneric : public CRendElement
  {
  public:
    explicit CREGeneric(EDataType edt)
    {
      mfSetType(edt);
      mfUpdateFlags(FCEF_TRANSFORM);
    }
  };

  I3DEngine *Get3DEngine()
  {
    return iSystem ? iSystem->GetI3DEngine() : nullptr;
  }

  //! The result an occlusion query reports: everything visible.
  int VisibleSamples()
  {
    const int nScreen = g_pRenderer ? g_pRenderer->m_nWidth * g_pRenderer->m_nHeight : 0;
    return std::max(800 * 600, nScreen);
  }

  int CurrentFrame()
  {
    return g_pRenderer ? g_pRenderer->m_nFrameID : 0;
  }

  //! The scene keeps the screen effect elements for the per-frame timing (again after a scene
  //! made anew: the engine keeps its element).
  void RegisterWithScene(CREScreenProcess *pRE)
  {
    if (g_pRenderer && g_pRenderer->m_pScene)
      g_pRenderer->m_pScene->RegisterScreenProcess(pRE);
  }
}

//////////////////////////////////////////////////////////////////////////
// Creation

CRendElement *RE_Create(EDataType edt)
{
  CRendElement *pRE = nullptr;
  switch (edt)
  {
  case eDATA_Dummy:                      pRE = new CREDummy; break;
  case eDATA_Sky:                        pRE = new CRESky; break;
  case eDATA_Poly:                       pRE = new CREPolyMesh; break;
  case eDATA_TriMeshShadow:              pRE = new CRETriMeshShadow; break;
  case eDATA_OcLeaf:                     pRE = new CREOcLeaf; break;
  case eDATA_TerrainSector:              pRE = new CRECommon; break;
  case eDATA_2DQuad:                     pRE = new CRE2DQuad; break;
  case eDATA_FarTreeSprites:             pRE = new CREFarTreeSprites; break;
  case eDATA_ShadowMapGen:               pRE = new CREShadowMapGen; break;
  case eDATA_TerrainDetailTextureLayers: pRE = new CRETerrainDetailTextureLayers; break;
  case eDATA_TerrainParticles:           pRE = new CRETerrainParticles; break;
  case eDATA_Glare:                      pRE = new CREGlare; break;
  case eDATA_OcclusionQuery:             pRE = new CREOcclusionQuery; break;
  case eDATA_ClearStencil:               pRE = new CREClearStencil; break;
  case eDATA_FlashBang:                  pRE = new CREFlashBang; break;
  case eDATA_ScreenProcess:              pRE = new CREScreenProcess; break;
  case eDATA_HDRProcess:                 pRE = new CREGeneric(edt); break;   // the size of a bare element
  default:                               return nullptr;                     // Beam, Prefab, Flare, Ocean, ...
  }
  pRE->mfUpdateFlags(nREFlag_EngineOwned);
  return pRE;
}

CRendElement *RE_CreateGeneric(EDataType edt)
{
  return new CREGeneric(edt);
}

void CScene::DestroyEngineElements()
{
  CRendElement *pRoot = &CRendElement::m_RootGlobal;
  if (!pRoot->m_NextGlobal)
    return;     // no element was ever made
  CRendElement *pRE = pRoot->m_NextGlobal;
  while (pRE && pRE != pRoot)
  {
    // The destructor unlinks the element, its neighbours stay.
    CRendElement *pNext = pRE->m_NextGlobal;
    if (pRE->m_Flags & nREFlag_EngineOwned)
      delete pRE;
    pRE = pNext;
  }
}

//////////////////////////////////////////////////////////////////////////
// CRendElement

const char *CRendElement::mfTypeString()
{
  switch (m_Type)
  {
  case eDATA_Unknown:                    return "Unknown";
  case eDATA_Dummy:                      return "Dummy";
  case eDATA_Sky:                        return "Sky";
  case eDATA_Beam:                       return "Beam";
  case eDATA_Poly:                       return "Poly";
  case eDATA_Curve:                      return "Curve";
  case eDATA_MotModel:                   return "MotModel";
  case eDATA_MeshModel:                  return "MeshModel";
  case eDATA_PolyBlend:                  return "PolyBlend";
  case eDATA_AnimPolyBlend:              return "AnimPolyBlend";
  case eDATA_ClientPoly:                 return "ClientPoly";
  case eDATA_ClientPoly2D:               return "ClientPoly2D";
  case eDATA_ParticleSpray:              return "ParticleSpray";
  case eDATA_TriMesh:                    return "TriMesh";
  case eDATA_TriMeshShadow:              return "TriMeshShadow";
  case eDATA_Prefab:                     return "Prefab";
  case eDATA_Flare:                      return "Flare";
  case eDATA_FlareGeom:                  return "FlareGeom";
  case eDATA_FlareProp:                  return "FlareProp";
  case eDATA_Tree:                       return "Tree";
  case eDATA_Tree_Leaves:                return "Tree_Leaves";
  case eDATA_Tree_Branches:              return "Tree_Branches";
  case eDATA_Terrain:                    return "Terrain";
  case eDATA_SkyZone:                    return "SkyZone";
  case eDATA_OcLeaf:                     return "OcLeaf";
  case eDATA_TerrainSector:              return "TerrainSector";
  case eDATA_2DQuad:                     return "2DQuad";
  case eDATA_FarTreeSprites:             return "FarTreeSprites";
  case eDATA_AnimModel:                  return "AnimModel";
  case eDATA_MotionBlur:                 return "MotionBlur";
  case eDATA_ShadowMapGen:               return "ShadowMapGen";
  case eDATA_TerrainDetailTextureLayers: return "TerrainDetailTextureLayers";
  case eDATA_TerrainParticles:           return "TerrainParticles";
  case eDATA_Ocean:                      return "Ocean";
  case eDATA_Glare:                      return "Glare";
  case eDATA_OcclusionQuery:             return "OcclusionQuery";
  case eDATA_TempMesh:                   return "TempMesh";
  case eDATA_ClearStencil:               return "ClearStencil";
  case eDATA_FlashBang:                  return "FlashBang";
  case eDATA_ScreenProcess:              return "ScreenProcess";
  case eDATA_HDRProcess:                 return "HDRProcess";
  default:                               return "Unknown";
  }
}

void CRendElement::mfPrepare()
{
}

bool CRendElement::mfCullByClipPlane(CCObject *pObj)
{
  return false;
}

CMatInfo *CRendElement::mfGetMatInfo()
{
  return nullptr;
}

list2<CMatInfo> *CRendElement::mfGetMatInfoList()
{
  return nullptr;
}

int CRendElement::mfGetMatId()
{
  return -1;
}

bool CRendElement::mfCull(CCObject *obj)
{
  return false;
}

bool CRendElement::mfCull(CCObject *obj, SShader *ef)
{
  return false;
}

void CRendElement::mfReset()
{
}

CRendElement *CRendElement::mfCopyConstruct(void)
{
  return nullptr;
}

void CRendElement::mfCenter(Vec3 &centr, CCObject *pObj)
{
  centr = pObj ? pObj->GetTranslation() : Vec3(0, 0, 0);
}

void CRendElement::mfGetPlane(Plane &pl)
{
  pl.n = Vec3(0, 0, 1);
  pl.d = 0;
}

float CRendElement::mfDistanceToCameraSquared(const CCObject &thisObject)
{
  if (!g_pRenderer)
    return 0;
  return (thisObject.GetTranslation() - g_pRenderer->m_Camera.GetPos()).GetLengthSquared();
}

void CRendElement::mfEndFlush()
{
}

void CRendElement::Release()
{
  delete this;
}

int CRendElement::mfTransform(Matrix44 &ViewMatr, Matrix44 &ProjMatr, vec4_t *verts, vec4_t *vertsp, int Num)
{
  return 0;
}

bool CRendElement::mfIsValidTime(SShader *ef, CCObject *obj, float curtime)
{
  return true;
}

void CRendElement::mfBuildGeometry(SShader *ef)
{
}

bool CRendElement::mfCompile(SShader *ef, char *scr)
{
  return false;
}

CRendElement *CRendElement::mfCreateWorldRE(SShader *ef, SInpData *ds)
{
  return nullptr;
}

bool CRendElement::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;
}

void *CRendElement::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags)
{
  return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// CREOcLeaf: one material (m_pChunk) of a leaf buffer (m_pBuffer)

SLightIndicies *CREOcLeaf::mfGetIndiciesForLight(CDLight *pDLight)
{
  return nullptr;
}

void CREOcLeaf::mfGenerateIndicesInsideFrustrum(SLightIndicies *li, CDLight *pDLight)
{
}

void CREOcLeaf::mfGenerateIndicesForAttenuation(SLightIndicies *li, CDLight *pDLight)
{
}

void CREOcLeaf::mfFillRB(CCObject *pObj)
{
}

CMatInfo *CREOcLeaf::mfGetMatInfo()
{
  return m_pChunk;
}

list2<CMatInfo> *CREOcLeaf::mfGetMatInfoList()
{
  return m_pBuffer ? m_pBuffer->m_pMats : nullptr;
}

int CREOcLeaf::mfGetMatId()
{
  if (!m_pChunk || !m_pBuffer || !m_pBuffer->m_pMats)
    return -1;
  list2<CMatInfo> &mats = *m_pBuffer->m_pMats;
  for (int i = 0; i < mats.Count(); i++)
  {
    if (mats.Get(i) == m_pChunk)
      return i;
  }
  return -1;
}

bool CREOcLeaf::mfPreDraw(SShaderPass *sl)
{
  return true;
}

void CREOcLeaf::mfGetPlane(Plane &pl)
{
  CRendElement::mfGetPlane(pl);
}

void CREOcLeaf::mfPrepare()
{
}

bool CREOcLeaf::mfCullByClipPlane(CCObject *pObj)
{
  return false;
}

void CREOcLeaf::mfCenter(Vec3 &Pos, CCObject *pObj)
{
  if (m_pChunk)
    Pos = m_pChunk->m_vCenter;
  else if (m_pBuffer)
    Pos = (m_pBuffer->m_vBoxMin + m_pBuffer->m_vBoxMax) * 0.5f;
  else
    Pos = Vec3(0, 0, 0);
  if (pObj)
    Pos = pObj->m_Matrix.TransformPointOLD(Pos);
}

bool CREOcLeaf::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;   // the renderer draws the chunk from the leaf buffer
}

void *CREOcLeaf::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags)
{
  return nullptr;
}

void CREOcLeaf::mfEndFlush()
{
}

float CREOcLeaf::mfMinDistanceToCamera(CCObject *pObj)
{
  if (!pObj)
    return -1;
  const float fDist = sqrtf(mfDistanceToCameraSquared(*pObj));
  const float fRadius = m_pChunk ? m_pChunk->m_fRadius * pObj->GetScaleX() : 0;
  return std::max(0.0f, fDist - fRadius);
}

float CREOcLeaf::mfDistanceToCameraSquared(const CCObject &thisObject)
{
  if (!g_pRenderer)
    return 0;
  Vec3 vCenter;
  mfCenter(vCenter, const_cast<CCObject *>(&thisObject));
  return (vCenter - g_pRenderer->m_Camera.GetPos()).GetLengthSquared();
}

bool CREOcLeaf::mfCheckUpdate(int nVertFormat, int Flags)
{
  if (!m_pBuffer)
    return false;
  return m_pBuffer->CheckUpdate(nVertFormat, Flags, false);
}

void CREOcLeaf::mfGetBBox(Vec3 &vMins, Vec3 &vMaxs)
{
  if (m_pChunk)
  {
    const Vec3 r(m_pChunk->m_fRadius, m_pChunk->m_fRadius, m_pChunk->m_fRadius);
    vMins = m_pChunk->m_vCenter - r;
    vMaxs = m_pChunk->m_vCenter + r;
  }
  else if (m_pBuffer)
  {
    vMins = m_pBuffer->m_vBoxMin;
    vMaxs = m_pBuffer->m_vBoxMax;
  }
  else
    CRendElement::mfGetBBox(vMins, vMaxs);
}

//////////////////////////////////////////////////////////////////////////
// CRESky

CRESky::~CRESky()
{
  SAFE_DELETE(m_parrFogLayer);
  SAFE_DELETE(m_parrFogLayer2);
}

void CRESky::mfPrepare()
{
  // The fog layer lists exist from the first draw on.
  if (!m_parrFogLayer)
    m_parrFogLayer = new list2<struct_VERTEX_FORMAT_P3F_COL4UB>;
  if (!m_parrFogLayer2)
    m_parrFogLayer2 = new list2<struct_VERTEX_FORMAT_P3F_COL4UB>;
}

bool CRESky::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;
}

void CRESky::DrawSkySphere(float fHeight)
{
}

bool CRESky::DrawFogLayer()
{
  return false;
}

bool CRESky::DrawBlackPortal()
{
  return false;
}

//////////////////////////////////////////////////////////////////////////
// CRE2DQuad

void CRE2DQuad::mfPrepare()
{
}

bool CRE2DQuad::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;
}

void *CRE2DQuad::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags)
{
  if (Stride)
    *Stride = sizeof(struct_VERTEX_FORMAT_P3F_TEX2F);
  switch (ePT)
  {
  case eSrcPointer_Vert: return &m_arrVerts[0].xyz;
  case eSrcPointer_Tex:  return &m_arrVerts[0].st[0];
  default:               return nullptr;
  }
}

//////////////////////////////////////////////////////////////////////////
// CREDummy

void CREDummy::mfPrepare()
{
}

bool CREDummy::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return true;    // nothing to draw
}

//////////////////////////////////////////////////////////////////////////
// CRECommon and the elements that call back into the 3D engine

void CRECommon::mfPrepare()
{
}

bool CREFarTreeSprites::mfDraw(SShader *ef, SShaderPass *sfm)
{
  // The engine calls IRenderer::DrawObjSprites back with the instances to draw as sprites.
  if (I3DEngine *p3D = Get3DEngine())
    p3D->DrawFarTrees();
  return true;
}

bool CRETerrainDetailTextureLayers::mfDraw(SShader *ef, SShaderPass *sfm)
{
  if (I3DEngine *p3D = Get3DEngine())
    p3D->DrawTerrainDetailTextureLayers();
  return true;
}

bool CRETerrainParticles::mfDraw(SShader *ef, SShaderPass *sfm)
{
  // The decals on the terrain: SetState, SetTexture, DrawTriStrip from inside the call.
  if (I3DEngine *p3D = Get3DEngine())
    p3D->DrawTerrainParticles(RE_Shader(ef));
  return true;
}

bool CREClearStencil::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;   // the renderer clears the stencil buffer
}

//////////////////////////////////////////////////////////////////////////
// CRETriMeshShadow (stencil shadow volumes): no volumes are built; the element draws nothing

CRETriMeshShadow::~CRETriMeshShadow()
{
  // m_arrLBuffers holds no buffer of this renderer (it makes no volumes).
}

void CRETriMeshShadow::mfPrepare()
{
}

bool CRETriMeshShadow::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return true;    // nothing to draw
}

void *CRETriMeshShadow::mfGetPointer(ESrcPointer ePT, int *Stride, int Type, ESrcPointer Dst, int Flags)
{
  return nullptr;
}

bool CRETriMeshShadow::mfCheckUpdate(int nVertFormat, int Flags)
{
  m_nCurrInst = -1;   // no volume selected for the draw
  return false;
}

bool CRETriMeshShadow::mfCheckUnload()
{
  return false;
}

int CRETriMeshShadow::GetAndResetRebuildsPerFrrameCounter()
{
  const int n = m_nCRETriMeshShadowRebuildsPerFrrame;
  m_nCRETriMeshShadowRebuildsPerFrrame = 0;
  return n;
}

int CRETriMeshShadow::GetAndResetShadowVolumesPerFrrameCounter()
{
  const int n = m_nCRETriMeshShadowShadowsPerFrrame;
  m_nCRETriMeshShadowShadowsPerFrrame = 0;
  return n;
}

int CRETriMeshShadow::GetShadowVolumesAllocatedCounter()
{
  return m_nCRETriMeshShadowAloocatedShadows;
}

void CRETriMeshShadow::PrintStats()
{
}

//////////////////////////////////////////////////////////////////////////
// CREOcclusionQuery: every query reports its box visible

CREOcclusionQuery::~CREOcclusionQuery()
{
}

void CREOcclusionQuery::mfPrepare()
{
  m_nVisSamples = VisibleSamples();
  m_nCheckFrame = CurrentFrame();
  m_nDrawFrame = CurrentFrame();
}

bool CREOcclusionQuery::mfDraw(SShader *ef, SShaderPass *sfm)
{
  mfPrepare();
  return true;
}

void CREOcclusionQuery::mfReset()
{
  m_nVisSamples = VisibleSamples();
  m_nCheckFrame = 0;
  m_nDrawFrame = 0;
}

//////////////////////////////////////////////////////////////////////////
// CREFlashBang, CREGlare

void CREFlashBang::mfPrepare()
{
}

bool CREFlashBang::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;
}

void CREGlare::mfInit()
{
  mfSetType(eDATA_Glare);
  mfUpdateFlags(FCEF_TRANSFORM);
  m_GlareWidth = 0;
  m_GlareHeight = 0;
  m_fGlareAmount = 0;
}

void CREGlare::mfPrepare()
{
}

bool CREGlare::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;
}

//////////////////////////////////////////////////////////////////////////
// CREPolyMesh

CREPolyMesh::~CREPolyMesh()
{
  SAFE_DELETE_ARRAY(TriVerts);
  SAFE_DELETE_ARRAY(Indices);
  SAFE_DELETE_ARRAY(bNoDeform);
}

void CREPolyMesh::mfCheckSun(SShader *ef)
{
}

bool CREPolyMesh::mfCullFace(ECull cl)
{
  return false;
}

CRendElement *CREPolyMesh::mfCopyConstruct(void)
{
  CREPolyMesh *pCopy = new CREPolyMesh;
  pCopy->m_Flags = m_Flags & ~nREFlag_EngineOwned;
  pCopy->m_Color = m_Color;
  pCopy->m_SortId = m_SortId;
  pCopy->m_Plane = m_Plane;
  pCopy->Srf = Srf;
  pCopy->NumLightRE = NumLightRE;
  pCopy->mFrameBr = mFrameBr;
  pCopy->NumVerts = NumVerts;
  pCopy->NumIndices = NumIndices;
  if (TriVerts && NumVerts > 0)
  {
    pCopy->TriVerts = new SMTriVert[NumVerts];
    memcpy(pCopy->TriVerts, TriVerts, NumVerts * sizeof(SMTriVert));
  }
  if (bNoDeform && NumVerts > 0)
  {
    pCopy->bNoDeform = new bool[NumVerts];
    memcpy(pCopy->bNoDeform, bNoDeform, NumVerts * sizeof(bool));
  }
  if (Indices && NumIndices > 0)
  {
    pCopy->Indices = new ushort[NumIndices];
    memcpy(pCopy->Indices, Indices, NumIndices * sizeof(ushort));
  }
  return pCopy;
}

void CREPolyMesh::mfCenter(Vec3d &centr, CCObject *pObj)
{
  centr = Vec3(0, 0, 0);
  if (TriVerts && NumVerts > 0)
  {
    for (int i = 0; i < NumVerts; i++)
      centr += TriVerts[i].vert;
    centr = centr * (1.0f / NumVerts);
  }
  if (pObj)
    centr = pObj->m_Matrix.TransformPointOLD(centr);
}

int CREPolyMesh::mfTransform(Matrix44 &ViewMatr, Matrix44 &ProjMatr, vec4_t *verts, vec4_t *vertsp, int Num)
{
  return 0;
}

void CREPolyMesh::mfPrepare(void)
{
}

void CREPolyMesh::mfGetPlane(Plane &pl)
{
  pl = m_Plane;
}

void CREPolyMesh::mfPrintStat()
{
}

//////////////////////////////////////////////////////////////////////////
// CREScreenProcess: the screen effects (RendererStructures.h, "Screen effects")

//! The state behind CREScreenProcess::m_pVars: every parameter of every process code, readable
//! back with mfGetParameter, and the renderer's timing of FlashBang and ScreenFade.
class CScreenVars
{
public:
  static constexpr int NUM_PROCESSES = 16;
  static constexpr int NUM_PARAMS = 32;

  float m_fParams[NUM_PROCESSES][NUM_PARAMS] = {};
  bool m_bActive[NUM_PROCESSES] = {};   //!< parameter nBin64ScreenParam_Active: one byte
  float m_fUnknown = 0;                 //!< the value of codes out of range
  int m_nFadePreFrames = 0;             //!< ScreenFade: frames still to wait (ScreenPreFadeTime)
  float m_fFadeElapsed = 0;             //!< ScreenFade: seconds of the fade so far

  void Reset();
  void Update(float fFrameTime);
  float FadeAlpha() const;
  float &Param(int nProcess, int nParam) { return m_fParams[nProcess][nParam]; }
  float Param(int nProcess, int nParam) const { return m_fParams[nProcess][nParam]; }
};
static_assert(sizeof(CScreenVars) >= nBin64Size_CScreenVars, "CREScreenProcess::m_pVars is smaller than the engine expects");

void CScreenVars::Reset()
{
  *this = CScreenVars();
  float *pFB = m_fParams[nBin64ScreenProcess_FlashBang];
  pFB[nBin64ScreenParam_FlashBangTimeScale] = 1;
  pFB[nBin64ScreenParam_FlashBangFlashPosX] = 200;
  pFB[nBin64ScreenParam_FlashBangFlashPosY] = 100;
  pFB[nBin64ScreenParam_FlashBangFlashSizeX] = 400;
  pFB[nBin64ScreenParam_FlashBangFlashSizeY] = 400;
  pFB[nBin64ScreenParam_FlashBangCurrent] = 1;
}

void CScreenVars::Update(float fFrameTime)
{
  // FlashBang: the part still to come falls by the frame time over TimeScale; at its end the
  // renderer sets it back to 1 and switches the effect off.
  if (m_bActive[nBin64ScreenProcess_FlashBang])
  {
    float fScale = Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangTimeScale);
    if (fScale <= 0)
      fScale = 1;
    float &fCur = Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangCurrent);
    const float fNext = fCur - fFrameTime / fScale;
    if (fNext <= 0)
    {
      fCur = 1;
      m_bActive[nBin64ScreenProcess_FlashBang] = false;
    }
    else
      fCur = fNext;
  }

  // ScreenFade: ScreenPreFadeTime frames, then |ScreenFadeTime| seconds; a fade in (negative
  // time) switches itself off at its end and clears ScreenPreFadeTime.
  if (m_bActive[nBin64ScreenProcess_ScreenFade])
  {
    if (m_nFadePreFrames > 0)
      m_nFadePreFrames--;
    else
    {
      m_fFadeElapsed += fFrameTime;
      const float fTime = Param(nBin64ScreenProcess_ScreenFade, nBin64ScreenParam_ScreenFadeTime);
      if (fTime <= 0 && m_fFadeElapsed >= -fTime)
      {
        m_bActive[nBin64ScreenProcess_ScreenFade] = false;
        Param(nBin64ScreenProcess_ScreenFade, nBin64ScreenParam_ScreenPreFadeTime) = 0;
        m_fFadeElapsed = 0;
      }
    }
  }
}

float CScreenVars::FadeAlpha() const
{
  if (!m_bActive[nBin64ScreenProcess_ScreenFade])
    return 0;
  const float fTime = Param(nBin64ScreenProcess_ScreenFade, nBin64ScreenParam_ScreenFadeTime);
  if (fTime < 0)
    return m_nFadePreFrames > 0 ? 1.0f : std::clamp(1.0f + m_fFadeElapsed / fTime, 0.0f, 1.0f);
  if (fTime > 0)
    return m_nFadePreFrames > 0 ? 0.0f : std::clamp(m_fFadeElapsed / fTime, 0.0f, 1.0f);
  return 0;
}

CREScreenProcess::CREScreenProcess()
{
  mfSetType(eDATA_ScreenProcess);
  mfUpdateFlags(FCEF_TRANSFORM);
  m_pVars = new CScreenVars;
  m_pVars->Reset();
  RegisterWithScene(this);
}

CREScreenProcess::~CREScreenProcess()
{
  if (g_pRenderer && g_pRenderer->m_pScene)
    g_pRenderer->m_pScene->UnregisterScreenProcess(this);
  SAFE_DELETE(m_pVars);
}

void CREScreenProcess::mfPrepare()
{
  RegisterWithScene(this);
}

bool CREScreenProcess::mfDraw(SShader *ef, SShaderPass *sfm)
{
  return false;   // the renderer draws the effects (CScene::GetScreenFx)
}

bool CREScreenProcess::mfDrawLowSpec(SShader *ef, SShaderPass *sfm)
{
  return false;
}

void CREScreenProcess::mfActivate(int iProcess)
{
  if (m_pVars && iProcess >= 0 && iProcess < CScreenVars::NUM_PROCESSES)
  {
    bool bOn = true;
    mfSetParameter(iProcess, nBin64ScreenParam_Active, &bOn);
  }
}

void CREScreenProcess::mfReset(void)
{
  if (m_pVars)
    m_pVars->Reset();
}

int CREScreenProcess::mfSetParameter(int iProcess, int iParams, void *dwValue)
{
  RegisterWithScene(this);
  if (!m_pVars || !dwValue || iProcess < 0 || iProcess >= CScreenVars::NUM_PROCESSES || iParams < 0 || iParams >= CScreenVars::NUM_PARAMS)
    return 0;
  CScreenVars &v = *m_pVars;
  if (iParams == nBin64ScreenParam_Active)
  {
    const bool bOn = *(const bool *)dwValue;
    v.m_bActive[iProcess] = bOn;
    if (bOn && iProcess == nBin64ScreenProcess_FlashBang)
      v.Param(iProcess, nBin64ScreenParam_FlashBangCurrent) = 1;
    if (bOn && iProcess == nBin64ScreenProcess_ScreenFade)
    {
      v.m_fFadeElapsed = 0;
      v.m_nFadePreFrames = (int)(v.Param(iProcess, nBin64ScreenParam_ScreenPreFadeTime) + 0.5f);
    }
    return 0;
  }
  const float fValue = *(const float *)dwValue;
  v.Param(iProcess, iParams) = fValue;
  if (iProcess == nBin64ScreenProcess_ScreenFade)
  {
    if (iParams == nBin64ScreenParam_ScreenFadeTime)
      v.m_fFadeElapsed = 0;
    else if (iParams == nBin64ScreenParam_ScreenPreFadeTime)
    {
      v.m_fFadeElapsed = 0;
      v.m_nFadePreFrames = std::max(0, (int)(fValue + 0.5f));
    }
  }
  return 0;
}

void *CREScreenProcess::mfGetParameter(int iProcess, int iParams)
{
  RegisterWithScene(this);
  if (!m_pVars)
    return nullptr;
  CScreenVars &v = *m_pVars;
  if (iProcess < 0 || iProcess >= CScreenVars::NUM_PROCESSES || iParams < 0 || iParams >= CScreenVars::NUM_PARAMS)
  {
    v.m_fUnknown = 0;
    return &v.m_fUnknown;
  }
  if (iParams == nBin64ScreenParam_Active)
    return &v.m_bActive[iProcess];
  return &v.m_fParams[iProcess][iParams];
}

//////////////////////////////////////////////////////////////////////////
// CScene: the screen effect elements

void CScene::RegisterScreenProcess(CREScreenProcess *pRE)
{
  if (std::find(m_ScreenProcesses.begin(), m_ScreenProcesses.end(), pRE) == m_ScreenProcesses.end())
    m_ScreenProcesses.push_back(pRE);
}

void CScene::UnregisterScreenProcess(CREScreenProcess *pRE)
{
  std::erase(m_ScreenProcesses, pRE);
}

void CScene::UpdateScreenFx()
{
  if (!g_pRenderer || g_pRenderer->m_nFrameID == m_nScreenFxFrame)
    return;
  m_nScreenFxFrame = g_pRenderer->m_nFrameID;
  const float fFrameTime = iTimer ? iTimer->GetFrameTime() : 0.0f;
  for (CREScreenProcess *pRE : m_ScreenProcesses)
  {
    if (CScreenVars *pVars = pRE->GetVars())
      pVars->Update(fFrameTime);
  }
}

SScreenFxState CScene::GetScreenFx() const
{
  SScreenFxState fx;
  if (m_ScreenProcesses.empty() || (g_pRenderer && g_pRenderer->CV_r_DisableSfx))
    return fx;
  const CScreenVars *pVars = m_ScreenProcesses.front()->GetVars();
  if (!pVars)
    return fx;
  const CScreenVars &v = *pVars;
  fx.bNightVision = v.m_bActive[nBin64ScreenProcess_NightVision];
  fx.bFlashBang = v.m_bActive[nBin64ScreenProcess_FlashBang];
  fx.fFlashBang = v.Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangCurrent);
  fx.fFlashPos[0] = v.Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangFlashPosX);
  fx.fFlashPos[1] = v.Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangFlashPosY);
  fx.fFlashSize[0] = v.Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangFlashSizeX);
  fx.fFlashSize[1] = v.Param(nBin64ScreenProcess_FlashBang, nBin64ScreenParam_FlashBangFlashSizeY);
  fx.bScreenFade = v.m_bActive[nBin64ScreenProcess_ScreenFade];
  fx.fFadeAlpha = v.FadeAlpha();
  fx.bScreenBlur = v.m_bActive[nBin64ScreenProcess_ScreenBlur];
  fx.fBlurAmount = v.Param(nBin64ScreenProcess_ScreenBlur, nBin64ScreenParam_ScreenBlurAmount);
  fx.fBlurColor[0] = v.Param(nBin64ScreenProcess_ScreenBlur, nBin64ScreenParam_ScreenBlurColorRed);
  fx.fBlurColor[1] = v.Param(nBin64ScreenProcess_ScreenBlur, nBin64ScreenParam_ScreenBlurColorGreen);
  fx.fBlurColor[2] = v.Param(nBin64ScreenProcess_ScreenBlur, nBin64ScreenParam_ScreenBlurColorBlue);
  return fx;
}
