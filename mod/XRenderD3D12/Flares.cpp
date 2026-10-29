// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Flares.cpp
//	Description: The sun's flare and the coronas of the lights.
//
//	A flare is a square facing the camera, drawn additively over the 3D
//	scene of the main view (after everything else of it, before the screen
//	effects) with the map of its shader's corona, its colour faded by how
//	much of it is seen. The flares are the Flare items of the scene (the
//	sun's, with the engine's object) and the coronas of its lights (the
//	object CScene::CheckCorona gives the light in m_pObject[1][0]); only with
//	r_Coronas.
//
//	How much is seen comes from a test: a small white square at the flare,
//	without colour or depth writes, against the depth of the scene, inside
//	an occlusion query. The result arrives when the GPU has finished the
//	frame (two frames later) as a sample: the samples that passed over the
//	area of the square on the screen. The flare's object keeps the last 8
//	samples; their mean is the flare's alpha.
//
//	With r_checkSunVis 2 the sun's flare is two squares: the first depth
//	tested, so that what stands in front of the sun cuts it, and a second
//	one over everything, times a mask of the sky around the sun (the
//	screen's pixels, SUN_PATCH_SIZE of 768 rows, where the depth is the far
//	plane, blurred towards the sun; Post.hlsl), so that the sun glows
//	through gaps. Otherwise a flare is the first square without the depth
//	test.
//
//	A procedural flare (a material of FlareProc) is a glow around its
//	surface: the outline of the surface's vertices as the camera sees them,
//	grown across the view by FlareSize metres with rounded corners, added
//	with the material's colour, full inside the outline and fading out to
//	the grown edge, depth tested at its front: FlareSize nearer than the
//	surface (DrawProcFlare). Seen from the side it fades, gone edge on and
//	from behind.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Pipeline.h"
#include "D3D12Upload.h"
#include "Geometry.h"
#include "Texture.h"
#include "Scene.h"

#include <algorithm>
#include <array>

static_assert(CD3D12Renderer::FLARE_SLOTS == D3D12_FRAMES);

namespace
{

constexpr int FLARE_SAMPLES = 8;
constexpr float FLARE_TEST_SIZE = 0.2f;     //!< the side of the test square over the half side of the flare
// The sun's mask (Post.hlsl): its patch of the screen and the mask, in texels; one texel of the
// mask is a pixel of a screen SUN_MASK_ROWS rows high.
constexpr int SUN_PATCH_SIZE = 64;
constexpr int SUN_MASK_SIZE = 128;
constexpr float SUN_MASK_ROWS = 768;
// A procedural flare: the fall off of its glow (saturate(1 - u)^k from the outline out), the
// largest angle of a segment of its rounded corners and the cosine between its surface's front and
// the camera below which the glow fades (to nothing edge on).
constexpr float PROC_FLARE_FALLOFF = 1.8f;
constexpr float PROC_FLARE_CORNER_STEP = gf_PI / 8;
constexpr float PROC_FLARE_FADE_COS = 0.125f;

//! A flare of the scene being drawn.
struct SFlare
{
  CCObject *pObj;           //!< keeps the samples and the alpha
  CRendElement *pRE;        //!< the shader's Flare element (m_RE of a tested object)
  const SCorona *pCorona;
  Vec3 vPos;
  CFColor Color;            //!< rgb, before the alpha
  float fScale;             //!< the corona's Scale, times the light's m_fCoronaScale
  bool bSun;                //!< a Flare item (the sun's), not a light's corona
};

//! The samples of a flare's object, oldest first.
std::array<float *, FLARE_SAMPLES> Samples(CCObject *pObj)
{
  return { &pObj->m_AmbColor.x, &pObj->m_AmbColor.y, &pObj->m_AmbColor.z, &pObj->m_TempVars[3],
           &pObj->m_Angs2[0], &pObj->m_Angs2[1], &pObj->m_Angs2[2], &pObj->m_TempVars[4] };
}

float SampleMean(CCObject *pObj)
{
  float fSum = 0;
  for (float *p : Samples(pObj))
    fSum += *p;
  return fSum / FLARE_SAMPLES;
}

float CVarFloat(ICVar *&pVar, const char *szName, float fDefault)
{
  if (!pVar && iConsole)
    pVar = iConsole->GetCVar(szName);
  return pVar ? pVar->GetFVal() : fDefault;
}

//! p (row vector) through the matrix: x, y, z, w.
void Transform(const Matrix44 &m, const Vec3 &p, float Out[4])
{
  const float *d = m.data;
  for (int c = 0; c < 4; c++)
    Out[c] = p.x * d[c] + p.y * d[4 + c] + p.z * d[8 + c] + d[12 + c];
}

struct SPoint2
{
  float x, y;
  int nPoint;     //!< the point it was made from
};

//! The convex hull of the points, counter-clockwise (x right, y up); one or two points when they
//! are all at one place or on one line.
std::vector<SPoint2> ConvexHull(std::vector<SPoint2> Points)
{
  std::sort(Points.begin(), Points.end(), [](const SPoint2 &a, const SPoint2 &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  auto Cross = [](const SPoint2 &o, const SPoint2 &a, const SPoint2 &b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
  const size_t n = Points.size();
  if (n < 3)
    return Points;
  std::vector<SPoint2> Hull(2 * n);
  size_t k = 0;
  for (size_t i = 0; i < n; i++)
  {
    while (k >= 2 && Cross(Hull[k - 2], Hull[k - 1], Points[i]) <= 0)
      k--;
    Hull[k++] = Points[i];
  }
  for (size_t i = n - 1, t = k + 1; i > 0; i--)
  {
    while (k >= t && Cross(Hull[k - 2], Hull[k - 1], Points[i - 1]) <= 0)
      k--;
    Hull[k++] = Points[i - 1];
  }
  Hull.resize(k - 1);
  return Hull;
}

} // namespace

void CD3D12Renderer::DrawFlares(std::span<const SRenderItem> Items)
{
  if (m_bReflectionPass || m_bShadowMapPass || m_bSpritePass || m_nRecurseLevel > 1 || m_pDevice->IsOffscreen() || !m_pScene || !m_pTextures)
    return;
  if (CVarFloat(m_pCoronas, "r_Coronas", 0) == 0)
    return;

  // The flares: the Flare items with a corona, the coronas of the scene's lights.
  std::vector<SFlare> Flares;
  for (const SRenderItem &it : Items)
  {
    if (it.pRE && it.pRE->m_Type == eDATA_Flare && it.pShader && it.pShader->GetCorona() && it.pObj && it.pObj != m_pScene->GetDefaultObject())
    {
      const SCorona *pCorona = it.pShader->GetCorona();
      Flares.push_back({ it.pObj, it.pRE, pCorona, it.pObj->GetTranslation(), CFColor(pCorona->Color[0], pCorona->Color[1], pCorona->Color[2], 1),
                         pCorona->fScale, true });
    }
  }
  auto AddCoronas = [&](std::span<CDLight *const> Lights)
  {
    for (CDLight *pLight : Lights)
    {
      CCObject *pObj = pLight ? pLight->m_pObject[1][0] : nullptr;
      const CShader *pSh = pLight ? static_cast<const CShader *>(pLight->m_pShader) : nullptr;
      if (!pObj || !pSh || !pSh->GetCorona() || !pSh->GetFlare() || pLight->m_fCoronaScale <= 0 || !(pObj->m_ObjFlags & FOB_HASALPHA))
        continue;
      if (std::any_of(Flares.begin(), Flares.end(), [pObj](const SFlare &f) { return f.pObj == pObj; }))
        continue;
      const SCorona *pCorona = pSh->GetCorona();
      const CFColor Color = pCorona->bFromLight ? pLight->m_Color : CFColor(pCorona->Color[0], pCorona->Color[1], pCorona->Color[2], 1);
      Flares.push_back({ pObj, pSh->GetFlare(), pCorona, pLight->m_Origin, Color, pCorona->fScale * pLight->m_fCoronaScale, false });
    }
  };
  AddCoronas(m_pScene->GetLights());
  AddCoronas(m_pScene->GetFakeLights());
  if (Flares.empty())
    return;

  GpuSection("flares");
  const float fSizeScale = CVarFloat(m_pCoronaSizeScale, "r_CoronaSizeScale", 1);
  const float fTestInterval = CVarFloat(m_pCoronaFade, "r_CoronaFade", 0.125f) / 8;
  const float fNow = iTimer ? iTimer->GetCurrTime() : 0.0f;
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  const float *v = m_CameraView.data;
  const Vec3 vRight(v[0], v[4], v[8]), vUp(v[1], v[5], v[9]);
  const Vec3 vCamPos = m_Camera.GetPos();
  const float vx = (float)m_Viewport[0], vy = (float)m_Viewport[1], vw = (float)std::max(m_Viewport[2], 1), vh = (float)std::max(m_Viewport[3], 1);
  float fScaleX, fScaleY;
  GetSceneScale(fScaleX, fScaleY);
  const float fSampleScale = fScaleX * fScaleY;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
  m_bItemScissor = false;
  m_fDrawHeat = 0;

  // The half side of a flare: Scale * (the light's m_fCoronaScale) * r_CoronaSizeScale * distance^DistFactor.
  auto HalfSize = [&](const SFlare &f)
  {
    const float fDist = (f.vPos - vCamPos).GetLength();
    return f.fScale * fSizeScale * powf(fDist, f.pCorona->fDistFactor);
  };
  // The screen position (pixels, depth) of a point; false behind the camera.
  auto ToScreen = [&](const Vec3 &p, float &x, float &y, float &z)
  {
    float c[4];
    Transform(ViewProj, p, c);
    if (c[3] <= 1e-6f)
      return false;
    x = vx + (c[0] / c[3] * 0.5f + 0.5f) * vw;
    y = vy + (0.5f - c[1] / c[3] * 0.5f) * vh;
    z = c[2] / c[3];
    return true;
  };

  // The tests: a white square of the side FLARE_TEST_SIZE * h, depth tested, without colour or
  // depth writes, for each flare whose centre is on the screen, at most every r_CoronaFade / 8
  // seconds.
  std::vector<SFlareTest> &Tests = m_FlareTests[m_pDevice->GetFrameSlot()];
  std::vector<float> Sizes(Flares.size());
  for (size_t i = 0; i < Flares.size(); i++)
  {
    SFlare &f = Flares[i];
    CCObject *pObj = f.pObj;
    const float h = Sizes[i] = HalfSize(f);
    pObj->m_Color.r = f.Color.r;
    pObj->m_Color.g = f.Color.g;
    pObj->m_Color.b = f.Color.b;
    float sx, sy, sz;
    if (!ToScreen(f.vPos, sx, sy, sz) || sx < vx || sx >= vx + vw || sy < vy || sy >= vy + vh)
      continue;
    if (fNow >= pObj->m_fLightFadeTime && fNow - pObj->m_fLightFadeTime < fTestInterval && pObj->m_RE == f.pRE)
      continue;
    // The first test of an object: no samples yet, so the flare fades in.
    if (pObj->m_RE != f.pRE)
    {
      for (float *p : Samples(pObj))
        *p = 0;
      pObj->m_Color.a = 0;
      pObj->m_RE = f.pRE;
    }
    pObj->m_ObjFlags |= FOB_HASALPHA;
    pObj->m_fLightFadeTime = fNow;
    pObj->m_TempVars[2] = 0;
    pObj->m_Trans2[0] = sx;
    pObj->m_Trans2[1] = sy;
    pObj->m_Trans2[2] = sz;

    const float t = 0.5f * FLARE_TEST_SIZE * h;
    const Vec3 R = vRight * t, U = vUp * t;
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Quad[4] = {};
    const Vec3 Corners[4] = { f.vPos + R + U, f.vPos - R + U, f.vPos + R - U, f.vPos - R - U };
    float px[4], py[4];
    bool bProjected = true;
    for (int k = 0; k < 4; k++)
    {
      Quad[k].xyz = Corners[k];
      Quad[k].color.dcolor = 0xffffffff;
      float z;
      bProjected = ToScreen(Corners[k], px[k], py[k], z) && bProjected;
    }
    if (!bProjected)
      continue;
    // Its area on the screen (the strip's corners go round as 0, 1, 3, 2).
    const int Ring[4] = { 0, 1, 3, 2 };
    float fArea = 0;
    for (int k = 0; k < 4; k++)
    {
      const int a = Ring[k], b = Ring[(k + 1) % 4];
      fArea += px[a] * py[b] - px[b] * py[a];
    }
    // In the pixels the samples are counted in (an upscaler's scene is smaller).
    fArea = fabsf(fArea) * 0.5f * fSampleScale;
    const int nQuery = m_pDevice->BeginOcclusion();
    if (nQuery < 0)
      break;
    SPipelineKey Key = {};
    Key.nState = GS_NOCOLMASK;
    Key.nProgram = eD3D12P_General;
    Key.nCull = R_CULL_NONE;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Quad, 4);
    m_pDevice->EndOcclusion(nQuery);
    if ((int)Tests.size() <= nQuery)
      Tests.resize(nQuery + 1, SFlareTest{ nullptr, 0 });
    Tests[nQuery] = { pObj, fArea };
  }

  // The flares seen: a fan of 8 triangles around the centre (the corners and the middles of the
  // sides of a square facing the camera), the corona's map times the colour * min(alpha, 1),
  // added, no fog; without the depth test, but the sun's with r_checkSunVis 2, which has a second
  // fan over everything times the sun's mask (the map turned a quarter: u from the left, v from
  // the top, as the mask).
  const bool bSunMask = CVarFloat(m_pCheckSunVis, "r_checkSunVis", 1) >= 2;
  for (size_t i = 0; i < Flares.size(); i++)
  {
    const SFlare &f = Flares[i];
    CCObject *pObj = f.pObj;
    if (SampleMean(pObj) <= 0)
      continue;
    CTexture *pTex = GetFlareTexture(f.pCorona->Map);
    if (!pTex)
      continue;
    float sx, sy, sz;
    const bool bMask = f.bSun && bSunMask && ToScreen(f.vPos, sx, sy, sz) && DrawSunMask(sx, sy);
    const float a = std::clamp(pObj->m_Color.a, 0.0f, 1.0f);
    auto Byte = [](float x) { return (uint8)std::clamp(x * 255.0f, 0.0f, 255.0f); };
    UCol Col;
    Col.bcolor[0] = Byte(f.Color.b * a);
    Col.bcolor[1] = Byte(f.Color.g * a);
    Col.bcolor[2] = Byte(f.Color.r * a);
    Col.bcolor[3] = Byte(a);
    // The point (u, v) is at centre + h ((1 - 2v) R + (1 - 2u) U).
    static const float s_UV[10][2] = { { 0.5f, 0.5f }, { 0, 0 }, { 0, 0.5f }, { 0, 1 }, { 0.5f, 1 }, { 1, 1 }, { 1, 0.5f }, { 1, 0 }, { 0.5f, 0 }, { 0, 0 } };
    const float h = Sizes[i];
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Fan[10];
    for (int k = 0; k < 10; k++)
    {
      const float u = s_UV[k][0], w = s_UV[k][1];
      Fan[k].xyz = f.vPos + vRight * (h * (1 - 2 * w)) + vUp * (h * (1 - 2 * u));
      Fan[k].color = Col;
      Fan[k].st[0] = u;
      Fan[k].st[1] = w;
    }
    ushort Inds[24];
    for (int k = 0; k < 8; k++)
    {
      Inds[k * 3 + 0] = 0;
      Inds[k * 3 + 1] = (ushort)(k + 1);
      Inds[k * 3 + 2] = (ushort)(k + 2);
    }
    SPipelineKey Key = {};
    Key.nState = (bMask ? 0 : GS_NODEPTHTEST) | GS_BLSRC_ONE | GS_BLDST_ONE;
    Key.nProgram = eD3D12P_General;
    Key.nCull = R_CULL_NONE;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    dc.nFlags = DF_TEX0 | DF_VCOLOR;
    dc.nTex0 = pTex->GetSRV();
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
    if (m_pFrameDump)
    {
      fprintf(m_pFrameDump.get(), "  flare at (%.2f %.2f %.2f) half side %.3f alpha %.4f mean %.4f map '%s'%s\n", f.vPos.x, f.vPos.y, f.vPos.z, h,
              pObj->m_Color.a, SampleMean(pObj), f.pCorona->Map.c_str(), bMask ? ", with the sun's mask" : "");
      DumpDraw(dc, Key, pTex);
    }
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Fan, 10, Inds, 24);
    if (!bMask)
      continue;

    // The mask's uv from the world position: (0.5 + (P - c).R / 2h, 0.5 - (P - c).U / 2h).
    for (int k = 0; k < 10; k++)
    {
      const float u = Fan[k].st[0], w = Fan[k].st[1];
      Fan[k].st[0] = 1 - w;
      Fan[k].st[1] = u;
    }
    const float fInv = 0.5f / std::max(h, 1e-3f);
    dc.nFlags |= DF_TEX1 | DF_TEXGEN1_WORLD;
    dc.nTex1 = (uint32)m_pSunMask->m_nSRV;
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP | (XR_SAMPLER_LINEAR_CLAMP << 4);
    const Vec3 vU = vRight * fInv, vV = vUp * -fInv;
    dc.vTexGen1U[0] = vU.x; dc.vTexGen1U[1] = vU.y; dc.vTexGen1U[2] = vU.z; dc.vTexGen1U[3] = 0.5f - f.vPos.Dot(vU);
    dc.vTexGen1V[0] = vV.x; dc.vTexGen1V[1] = vV.y; dc.vTexGen1V[2] = vV.z; dc.vTexGen1V[3] = 0.5f - f.vPos.Dot(vV);
    Key.nState = GS_NODEPTHTEST | GS_BLSRC_ONE | GS_BLDST_ONE;
    if (m_pFrameDump)
      DumpDraw(dc, Key, pTex);
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Fan, 10, Inds, 24);
  }
}

void CD3D12Renderer::DrawProcFlare(const SRenderItem &it, const Matrix44 &World)
{
  // The vertices of the element's chunk, in the world.
  const CREOcLeaf *pLeaf = static_cast<const CREOcLeaf *>(it.pRE);
  const CMatInfo *pChunk = pLeaf->m_pChunk;
  const CLeafBuffer *pVC = pLeaf->m_pBuffer ? pLeaf->m_pBuffer->GetVertexContainer() : nullptr;
  if (!pChunk || !pVC)
    return;
  const CVertexBuffer *pVB = pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData ? pVC->m_pSecVertBuffer : pVC->m_pVertexBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
    return;
  const int nStride = CD3D12Pipeline::GetVertexSize(pVB->m_vertexformat);
  const int nFirst = std::max(pChunk->nFirstVertId, 0);
  const int nEnd = std::min(nFirst + pChunk->nNumVerts, pVB->m_NumVerts);
  const float fSize = it.pShader->GetProps().fFlareSize;
  if (nStride < (int)sizeof(Vec3) || nFirst >= nEnd || fSize <= 0)
    return;
  const byte *pData = (const byte *)pVB->m_VS[VSF_GENERAL].m_VData;
  std::vector<Vec3> Points;
  Vec3 vCenter(0, 0, 0);
  for (int i = nFirst; i < nEnd; i++)
  {
    float p[4];
    Transform(World, *(const Vec3 *)(pData + (size_t)i * nStride), p);
    Points.emplace_back(p[0], p[1], p[2]);
    vCenter += Points.back();
  }
  vCenter = vCenter / (float)Points.size();

  // Cull Back: no glow while the surface turns its back to the camera (its front: where its
  // triangles go round counter-clockwise; the other way in a mirroring matrix); seen from the side
  // the glow fades with the cosine between the front and the camera, from PROC_FLARE_FADE_COS down.
  Vec3 vFront(0, 0, 0);
  const std::span<const uint16> Tris = Geometry::GetTriangles(pLeaf->m_pBuffer, pChunk);
  auto At = [&](uint16 i) { return i >= nFirst && i < nEnd ? &Points[i - nFirst] : nullptr; };
  for (size_t i = 0; i + 2 < Tris.size(); i += 3)
  {
    const Vec3 *a = At(Tris[i]), *b = At(Tris[i + 1]), *c = At(Tris[i + 2]);
    if (a && b && c)
      vFront += (*b - *a).Cross(*c - *a);
  }
  const float *w = World.data;
  const float fDet = w[0] * (w[5] * w[10] - w[6] * w[9]) - w[1] * (w[4] * w[10] - w[6] * w[8]) + w[2] * (w[4] * w[9] - w[5] * w[8]);
  const Vec3 vCamPos = m_Camera.GetPos();
  float fFacing = 1;
  if (const float fLen = vFront.GetLength() * (vCamPos - vCenter).GetLength(); fLen > 0)
  {
    const float fCos = (fDet < 0 ? -1.0f : 1.0f) * vFront.Dot(vCamPos - vCenter) / fLen;
    if (fCos <= 0)
      return;
    fFacing = std::min(fCos / PROC_FLARE_FADE_COS, 1.0f);
  }

  // Their outline as the camera sees them: on the plane through their centre across the line of
  // sight, each point where its line of sight meets it (x right, y up), without points closer than
  // a millimetre. Off the middle of the view the camera's own axes would turn and fold the glow.
  Vec3 vDir = vCenter - vCamPos;
  const float fCenterDist = vDir.GetLength();
  if (fCenterDist < 1e-3f)
    return;
  vDir = vDir / fCenterDist;
  const float *v = m_CameraView.data;
  Vec3 vRight = vDir.Cross(Vec3(v[1], v[5], v[9]));
  if (vRight.GetLength() < 1e-3f)
    vRight = Vec3(v[0], v[4], v[8]);
  vRight.Normalize();
  const Vec3 vUp = vRight.Cross(vDir);
  std::vector<Vec3> Seen(Points.size());
  std::vector<SPoint2> Flat;
  for (size_t i = 0; i < Points.size(); i++)
  {
    const float fDepth = (Points[i] - vCamPos).Dot(vDir);
    if (fDepth < 1e-3f)
      return;
    Seen[i] = vCamPos + (Points[i] - vCamPos) * (fCenterDist / fDepth);
    Flat.push_back({ (Seen[i] - vCenter).Dot(vRight), (Seen[i] - vCenter).Dot(vUp), (int)i });
  }
  std::vector<SPoint2> Hull = ConvexHull(std::move(Flat));
  auto Near = [](const SPoint2 &a, const SPoint2 &b) { return fabsf(a.x - b.x) < 1e-3f && fabsf(a.y - b.y) < 1e-3f; };
  Hull.erase(std::unique(Hull.begin(), Hull.end(), Near), Hull.end());
  while (Hull.size() > 1 && Near(Hull.front(), Hull.back()))
    Hull.pop_back();
  const size_t n = Hull.size();
  if (!n)
    return;

  // The outline's points (u 0) and around each of them an arc fSize out on the plane (u 1), from
  // the outward normal of the edge before it to that of the edge after it (all around a single
  // point).
  std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> Verts;
  std::vector<ushort> Inds;
  auto AddVertex = [&](const Vec3 &vPos, float u)
  {
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Vert = {};
    Vert.xyz = vPos;
    Vert.color.dcolor = 0xffffffff;
    Vert.st[0] = u;
    Verts.push_back(Vert);
    return (ushort)(Verts.size() - 1);
  };
  for (const SPoint2 &p : Hull)
    AddVertex(Seen[p.nPoint], 0);
  std::vector<std::pair<ushort, ushort>> Arcs(n);     // the first and the last vertex of each corner's arc
  for (size_t i = 0; i < n; i++)
  {
    const SPoint2 &c = Hull[i];
    float a0 = 0, fSpan = 2 * gf_PI;
    if (n > 1)
    {
      const SPoint2 &a = Hull[(i + n - 1) % n], &b = Hull[(i + 1) % n];
      a0 = atan2f(-(c.x - a.x), c.y - a.y);
      fSpan = atan2f(-(b.x - c.x), b.y - c.y) - a0;
      while (fSpan < 0)
        fSpan += 2 * gf_PI;
    }
    const int nSegments = std::max(1, (int)ceilf(fSpan / PROC_FLARE_CORNER_STEP - 1e-3f));
    for (int k = 0; k <= nSegments; k++)
    {
      const float a = a0 + fSpan * k / nSegments;
      const ushort nVert = AddVertex(Seen[c.nPoint] + vRight * (fSize * cosf(a)) + vUp * (fSize * sinf(a)), 1);
      if (k == 0)
        Arcs[i].first = nVert;
      else
        Inds.insert(Inds.end(), { (ushort)i, (ushort)(nVert - 1), nVert });
      Arcs[i].second = nVert;
    }
  }
  // Inside the outline, and along each of its edges a band from the edge out.
  for (size_t i = 1; i + 1 < n; i++)
    Inds.insert(Inds.end(), { 0, (ushort)i, (ushort)(i + 1) });
  for (size_t i = 0; i < n && n > 1; i++)
  {
    const size_t j = (i + 1) % n;
    Inds.insert(Inds.end(), { (ushort)i, Arcs[i].second, Arcs[j].first, (ushort)i, Arcs[j].first, (ushort)j });
  }
  // The glow is fSize deep around the surface: its depth is that of its front, every point moved
  // along its line of sight fSize nearer (at most half way), so that the lamp around the surface
  // does not cut it.
  for (struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F &Vert : Verts)
  {
    const Vec3 vToCam = vCamPos - Vert.xyz;
    const float fDist = vToCam.GetLength();
    if (fDist > 1e-3f)
      Vert.xyz += vToCam * (std::min(fSize, 0.5f * fDist) / fDist);
  }

  // The material's colour (the ambient of its light material) times the object's colour and the
  // fade, added, depth tested, fogged towards black.
  const SLightMaterial *pMat = it.pRes ? it.pRes->m_LMaterial : nullptr;
  const CFColor MatColor = pMat ? pMat->Front.m_Ambient : CFColor(1, 1, 1, 1);
  const CCObject *pObj = it.pObj;
  SPipelineKey Key = {};
  Key.nState = GS_BLSRC_ONE | GS_BLDST_ONE;
  Key.nProgram = eD3D12P_General;
  Key.nCull = R_CULL_NONE;
  Key.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB_TEX2F;
  SDrawConstants dc = DefaultDrawConstants();
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
  dc.nFlags = m_bFog ? DF_FOG : 0;
  dc.nFlags2 = DF2_GLOW_FALLOFF | DF2_FOG_BLACK;
  dc.vParams0[0] = PROC_FLARE_FALLOFF;
  dc.vColor[0] = MatColor.r * pObj->m_Color.r * fFacing;
  dc.vColor[1] = MatColor.g * pObj->m_Color.g * fFacing;
  dc.vColor[2] = MatColor.b * pObj->m_Color.b * fFacing;
  dc.vColor[3] = 1;
  if (m_pFrameDump)
  {
    fprintf(m_pFrameDump.get(), "  proc flare: %d vertices, outline %d, size %g, centre (%.2f %.2f %.2f), facing %.3f", nEnd - nFirst, (int)n, fSize,
            vCenter.x, vCenter.y, vCenter.z, fFacing);
    if (pMat)
      fprintf(m_pFrameDump.get(), ", material ambient (%.3f %.3f %.3f) diffuse (%.3f %.3f %.3f) specular (%.3f %.3f %.3f) emission (%.3f %.3f %.3f)",
              pMat->Front.m_Ambient.r, pMat->Front.m_Ambient.g, pMat->Front.m_Ambient.b, pMat->Front.m_Diffuse.r, pMat->Front.m_Diffuse.g,
              pMat->Front.m_Diffuse.b, pMat->Front.m_Specular.r, pMat->Front.m_Specular.g, pMat->Front.m_Specular.b, pMat->Front.m_Emission.r,
              pMat->Front.m_Emission.g, pMat->Front.m_Emission.b);
    fprintf(m_pFrameDump.get(), ", object colour (%.3f %.3f %.3f %.3f) ambient (%.3f %.3f %.3f)\n", pObj->m_Color.r, pObj->m_Color.g, pObj->m_Color.b,
            pObj->m_Color.a, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z);
    DumpDraw(dc, Key, nullptr);
  }
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Verts.data(), (int)Verts.size(), Inds.data(),
                (int)Inds.size());
}

bool CD3D12Renderer::DrawSunMask(float sx, float sy)
{
  if (!m_pUpload || !m_pDescriptors)
    return false;
  if (!m_pSunPatch)
    m_pSunPatch = m_pUpload->CreateRenderTarget(SUN_PATCH_SIZE, SUN_PATCH_SIZE, DXGI_FORMAT_R8G8B8A8_UNORM);
  if (!m_pSunMask)
    m_pSunMask = m_pUpload->CreateRenderTarget(SUN_MASK_SIZE, SUN_MASK_SIZE, DXGI_FORMAT_R8G8B8A8_UNORM);
  if (!m_pSunPatch || !m_pSunMask || m_pSunPatch->m_nSRV < 0 || m_pSunMask->m_nSRV < 0)
    return false;
  // The depth of the scene so far (the copy the blended list's water made, when there is one), in
  // its pixels (an upscaler's scene is smaller than the back buffer).
  if (!m_bSceneCopied && !CopySceneForWater())
    return false;
  float fScaleX, fScaleY;
  GetSceneScale(fScaleX, fScaleY);
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  SDrawConstants dc = DefaultDrawConstants();
  dc.nFlags = POST_SUN_PATCH;
  dc.nTex0 = (uint32)m_nSceneDepthSRV;
  dc.vParams1[0] = sx * fScaleX;
  dc.vParams1[1] = sy * fScaleY;
  dc.vParams1[2] = h / SUN_MASK_ROWS;
  dc.vParams1[3] = (float)w;
  dc.vParams2[0] = (float)h;
  DrawPostPass(m_pSunPatch, dc);
  dc = DefaultDrawConstants();
  dc.nFlags = POST_SUN_MASK;
  dc.nTex0 = (uint32)m_pSunPatch->m_nSRV;
  DrawPostPass(m_pSunMask, dc);
  m_pDevice->SetBackBufferTargets();
  return true;
}

void CD3D12Renderer::ReadFlareTests()
{
  if (!m_pDevice)
    return;
  std::vector<SFlareTest> &Tests = m_FlareTests[m_pDevice->GetFrameSlot()];
  const std::span<const uint64> Results = m_pDevice->GetOcclusionResults();
  for (size_t i = 0; i < Tests.size() && i < Results.size(); i++)
  {
    CCObject *pObj = Tests[i].pObj;
    if (!pObj)
      continue;
    // The newest sample at the end; the mean is the alpha (kept while it is 0).
    const std::array<float *, FLARE_SAMPLES> s = Samples(pObj);
    for (int k = 0; k + 1 < FLARE_SAMPLES; k++)
      *s[k] = *s[k + 1];
    *s[FLARE_SAMPLES - 1] = (float)Results[i] / std::max(Tests[i].fArea, 1.0f);
    if (const float fMean = SampleMean(pObj); fMean > 0)
      pObj->m_Color.a = fMean;
  }
  Tests.clear();
}

void CD3D12Renderer::ForgetFlare(const CCObject *pObj)
{
  for (std::vector<SFlareTest> &Tests : m_FlareTests)
    for (SFlareTest &t : Tests)
      if (t.pObj == pObj)
        t.pObj = nullptr;
}

CTexture *CD3D12Renderer::GetFlareTexture(const std::string &Map)
{
  if (Map.empty() || !m_pTextures)
    return nullptr;
  auto it = std::find_if(m_FlareTextures.begin(), m_FlareTextures.end(), [&](const auto &e) { return e.first == Map; });
  if (it == m_FlareTextures.end())
  {
    CTexture *pTex = m_pTextures->LoadTexture(Map.c_str(), 0, 0, eTT_Base, -1, -1, -1, 0);
    if (pTex && (!pTex->IsTextureLoaded() || pTex->IsCube()))
      pTex = nullptr;
    m_FlareTextures.emplace_back(Map, pTex);
    it = m_FlareTextures.end() - 1;
  }
  return it->second;
}

void CD3D12Renderer::ReleaseFlares()
{
  for (std::vector<SFlareTest> &Tests : m_FlareTests)
    Tests.clear();
  m_FlareTextures.clear();
  if (m_pUpload)
  {
    m_pUpload->ReleaseTexture(m_pSunPatch);
    m_pUpload->ReleaseTexture(m_pSunMask);
  }
  m_pSunPatch = m_pSunMask = nullptr;
  m_pCoronas = m_pCoronaSizeScale = m_pCoronaFade = m_pCheckSunVis = nullptr;
}
