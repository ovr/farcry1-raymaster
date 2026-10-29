// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: Shadows.cpp
//	Description: The shadows from the sun: shadow maps.
//
//	A caster has a frustum of the 3D engine (RendererStructures.h, "Shadow
//	maps") in a space of its own: the world moved to the caster instance's
//	position and scaled by its scale. A static object's frustum comes from
//	MakeShadowMapFrustum at the level load and lists the objects the map
//	shows, in that space without a rotation, so one map serves every
//	instance of the object; an entity's frustum is made by the engine and
//	lists the entities to draw (in the world, as they are now). Objects that
//	receive a shadow (the terrain under a caster: "EntityShadowOnTerrain"
//	elements; static objects) come with CCObject::m_pShadowCasters, the
//	caster instances whose maps fall on them.
//
//	Before the main view is drawn, every frustum named by the casters of
//	its items gets its map: once for a static object (a map unused for a
//	while is freed and drawn again when it is needed again), in every frame
//	for an entity. A map holds the depth of the opaque and alpha tested
//	parts (both sides) seen from the sun by an orthographic camera, in a
//	circle around the caster as wide as the caster.
//
//	The receivers sample the maps of their casters (SunVisibility,
//	Common.hlsli): a soft filter of 16 depth comparisons, the shadow fading
//	out towards the circle's edge. Lit objects lose the sun's light where
//	it is hidden (DF_SHADOWS); the terrain gets its shadow blended over it
//	(eDC_TerrainShadow: the sector texture in the shadow's colour).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Texture.h"
#include "Scene.h"

// The header calls min and max unqualified.
using std::max;
using std::min;
#include <IEntityRenderState.h>

#include <chrono>

using namespace RendererStructures;

namespace
{

//! A static object's map not used for this many frames is freed.
constexpr int SHADOW_MAP_KEEP_FRAMES = 600;
//! The sizes d3d12_ShadowMapSize may give.
constexpr int SHADOW_MAP_MIN_SIZE = 128, SHADOW_MAP_MAX_SIZE = 4096;
//! The receivers' comparison: a depth bias in metres, the filter's radius in texels and the
//! offset of the receiving point along its normal in metres.
constexpr float SHADOW_DEPTH_BIAS_M = 0.03f;
constexpr float SHADOW_FILTER_TEXELS = 2.0f;
constexpr float SHADOW_NORMAL_OFFSET_M = 0.04f;

//! The layout of an SDK list2 (its members are private).
struct SRawList2
{
  byte *pElements;
  int nCount;
  int nAllocatedCount;
};
static_assert(sizeof(SRawList2) == 16);

template <class T> T &At(void *p, int nOffset)
{
  return *reinterpret_cast<T *>(static_cast<byte *>(p) + nOffset);
}

template <class T> const T &At(const void *p, int nOffset)
{
  return *reinterpret_cast<const T *>(static_cast<const byte *>(p) + nOffset);
}

//! The frustums of a light source: their list2 is part of it.
const SRawList2 &Frustums(const void *pLs)
{
  return At<SRawList2>(pLs, nBin64Offset_ShadowMapLightSource_Frustums);
}

//! The static objects a frustum shows (nullptr for an entity's).
const SRawList2 *FrustumModels(const void *pFrustum)
{
  const SRawList2 *pList = At<SRawList2 *>(pFrustum, nBin64Offset_ShadowMapFrustum_Models);
  return pList && pList->nCount > 0 ? pList : nullptr;
}

//! The entities a frustum shows (nullptr for a static object's).
const SRawList2 *FrustumEntities(const void *pFrustum)
{
  const SRawList2 *pList = At<SRawList2 *>(pFrustum, nBin64Offset_ShadowMapFrustum_Entities);
  return pList && pList->nCount > 0 ? pList : nullptr;
}

//! The frame of the sun's view of a frustum: z from vAt towards the light, x across, y up the map.
void ShadowBasis(const Vec3 &vLight, const Vec3 &vAt, Vec3 &x, Vec3 &y, Vec3 &z)
{
  z = vLight - vAt;
  z.Normalize();
  x = Vec3(0, 0, 1).Cross(z);
  if (x.GetLength() < 1e-4f)
    x = Vec3(0, 1, 0).Cross(z);
  x.Normalize();
  y = z.Cross(x);
}

}

ShadowMapFrustum * CD3D12Renderer::MakeShadowMapFrustum(ShadowMapFrustum * lof, ShadowMapLightSource * pLs, const Vec3 & obj_pos, list2<IStatObj*> * pStatObjects, int shadow_type)
{
  if (!lof || !pLs)
    return lof;
  // The objects' radius about obj_pos: the farthest corner of their boxes.
  float r = 0;
  const SRawList2 *pObjects = reinterpret_cast<const SRawList2 *>(pStatObjects);
  for (int i = 0; pObjects && i < pObjects->nCount; i++)
  {
    IStatObj *pObj = reinterpret_cast<IStatObj *const *>(pObjects->pElements)[i];
    if (!pObj)
      continue;
    const Vec3 vMin = pObj->GetBoxMin(), vMax = pObj->GetBoxMax();
    for (int k = 0; k < 8; k++)
    {
      const Vec3 c((k & 1) ? vMax.x : vMin.x, (k & 2) ? vMax.y : vMin.y, (k & 4) ? vMax.z : vMin.z);
      r = std::max(r, (c - obj_pos).GetLength());
    }
  }
  r = std::max(r, 0.1f);
  const Vec3 vLight = At<Vec3>(pLs, nBin64Offset_ShadowMapLightSource_Pos);
  const float d = std::max((vLight - obj_pos).GetLength(), 2 * r);

  At<ShadowMapLightSource *>(lof, nBin64Offset_ShadowMapFrustum_LightSource) = pLs;
  At<float>(lof, nBin64Offset_ShadowMapFrustum_Fov) = 2 * atanf(r / d) * (180.0f / gf_PI);
  At<float>(lof, nBin64Offset_ShadowMapFrustum_Near) = d - r;
  At<float>(lof, nBin64Offset_ShadowMapFrustum_Far) = d + r;
  At<Vec3>(lof, nBin64Offset_ShadowMapFrustum_ObjPos) = obj_pos;
  // The objects the map shows, into the engine's list (as far as its room goes: its memory is the
  // engine's).
  SRawList2 *pModels = At<SRawList2 *>(lof, nBin64Offset_ShadowMapFrustum_Models);
  for (int i = 0; pModels && pObjects && i < pObjects->nCount && pModels->nCount < pModels->nAllocatedCount; i++)
    reinterpret_cast<IStatObj **>(pModels->pElements)[pModels->nCount++] = reinterpret_cast<IStatObj *const *>(pObjects->pElements)[i];
  return lof;
}

void CD3D12Renderer::NoteShadowCasters(const CCObject *pObj)
{
  if (pObj && pObj->m_pShadowCasters && CV_d3d12_Shadows && !m_bShadowMapPass)
    m_ShadowCasterLists.push_back(pObj->m_pShadowCasters);
}

CD3D12Renderer::SShadowMap *CD3D12Renderer::FindShadowMap(const void *pFrustum)
{
  auto It = m_ShadowMaps.find(At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_DepthTexID));
  if (It == m_ShadowMaps.end() || It->second.pFrustum != pFrustum)
    return nullptr;
  return &It->second;
}

CD3D12Renderer::SShadowMap *CD3D12Renderer::FindDrawnShadowMap(const void *pFrustum)
{
  SShadowMap *pMap = FindShadowMap(pFrustum);
  if (!pMap || At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_Drawn) != 1 || (pMap->bEntities && pMap->nDrawnFrame != m_nFrames))
    return nullptr;
  return pMap;
}

void CD3D12Renderer::UpdateShadowMaps()
{
  std::vector<const void *> Lists;
  Lists.swap(m_ShadowCasterLists);
  if (!CV_d3d12_Shadows || m_bShadowMapPass || m_bSpritePass || m_bReflectionPass || !m_pDevice || !m_pDescriptors || !m_pScene ||
      !EnsureFrame())
    return;

  // The frustums whose maps are not drawn (for this frame), once each, with the position of
  // their caster instance.
  struct SToDraw
  {
    void *pFrustum;
    Vec3 vOrigin;
  };
  std::vector<SToDraw> ToDraw;
  for (const void *pList : Lists)
  {
    const SRawList2 &Casters = *static_cast<const SRawList2 *>(pList);
    for (int i = 0; i < Casters.nCount; i++)
    {
      const byte *pInst = Casters.pElements + (size_t)i * nBin64Size_ShadowMapLightSourceInstance;
      const void *pLs = At<void *>(pInst, nBin64Offset_ShadowMapLightSourceInstance_LightSource);
      if (!pLs)
        continue;
      const SRawList2 &Fr = Frustums(pLs);
      for (int f = 0; f < Fr.nCount; f++)
      {
        void *pFrustum = Fr.pElements + (size_t)f * nBin64Size_ShadowMapFrustum;
        if (SShadowMap *pMap = FindDrawnShadowMap(pFrustum))
          pMap->nLastUsedFrame = m_nFrames;
        else if (std::find_if(ToDraw.begin(), ToDraw.end(), [pFrustum](const SToDraw &t) { return t.pFrustum == pFrustum; }) == ToDraw.end())
          ToDraw.push_back({ pFrustum, At<Vec3>(pInst, nBin64Offset_ShadowMapLightSourceInstance_Pos) });
      }
    }
  }
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "shadow maps: %d caster lists, %d maps to draw, %d maps\n", (int)Lists.size(), (int)ToDraw.size(), (int)m_ShadowMaps.size());

  // Maps unused for a while.
  for (auto It = m_ShadowMaps.begin(); It != m_ShadowMaps.end();)
  {
    if (m_nFrames - It->second.nLastUsedFrame > (It->second.bEntities ? 1 : SHADOW_MAP_KEEP_FRAMES))
    {
      FreeShadowMap(It->second);
      It = m_ShadowMaps.erase(It);
    }
    else
      ++It;
  }
  if (ToDraw.empty())
    return;

  // What the maps' scenes change.
  const CCamera Camera = m_Camera;
  const Matrix44 CameraView = m_CameraView, ViewMatrix = m_ViewMatrix, ProjMatrix = m_ProjMatrix, SkyProjMatrix = m_SkyProjMatrix;
  const int nMatrixDepth = m_nMatrixDepth;
  const bool bFog = m_bFog;
  const auto tStart = std::chrono::steady_clock::now();
  int nDrawn = 0, nStatic = 0;
  for (const SToDraw &t : ToDraw)
  {
    if (DrawShadowMap(t.pFrustum, t.vOrigin))
    {
      nDrawn++;
      nStatic += FrustumModels(t.pFrustum) ? 1 : 0;
    }
  }
  m_pDevice->SetBackBufferTargets();
  m_Camera = Camera;
  m_CameraView = CameraView;
  m_ViewMatrix = ViewMatrix;
  m_ProjMatrix = ProjMatrix;
  m_SkyProjMatrix = SkyProjMatrix;
  m_nMatrixDepth = nMatrixDepth;
  m_bFog = bFog;
  InvalidateFrameConstants();
  if (nStatic && iLog)
    iLog->Log("XRenderD3D12: %d shadow maps of static objects drawn (%d in all) in %.1f ms", nStatic, (int)m_ShadowMaps.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count());
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "shadow maps: %d of %d drawn\n", nDrawn, (int)ToDraw.size());
}

bool CD3D12Renderer::DrawShadowMap(void *pFrustum, const Vec3 &vOrigin)
{
  void *pLs = At<void *>(pFrustum, nBin64Offset_ShadowMapFrustum_LightSource);
  const SRawList2 *pModels = FrustumModels(pFrustum);
  const SRawList2 *pEntities = pModels ? nullptr : FrustumEntities(pFrustum);
  const float fNear = At<float>(pFrustum, nBin64Offset_ShadowMapFrustum_Near), fFar = At<float>(pFrustum, nBin64Offset_ShadowMapFrustum_Far);
  const float r = (fFar - fNear) * 0.5f;
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "shadow map of frustum %p: light source %p, %d objects, %d entities, origin (%.2f %.2f %.2f), near %g far %g\n", pFrustum, pLs,
            pModels ? pModels->nCount : 0, pEntities ? pEntities->nCount : 0, vOrigin.x, vOrigin.y, vOrigin.z, fNear, fFar);
  if (!pLs || (!pModels && !pEntities) || !(r > 0))
    return false;
  const Vec3 vLight = At<Vec3>(pLs, nBin64Offset_ShadowMapLightSource_Pos);
  const Vec3 vAt = At<Vec3>(pFrustum, nBin64Offset_ShadowMapFrustum_ObjPos);
  const float d = (vLight - vAt).GetLength();
  if (m_pFrameDump)
  {
    const Vec3 vDir = (vLight - vAt) / std::max(d, 1e-6f);
    fprintf(m_pFrameDump.get(), "  light (%.1f %.1f %.1f), at (%.2f %.2f %.2f): direction (%.3f %.3f %.3f), fov %g\n", vLight.x, vLight.y, vLight.z, vAt.x,
            vAt.y, vAt.z, vDir.x, vDir.y, vDir.z, At<float>(pFrustum, nBin64Offset_ShadowMapFrustum_Fov));
  }
  // Across the light: the frustum's field of view at vAt (an entity's frustum is wider than its
  // depth range), else the radius.
  const float fFov = At<float>(pFrustum, nBin64Offset_ShadowMapFrustum_Fov) * (gf_PI / 180.0f);
  const float rWidth = fFov > 0 ? std::max(d * tanf(fFov * 0.5f), r) : r;

  // The map: d3d12_ShadowMapSize, else the frustum's size; a power of two. An entity's map is
  // drawn into the one it had in the last frame.
  int nWanted = CV_d3d12_ShadowMapSize > 0 ? CV_d3d12_ShadowMapSize : At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_Size);
  nWanted = std::clamp(nWanted, SHADOW_MAP_MIN_SIZE, SHADOW_MAP_MAX_SIZE);
  int nSize = SHADOW_MAP_MIN_SIZE;
  while (nSize * 2 <= nWanted)
    nSize *= 2;
  int nID = At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_DepthTexID);
  SShadowMap *pMap = FindShadowMap(pFrustum);
  if (pMap && pMap->nSize != nSize)
  {
    FreeShadowMap(*pMap);
    m_ShadowMaps.erase(nID);
    pMap = nullptr;
  }
  if (!pMap)
  {
    SShadowMap Map;
    Map.pFrustum = pFrustum;
    Map.nSize = nSize;
    Map.hDSV = m_pDescriptors->AllocDSV();
    if (!Map.hDSV.ptr || !m_pDevice->CreateShadowMap(nSize, Map.pDepth, Map.hDSV))
    {
      FreeShadowMap(Map);
      return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Format = DXGI_FORMAT_R32_FLOAT;
    Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Texture2D.MipLevels = 1;
    Map.nSRV = m_pDescriptors->AllocSRV(Map.pDepth.Get(), &Desc);
    if (Map.nSRV < 0)
    {
      FreeShadowMap(Map);
      return false;
    }
    nID = m_nNextShadowMapID++;
    pMap = &(m_ShadowMaps[nID] = std::move(Map));
  }
  pMap->bEntities = pEntities != nullptr;
  pMap->fRadius = r;
  pMap->nLastUsedFrame = m_nFrames;
  pMap->nDrawnFrame = m_nFrames;

  // The caster's space to the map: x, y -1..1 across the circle of radius rWidth about vAt, z 0..1
  // from r in front of vAt to r behind it, seen from the light (orthographic: the light is far away).
  Vec3 x, y, z;
  ShadowBasis(vLight, vAt, x, y, z);
  Matrix44 &M = pMap->LocalToMap;
  memset(M.data, 0, sizeof(M.data));
  const Vec3 Rows[3] = { x / rWidth, y / rWidth, z * (-0.5f / r) };
  for (int c = 0; c < 3; c++)
  {
    M.data[c] = Rows[c].x;
    M.data[4 + c] = Rows[c].y;
    M.data[8 + c] = Rows[c].z;
    M.data[12 + c] = -vAt.Dot(Rows[c]);
  }
  M.data[14] += 0.5f;
  M.data[15] = 1;

  // The depth of the frustum's objects (in the caster's space), or of its entities (in the world:
  // moved by the instance's position).
  Matrix44 View = M;
  if (pEntities)
    for (int c = 0; c < 4; c++)
      View.data[12 + c] -= vOrigin.x * M.data[c] + vOrigin.y * M.data[4 + c] + vOrigin.z * M.data[8 + c];
  m_pDevice->SetShadowMapTarget(pMap->pDepth.Get(), pMap->hDSV, nSize);
  m_pDevice->ClearDepth(1.0f, 0, true, false);
  m_CameraView = m_ViewMatrix = View;
  m_ProjMatrix.SetIdentity();
  m_SkyProjMatrix = m_ProjMatrix;
  m_nMatrixDepth = 0;
  m_bFog = false;
  m_bShadowMapPass = true;
  InvalidateFrameConstants();
  EF_StartEf();
  for (int i = 0; pModels && i < pModels->nCount; i++)
  {
    IStatObj *pObj = reinterpret_cast<IStatObj *const *>(pModels->pElements)[i];
    CLeafBuffer *pLB = pObj ? pObj->GetLeafBuffer() : nullptr;
    if (!pLB)
      continue;
    CCObject *pOb = m_pScene->GetTempObject();
    pOb->m_Matrix.SetIdentity();
    pOb->m_Color = CFColor(1, 1, 1, 1);
    pOb->m_AmbColor = Vec3(1, 1, 1);
    pLB->AddRenderElements(pOb, 0, -1, 0, 0, nullptr);
  }
  for (int i = 0; pEntities && i < pEntities->nCount; i++)
  {
    IEntityRender *pEnt = reinterpret_cast<IEntityRender *const *>(pEntities->pElements)[i];
    if (!pEnt)
      continue;
    // As the main view draws it (its level of detail at its distance from the camera); characters
    // draw nothing without FOB_RENDER_INTO_SHADOWMAP, and the objects' matrices turn them only
    // with FOB_TRANS_MASK (a vehicle, the things a character holds).
    SRendParams Params;
    Params.vPos = pEnt->GetPos();
    Params.vAngles = pEnt->GetAngles();
    Params.fScale = pEnt->GetScale();
    Params.fDistance = (Params.vPos - m_Camera.GetPos()).GetLength();
    Params.fSQDistance = Params.fDistance * Params.fDistance;
    Params.dwFObjFlags = FOB_RENDER_INTO_SHADOWMAP | FOB_TRANS_MASK;
    if (m_pFrameDump)
      fprintf(m_pFrameDump.get(), "  entity at (%.2f %.2f %.2f) angles (%.1f %.1f %.1f) scale %g\n", Params.vPos.x, Params.vPos.y, Params.vPos.z, Params.vAngles.x,
              Params.vAngles.y, Params.vAngles.z, Params.fScale);
    pEnt->DrawEntity(Params);
  }
  EF_EndEf3D(0);
  m_bShadowMapPass = false;

  // What the engine reads back: the light's projection and view (right handed, as D3DX's
  // PerspectiveFovRH and LookAtRH from the light to vAt with z up), the map's ID, drawn.
  float *pProj = &At<float>(pFrustum, nBin64Offset_ShadowMapFrustum_Proj);
  memset(pProj, 0, 16 * sizeof(float));
  pProj[0] = pProj[5] = fFov > 0 ? 1 / tanf(fFov * 0.5f) : d / r;
  pProj[10] = fFar / (fNear - fFar);
  pProj[11] = -1;
  pProj[14] = fNear * fFar / (fNear - fFar);
  float *pView = &At<float>(pFrustum, nBin64Offset_ShadowMapFrustum_View);
  memset(pView, 0, 16 * sizeof(float));
  const Vec3 Axes[3] = { x, y, z };
  for (int c = 0; c < 3; c++)
  {
    pView[c] = Axes[c].x;
    pView[4 + c] = Axes[c].y;
    pView[8 + c] = Axes[c].z;
    pView[12 + c] = -vLight.Dot(Axes[c]);
  }
  pView[15] = 1;
  At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_DepthTexID) = nID;
  At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_Drawn) = 1;
  At<int>(pFrustum, nBin64Offset_ShadowMapFrustum_Drawn2) = 1;
  return true;
}

bool CD3D12Renderer::SetupShadowCasterDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it)
{
  // Opaque and alpha tested parts; the alpha test with the texture's alpha only.
  const float fRef = GetAlphaRef(it.pShader, it.pRes);
  float fAlphaRef = 0;
  switch (it.eDrawClass)
  {
  case eDC_Opaque:
  case eDC_VColor:
  case eDC_ShadowOnly:
    fAlphaRef = fRef;
    break;
  case eDC_AlphaTest:
    fAlphaRef = fRef > 0 ? fRef : 0.5f;
    break;
  case eDC_AlphaTestVColor:
    fAlphaRef = fRef > 0 ? fRef : 128.0f / 255.0f;
    break;
  default:
    return false;
  }
  Key.nTarget = 2;
  Key.nState = GS_DEPTHWRITE;
  Key.nCull = R_CULL_NONE;
  Key.bDepthBias = 1;
  Key.bWireframe = 0;
  const uint32 nTexFlags = dc.nFlags & (DF_TEX0 | DF_TEXMATRIX);
  dc.nFlags = fAlphaRef > 0 && (nTexFlags & DF_TEX0) ? nTexFlags | DF_ALPHATEST | DF_ALPHA_FROM_TEX : 0;
  dc.fAlphaRef = fAlphaRef;
  return true;
}

bool CD3D12Renderer::SetupShadowReceiver(SDrawConstants &dc, const CCObject *pObj)
{
  if (!CV_d3d12_Shadows || !pObj->m_pShadowCasters || m_bShadowMapPass || m_bSpritePass || m_bEnvCubePass)
    return false;
  const SRawList2 &Casters = *reinterpret_cast<const SRawList2 *>(pObj->m_pShadowCasters);
  uint32 n = 0;
  for (int i = 0; i < Casters.nCount && n < XR_MAX_DRAW_SHADOWS; i++)
  {
    const byte *pInst = Casters.pElements + (size_t)i * nBin64Size_ShadowMapLightSourceInstance;
    const void *pLs = At<void *>(pInst, nBin64Offset_ShadowMapLightSourceInstance_LightSource);
    if (!pLs)
      continue;
    const Vec3 vPos = At<Vec3>(pInst, nBin64Offset_ShadowMapLightSourceInstance_Pos);
    float s = At<float>(pInst, nBin64Offset_ShadowMapLightSourceInstance_Scale);
    if (!(s > 0))
      s = 1;
    const SRawList2 &Fr = Frustums(pLs);
    for (int f = 0; f < Fr.nCount && n < XR_MAX_DRAW_SHADOWS; f++)
    {
      SShadowMap *pMap = FindDrawnShadowMap(Fr.pElements + (size_t)f * nBin64Size_ShadowMapFrustum);
      if (!pMap)
        continue;
      pMap->nLastUsedFrame = m_nFrames;
      // The world to the caster's space ((p - vPos) / s), then to its map; the depth bias (metres in
      // the world) folded into the depth.
      const float *L = pMap->LocalToMap.data;
      float *M = dc.mShadow[n];
      for (int c = 0; c < 4; c++)
      {
        M[c] = L[c] / s;
        M[4 + c] = L[4 + c] / s;
        M[8 + c] = L[8 + c] / s;
        M[12 + c] = L[12 + c] - (vPos.x * L[c] + vPos.y * L[4 + c] + vPos.z * L[8 + c]) / s;
      }
      M[14] -= SHADOW_DEPTH_BIAS_M / (2 * pMap->fRadius * s);
      dc.nShadowTex[n] = (uint32)pMap->nSRV;
      if (m_pFrameDump)
        fprintf(m_pFrameDump.get(), "    shadow map %d: SRV %d%s, caster (%.2f %.2f %.2f) scale %g, world to map x (%g %g %g %g) y (%g %g %g %g) z (%g %g %g %g)\n", n, pMap->nSRV,
                pMap->bEntities ? " (entities)" : "", vPos.x, vPos.y, vPos.z, s, M[0], M[4], M[8], M[12], M[1], M[5], M[9], M[13], M[2], M[6], M[10], M[14]);
      n++;
    }
  }
  if (!n)
    return false;
  dc.nFlags |= DF_SHADOWS | (CV_d3d12_ShadowDebug == 2 ? DF_SHADOW_DEBUG : 0);
  dc.vShadowParams[0] = (float)n;
  dc.vShadowParams[1] = 0;
  dc.vShadowParams[2] = SHADOW_FILTER_TEXELS;
  dc.vShadowParams[3] = SHADOW_NORMAL_OFFSET_M;
  return true;
}

void CD3D12Renderer::DrawShadowMapDebug()
{
  if (!CV_d3d12_ShadowDebug || m_bShadowMapPass || m_bSpritePass || m_bReflectionPass || m_Viewport[2] <= 0 || m_Viewport[3] <= 0)
    return;
  // The maps used in this frame in a row at the top left, their depth in red (black near the sun).
  int n = 0;
  for (const auto &[nID, Map] : m_ShadowMaps)
  {
    if (Map.nLastUsedFrame != m_nFrames || n >= 8)
      continue;
    const float s = 120, x0 = 8 + n * (s + 8), y0 = 8;
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F v[4];
    for (int i = 0; i < 4; i++)
    {
      const float u = (i == 1 || i == 2) ? 1.0f : 0.0f, t = i >= 2 ? 1.0f : 0.0f;
      v[i].xyz = Vec3(x0 + u * s, y0 + t * s, 0);
      v[i].color.dcolor = 0xFFFFFFFF;
      v[i].st[0] = u;
      v[i].st[1] = t;
    }
    static const ushort Inds[6] = { 0, 1, 2, 0, 2, 3 };
    SPipelineKey Key = {};
    Key.nState = GS_NODEPTHTEST;
    Key.nProgram = eD3D12P_General;
    Key.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB_TEX2F;
    Key.nCull = R_CULL_NONE;
    SDrawConstants dc = DefaultDrawConstants();
    dc.nFlags = DF_TEX0;
    dc.nTex0 = (uint32)Map.nSRV;
    dc.nSamplers = XR_SAMPLER_POINT_CLAMP;
    const Matrix44 Ortho = MakeOrtho((float)m_Viewport[2], (float)m_Viewport[3]);
    memcpy(dc.mWorldViewProj, Ortho.data, sizeof(dc.mWorldViewProj));
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, v, 4, Inds, 6);
    if (m_pFrameDump)
      fprintf(m_pFrameDump.get(), "debug square %d: SRV %d entities %d radius %g\n", n, Map.nSRV, Map.bEntities ? 1 : 0, Map.fRadius);
    n++;
  }
}

void CD3D12Renderer::FreeShadowMap(SShadowMap &Map)
{
  if (m_pDescriptors)
  {
    if (Map.nSRV >= 0)
      m_pDescriptors->FreeSRV(Map.nSRV);
    if (Map.hDSV.ptr)
      m_pDescriptors->FreeDSV(Map.hDSV);
  }
  Map.nSRV = -1;
  Map.hDSV = {};
  if (m_pDevice)
    m_pDevice->DeferredRelease(Map.pDepth);
  Map.pDepth.Reset();
}

void CD3D12Renderer::ReleaseShadowMaps()
{
  for (auto &[nID, Map] : m_ShadowMaps)
    FreeShadowMap(Map);
  m_ShadowMaps.clear();
  m_ShadowCasterLists.clear();
}
