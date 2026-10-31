// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Cascades.cpp
//	Description: The sun's cascaded shadow maps.
//
//	With d3d12_ShadowCascades and the game's shadows on (e_shadow_maps) in
//	a view with the sun, the main view's depth up to d3d12_ShadowDistance
//	is split into cascades (the practical split: mostly logarithmic, partly
//	even), each with a slice of two texture arrays seen from the sun by an
//	orthographic camera. A slice is a square around the sphere of its part
//	of the view's frustum (the same size whatever the view's direction),
//	moved in whole texels, so that the shadows do not crawl as the camera
//	moves and turns. Its depth runs from the sphere's side towards the sun
//	plus a margin (0) to its other side (1); what is nearer to the sun
//	still is drawn at the near end (the depth clamped), so that it casts.
//
//	The full set holds all that casts: the static objects and the entities
//	the 3D engine gives a camera at the view's position looking towards the
//	sun, 170 degrees wide (the engine culls by its own frustum of a camera,
//	so the casters between the view and the sun come from there; at the
//	view's position the engine's choices of detail stay those of the view),
//	the main view's own items (the terrain, what is indoors) and the live
//	casters, each part once. The live set holds the casters the engine
//	names for its own shadows in the frame (characters, vehicles, the
//	vegetation that casts: AddLiveCasters), each only where nothing in the
//	full set is nearer to the sun (DF2_CASCADE_CASTER). A part of either
//	set goes into a slice only where its shadow falls on the slice's
//	receivers (the view's points that take their shadow from it): the
//	slice is not read elsewhere.
//
//	The receivers (SunCascades, Common.hlsli): the lit objects take the
//	sun's visibility from the full set; the light maps and the terrain,
//	whose light has the shadows of the static world already, from the live
//	set, the terrain in the colour of the engine's shadows on it (the
//	"EntityShadowOnTerrain" elements of the shadow maps, which fade out
//	where the cascades reach). Beyond them the shadow maps of the objects
//	(Shadows.cpp), as without the cascades.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Scene.h"

namespace
{

//! The share of the logarithmic split in the cascades' ends (the rest: the even split).
constexpr float CASCADE_SPLIT_LOG = 0.8f;
//! How much farther towards the sun than their spheres the maps reach (m).
constexpr float CASCADE_SUN_MARGIN_M = 100.0f;
//! The texels at each side of a map that the spheres leave for the filter.
constexpr int CASCADE_EDGE_TEXELS = 2;
//! The part of a cascade blended into the next one.
constexpr float CASCADE_BLEND = 0.1f;
//! The receivers' depth bias (m), besides a texel of their cascade.
constexpr float CASCADE_DEPTH_BIAS_M = 0.03f;
//! How much farther from the sun than the full set's map a live caster may be and still cast (m;
//! at least two texels).
constexpr float CASCADE_LIVE_EPS_M = 0.25f;
//! The camera whose objects cast into the full set: at the view's camera, looking towards the
//! sun, this wide up the screen.
constexpr float CASCADE_CAMERA_FOV_DEG = 170.0f;
//! What it draws (I3DEngine::DrawLowDetail): the static objects and the entities. Not the terrain:
//! its sectors for another camera get the textures for that camera, which the main view then
//! draws with (the terrain casts from the main view's sectors).
constexpr int CASCADE_CAMERA_FLAGS = DLD_STATIC_OBJECTS | DLD_ENTITIES;

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! The object's world matrix as DrawLeafItem takes it: the whole matrix with FOB_TRANS_* flags,
//! else its translation only.
Matrix44 GetWorldMatrix(const CCObject *pObj)
{
  if (pObj->m_ObjFlags & FOB_TRANS_MASK)
    return pObj->m_Matrix;
  Matrix44 m;
  m.SetIdentity();
  m.data[12] = pObj->m_Matrix.data[12];
  m.data[13] = pObj->m_Matrix.data[13];
  m.data[14] = pObj->m_Matrix.data[14];
  return m;
}

//! The texels around the outline of a cascade's receivers that a caster may still reach them in
//! (CascadeSample: the point moved off the surface by a texel and a half, the filter's bilinear
//! taps), besides the filter's radius.
constexpr float CASCADE_RECEIVER_TEXELS = 5.0f;
//! Around the camera in the first cascade's outline (m): the weapon in the hands, drawn with a
//! projection of its own.
constexpr float CASCADE_NEAR_MARGIN_M = 1.0f;

//! The frame of the sun's view: z towards the sun, x across, y up the map.
void SunBasis(const Vec3 &vToSun, Vec3 &x, Vec3 &y, Vec3 &z)
{
  z = vToSun;
  z.Normalize();
  x = Vec3(0, 0, 1).Cross(z);
  if (x.GetLength() < 1e-4f)
    x = Vec3(0, 1, 0).Cross(z);
  x.Normalize();
  y = z.Cross(x);
}

//! A part at its place: its render element and its object's position.
uint64 PartKey(const SRenderItem &it)
{
  const Vec3 v = it.pObj->GetTranslation();
  uint32 Bits[3];
  memcpy(Bits, &v, sizeof(Bits));
  uint64 h = (uint64)(uintptr_t)it.pRE;
  for (uint32 b : Bits)
    h = (h ^ b) * 0x100000001B3ull;
  return h;
}

//! The sphere of an item's leaf buffer in the world: the box's centre and half its diagonal times
//! the object's largest scale; r < 0 without a box.
void PartSphere(const SRenderItem &it, Vec3 &c, float &r)
{
  CLeafBuffer *pLB = static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer;
  const CLeafBuffer *pVC = pLB ? pLB->GetVertexContainer() : nullptr;
  r = -1;
  if (!pVC || !(pVC->m_vBoxMin.x <= pVC->m_vBoxMax.x))
    return;
  const Matrix44 World = GetWorldMatrix(it.pObj);
  const float *W = World.data;
  const Vec3 b = (pVC->m_vBoxMin + pVC->m_vBoxMax) * 0.5f;
  c = Vec3(b.x * W[0] + b.y * W[4] + b.z * W[8] + W[12], b.x * W[1] + b.y * W[5] + b.z * W[9] + W[13], b.x * W[2] + b.y * W[6] + b.z * W[10] + W[14]);
  float s = 0;
  for (int i = 0; i < 3; i++)
    s = std::max(s, Vec3(W[i * 4], W[i * 4 + 1], W[i * 4 + 2]).GetLength());
  r = (pVC->m_vBoxMax - pVC->m_vBoxMin).GetLength() * 0.5f * s;
}

}

bool CD3D12Renderer::SetupCascades(int nCascades)
{
  // The view's depth from its near plane to d3d12_ShadowDistance; the frustum's corners at depth d
  // are d * k from its axis (k^2 = tan^2 of the half angles across and up).
  const CCamera &Cam = m_Camera;
  const float fNear = std::max(Cam.GetZMin(), 0.05f);
  const float fFar = std::min(std::max(CV_d3d12_ShadowDistance, fNear + 1), std::max(Cam.GetZMax(), fNear + 1));
  const float tx = m_ProjMatrix.data[0] != 0 ? 1 / m_ProjMatrix.data[0] : 1, ty = m_ProjMatrix.data[5] != 0 ? 1 / m_ProjMatrix.data[5] : 1;
  const float k2 = tx * tx + ty * ty;
  Vec3 &X = m_vCascadeAxes[0], &Y = m_vCascadeAxes[1], &Z = m_vCascadeAxes[2];
  SunBasis(m_pScene->GetSunDirection(), X, Y, Z);
  Vec3 vDir(-m_CameraView.data[2], -m_CameraView.data[6], -m_CameraView.data[10]);
  vDir.Normalize();
  const Vec3 vPos = Cam.GetPos();
  const int nSize = m_nCascadeSize;
  // The frustum's sides at depth d: d times (1 + the off-centre term) over the scale, across and up.
  Vec3 vRight(m_CameraView.data[0], m_CameraView.data[4], m_CameraView.data[8]), vUp(m_CameraView.data[1], m_CameraView.data[5], m_CameraView.data[9]);
  vRight.Normalize();
  vUp.Normalize();
  const float fSideX = m_ProjMatrix.data[0] != 0 ? (1 + fabsf(m_ProjMatrix.data[8])) / fabsf(m_ProjMatrix.data[0]) * 1.01f : 1e3f;
  const float fSideY = m_ProjMatrix.data[5] != 0 ? (1 + fabsf(m_ProjMatrix.data[9])) / fabsf(m_ProjMatrix.data[5]) * 1.01f : 1e3f;
  float fStart = fNear, fBlendFrom = 0;
  for (int i = 0; i < nCascades; i++)
  {
    const float t = (float)(i + 1) / nCascades;
    const float fEnd = i + 1 == nCascades ? fFar : CASCADE_SPLIT_LOG * fNear * powf(fFar / fNear, t) + (1 - CASCADE_SPLIT_LOG) * (fNear + (fFar - fNear) * t);
    // The sphere through the corners of the part from fStart to fEnd: its centre on the axis where
    // the near and the far corners are as far, at most at the far end.
    const float c = std::min(fEnd, (fStart + fEnd) * (1 + k2) * 0.5f);
    const float r = sqrtf((fEnd - c) * (fEnd - c) + fEnd * fEnd * k2);
    const Vec3 vC = vPos + vDir * c;
    SCascade &Cas = m_Cascades[i];
    Cas = {};
    Cas.fEnd = fEnd;
    Cas.fHalfSize = r * nSize / (float)(nSize - 2 * CASCADE_EDGE_TEXELS);
    Cas.fTexel = 2 * Cas.fHalfSize / nSize;
    Cas.fCenter[0] = floorf(vC.Dot(X) / Cas.fTexel) * Cas.fTexel;
    Cas.fCenter[1] = floorf(vC.Dot(Y) / Cas.fTexel) * Cas.fTexel;
    Cas.fZMin = vC.Dot(Z) - r;
    Cas.fZMax = vC.Dot(Z) + r + CASCADE_SUN_MARGIN_M;
    // The world to the slice: x, y -1..1 across the square, z 0 at fZMax to 1 at fZMin.
    const float fDepth = 1 / (Cas.fZMax - Cas.fZMin);
    const Vec3 Rows[3] = { X / Cas.fHalfSize, Y / Cas.fHalfSize, Z * -fDepth };
    const float Offsets[3] = { -Cas.fCenter[0] / Cas.fHalfSize, -Cas.fCenter[1] / Cas.fHalfSize, Cas.fZMax * fDepth };
    float *M = Cas.WorldToMap.data;
    memset(M, 0, sizeof(Cas.WorldToMap.data));
    for (int k = 0; k < 3; k++)
    {
      M[k] = Rows[k].x;
      M[4 + k] = Rows[k].y;
      M[8 + k] = Rows[k].z;
      M[12 + k] = Offsets[k];
    }
    M[15] = 1;

    // Its receivers (SunCascades): the view's points from where the cascade before blends into it
    // (CascadeOf: over the last CASCADE_BLEND of that one, counted from 0 for the first) to its end;
    // the first one's from the camera. Seen from the sun, the outline of the corners of that part of
    // the frustum (counter-clockwise).
    struct SPoint
    {
      float x, y;
    };
    const float fFrom = i ? fStart - (fStart - fBlendFrom) * CASCADE_BLEND : 0;
    SPoint Points[8];
    for (int k = 0; k < 8; k++)
    {
      const float d = (k & 4) ? fEnd : fFrom;
      const Vec3 p = vPos + vDir * d + vRight * ((k & 1 ? 1 : -1) * d * fSideX) + vUp * ((k & 2 ? 1 : -1) * d * fSideY);
      Points[k] = { p.Dot(X), p.Dot(Y) };
    }
    std::sort(std::begin(Points), std::end(Points), [](const SPoint &a, const SPoint &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    const auto Turn = [](const SPoint &o, const SPoint &a, const SPoint &b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    SPoint Hull[16];
    int nHull = 0;
    for (int k = 0; k < 8; k++)
    {
      while (nHull >= 2 && Turn(Hull[nHull - 2], Hull[nHull - 1], Points[k]) <= 0)
        nHull--;
      Hull[nHull++] = Points[k];
    }
    for (int k = 6, nLower = nHull + 1; k >= 0; k--)
    {
      while (nHull >= nLower && Turn(Hull[nHull - 2], Hull[nHull - 1], Points[k]) <= 0)
        nHull--;
      Hull[nHull++] = Points[k];
    }
    nHull--;
    for (int k = 0; k < nHull && Cas.nEdges < 8; k++)
    {
      const SPoint &a = Hull[k], &b = Hull[(k + 1) % nHull];
      const float nx = b.y - a.y, ny = a.x - b.x, l = sqrtf(nx * nx + ny * ny);
      if (l <= 0)
        continue;
      float *e = Cas.Edges[Cas.nEdges++];
      e[0] = nx / l;
      e[1] = ny / l;
      e[2] = e[0] * a.x + e[1] * a.y;
    }
    Cas.fMargin = std::max(CV_d3d12_ShadowSoftness, 1.5f * Cas.fTexel) + CASCADE_RECEIVER_TEXELS * Cas.fTexel + (i ? 0 : CASCADE_NEAR_MARGIN_M);
    fBlendFrom = i ? fStart : 0;
    fStart = fEnd;
  }
  return fFar > fNear;
}

bool CD3D12Renderer::EnsureCascadeMaps()
{
  const int nSize = std::clamp(CV_d3d12_ShadowCascadeSize, 512, 4096) & ~1;
  if (m_nCascadeSize == nSize && m_CascadeMaps[0].pDepth && m_CascadeMaps[1].pDepth)
    return true;
  ReleaseCascades();
  for (SCascadeMaps &Maps : m_CascadeMaps)
  {
    for (D3D12_CPU_DESCRIPTOR_HANDLE &h : Maps.hDSV)
      h = m_pDescriptors->AllocDSV();
    bool bOk = std::all_of(std::begin(Maps.hDSV), std::end(Maps.hDSV), [](const D3D12_CPU_DESCRIPTOR_HANDLE &h) { return h.ptr != 0; }) &&
               m_pDevice->CreateShadowMapArray(nSize, XR_MAX_CASCADES, Maps.pDepth, Maps.hDSV);
    if (bOk)
    {
      D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
      Desc.Format = DXGI_FORMAT_R32_FLOAT;
      Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      Desc.Texture2DArray.MipLevels = 1;
      Desc.Texture2DArray.ArraySize = XR_MAX_CASCADES;
      Maps.nSRV = m_pDescriptors->AllocSRV(Maps.pDepth.Get(), &Desc);
      bOk = Maps.nSRV >= 0;
    }
    if (!bOk)
    {
      ReleaseCascades();
      return false;
    }
  }
  m_nCascadeSize = nSize;
  if (iLog)
    iLog->Log("XRenderD3D12: the sun's cascades: 2 x %d maps of %d x %d", XR_MAX_CASCADES, nSize, nSize);
  return true;
}

void CD3D12Renderer::UpdateSunCascades(std::span<const SRenderItem> MainItems)
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  if (m_nCascades)
  {
    m_nCascades = 0;
    InvalidateFrameConstants();
  }
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  const int nCascades = std::min(CV_d3d12_ShadowCascades, XR_MAX_CASCADES);
  if (nCascades <= 0 || !(m_nSceneFlags & SHDF_ALLOWHDR) || m_nRecurseLevel > 1 || m_bReflectionPass || m_bEnvCubePass || m_bSpritePass ||
      m_bShadowMapPass || m_bCascadePass || !p3D || !m_pDevice || !m_pDescriptors || !m_pScene || !m_pScene->GetSun() || !IsShadowMapsOn() ||
      !EnsureFrame() || !EnsureCascadeMaps() || !SetupCascades(nCascades))
    return;
  m_nCascades = nCascades;

  // The terrain in the shadow: the colour of the engine's shadows on it in this frame (the mean of
  // their objects' colours), else the last one seen.
  float Tint[3] = {};
  int nTint = 0;
  for (const SRenderItem &it : MainItems)
  {
    if (it.eDrawClass != eDC_TerrainShadow)
      continue;
    Tint[0] += it.pObj->m_Color.r;
    Tint[1] += it.pObj->m_Color.g;
    Tint[2] += it.pObj->m_Color.b;
    nTint++;
  }
  for (int i = 0; i < 3 && nTint; i++)
    m_ShadowTint[i] = Tint[i] / nTint;

  // What the casters' scenes change.
  const CCamera Camera = m_Camera;
  const Matrix44 CameraView = m_CameraView, ViewMatrix = m_ViewMatrix, ProjMatrix = m_ProjMatrix, SkyProjMatrix = m_SkyProjMatrix;
  const int nMatrixDepth = m_nMatrixDepth;
  CCamera ViewCamera = iSystem->GetViewCamera();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bFog = m_bFog;
  const float fFogStart = m_fFogStart, fFogEnd = m_fFogEnd;
  float FogColor[4], WorldColor[4];
  memcpy(FogColor, m_FogColor, sizeof(FogColor));
  memcpy(WorldColor, m_WorldColor, sizeof(WorldColor));
  const Vec3 vClearColor = m_vClearColor;
  const int nState = m_nState, nCullMode = m_nCullMode;
  auto ClearSet = [this](const SCascadeMaps &Maps)
  {
    for (int i = 0; i < m_nCascades; i++)
    {
      m_pDevice->SetShadowMapTarget(Maps.pDepth.Get(), Maps.hDSV[i], m_nCascadeSize);
      m_pDevice->ClearDepth(1.0f, 0, true, false);
    }
  };

  // The full set: what the 3D engine gives the camera towards the sun (its scene drawn by
  // DrawCascadeCasters), then the main view's parts that it did not have. The engine stretches a
  // camera's field of view across the screen: up the screen it is the one given times the
  // screen's height over its width.
  m_bCascadePass = true;
  m_nCascadeSet = 0;
  m_CascadeDrawn.clear();
  ClearSet(m_CascadeMaps[0]);
  const int w = std::max(GetWidth(), 1), h = std::max(GetHeight(), 1);
  CCamera SunCam = Camera;
  SunCam.Init(w, h, DEG2RAD(CASCADE_CAMERA_FOV_DEG) * w / h, std::max(Camera.GetZMax(), 1.0f), (float)h / w, 0.25f);
  SunCam.SetPos(Camera.GetPos());
  SunCam.SetAngle(ConvertVectorToCameraAngles(m_vCascadeAxes[2]));
  SunCam.Update();
  iSystem->SetViewCamera(SunCam);
  p3D->SetCamera(SunCam, false);
  {
    SCpuTime Time(m_fFrameLowDetailMs[LD_CASCADES]);
    p3D->DrawLowDetail(CASCADE_CAMERA_FLAGS);
  }
  iSystem->SetViewCamera(ViewCamera);
  p3D->SetCamera(Camera, false);
  m_Camera = Camera;
  const size_t nFromCamera = m_CascadeDrawn.size();
  DrawCascadeItems(MainItems, &m_CascadeDrawn);

  // The live set: the casters the engine named in the frame, as its shadow maps draw them.
  m_nCascadeSet = 1;
  ClearSet(m_CascadeMaps[1]);
  EF_StartEf();
  const int nLive = AddLiveCasters();
  EF_EndEf3D(0);
  m_bCascadePass = false;
  m_pDevice->SetBackBufferTargets();

  // The frame as it was.
  m_Camera = Camera;
  m_CameraView = CameraView;
  m_ViewMatrix = ViewMatrix;
  m_ProjMatrix = ProjMatrix;
  m_SkyProjMatrix = SkyProjMatrix;
  m_nMatrixDepth = nMatrixDepth;
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bFog = bFog;
  m_fFogStart = fFogStart;
  m_fFogEnd = fFogEnd;
  memcpy(m_FogColor, FogColor, sizeof(FogColor));
  memcpy(m_WorldColor, WorldColor, sizeof(WorldColor));
  m_vClearColor = vClearColor;
  m_nState = nState;
  m_nCullMode = nCullMode;
  m_nCascadeFrame = m_nFrames;
  InvalidateFrameConstants();

  if (m_pFrameDump)
  {
    const Vec3 &X = m_vCascadeAxes[0], &Y = m_vCascadeAxes[1], &Z = m_vCascadeAxes[2];
    fprintf(m_pFrameDump.get(), "sun cascades: %d of %d x %d, towards the sun (%.3f %.3f %.3f), x (%.3f %.3f %.3f), y (%.3f %.3f %.3f); %d parts from the camera towards the sun, %d in all; "
            "%d live casters; shadow tint (%.2f %.2f %.2f) of %d\n", m_nCascades, m_nCascadeSize, m_nCascadeSize, Z.x, Z.y, Z.z, X.x, X.y, X.z, Y.x, Y.y, Y.z, (int)nFromCamera,
            (int)m_CascadeDrawn.size(), nLive, m_ShadowTint[0], m_ShadowTint[1], m_ShadowTint[2], nTint);
    for (int i = 0; i < m_nCascades; i++)
    {
      const SCascade &Cas = m_Cascades[i];
      fprintf(m_pFrameDump.get(), "  cascade %d: to %.2f m, centre (%.2f %.2f), half size %.2f m, texel %.4f m, z %.2f to %.2f; %d full, %d live parts drawn\n", i, Cas.fEnd,
              Cas.fCenter[0], Cas.fCenter[1], Cas.fHalfSize, Cas.fTexel, Cas.fZMin, Cas.fZMax, Cas.nCasters[0], Cas.nCasters[1]);
    }
  }
}

void CD3D12Renderer::DrawCascadeItems(std::span<const SRenderItem> Items, std::unordered_set<uint64> *pDrawn)
{
  if (!m_nCascades || !EnsureFrame())
    return;
  // The parts that cast (as SetupShadowCasterDraw takes them; not the weapon in the hands, not the
  // light passes), once each, with their spheres.
  struct SPart
  {
    const SRenderItem *pItem;
    Vec3 c;
    float r;
    bool bDiscards;   //!< alpha tested (SetupShadowCasterDraw): its depth tested after its pixel shader
  };
  std::vector<SPart> Parts;
  Parts.reserve(Items.size());
  for (const SRenderItem &it : Items)
  {
    if (!it.pRE || it.pRE->m_Type != eDATA_OcLeaf || (it.pObj->m_ObjFlags & (FOB_NEAREST | FOB_LIGHTPASS)))
      continue;
    switch (it.eDrawClass)
    {
    case eDC_Opaque:
    case eDC_AlphaTest:
    case eDC_AlphaTestVColor:
    case eDC_VColor:
    case eDC_ShadowOnly:
    case eDC_Terrain:
      break;
    default:
      continue;
    }
    if (pDrawn && !pDrawn->insert(PartKey(it)).second)
      continue;
    SPart p = { &it, Vec3(0, 0, 0), -1 };
    PartSphere(it, p.c, p.r);
    p.bDiscards = it.eDrawClass == eDC_AlphaTest || it.eDrawClass == eDC_AlphaTestVColor || GetAlphaRef(it.pShader, it.pRes) > 0;
    Parts.push_back(p);
  }
  // Those that discard after the others (each kind in the order given, which keeps the draws of a
  // shader together): the pixels of a part that discards are tested for depth after its pixel
  // shader, so the fewer reach it the better (the hierarchical depth drops those behind what is
  // drawn already). The maps are the same in any order: the nearest depth of each texel.
  std::stable_partition(Parts.begin(), Parts.end(), [](const SPart &p) { return !p.bDiscards; });

  // Into each slice that a part's sphere reaches: across the square, not all of it farther from the
  // sun than the slice's range, and its shadow (the sphere drawn out away from the sun) over the
  // outline of the slice's receivers: elsewhere the slice is not read.
  const SCascadeMaps &Maps = m_CascadeMaps[m_nCascadeSet];
  const Vec3 &X = m_vCascadeAxes[0], &Y = m_vCascadeAxes[1], &Z = m_vCascadeAxes[2];
  auto Reaches = [&](const SPart &p, const SCascade &Cas)
  {
    if (p.r < 0)
      return true;
    const float x = p.c.Dot(X), y = p.c.Dot(Y);
    if (fabsf(x - Cas.fCenter[0]) > Cas.fHalfSize + p.r || fabsf(y - Cas.fCenter[1]) > Cas.fHalfSize + p.r || p.c.Dot(Z) + p.r < Cas.fZMin)
      return false;
    for (int e = 0; e < Cas.nEdges; e++)
      if (Cas.Edges[e][0] * x + Cas.Edges[e][1] * y - Cas.Edges[e][2] > p.r + Cas.fMargin)
        return false;
    return true;
  };
  auto SetSlice = [&](int i, D3D12_CPU_DESCRIPTOR_HANDLE hDSV)
  {
    m_pDevice->SetShadowMapTarget(Maps.pDepth.Get(), hDSV, m_nCascadeSize);
    m_CameraView = m_ViewMatrix = m_Cascades[i].WorldToMap;
    m_ProjMatrix.SetIdentity();
    m_SkyProjMatrix = m_ProjMatrix;
    m_nMatrixDepth = 0;
    m_bFog = false;
    m_nCascadeSlice = i;
    InvalidateFrameConstants();
  };
  m_bShadowMapPass = true;
  // The full set: each part once, into all the slices it reaches (an instance each; the depth is
  // the same as slice by slice, with one draw per part instead of one per slice). The live set
  // compares each slice with the full set's (SetupCascadeCaster): slice by slice.
  if (m_nCascadeSet == 0 && CV_d3d12_CascadeInstancing && !Parts.empty())
  {
    SetSlice(0, Maps.hDSV[XR_MAX_CASCADES]);
    for (const SPart &p : Parts)
    {
      m_nCascadeInstances = 0;
      for (int i = 0; i < m_nCascades; i++)
        if (Reaches(p, m_Cascades[i]))
        {
          m_CascadeInstances[m_nCascadeInstances++] = i;
          m_Cascades[i].nCasters[0]++;
        }
      if (m_nCascadeInstances)
        DrawLeafItem(*p.pItem);
    }
    m_nCascadeInstances = 0;
  }
  else
  {
    for (int i = 0; i < m_nCascades && !Parts.empty(); i++)
    {
      SCascade &Cas = m_Cascades[i];
      SetSlice(i, Maps.hDSV[i]);
      for (const SPart &p : Parts)
      {
        if (!Reaches(p, Cas))
          continue;
        DrawLeafItem(*p.pItem);
        Cas.nCasters[m_nCascadeSet]++;
      }
    }
  }
  m_bShadowMapPass = false;
  m_bItemScissor = false;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
}

void CD3D12Renderer::DrawCascadeCasters(std::span<const SRenderItem> Items)
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  // The live casters are in the full set too (what the camera towards the sun does not get: the
  // player's body), before their own set, which compares with it.
  const int nSet = m_nCascadeSet;
  m_nCascadeSet = 0;
  DrawCascadeItems(Items, &m_CascadeDrawn);
  m_nCascadeSet = nSet;
  if (nSet == 1)
    DrawCascadeItems(Items, nullptr);
}

void CD3D12Renderer::SetupCascadeCaster(SDrawConstants &dc) const
{
  dc.nFlags2 = 0;
  if (m_nCascadeSet != 1)
    return;
  // A live caster: only where the full set has nothing nearer to the sun (beyond the margin).
  const SCascade &Cas = m_Cascades[m_nCascadeSlice];
  dc.nFlags2 = DF2_CASCADE_CASTER;
  dc.nTex4 = (uint32)m_CascadeMaps[0].nSRV;
  dc.vParams3[0] = (float)m_nCascadeSlice;
  dc.vParams3[1] = std::max(CASCADE_LIVE_EPS_M, 2 * Cas.fTexel) / (Cas.fZMax - Cas.fZMin);
}

uint32 CD3D12Renderer::CascadeReceiverFlags(bool bLive) const
{
  if (!m_nCascades || m_nCascadeFrame != m_nFrames || m_bCascadePass || m_bShadowMapPass || m_bReflectionPass || m_bEnvCubePass || m_bSpritePass ||
      m_nRecurseLevel > 1 || !(m_nSceneFlags & SHDF_ALLOWHDR))
    return 0;
  return DF2_SUN_CASCADES | (bLive ? DF2_CASCADES_LIVE : 0);
}

bool CD3D12Renderer::IsWithinCascades(const Matrix44 &World, const Vec3 &vMin, const Vec3 &vMax) const
{
  if (!m_nCascades)
    return false;
  // The view depth (ViewDepth: |z| in the view) of the box's corners before the band over which
  // the last cascade gives way (CascadeOf); the depth is linear, so the box's is between theirs.
  const float fEnd = m_Cascades[m_nCascades - 1].fEnd, fStart = m_nCascades > 1 ? m_Cascades[m_nCascades - 2].fEnd : 0;
  const float fFull = fEnd - (fEnd - fStart) * CASCADE_BLEND;
  const Matrix44 WorldView = World * m_CameraView;
  for (int c = 0; c < 8; c++)
  {
    const Vec3 p((c & 1) ? vMax.x : vMin.x, (c & 2) ? vMax.y : vMin.y, (c & 4) ? vMax.z : vMin.z);
    const float z = p.x * WorldView.data[2] + p.y * WorldView.data[6] + p.z * WorldView.data[10] + WorldView.data[14];
    if (!(fabsf(z) < fFull))
      return false;
  }
  return true;
}

void CD3D12Renderer::SetCascadeConstants(SFrameConstants &fc) const
{
  if (!CascadeReceiverFlags(false))
    return;
  for (int i = 0; i < XR_MAX_CASCADES; i++)
  {
    const SCascade &Cas = m_Cascades[std::min(i, m_nCascades - 1)];
    memcpy(fc.mCascade[i], Cas.WorldToMap.data, sizeof(fc.mCascade[i]));
    fc.vCascadeEnd[i] = Cas.fEnd;
    fc.vCascadeTexel[i] = Cas.fTexel;
    fc.vCascadeDepth[i] = 1 / (Cas.fZMax - Cas.fZMin);
  }
  Set4(fc.vCascadeParams, (float)m_nCascades, std::max(CV_d3d12_ShadowSoftness, 0.0f), CASCADE_BLEND, CASCADE_DEPTH_BIAS_M);
  fc.nCascadeTex[0] = (uint32)m_CascadeMaps[0].nSRV;
  fc.nCascadeTex[1] = (uint32)m_CascadeMaps[1].nSRV;
  Set4(fc.vShadowTint, m_ShadowTint[0], m_ShadowTint[1], m_ShadowTint[2], (float)CV_d3d12_ShadowDebug);
}

void CD3D12Renderer::DrawCascadeDebug(int nRow)
{
  if (!m_nCascades || m_nCascadeFrame != m_nFrames)
    return;
  // A row for each set (the full one, then the live one), a square for each slice, the depth in
  // red (black near the sun).
  for (int nSet = 0; nSet < 2; nSet++)
  {
    for (int i = 0; i < m_nCascades; i++)
    {
      const float s = 120, x0 = 8 + i * (s + 8), y0 = 8 + (nRow + nSet) * (s + 8);
      struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F v[4];
      for (int k = 0; k < 4; k++)
      {
        const float u = (k == 1 || k == 2) ? 1.0f : 0.0f, t = k >= 2 ? 1.0f : 0.0f;
        v[k].xyz = Vec3(x0 + u * s, y0 + t * s, 0);
        v[k].color.dcolor = 0xFFFFFFFF;
        // u from 2 i to 2 i + 1: slice i (DF_TEX0_ARRAY).
        v[k].st[0] = 2 * i + u;
        v[k].st[1] = t;
      }
      static const ushort Inds[6] = { 0, 1, 2, 0, 2, 3 };
      SPipelineKey Key = {};
      Key.nState = GS_NODEPTHTEST;
      Key.nProgram = eD3D12P_General;
      Key.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB_TEX2F;
      Key.nCull = R_CULL_NONE;
      SDrawConstants dc = DefaultDrawConstants();
      dc.nFlags = DF_TEX0_ARRAY;
      dc.nTex0 = (uint32)m_CascadeMaps[nSet].nSRV;
      dc.nSamplers = XR_SAMPLER_POINT_CLAMP;
      const Matrix44 Ortho = MakeOrtho((float)m_Viewport[2], (float)m_Viewport[3]);
      memcpy(dc.mWorldViewProj, Ortho.data, sizeof(dc.mWorldViewProj));
      DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, v, 4, Inds, 6);
    }
  }
}

void CD3D12Renderer::ReleaseCascades()
{
  for (SCascadeMaps &Maps : m_CascadeMaps)
  {
    if (m_pDescriptors)
    {
      if (Maps.nSRV >= 0)
        m_pDescriptors->FreeSRV(Maps.nSRV);
      for (const D3D12_CPU_DESCRIPTOR_HANDLE &h : Maps.hDSV)
        if (h.ptr)
          m_pDescriptors->FreeDSV(h);
    }
    Maps.nSRV = -1;
    for (D3D12_CPU_DESCRIPTOR_HANDLE &h : Maps.hDSV)
      h = {};
    if (m_pDevice)
      m_pDevice->DeferredRelease(Maps.pDepth);
    Maps.pDepth.Reset();
  }
  m_nCascadeSize = 0;
  m_nCascades = 0;
  m_CascadeDrawn.clear();
}
