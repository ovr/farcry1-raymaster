//////////////////////////////////////////////////////////////////////
//
//	File: Scene.cpp
//	Description: The scene of a frame: EF_StartEf .. EF_EndEf3D, the render
//	items and their order, lights, particle sprites, fog volumes.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Scene.h"

#include <ICryAnimation.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace
{
  //! CLeafBuffer::CheckUpdate flag for the buffers of characters: their skinned vertices arrive
  //! after it (IDeformableRenderMesh::ProcessSkinning).
  constexpr int nCheckUpdate_Character = 0x10;

  //! The shader an item is drawn with: the template nTempl of the shader (-1: its preferred one,
  //! user templates from EFT_USER_FIRST), the shader itself when it has none.
  CShader *ResolveShader(IShader *pEf, int nTempl)
  {
    IShader *pSh = pEf->GetTemplate(nTempl < -1 ? -1 : nTempl);
    return static_cast<CShader *>(pSh ? pSh : pEf);
  }

  //! How an item is drawn: the shader's draw class; resources with an opacity below 1 make an
  //! opaque class alpha blended (as SShaderItem::IsTransparent).
  EDrawClass DrawClassOf(CShader *pSh, SRenderShaderResources *pRes)
  {
    EDrawClass eDC = pSh->GetDrawClass();
    if (pRes && pRes->m_Opacity < 1.0f)
    {
      switch (eDC)
      {
      case eDC_Opaque:
      case eDC_AlphaTest:
      case eDC_AlphaTestVColor:
      case eDC_VColor:
        eDC = eDC_AlphaBlend;
        break;
      default:
        break;
      }
    }
    return eDC;
  }

  bool IsWaterClass(EDrawClass eDC)
  {
    return eDC == eDC_Water || eDC == eDC_WaterBeach;
  }

  bool IsBlendedClass(EDrawClass eDC)
  {
    return eDC == eDC_AlphaBlend || eDC == eDC_Additive || IsWaterClass(eDC);
  }

  //! Elements drawn over the finished 3D scene, in this order (0: not one of them).
  int ScreenElementRank(const CRendElement *pRE)
  {
    switch (pRE->m_Type)
    {
    case eDATA_Flare:         return 1;
    case eDATA_2DQuad:        return 2;
    case eDATA_Glare:
    case eDATA_HDRProcess:
    case eDATA_FlashBang:     return 3;
    case eDATA_ScreenProcess: return 4;
    default:                  return 0;
    }
  }

  //! Rendering order of the EFSLIST_* lists of the SDK (IShader.h).
  int PassRank(int nSort)
  {
    switch ((nSort & EFSLIST_MASK) >> 28)
    {
    case EFSLIST_PREPROCESS_ID: return 0;
    case EFSLIST_STENCIL_ID:    return 1;
    case EFSLIST_UNSORTED_ID:   return 2;
    case EFSLIST_GENERAL_ID:    return 3;
    case EFSLIST_DISTSORT_ID:   return 4;
    case EFSLIST_LAST_ID:       return 5;
    default:                    return 3;
    }
  }

  int SortGroup(int nSort)
  {
    return std::clamp(nSort & ~EFSLIST_MASK, 0, (int)eS_Max - 1);
  }

  ERenderList ChooseList(const SRenderItem &it)
  {
    const int nList = (it.nSort & EFSLIST_MASK) >> 28;
    if (ScreenElementRank(it.pRE) || nList == EFSLIST_LAST_ID || nList == EFSLIST_DISTSORT_ID)
      return eRL_Blended;
    // The first person weapon: everything sorted eS_Nearest. A blended object with FOB_NEAREST
    // alone (the muzzle flash) is drawn among the blended ones, after the weapon.
    if (SortGroup(it.nSort) == eS_Nearest)
      return eRL_Nearest;
    if (IsBlendedClass(it.eDrawClass))
      return eRL_Blended;
    if (it.pObj->m_ObjFlags & FOB_NEAREST)
      return eRL_Nearest;
    return eRL_Opaque;
  }

  //! Ascending in the distance: unsigned compare of the bits of a float >= 0.
  uint32 DistanceBits(float fDist)
  {
    return std::bit_cast<uint32>(fDist > 0 ? fDist : 0.0f);
  }

  //! Ascending from far to near (32 bits).
  uint64 FarFirstBits(float fDist)
  {
    return (uint64)(0xFFFFFFFFu - DistanceBits(fDist));
  }

  uint64 MakeSortKey(const SRenderItem &it)
  {
    const uint64 nPass = (uint64)PassRank(it.nSort);
    const uint64 nGroup = (uint64)SortGroup(it.nSort);
    const bool bBlended = IsBlendedClass(it.eDrawClass);
    if (it.eList == eRL_Blended)
    {
      if (const int nRank = ScreenElementRank(it.pRE); nRank || nPass == 5)
      {
        // Over the scene: flares, screen quads, screen effects, then by sort group.
        return (1ull << 63) | ((uint64)nRank << 56) | (nGroup << 48);
      }
      // The water surfaces first (they show what the opaque scene left behind them) by sort group
      // (the ocean and water volumes before the beach), then the rest far to near.
      if (IsWaterClass(it.eDrawClass))
        return (nGroup << 32) | FarFirstBits(it.fDistance);
      return (1ull << 62) | (FarFirstBits(it.fDistance) << 30);
    }
    // Opaque and nearest: by pass, sort group, shader and resources; in the nearest list the
    // blended ones after the opaque ones, far to near.
    uint64 nKey = (nPass << 60) | (nGroup << 55);
    if (it.eList == eRL_Nearest && bBlended)
      return (1ull << 63) | nKey | (FarFirstBits(it.fDistance) >> 9);
    const uint64 nShader = (uint64)(it.pShader->GetID() & 0x7FFFFF);
    const uint64 nRes = it.pRes ? (uint64)(uint32)it.pRes->m_Id : 0;
    return nKey | (nShader << 32) | nRes;
  }

  //! The vertex format the leaf buffer of an element is checked with: the format of the
  //! buffer that holds its vertices (the draw classes of this renderer take every format). The
  //! lit draw classes ask for normals as well (P3F_N_TEX2F) when the buffer keeps them outside
  //! its vertices (m_TempNormals of a mesh with vertex colours): such an object is lit per vertex
  //! like any other, its vertex colours are not used.
  int VertexFormatOf(const SRenderItem &it)
  {
    if (it.pRE->m_Type == eDATA_OcLeaf)
    {
      CREOcLeaf *pLeaf = static_cast<CREOcLeaf *>(it.pRE);
      if (pLeaf->m_pBuffer)
      {
        const CLeafBuffer *pVC = pLeaf->m_pBuffer->GetVertexContainer();
        const bool bLit = it.eDrawClass == eDC_Opaque || it.eDrawClass == eDC_AlphaTest || it.eDrawClass == eDC_AlphaBlend;
        if (bLit && pVC->m_TempNormals)
          return VERTEX_FORMAT_P3F_N_TEX2F;
        return pVC->m_nVertexFormat;
      }
    }
    return it.pShader->GetVertexFormat();
  }

  //! The CheckUpdate flags of an element: characters get nCheckUpdate_Character. No tangent
  //! stream (VSM_TANGENTS): no draw class of this renderer uses it.
  int CheckUpdateFlagsOf(const SRenderItem &it)
  {
    return it.pObj->m_pCharInstance ? nCheckUpdate_Character : 0;
  }
}

//////////////////////////////////////////////////////////////////////////
// CScene

CScene::CScene() = default;

CScene::~CScene()
{
  Shutdown();
}

bool CScene::Init()
{
  m_bShutdown = false;
  if (!m_pDefaultObject)
  {
    m_pDefaultObject = std::make_unique<CCObject>();
    m_pDefaultObject->Init();
    m_pDefaultObject->m_Id = -1;
  }
  m_TempObjects.reserve(1024);
  if (CCObject::m_Waves.GetSize() < 1024)
    CCObject::m_Waves.Alloc(1024);
  if (CCObject::m_Waves.Num() == 0)
  {
    CCObject::m_Waves.AddIndex(1);
    CCObject::m_Waves[0] = SWaveForm2();
  }
  CCObject::m_ObjMatrices.reserve(1024);
  m_nDepth = 0;
  m_nDrawing = 0;
  m_nResetFrame = -1;
  return true;
}

void CScene::Shutdown()
{
  if (m_bShutdown)
    return;
  m_bShutdown = true;
  for (SSceneSlot &s : m_Slots)
  {
    s = SSceneSlot();
  }
  m_nDepth = 0;
  m_nDrawing = 0;
  m_nOverflow = 0;
  DestroyEngineElements();
  std::vector<CREScreenProcess *>().swap(m_ScreenProcesses);
  std::vector<std::unique_ptr<CCObject>>().swap(m_TempObjects);
  m_nTempUsed = 0;
  std::vector<SPermanent>().swap(m_Permanent);
  m_pDefaultObject.reset();
  std::vector<SFogVolume>().swap(m_FogVolumes);
  std::vector<CCObject *>().swap(m_Skinned);
  std::vector<const CCObject *>().swap(m_WaveOwners);
  CCObject::m_Waves.Free();
  CCObject::m_ObjMatrices.clear();
  CCObject::m_ObjMatrices.shrink();
}

void CScene::SSceneSlot::ClearItems()
{
  Items.clear();
  for (std::vector<SRenderItem> &l : Lists)
    l.clear();
  Sprites.clear();
  SpriteVerts.clear();
  SpriteIndices.clear();
}

void CScene::BeginFrame()
{
  if (m_nDrawing == 0)
  {
    // A scene left open (EF_StartEf without EF_EndEf3D) ends here.
    for (SSceneSlot &s : m_Slots)
      s.ClearItems();
    m_nDepth = 0;
    m_nOverflow = 0;
    if (g_pRenderer)
      g_pRenderer->m_nRecurseLevel = 0;
  }
  CheckNewFrame();
}

void CScene::StartEf()
{
  if (m_nDrawing == 0)
  {
    // A new main scene.
    CheckNewFrame();
    m_nDepth = 1;
    m_nOverflow = 0;
  }
  else
  {
    // A scene rendered from inside the one being drawn: a slot of its own.
    const int nDepth = std::max(m_nDepth, 1) + 1;
    if (nDepth > MAX_SCENE_DEPTH)
    {
      m_nOverflow++;
      return;
    }
    m_nDepth = nDepth;
  }
  SSceneSlot &s = Cur();
  s.ClearItems();
  s.Lights.clear();
  s.FakeLights.clear();
  BumpMatrixSerial();
  g_pRenderer->m_nRecurseLevel = m_nDepth;
}

void CScene::AddEf(int nFog, CRendElement *pRE, IShader *pEf, SRenderShaderResources *pRes, CCObject *pObj, int nTempl, IShader *pStateEf, int nSort)
{
  if (!pRE || !pEf || m_nOverflow)
    return;
  // Occlusion queries answer at once, also when their shader is not drawn.
  if (pRE->m_Type == eDATA_OcclusionQuery)
    pRE->mfPrepare();
  CShader *pSh = ResolveShader(pEf, nTempl);
  const EDrawClass eDC = DrawClassOf(pSh, pRes);
  if (eDC == eDC_NoDraw)
    return;

  SRenderItem it;
  it.pRE = pRE;
  it.pObj = pObj ? pObj : m_pDefaultObject.get();
  it.pShader = pSh;
  it.pRes = pRes;
  it.pStateShader = pStateEf;
  it.nFog = nFog;
  it.nTempl = nTempl;
  it.eDrawClass = eDC;
  if (nSort)
    it.nSort = nSort;
  else
  {
    int nShSort = (int)pSh->GetSort();
    if (!nShSort && pSh != pEf)
      nShSort = (int)pEf->GetSort();
    // As SShaderItem::GetSort: an opacity below 1 sorts an opaque shader with the blended ones.
    if (pRes && pRes->m_Opacity != 1.0f && (nShSort & ~EFSLIST_MASK) <= eS_Opaque)
      nShSort = eS_Banner | (nShSort & EFSLIST_MASK);
    it.nSort = nShSort;
  }
  it.eList = ChooseList(it);

  SSceneSlot &s = Cur();
  it.nOrder = (int)s.Items.size();
  s.Items.push_back(it);
  m_nFrameItems++;
}

int CScene::PrepareItems(SSceneSlot &Slot)
{
  // Items added from inside these calls (none expected) are not drawn: indices, no iterators.
  const size_t nItems = Slot.Items.size();

  // The video buffers first: CheckUpdate makes them on the first draw.
  for (size_t i = 0; i < nItems; i++)
  {
    const SRenderItem it = Slot.Items[i];
    it.pRE->mfPrepare();
    it.pRE->mfCheckUpdate(VertexFormatOf(it), CheckUpdateFlagsOf(it));
  }

  // Then the characters' skinned vertices, once per object: the engine writes them into the
  // video buffers (IRenderer::UpdateBuffer) from inside ProcessSkinning.
  m_Skinned.clear();
  for (size_t i = 0; i < nItems; i++)
  {
    const SRenderItem it = Slot.Items[i];
    IDeformableRenderMesh *pMesh = it.pObj->m_pCharInstance;
    if (!pMesh || std::find(m_Skinned.begin(), m_Skinned.end(), it.pObj) != m_Skinned.end())
      continue;
    m_Skinned.push_back(it.pObj);
    pMesh->ProcessSkinning(it.pObj->GetTranslation(), it.pObj->m_Matrix, it.nTempl, -1, false);
  }

  // The matrices the renderer derives from the objects, and the distances.
  for (size_t i = 0; i < nItems; i++)
  {
    SRenderItem &it = Slot.Items[i];
    it.pObj->GetVPMatrix();
    it.pObj->GetInvMatrix();
    it.fDistance = sqrtf(std::max(0.0f, it.pRE->mfDistanceToCameraSquared(*it.pObj)));
  }
  return (int)nItems;
}

void CScene::BuildLists(SSceneSlot &Slot, int nItems, bool bSort)
{
  for (std::vector<SRenderItem> &l : Slot.Lists)
    l.clear();
  for (int i = 0; i < nItems; i++)
  {
    SRenderItem &it = Slot.Items[i];
    it.nSortKey = MakeSortKey(it);
    Slot.Lists[it.eList].push_back(it);
  }
  if (!bSort)
    return;
  for (std::vector<SRenderItem> &l : Slot.Lists)
  {
    // Stable: equal keys keep the order of EF_AddEf (shadow volumes between their stencil clears).
    std::stable_sort(l.begin(), l.end(), [](const SRenderItem &a, const SRenderItem &b) { return a.nSortKey < b.nSortKey; });
  }
}

void CScene::EndEf3D(int nFlags)
{
  if (m_nOverflow)
  {
    m_nOverflow--;
    return;
  }
  SSceneSlot &s = Cur();
  m_nDrawing++;
  if (g_pRenderer)
    g_pRenderer->m_nRecurseLevel = std::max(m_nDepth, 1);
  if (m_nDrawing == 1)
  {
    UpdateScreenFx();
    // The scenes this one shows (the water reflection), rendered from here, nested in this one.
    g_pRenderer->UpdateWaterReflection(s.Items);
  }

  const int nItems = PrepareItems(s);
  BuildLists(s, nItems, (nFlags & SHDF_SORT) != 0);
  for (int l = 0; l < eRL_Num; l++)
  {
    std::vector<SRenderItem> &list = s.Lists[l];
    g_pRenderer->DrawItems(list.data(), (int)list.size(), l);
  }

  m_nDrawing--;
  s.ClearItems();
  if (m_nDepth > 0)
    m_nDepth--;
  g_pRenderer->m_nRecurseLevel = m_nDepth;
  BumpMatrixSerial();
}

CCObject *CScene::AddSprite(int nShaderID, int nPts, const SColorVert *pVerts, CCObject *pObj, const byte *pInds, int nInds, int nFog)
{
  if (!pObj)
    pObj = GetTempObject();
  if (m_nOverflow || nPts < 3 || !pVerts)
    return pObj;

  SSceneSlot &s = Cur();
  const int nFirstVert = (int)s.SpriteVerts.size();
  // Sprites of one object, shader and fog volume in a row make one batch.
  SSceneSprite *pBatch = s.Sprites.empty() ? nullptr : &s.Sprites.back();
  if (!pBatch || pBatch->pObj != pObj || pBatch->nShaderID != nShaderID || pBatch->nFog != nFog ||
      pBatch->nVerts + nPts > 0xFFFF)
  {
    SSceneSprite sp;
    sp.nShaderID = nShaderID;
    sp.pObj = pObj;
    sp.nFog = nFog;
    sp.nFirstVert = nFirstVert;
    sp.nFirstIndex = (int)s.SpriteIndices.size();
    s.Sprites.push_back(sp);
    pBatch = &s.Sprites.back();
  }
  const int nBase = nFirstVert - pBatch->nFirstVert;
  s.SpriteVerts.insert(s.SpriteVerts.end(), pVerts, pVerts + nPts);
  if (pInds && nInds >= 3)
  {
    for (int i = 0; i < nInds - nInds % 3; i++)
      s.SpriteIndices.push_back((ushort)(nBase + std::min((int)pInds[i], nPts - 1)));
    pBatch->nIndices += nInds - nInds % 3;
  }
  else
  {
    // A fan: (0 1 2), (0 2 3), ...
    for (int i = 1; i + 1 < nPts; i++)
    {
      s.SpriteIndices.push_back((ushort)nBase);
      s.SpriteIndices.push_back((ushort)(nBase + i));
      s.SpriteIndices.push_back((ushort)(nBase + i + 1));
    }
    pBatch->nIndices += (nPts - 2) * 3;
  }
  pBatch->nVerts += nPts;
  return pObj;
}

//////////////////////////////////////////////////////////////////////////
// Lights

void CScene::AddLight(CDLight *pLight)
{
  if (!pLight)
    return;
  CheckCorona(pLight);
  SSceneSlot &s = Cur();
  if (pLight->m_Flags & DLF_FAKE)
  {
    pLight->m_Id = -1;
    if (std::find(s.FakeLights.begin(), s.FakeLights.end(), pLight) == s.FakeLights.end())
      s.FakeLights.push_back(pLight);
    return;
  }
  auto it = std::find(s.Lights.begin(), s.Lights.end(), pLight);
  if (it != s.Lights.end())
  {
    pLight->m_Id = (int)(it - s.Lights.begin());
    return;
  }
  pLight->m_Id = (int)s.Lights.size();
  s.Lights.push_back(pLight);
}

void CScene::ClearLights()
{
  SSceneSlot &s = Cur();
  s.Lights.clear();
  s.FakeLights.clear();
}

CDLight *CScene::GetLight(int n)
{
  const SSceneSlot &s = Cur();
  return n >= 0 && n < (int)s.Lights.size() ? s.Lights[n] : nullptr;
}

CDLight *CScene::GetSun() const
{
  const SSceneSlot &s = Cur();
  for (auto it = s.Lights.rbegin(); it != s.Lights.rend(); ++it)
  {
    if ((*it)->m_Flags & DLF_SUN)
      return *it;
  }
  return nullptr;
}

Vec3 CScene::GetSunDirection() const
{
  const CDLight *pSun = GetSun();
  if (!pSun)
    return Vec3(0, 0, 1);
  Vec3 v = pSun->m_Origin - (g_pRenderer ? g_pRenderer->m_Camera.GetPos() : Vec3(0, 0, 0));
  if (v.GetLengthSquared() < 1e-6f)
    v = pSun->m_Origin;
  if (v.GetLengthSquared() < 1e-12f)
    return Vec3(0, 0, 1);
  v.Normalize();
  return v;
}

CFColor CScene::GetSunColor() const
{
  const CDLight *pSun = GetSun();
  return pSun ? pSun->m_Color : CFColor(0, 0, 0, 1);
}

void CScene::CheckCorona(CDLight *pLight)
{
  // A light with a corona (or a beam) gets an object of the renderer the first time it comes:
  // m_pObject[1][0], kept while the engine passes the same light; RemovePermanent frees it.
  if (pLight->m_fCoronaScale <= 0 || !pLight->m_pShader)
    return;
  CCObject *pObj = pLight->m_pObject[1][0];
  bool bNew = false;
  if (pObj && IsRemovedPermanent(pObj))
  {
    pObj = GetPermanentObject(pObj->m_Id);    // handed out again: starts anew
    bNew = true;
  }
  if (!pObj)
  {
    pObj = GetPermanentObject(-1);
    pLight->m_pObject[1][0] = pObj;
    bNew = true;
  }
  if (bNew)
  {
    bool bFlare = false;
    CRendElement *pFirst = nullptr;
    if (TArray<CRendElement *> *pREs = pLight->m_pShader->GetREs())
    {
      for (int i = 0; i < pREs->Num(); i++)
      {
        CRendElement *pRE = (*pREs)[i];
        if (!pRE)
          continue;
        if (!pFirst)
          pFirst = pRE;
        if (pRE->m_Type == eDATA_Flare)
          bFlare = true;
      }
    }
    pObj->m_ObjFlags = FOB_TRANS_MASK | FOB_BENDED | (bFlare ? FOB_HASALPHA : 0);
    if (!bFlare)
      pObj->m_RE = pFirst;
    pObj->m_Color = CFColor(pLight->m_Color.r, pLight->m_Color.g, pLight->m_Color.b, 0);
  }
  // Tested and drawn at the light's position.
  pObj->m_Matrix.SetIdentity();
  pObj->m_Matrix.SetTranslationOLD(pLight->m_Origin);
  pObj->m_Color.r = pLight->m_Color.r;
  pObj->m_Color.g = pLight->m_Color.g;
  pObj->m_Color.b = pLight->m_Color.b;
}

bool CScene::UpdateLight(CDLight *pLight)
{
  // Always false: the engine adds (EF_ADDDlight) only the lights for which this returns false.
  if (!pLight || !(pLight->m_Flags & DLF_PROJECT))
    return false;

  // R = Rz(z) * Ry(y) * Rx(x), right-handed rotations about the world axes (column vectors),
  // angles in degrees.
  constexpr float fDegToRad = std::numbers::pi_v<float> / 180.0f;
  const float ax = pLight->m_ProjAngles.x * fDegToRad;
  const float ay = pLight->m_ProjAngles.y * fDegToRad;
  const float az = pLight->m_ProjAngles.z * fDegToRad;
  const float cx = cosf(ax), sx = sinf(ax);
  const float cy = cosf(ay), sy = sinf(ay);
  const float cz = cosf(az), sz = sinf(az);
  const float R[3][3] =
  {
    { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx },
    { sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx },
    { -sy,     cy * sx,                cy * cx },
  };
  // The basis: forward R (1,0,0), up R (0,1,0), right R (0,0,1) = forward x up.
  const Vec3 F(R[0][0], R[1][0], R[2][0]);
  const Vec3 U(R[0][1], R[1][1], R[2][1]);
  const Vec3 V(R[0][2], R[1][2], R[2][2]);
  pLight->m_Orientation.m_vForward = F;
  pLight->m_Orientation.m_vUp = U;
  pLight->m_Orientation.m_vRight = V;

  // The texture matrix (p * M) takes a world point p to the cube lookup vector
  // (F.d, s U.d, s V.d), d = p - m_Origin, s = 1 / tan(m_fLightFrustumAngle).
  const float fAngle = std::clamp(pLight->m_fLightFrustumAngle, 0.01f, 179.99f) * fDegToRad;
  const float s = cosf(fAngle) / sinf(fAngle);
  const Vec3 sU = U * s;
  const Vec3 sV = V * s;
  const Vec3 &o = pLight->m_Origin;
  Matrix44 &M = pLight->m_TextureMatrix;
  M(0, 0) = F.x;  M(0, 1) = sU.x; M(0, 2) = sV.x; M(0, 3) = 0;
  M(1, 0) = F.y;  M(1, 1) = sU.y; M(1, 2) = sV.y; M(1, 3) = 0;
  M(2, 0) = F.z;  M(2, 1) = sU.z; M(2, 2) = sV.z; M(2, 3) = 0;
  M(3, 0) = -(F | o);
  M(3, 1) = -(sU | o);
  M(3, 2) = -(sV | o);
  M(3, 3) = 1;
  return false;
}

//////////////////////////////////////////////////////////////////////////
// Fog volumes

int CScene::RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, const CFColor &Color, int nIndex, bool bCaustics)
{
  if (nIndex <= 0)
  {
    m_FogVolumes.emplace_back();
    nIndex = (int)m_FogVolumes.size();
  }
  else if (nIndex > (int)m_FogVolumes.size())
    m_FogVolumes.resize(nIndex);
  SFogVolume &fv = m_FogVolumes[nIndex - 1];
  fv.fMaxFogDist = fMaxFogDist;
  fv.fFogLayerZ = fFogLayerZ;
  fv.Color = Color;
  fv.bCaustics = bCaustics;
  return nIndex;
}

const SFogVolume *CScene::GetFogVolume(int nId) const
{
  if (nId <= 0 || nId > (int)m_FogVolumes.size())
    return nullptr;
  return &m_FogVolumes[nId - 1];
}

//////////////////////////////////////////////////////////////////////////
// IRenderer

bool CD3D12Renderer::EF_PrecacheResource(CDLight *pLS, float fDist, float fTimeToReady, int Flags)
{
  return true;
}

void CD3D12Renderer::EF_EnableHeatVision(bool bEnable)
{
  m_bHeatVision = bEnable;
}

bool CD3D12Renderer::EF_GetHeatVision()
{
  return m_bHeatVision;
}

void CD3D12Renderer::EF_AddPolyToScene3D(int Ef, int numPts, SColorVert *verts, CCObject *obj, int nFogID)
{
  // Not drawn by this renderer.
}

CCObject *CD3D12Renderer::EF_AddSpriteToScene(int Ef, int numPts, SColorVert *verts, CCObject *obj, byte *inds, int ninds, int nFogID)
{
  if (!m_pScene)
    return obj;
  return m_pScene->AddSprite(Ef, numPts, verts, obj, inds, ninds, nFogID);
}

void CD3D12Renderer::EF_AddSplash(Vec3 Pos, eSplashType eST, float fForce, int Id)
{
  // No splashes.
}

CRendElement *CD3D12Renderer::EF_CreateRE(EDataType edt)
{
  return RE_Create(edt);
}

void CD3D12Renderer::EF_StartEf()
{
  if (m_pScene)
    m_pScene->StartEf();
}

CCObject *CD3D12Renderer::EF_GetObject(bool bTemp, int num)
{
  if (!m_pScene)
    return nullptr;
  return bTemp ? m_pScene->GetTempObject() : m_pScene->GetPermanentObject(num);
}

void CD3D12Renderer::EF_AddEf(int NumFog, CRendElement *re, IShader *ef, SRenderShaderResources *sr, CCObject *obj, int nTempl, IShader *efState, int nSort)
{
  if (m_pScene)
    m_pScene->AddEf(NumFog, re, ef, sr, obj, nTempl, efState, nSort);
}

void CD3D12Renderer::EF_EndEf3D(int nFlags)
{
  if (!m_pScene)
    return;
  const int nSavedFlags = m_nSceneFlags;
  m_nSceneFlags = nFlags;
  m_pScene->EndEf3D(nFlags);
  m_nSceneFlags = nSavedFlags;
}

bool CD3D12Renderer::EF_IsFakeDLight(CDLight *Source)
{
  return Source && (Source->m_Flags & DLF_FAKE) != 0;
}

void CD3D12Renderer::EF_ADDDlight(CDLight *Source)
{
  if (m_pScene)
    m_pScene->AddLight(Source);
}

void CD3D12Renderer::EF_ClearLightsList()
{
  if (m_pScene)
    m_pScene->ClearLights();
}

bool CD3D12Renderer::EF_UpdateDLight(CDLight *pDL)
{
  return CScene::UpdateLight(pDL);
}

void CD3D12Renderer::EF_SetWorldColor(float r, float g, float b, float a)
{
  m_WorldColor[0] = r;
  m_WorldColor[1] = g;
  m_WorldColor[2] = b;
  m_WorldColor[3] = a;
}

int CD3D12Renderer::EF_RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, CFColor color, int nIndex, bool bCaustics)
{
  if (!m_pScene)
    return 0;
  return m_pScene->RegisterFogVolume(fMaxFogDist, fFogLayerZ, color, nIndex, bCaustics);
}
