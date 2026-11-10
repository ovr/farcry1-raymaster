// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Mirrors.cpp
//	Description: The templates that show the scene behind or around them:
//	the mirrors (TemplGlassEnvTex, eDC_Mirror), the copy of the screen over
//	a model (TemplRefrEnvTex, eDC_ScreenTex), the scene behind a model moved
//	by its bump map (TemplBumpOffsetEnv, eDC_ScreenBump) and the heat haze
//	particles (ParticleMin/Med/MaxHeatHaze, eDC_HeatHaze).
//
//	A mirror: before the main view is drawn, the nearest mirror among its
//	items gets a picture of the scene mirrored in its plane, as the
//	original's environment texture of the template: the 3D engine renders
//	the template's scene (DrawLowDetail: the sky, the terrain, the static
//	objects, the entities, the particles, the far sprites) seen by the
//	frame's camera mirrored in the plane, into a target of half the main
//	view's size (the original's is 512 x 512), as a scene nested in the main
//	one, every frame. Its near plane is the mirror's plane (an oblique near
//	plane, as the water reflection's), so nothing behind the mirror shows.
//	The mirror is drawn blended: that picture at the pixel's world position
//	seen by the mirrored camera times ReflectAmount (0.9) plus its texture
//	times the rest, alpha Opacity times the texture's (the original's
//	CGVProgGlassTex and CGRCGlassTex; General.hlsl, Mirror). The other
//	mirrors of the frame have no picture: their texture times the rest, as
//	the original's mirror without its target. The plane is the one through
//	the mirror's vertices (the mirror is flat), its normal towards the
//	camera; the mirror is not drawn into its own picture.
//
//	TemplRefrEnvTex: the original's fixed function pass (its technique
//	without AlphaBlended) takes the copy of the back buffer made when the
//	refractive objects start (StretchRect) as its texture at the model's own
//	texture coordinates (TexGen Base), opaque and unlit: a picture of the
//	whole screen wrapped round the model. Here the copy of the scene drawn
//	so far (CopySceneForWater) over the model, in the blended list.
//
//	Heat haze (with r_HeatHaze, as the original): the particles ripple the
//	scene behind them as the original's shaders do: the copy of the screen
//	at the pixel moved by the slopes of WaterBump (a height map; at the
//	particle's texture coordinates times 0.125) times 0.01, the colour of
//	the copy alone, blended by haze_mask's alpha times the particle's
//	(General.hlsl, HeatHaze). The original samples its copy point filtered,
//	blocky at 4K; ours is filtered.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"
#include "Texture.h"
#include "Shader.h"
#include "Scene.h"

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! What a mirror's picture shows (EDrawLowDetailFlags): the template's scene (its RenderParams).
constexpr int kMirrorDrawFlags = DLD_DETAIL_TEXTURES | DLD_TERRAIN_WATER | DLD_FAR_SPRITES | DLD_STATIC_OBJECTS | DLD_PARTICLES |
                                 DLD_TERRAIN_FULLRES | DLD_FIRST_PERSON_CAMERA_OWNER | DLD_ENTITIES | DLD_TERRAIN;
constexpr float MIRROR_SCALE = 0.5f;          //!< the picture's size, of the main view's
constexpr float MIRROR_FOV_SCALE = 1.3f;      //!< the mirrored camera's view wider than the frame's (it cannot roll)
constexpr float HAZE_RIPPLE = 0.01f;          //!< the heat haze's shift of the scene per unit of WaterBump (its Matrix)
constexpr float HAZE_TILING = 0.125f;         //!< WaterBump's texture coordinates per the particle's (TexDetailScale)

//! The object's world matrix: the whole matrix with FOB_TRANS_* flags, else its translation only (as
//! Draw3D.cpp's).
Matrix44 ObjectWorld(const CCObject *pObj)
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

bool IsMirrorItem(const SRenderItem &it)
{
  return it.pShader && it.pRE && it.pRE->m_Type == eDATA_OcLeaf && GetDrawClass(it.pShader, it.pRes) == eDC_Mirror &&
         !(it.pObj->m_ObjFlags & FOB_LIGHTPASS);
}

//! The plane of a leaf item's chunk in world space through its vertices: a point and the normal (not
//! towards anything yet). False: too few vertices, or they lie on a line.
bool ChunkPlane(const SRenderItem &it, const Matrix44 &World, Vec3 &vPoint, Vec3 &vNormal)
{
  const CREOcLeaf *pLeaf = static_cast<const CREOcLeaf *>(it.pRE);
  const CMatInfo *pChunk = pLeaf->m_pChunk;
  const CLeafBuffer *pVC = pLeaf->m_pBuffer ? pLeaf->m_pBuffer->GetVertexContainer() : nullptr;
  if (!pChunk || !pVC)
    return false;
  const CVertexBuffer *pVB = pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData ? pVC->m_pSecVertBuffer : pVC->m_pVertexBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
    return false;
  const int nStride = CD3D12Pipeline::GetVertexSize(pVB->m_vertexformat);
  const int nFirst = std::max(pChunk->nFirstVertId, 0);
  const int nEnd = std::min(nFirst + pChunk->nNumVerts, pVB->m_NumVerts);
  if (nStride < (int)sizeof(Vec3) || nEnd - nFirst < 3)
    return false;
  const byte *pData = (const byte *)pVB->m_VS[VSF_GENERAL].m_VData;
  const float *m = World.data;
  auto At = [&](int i)
  {
    const Vec3 &p = *(const Vec3 *)(pData + (size_t)i * nStride);
    return Vec3(p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12], p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
                p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14]);
  };
  // The first vertex, the one farthest from it, and the one farthest from the line through both.
  const Vec3 a = At(nFirst);
  Vec3 b = a, c = a;
  float fBest = 0;
  for (int i = nFirst + 1; i < nEnd; i++)
  {
    const Vec3 p = At(i);
    if ((p - a).GetLengthSquared() > fBest)
    {
      fBest = (p - a).GetLengthSquared();
      b = p;
    }
  }
  fBest = 0;
  for (int i = nFirst + 1; i < nEnd; i++)
  {
    const Vec3 p = At(i);
    const float f = (b - a).Cross(p - a).GetLengthSquared();
    if (f > fBest)
    {
      fBest = f;
      c = p;
    }
  }
  const Vec3 n = (b - a).Cross(c - a);
  if (n.GetLengthSquared() < 1e-12f)
    return false;
  vPoint = a;
  vNormal = n.GetNormalized();
  return true;
}

}

bool CD3D12Renderer::IsHeatHazeOn()
{
  if (!m_pHeatHaze && iConsole)
    m_pHeatHaze = iConsole->GetCVar("r_HeatHaze");
  return m_pHeatHaze && m_pHeatHaze->GetIVal() != 0;
}

bool CD3D12Renderer::SetupScreenTexDraw(SPipelineKey &Key, SDrawConstants &dc)
{
  // In the main view, over the copy of the scene the list has (the water's in the blended list), or a
  // new one.
  if (m_bReflectionPass || m_bEnvCubePass || m_bShadowMapPass || m_bSpritePass || m_nRecurseLevel > 1 || (!m_bSceneCopied && !CopySceneForWater()))
    return false;
  dc.nFlags = DF_TEX0 | (dc.nFlags & DF_FOG);
  dc.nFlags2 = 0;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
  Set4(dc.vColor, 1, 1, 1, 1);
  Key.nState = GS_DEPTHWRITE;
  return true;
}

bool CD3D12Renderer::SetupScreenBumpDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it, CTexture *pTex)
{
  // The original's CGPSBumpOffset: the scene behind the pixel moved by the bump map (a du/dv map, the
  // material's BumpAmountX/Y its matrix) + the diffuse texture, opaque. Its scene there is the
  // template's environment target (the original draws nothing of the swatch): the copy of the scene
  // drawn so far here; its shift along the normal (Refraction 0.1) is left out.
  if (m_bReflectionPass || m_bEnvCubePass || m_bShadowMapPass || m_bSpritePass || m_nRecurseLevel > 1 || (!m_bSceneCopied && !CopySceneForWater()))
    return false;
  CTexture *pBump = GetNormalMap(it.pRes);
  dc.nFlags = 0;
  dc.nFlags2 = DF2_SHADER_FX;
  dc.vParams3[3] = XR_FX_BUMP_OFFSET;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  dc.nTex1 = pBump ? pBump->GetSRV() : D3D12_SRV_WHITE;
  dc.nTex2 = pTex ? pTex->GetSRV() : D3D12_SRV_WHITE;
  dc.nSamplers = pTex ? pTex->GetSampler() : XR_SAMPLER_LINEAR_WRAP;
  Set4(dc.vParams0, pBump ? GetResourceFloat(it.pRes, "BumpAmountX", 0.4f) : 0.0f, pBump ? GetResourceFloat(it.pRes, "BumpAmountY", 0.4f) : 0.0f,
       pTex ? 1.0f : 0.0f, 0);
  Set4(dc.vColor, 1, 1, 1, 1);
  Key.nState = GS_DEPTHWRITE;
  return true;
}

bool CD3D12Renderer::LoadHazeTextures()
{
  if (!m_pTextures)
    return false;
  if (!m_pHazeMask)
    m_pHazeMask = m_pTextures->LoadTexture("textures/haze_mask", FT_CLAMP, 0, eTT_Base, -1, -1, -1, 0);
  // WaterBump is a height map: as a bump map, a normal map, whose xy (the height's slopes) are the
  // ripple, as the original's du/dv map made from it (V8U8, texbem).
  if (!m_pHazeBump)
    m_pHazeBump = m_pTextures->LoadTexture("textures/waterbump", 0, 0, eTT_Bumpmap, -1, -1, -1, 0);
  return m_pHazeMask && m_pHazeBump;
}

bool CD3D12Renderer::SetupHeatHazeDraw(SPipelineKey &Key, SDrawConstants &dc)
{
  if (!IsHeatHazeOn() || m_bReflectionPass || m_bEnvCubePass || m_bShadowMapPass || m_bSpritePass || m_nRecurseLevel > 1 ||
      !LoadHazeTextures() || (!m_bSceneCopied && !CopySceneForWater()))
    return false;
  // The particles' alpha from their vertices (the particle system's), the scene's colour as it is
  // (RefrColor 1 1 1; their HeatMoveSpeed is 0: the ripple moves with the particles).
  dc.nFlags = DF_VCOLOR;
  dc.nFlags2 = DF2_SHADER_FX;
  dc.vParams3[3] = XR_FX_HAZE;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  dc.nTex1 = m_pHazeBump->GetSRV();
  dc.nTex2 = m_pHazeMask->GetSRV();
  Set4(dc.vParams0, HAZE_RIPPLE, HAZE_TILING, 0, 0);
  Set4(dc.vColor, 1, 1, 1, 1);
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  return true;
}

void CD3D12Renderer::UpdateMirror(std::span<const SRenderItem> Items)
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  // The main view of the game with a mirror in it.
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  if (!(m_nSceneFlags & SHDF_ALLOWHDR) || m_nRecurseLevel > 1 || m_bReflectionPass || m_bEnvCubePass || m_bSpritePass || m_bShadowMapPass ||
      !p3D || !m_pDevice || !m_pDescriptors || !EnsureFrame())
    return;
  // The nearest one whose plane the camera is in front of.
  const CCamera Camera = m_Camera;
  const Vec3 vCam = Camera.GetPos();
  const SRenderItem *pBest = nullptr;
  Vec3 vPoint, vNormal;
  float fBest = 0;
  for (const SRenderItem &it : Items)
  {
    if (!IsMirrorItem(it))
      continue;
    Vec3 p, n;
    if (!ChunkPlane(it, ObjectWorld(it.pObj), p, n))
      continue;
    float fSide = n.Dot(vCam - p);
    if (fabsf(fSide) < 0.01f)
      continue;
    if (fSide < 0)
      n = -n;
    Vec3 vCenter(0, 0, 0);
    it.pRE->mfCenter(vCenter, it.pObj);
    const float d = (vCenter - vCam).GetLength();
    if (!pBest || d < fBest)
    {
      pBest = &it;
      fBest = d;
      vPoint = p;
      vNormal = n;
    }
  }
  if (!pBest)
    return;

  // The target: half the main view, in the main scene's format (HDR with r_HDRRendering).
  const int w = std::max((int)(m_pDevice->GetWidth() * MIRROR_SCALE), 1), h = std::max((int)(m_pDevice->GetHeight() * MIRROR_SCALE), 1);
  const DXGI_FORMAT Format = IsHdrOn() ? D3D12_HDR_FORMAT : D3D12_BACKBUFFER_FORMAT;
  if (!m_pMirrorColor || w != m_nMirrorWidth || h != m_nMirrorHeight || m_pMirrorColor->GetDesc().Format != Format)
  {
    ReleaseMirror();
    m_hMirrorRTV = m_pDescriptors->AllocRTV();
    m_hMirrorDSV = m_pDescriptors->AllocDSV();
    if (!m_hMirrorRTV.ptr || !m_hMirrorDSV.ptr ||
        !m_pDevice->CreateOffscreenTargets(w, h, m_pMirrorColor, m_pMirrorDepth, m_hMirrorRTV, m_hMirrorDSV, Format, 1, nullptr) ||
        (m_nMirrorSRV = m_pDescriptors->AllocSRV(m_pMirrorColor.Get(), nullptr)) < 0)
    {
      ReleaseMirror();
      return;
    }
    m_nMirrorWidth = w;
    m_nMirrorHeight = h;
  }

  // The frame's camera mirrored in the plane: its position and its direction mirrored (no roll: the
  // picture is turned, which the projection of the mirror's pixels undoes), wider.
  const float *v = m_CameraView.data;
  const Vec3 vLook(-v[2], -v[6], -v[10]);
  const Vec3 vMirLook = vLook - vNormal * (2 * vLook.Dot(vNormal));
  CCamera Mirror = Camera;
  Mirror.SetPos(vCam - vNormal * (2 * vNormal.Dot(vCam - vPoint)));
  Mirror.SetAngle(ConvertVectorToCameraAngles(vMirLook));
  Mirror.SetFov(std::min(Camera.GetFov() * MIRROR_FOV_SCALE, DEG2RAD(170.0f)));
  Mirror.Update();

  // What the picture's scene changes.
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

  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "mirror: %d x %d%s, plane (%.3f %.3f %.3f) through (%.2f %.2f %.2f)\n", w, h, Format == D3D12_HDR_FORMAT ? " (HDR)" : "",
            vNormal.x, vNormal.y, vNormal.z, vPoint.x, vPoint.y, vPoint.z);
  m_pDevice->SetOffscreenTargets(m_pMirrorColor.Get(), m_hMirrorRTV, m_hMirrorDSV, w, h);
  const float Clear[4] = { m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 0 };
  m_pDevice->ClearColor(Clear);
  m_pDevice->ClearDepth(1.0f, 0, true, true);
  m_bReflectionPass = m_bMirrorPass = true;
  m_vMirrorNormal = vNormal;
  m_vMirrorPoint = vPoint;
  SetCamera(Mirror);
  // Where the mirror's pixels look up the picture: its camera's (SetCamera; the first person camera
  // the engine sets in the pass has a view of its own).
  const Matrix44 MirrorViewProj = m_MirrorViewProj;
  iSystem->SetViewCamera(Mirror);
  p3D->SetCamera(Mirror, false);
  p3D->DrawLowDetail(kMirrorDrawFlags);
  m_bReflectionPass = m_bMirrorPass = false;
  m_MirrorViewProj = MirrorViewProj;
  m_pDevice->SetBackBufferTargets();

  // The frame as it was.
  iSystem->SetViewCamera(ViewCamera);
  p3D->SetCamera(Camera, false);
  SetCamera(Camera);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bFog = bFog;
  m_fFogStart = fFogStart;
  m_fFogEnd = fFogEnd;
  memcpy(m_FogColor, FogColor, sizeof(FogColor));
  memcpy(m_WorldColor, WorldColor, sizeof(WorldColor));
  m_vClearColor = vClearColor;
  m_nState = nState;
  m_nCullMode = nCullMode;
  InvalidateFrameConstants();

  m_pMirrorObj = pBest->pObj;
  m_pMirrorRE = pBest->pRE;
  m_nMirrorFrame = m_nFrames;
}

bool CD3D12Renderer::SetupMirrorDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it, CTexture *pTex)
{
  if (m_bEnvCubePass || m_bShadowMapPass || m_bSpritePass)
    return false;
  const bool bPicture = m_nMirrorFrame == m_nFrames && m_nMirrorSRV >= 0 && it.pObj == m_pMirrorObj && it.pRE == m_pMirrorRE && !m_bReflectionPass &&
                        m_nRecurseLevel <= 1;
  const float fReflect = std::clamp(GetResourceFloat(it.pRes, "ReflectAmount", 0.9f), 0.0f, 1.0f);
  dc.nFlags = DF_VCOLOR | (dc.nFlags & DF_FOG);
  dc.nFlags2 = DF2_SHADER_FX;
  dc.vParams3[3] = XR_FX_MIRROR;
  dc.nTex0 = bPicture ? (uint32)m_nMirrorSRV : D3D12_SRV_WHITE;
  dc.nTex1 = pTex ? pTex->GetSRV() : D3D12_SRV_WHITE;
  dc.nSamplers = pTex ? pTex->GetSampler() : XR_SAMPLER_LINEAR_WRAP;
  memcpy(dc.mParams0, m_MirrorViewProj.data, sizeof(dc.mParams0));
  Set4(dc.vParams2, bPicture ? fReflect : 0.0f, 0, 1 - fReflect, 0);
  Set4(dc.vColor, 1, 1, 1, 1);
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  return true;
}

void CD3D12Renderer::ReleaseMirror()
{
  if (m_pDescriptors)
  {
    if (m_nMirrorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nMirrorSRV);
    if (m_hMirrorRTV.ptr)
      m_pDescriptors->FreeRTV(m_hMirrorRTV);
    if (m_hMirrorDSV.ptr)
      m_pDescriptors->FreeDSV(m_hMirrorDSV);
  }
  m_nMirrorSRV = -1;
  m_hMirrorRTV = {};
  m_hMirrorDSV = {};
  if (m_pDevice)
  {
    m_pDevice->DeferredRelease(m_pMirrorColor);
    m_pDevice->DeferredRelease(m_pMirrorDepth);
  }
  m_pMirrorColor.Reset();
  m_pMirrorDepth.Reset();
  m_nMirrorWidth = m_nMirrorHeight = 0;
  m_nMirrorFrame = -1;
}
