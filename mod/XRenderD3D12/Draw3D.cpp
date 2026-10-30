// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Draw3D.cpp
//	Description: Drawing the render items of EF_EndEf3D.
//
//	Each item is a render element with an object, a shader (its draw class,
//	Shader.h) and resources. The sky (CRESky) is drawn as the sky box its
//	shader's script names; leaf buffer elements (CREOcLeaf) are drawn with
//	the general program: the diffuse texture lit by the sun
//	and the object's ambient colour, or by its lightmap; vegetation by its
//	vertex colours; the terrain with texture coordinates from the world
//	position; the water by Water.cpp; the shadows by Shadows.cpp (in a
//	shadow map pass the items are the casters' depth). FOB_NEAREST objects (the first
//	person weapon) get a projection of their own and the depth range
//	0..0.1. The particle sprites of the scene are drawn in the blended
//	list after the water surfaces, the flares (Flares.cpp) after the rest
//	of the scene.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Texture.h"
#include "Scene.h"
#include "Geometry.h"

#include <bit>

namespace
{

constexpr float TERRAIN_SECTOR_SIZE = 64.0f;

bool HasNormal(int nFormat)
{
  switch (nFormat)
  {
  case VERTEX_FORMAT_P3F_N:
  case VERTEX_FORMAT_P3F_N_COL4UB:
  case VERTEX_FORMAT_P3F_N_TEX2F:
  case VERTEX_FORMAT_P3F_N_COL4UB_TEX2F:
  case VERTEX_FORMAT_P3F_N_COL4UB_COL4UB:
  case VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F:
    return true;
  default:
    return false;
  }
}

bool HasColor(int nFormat)
{
  switch (nFormat)
  {
  case VERTEX_FORMAT_P3F_COL4UB:
  case VERTEX_FORMAT_P3F_COL4UB_TEX2F:
  case VERTEX_FORMAT_TRP3F_COL4UB_TEX2F:
  case VERTEX_FORMAT_P3F_COL4UB_COL4UB:
  case VERTEX_FORMAT_P3F_N_COL4UB:
  case VERTEX_FORMAT_P3F_N_COL4UB_TEX2F:
  case VERTEX_FORMAT_P3F_N_COL4UB_COL4UB:
  case VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F:
  case VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F:
  case VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F:
    return true;
  default:
    return false;
  }
}

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

float Det3(const Matrix44 &m)
{
  const float *d = m.data;
  return d[0] * (d[5] * d[10] - d[6] * d[9]) - d[1] * (d[4] * d[10] - d[6] * d[8]) + d[2] * (d[4] * d[9] - d[5] * d[8]);
}

//! The first person projection: near 0.01, far 40, m11 = 1 / tan(fov * ratio / 3).
Matrix44 MakeNearestProjection(const CCamera &cam)
{
  const float fRatio = cam.GetProjRatio() > 0 ? cam.GetProjRatio() : 0.75f;
  const float fNear = 0.01f, fFar = 40.0f;
  const float m11 = 1.0f / tanf(cam.GetFov() * fRatio / 3.0f);
  Matrix44 m;
  memset(m.data, 0, sizeof(m.data));
  m.data[0] = m11 * fRatio;
  m.data[5] = m11;
  m.data[10] = fFar / (fNear - fFar);
  m.data[11] = -1;
  m.data[14] = fNear * fFar / (fNear - fFar);
  return m;
}

//! The object's world matrix: the whole matrix with FOB_TRANS_* flags, else its translation only.
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

//! The corner (x0, y0) of a terrain sector from its leaf buffer's box (or vertices).
void GetSectorCorner(CLeafBuffer *pLB, float &x0, float &y0)
{
  CLeafBuffer *pVC = pLB->GetVertexContainer();
  Vec3 vMin = pVC->m_vBoxMin;
  if (pVC->m_vBoxMin.x > pVC->m_vBoxMax.x && pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData)
  {
    const int nStride = CD3D12Pipeline::GetVertexSize(pVC->m_pSecVertBuffer->m_vertexformat);
    const byte *pData = (const byte *)pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData;
    vMin = Vec3(1e30f, 1e30f, 1e30f);
    for (int i = 0; i < pVC->m_SecVertCount && nStride > 0; i++)
    {
      const Vec3 &p = *(const Vec3 *)(pData + i * nStride);
      vMin.x = std::min(vMin.x, p.x);
      vMin.y = std::min(vMin.y, p.y);
    }
  }
  // The grid starts on a multiple of the sector size; half a metre of slack for rounding.
  x0 = floorf((vMin.x + 0.5f) / TERRAIN_SECTOR_SIZE) * TERRAIN_SECTOR_SIZE;
  y0 = floorf((vMin.y + 0.5f) / TERRAIN_SECTOR_SIZE) * TERRAIN_SECTOR_SIZE;
}

uint8 CullOf(ECull eCull, bool bMirrored)
{
  switch (eCull)
  {
  case eCULL_None:
    return R_CULL_NONE;
  case eCULL_Front:
    return bMirrored ? R_CULL_BACK : R_CULL_FRONT;
  default:
    return bMirrored ? R_CULL_FRONT : R_CULL_BACK;
  }
}

//! The sphere an object's point lights attenuate over: the centre of the leaf buffer's box plus the
//! object's position, half the box diagonal.
void GetLightSphere(const Matrix44 &World, CLeafBuffer *pLB, Vec3 &vCenter, float &r)
{
  vCenter = Vec3(World.data[12], World.data[13], World.data[14]);
  r = 0;
  const CLeafBuffer *pVC = pLB->GetVertexContainer();
  if (pVC->m_vBoxMin.x <= pVC->m_vBoxMax.x)
  {
    vCenter += (pVC->m_vBoxMin + pVC->m_vBoxMax) * 0.5f;
    r = (pVC->m_vBoxMax - pVC->m_vBoxMin).GetLength() * 0.5f;
  }
}

//! Light n of the draw: the point light pLight on the object, lit per vertex with the attenuation
//! 1 / (a0 + a1 d) up to its range. Characters get (0.01, 5.07756 / R) up to 50.416 R. Static
//! objects get a line through the falloff f(x) = 2 (1 - 3 v^2 + 2 v^3), v = min(x / (0.9 R), 1),
//! at the nearest and the farthest point of the object's sphere (GetLightSphere), up to where it
//! reaches 1/256. Bump mapped draws light per pixel, falling off linearly to the radius; the others
//! per pixel with d3d12_PixelLighting take these values (DF2_VERTEX_LIGHTS), static objects the
//! falloff f itself (DF2_SMOOTH_FALLOFF).
void SetPointLight(SDrawConstants &dc, uint32 n, const CDLight *pLight, const CCObject *pObj, const Vec3 &vCenter, float r)
{
  const float R = std::max(pLight->m_fRadius, 0.01f);
  float a0, a1, fRange;
  if (pObj->m_pCharInstance)
  {
    a0 = 0.01f;
    a1 = 5.07756f / R;
    fRange = 50.416f * R;
  }
  else
  {
    auto Falloff = [R](float x)
    {
      const float v = std::min(x / (0.9f * R), 1.0f);
      return 2 * (1 - 3 * v * v + 2 * v * v * v);
    };
    const float d = std::max((pLight->m_Origin - vCenter).GetLength(), 0.1f);
    const float x1 = std::max(d - r, 0.1f * R);
    const float x2 = std::min(std::max(d + r, 0.1f * R), 0.99f * R);
    const float g1 = 1 / std::max(Falloff(x1), 0.1f), g2 = 1 / std::max(Falloff(x2), 0.1f);
    float s = x2 > x1 ? (g2 - g1) / (x2 - x1) : 0.0f;
    a0 = g1 - s * x1;
    if (a0 < 0.01f)
    {
      a0 = 0.01f;
      s = (g1 - 0.01f) / x1;
    }
    a1 = std::max(s, 0.0f);
    fRange = a0 > 256 ? 0.0f : (256 - a0) / std::max(a1, 0.01f);
  }
  Set4(dc.vLightColor[n], pLight->m_Color.r, pLight->m_Color.g, pLight->m_Color.b, 1);
  Set4(dc.vLightSpec[n], pLight->m_SpecColor.r, pLight->m_SpecColor.g, pLight->m_SpecColor.b, 1);
  Set4(dc.vLightPos[n], pLight->m_Origin.x, pLight->m_Origin.y, pLight->m_Origin.z, 1);
  Set4(dc.vLightAtten[n], a0, a1, fRange, 1 / R);
}

}

void CD3D12Renderer::BeginScene3D()
{
  // The sun of the scene as the directional light (black without one); each object can also get
  // lights of its own (SetObjectLights).
  Vec3 vDir(0, 0, 1);
  CFColor Color(0, 0, 0, 1), Spec(0, 0, 0, 1);
  if (const CDLight *pSun = m_pScene->GetSun())
  {
    vDir = m_pScene->GetSunDirection();
    Color = m_pScene->GetSunColor();
    Spec = pSun->m_SpecColor;
  }
  vDir.Normalize();
  m_SunDir[0] = vDir.x; m_SunDir[1] = vDir.y; m_SunDir[2] = vDir.z;
  m_SunColor[0] = Color.r; m_SunColor[1] = Color.g; m_SunColor[2] = Color.b;
  m_SunSpec[0] = Spec.r; m_SunSpec[1] = Spec.g; m_SunSpec[2] = Spec.b;
  if (m_pFrameDump)
  {
    fprintf(m_pFrameDump.get(), "sun: direction (%.3f %.3f %.3f) colour (%.2f %.2f %.2f) specular (%.2f %.2f %.2f)\n", vDir.x, vDir.y, vDir.z,
            Color.r, Color.g, Color.b, Spec.r, Spec.g, Spec.b);
    fprintf(m_pFrameDump.get(), "fog: %s, %g to %g, colour (%.3f %.3f %.3f)\n", m_bFog ? "on" : "off", m_fFogStart, m_fFogEnd, m_FogColor[0],
            m_FogColor[1], m_FogColor[2]);
  }
  m_NearestProj = MakeNearestProjection(m_Camera);
  m_nSceneSkyBox = -1;
  InvalidateFrameConstants();

  // The main view of the game clears colour and depth before its scene (with an upscaler, its
  // scene targets; with r_HDRRendering, the HDR target).
  if ((m_nSceneFlags & SHDF_ALLOWHDR) && m_nRecurseLevel <= 1)
  {
    BeginMainScene();
    const float Clear[4] = { m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 0 };
    m_pDevice->ClearColor(Clear);
    m_pDevice->ClearDepth(1.0f, 0, true, true);
  }
}

void CD3D12Renderer::DrawItems(SRenderItem *pItems, int nItems, int eList)
{
  if (!m_pScene || !EnsureFrame())
    return;
  if (eList == eRL_Opaque)
    BeginScene3D();
  // The blended list starts with the water surfaces (over a copy of the scene so far); the
  // particle sprites come after them.
  bool bSprites = eList != eRL_Blended;
  // The flares after the rest of the scene, before the elements over it.
  bool bFlares = eList != eRL_Blended;
  if (eList == eRL_Blended)
    m_bSceneCopied = false;
  GpuSection(eList == eRL_Opaque ? "opaque" : eList == eRL_Nearest ? "nearest" : "water");
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "list %d: %d items, recursion %d, scene flags 0x%x\n", eList, nItems, m_nRecurseLevel, m_nSceneFlags);
  // Heat vision colours the main view's scene (DrawGeometry adds DF2_HEAT).
  m_bHeatPass = m_bHeatVision && (m_nSceneFlags & SHDF_ALLOWHDR) && m_nRecurseLevel <= 1 && !m_bReflectionPass && !m_bSpritePass &&
                !m_bShadowMapPass;
  for (int i = 0; i < nItems; i++)
  {
    const SRenderItem &it = pItems[i];
    if (!it.pRE)
      continue;
    if (!bSprites && it.eDrawClass != eDC_Water && it.eDrawClass != eDC_WaterBeach)
    {
      GpuSection("blended");
      m_fDrawHeat = 0;
      DrawSprites();
      bSprites = true;
    }
    if (!bFlares && ScreenElementRank(it.pRE) > 0)
    {
      DrawFlares({ pItems, (size_t)nItems });
      bFlares = true;
    }
    if (m_pFrameDump)
      DumpItem(i, it, eList);
    m_fDrawHeat = m_bHeatPass ? ItemHeat(it) : 0;
    switch (it.pRE->m_Type)
    {
    case eDATA_OcLeaf:
      DrawLeafItem(it);
      break;
    case eDATA_Sky:
      DrawSkyItem(it);
      break;
    case eDATA_FarTreeSprites:
    case eDATA_TerrainDetailTextureLayers:
    case eDATA_TerrainParticles:
      // They draw themselves through the 3D engine (immediate calls of the renderer).
      it.pRE->mfDraw(RE_ShaderArg(it.pShader), nullptr);
      break;
    default:
      break;
    }
  }
  if (!bSprites)
  {
    GpuSection("blended");
    m_fDrawHeat = 0;
    DrawSprites();
  }
  if (!bFlares)
    DrawFlares({ pItems, (size_t)nItems });
  m_bHeatPass = false;
  m_fDrawHeat = 0;
  m_bItemScissor = false;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
}

float CD3D12Renderer::ItemHeat(const SRenderItem &it) const
{
  // Characters come with FOB_HOTAMBIENT (m_fHeatFactor 1); the object a character carries has a
  // heat source (DLF_HEATSOURCE) of its own in its light mask.
  const CCObject *pObj = it.pObj;
  if (!pObj)
    return 0;
  float fHeat = 0;
  if (pObj->m_ObjFlags & FOB_HOTAMBIENT)
    fHeat = pObj->m_fHeatFactor > 0 ? std::min(pObj->m_fHeatFactor, 1.0f) : 1.0f;
  const std::span<CDLight *const> Lights = m_pScene->GetLights();
  for (size_t i = 0; i < Lights.size() && i < 32; i++)
  {
    if (Lights[i] && (Lights[i]->m_Flags & DLF_HEATSOURCE) && (pObj->m_DynLMMask & (1u << i)))
      fHeat = std::max(fHeat, 0.7f);
  }
  return fHeat;
}

void CD3D12Renderer::DrawLeafItem(const SRenderItem &it)
{
  CREOcLeaf *pLeaf = static_cast<CREOcLeaf *>(it.pRE);
  CLeafBuffer *pLB = pLeaf->m_pBuffer;
  if (!pLB)
    return;
  const std::optional<SGeomDraw> Geom = Geometry::GetDraw(pLB, pLeaf->m_pChunk);
  if (!Geom)
    return;
  const CCObject *pObj = it.pObj;
  const CShader *pSh = it.pShader;
  const SRenderShaderResources *pRes = it.pRes;
  const EDrawClass eDC = it.eDrawClass;
  if (eDC == eDC_NoDraw || eDC == eDC_Engine || eDC == eDC_Sky || (eDC == eDC_ShadowOnly && !m_bShadowMapPass))
    return;
  // The objects that show the scene around them are not in its environment cubes.
  if (m_bEnvCubePass && pSh->GetProps().eEnvMap == EEnvMap::Auto)
    return;
  // The fog volume pass: the element once more, with the ID of the fog volume it is in, in the
  // sort group eS_FogShader (the object of a static one has FOB_FOGPASS), and the volume's cap
  // ("foglayer"); drawn with r_VolumetricFog. The fog of the ocean's volume over what is under it
  // is the water's (Water.cpp) while the camera is above the water, but for the caustics of its
  // sectors; under the water it is the volume's own, as in the other volumes.
  const bool bFogPass = eDC == eDC_FogLayer || it.bFogPass ||
                        (eDC != eDC_Caustics && ((it.nSort & ~EFSLIST_MASK) == eS_FogShader || (pObj->m_ObjFlags & FOB_FOGPASS)));
  const SFogVolume *pFogVolume = bFogPass ? m_pScene->GetFogVolume(it.nFog) : nullptr;
  if (bFogPass && (!pFogVolume || (pFogVolume->bCaustics && (m_bReflectionPass || m_Camera.GetPos().z >= pFogVolume->fFogLayerZ)) ||
                   !IsVolumetricFogOn() || m_bShadowMapPass || m_bSpritePass))
    return;

  // Transform, depth range and scissor.
  const Matrix44 World = GetWorldMatrix(pObj);
  const bool bNearest = (pObj->m_ObjFlags & FOB_NEAREST) != 0;
  const Matrix44 WorldViewProj = World * m_CameraView * (bNearest ? m_NearestProj : m_ProjMatrix);
  m_fDepthMin = 0;
  m_fDepthMax = bNearest ? 0.1f : 1.0f;
  m_bItemScissor = pObj->m_nScissorX2 > pObj->m_nScissorX1 && pObj->m_nScissorY2 > pObj->m_nScissorY1;
  m_ItemScissor = { pObj->m_nScissorX1, pObj->m_nScissorY1, pObj->m_nScissorX2, pObj->m_nScissorY2 };
  // A procedural flare: a glow built around the chunk (Flares.cpp), casting no shadow.
  if (eDC == eDC_ProcFlare)
  {
    if (!m_bShadowMapPass && !m_bSpritePass)
      DrawProcFlare(it, World);
    return;
  }

  SPipelineKey Key = {};
  Key.nState = GS_DEPTHWRITE;
  Key.nProgram = eD3D12P_General;
  Key.nVertexFormat = (uint8)Geom->nVertexFormat;
  // A face of an environment cube is mirrored (EnvCubes.cpp).
  Key.nCull = CullOf(GetCullMode(pSh, pRes), (Det3(World) < 0) != m_bEnvCubePass);
  // The water reflection sees the land from below the water: both sides, so that where the waves move
  // the reflection over a shore, it shows the land rather than the sky behind it, and an object
  // standing in the water, cut open by the water plane, shows its inside rather than a hole.
  if (m_bReflectionPass)
    Key.nCull = R_CULL_NONE;
  Key.bWireframe = m_nPolygonMode == R_WIREFRAME_MODE ? 1 : 0;
  Key.bDepthBias = (it.nSort & ~EFSLIST_MASK) == eS_Decal ? 1 : 0;

  SDrawConstants dc = DefaultDrawConstants();
  memcpy(dc.mWorld, World.data, sizeof(dc.mWorld));
  memcpy(dc.mWorldViewProj, WorldViewProj.data, sizeof(dc.mWorldViewProj));
  SetObjectMotion(dc, pObj, World, bNearest);

  // The diffuse texture; without one the texture the leaf buffer was given (terrain, grass).
  CTexture *pTex = GetDiffuseTexture(pRes);
  if (!pTex && m_pTextures)
    pTex = m_pTextures->GetByID(pLB->m_nClientTextureBindID);
  if (pTex && !pTex->IsCube())
  {
    dc.nFlags |= DF_TEX0;
    dc.nTex0 = pTex->GetSRV();
    dc.nSamplers = pTex->GetSampler();
  }
  CTexture *const pDiffuse = (dc.nFlags & DF_TEX0) ? pTex : nullptr;
  // The cube map templates: the environment cube map in place of the texture, which is added
  // unlit: whole (TemplReflCMDecal), signed (- 0.5, TemplReflCMDecalAddSig), a tenth of it
  // (TemplGlassCM, 0.098 for TemplGlassCM_FP). The glass sees the cube through itself (the view
  // vector refracted with the ratio 1.05) and reflected, mixed by F = saturate(0.2 + 2 *
  // (1 - N.V)^2); not lit. TemplReflCMDecal_ReflAmountOpacity: the texture plus the cube times
  // the gloss map (the reflect amount; the whole cube without one), lit together. The auto cube
  // map is the cube of the scene around the object (EnvCubes.cpp); before it has one, the default
  // cube.
  const SShaderProps &Props = pSh->GetProps();
  if (Props.eEnvMap != EEnvMap::None)
  {
    const int nAutoCube = Props.eEnvMap == EEnvMap::Auto ? GetEnvCubeSRV(it) : -1;
    if (CTexture *pCube = nAutoCube < 0 ? GetEnvCubeMap(it.pRes) : nullptr; pCube || nAutoCube >= 0)
    {
      if (Props.eEnvMap == EEnvMap::CubeDecalAmount)
      {
        CTexture *pWhite = m_pTextures ? m_pTextures->GetWhite() : nullptr;
        CTexture *pAmount = GetGlowMap(it.pRes);
        const bool bTex = (dc.nFlags & DF_TEX0) != 0;
        dc.nTex4 = bTex || !pWhite ? dc.nTex0 : pWhite->GetSRV();
        dc.nTex5 = pAmount ? pAmount->GetSRV() : pWhite ? pWhite->GetSRV() : dc.nTex4;
        dc.nFlags2 |= DF2_REFL_AMOUNT;
        Set4(dc.vAddColor, bTex ? 1.0f : 0.0f, bTex ? 1.0f : 0.0f, bTex ? 1.0f : 0.0f, 0);
      }
      else if (Props.eEnvMap != EEnvMap::Cube && Props.eEnvMap != EEnvMap::Auto && (dc.nFlags & DF_TEX0))
      {
        const float k = Props.eEnvMap == EEnvMap::Glass ? 0.1f : Props.eEnvMap == EEnvMap::GlassFixed ? 25.0f / 255.0f : 1.0f;
        dc.nFlags2 |= DF2_ADD_TEX4;
        dc.nTex4 = dc.nTex0;
        Set4(dc.vAddColor, k, k, k, Props.eEnvMap == EEnvMap::CubeAddSigned ? -0.5f : 0.0f);
      }
      if (Props.eEnvMap == EEnvMap::Glass)
      {
        dc.nFlags2 |= DF2_GLASS;
        Set4(dc.vParams2, 2, 2, 0.2f, 1.05f);
      }
      dc.nFlags = (dc.nFlags & ~DF_TEX0) | DF_CUBE0;
      dc.nTex0 = pCube ? pCube->GetSRV() : nAutoCube;
      dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
      pTex = pCube;
    }
  }
  float fScale[2], fOffset[2];
  GetTexTransform(pSh, pRes, EFTT_DIFFUSE, fScale, fOffset);
  if (fScale[0] != 1 || fScale[1] != 1 || fOffset[0] != 0 || fOffset[1] != 0)
  {
    dc.nFlags |= DF_TEXMATRIX;
    Set4(dc.vTexMat0, fScale[0], 0, fOffset[0], 0);
    Set4(dc.vTexMat1, 0, fScale[1], fOffset[1], 0);
  }

  const int nFormat = Geom->nVertexFormat;
  const CFColor MatColor = GetMaterialColor(pRes);
  const float fOpacity = GetOpacity(pRes);
  // TemplDecalGlowSelfIllum with r_Quality_BumpMapping 3: the texture unlit, the gloss map added
  // twice; below as TemplDecalGlow.
  const bool bSelfIllum = Props.eGlow == EGlow::SelfIllum && CV_r_Quality_BumpMapping >= 3;
  const ELighting eLighting = bSelfIllum ? ELighting::None : Props.eLighting;
  Set4(dc.vAmbient, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z, eLighting == ELighting::Vertex ? 1.0f : 2.0f);
  std::optional<D3D12_VERTEX_BUFFER_VIEW> TexCoord1, Tangents;

  auto Draw = [&]()
  {
    if (m_pFrameDump)
      DumpDraw(dc, Key, pTex);
    const D3D12_VERTEX_BUFFER_VIEW *pTC1 = TexCoord1 ? &*TexCoord1 : nullptr, *pTangents = Tangents ? &*Tangents : nullptr;
    if (Geom->IsIndexed())
      DrawGeometry(Key, dc, Geom->Topology, Geom->VB, &Geom->IB, Geom->nNumIndices, Geom->nFirstIndex, Geom->nBaseVertex, pTC1, pTangents);
    else
      DrawGeometry(Key, dc, Geom->Topology, Geom->VB, nullptr, Geom->nNumVertices, Geom->nFirstVertex, 0, pTC1, pTangents);
  };
  // An object lit in passes (FOB_ZPASS: the ambient and the light map; then for each light an
  // FOB_LIGHTPASS element with that light alone in m_DynLMMask and no ambient): the light passes
  // add their light over the first pass (see below).
  const bool bLightPass = (pObj->m_ObjFlags & FOB_LIGHTPASS) != 0;
  // A shadow map: the depth of the casters.
  if (m_bShadowMapPass)
  {
    if (bLightPass)
      return;
    if (SetupShadowCasterDraw(Key, dc, it))
      Draw();
    return;
  }

  // A fog volume over the object (FogVolumeAlpha, General.hlsl) where its first pass is: blended
  // objects where they are seen, by their alpha; alpha tested ones where the test passes. The cap
  // and the sea floor beyond the terrain from both sides.
  if (pFogVolume)
  {
    const bool bCap = eDC == eDC_FogLayer;
    const bool bBlended = eDC == eDC_AlphaBlend || IsBlendedVColor(eDC, pRes, pObj);
    const float fAlphaRef = GetAlphaRef(pSh, pRes);
    const bool bAlphaTest = eDC == eDC_AlphaTest || eDC == eDC_AlphaTestVColor || fAlphaRef > 0;
    dc.nFlags &= (bBlended || bAlphaTest) ? (DF_TEX0 | DF_TEXMATRIX) : 0;
    dc.nFlags2 = DF2_FOG_VOLUME;
    if (bAlphaTest)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = fAlphaRef > 0 ? fAlphaRef : eDC == eDC_AlphaTestVColor ? 128.0f / 255.0f : 0.5f;
    }
    const CFColor &c = pFogVolume->Color;
    Set4(dc.vColor, c.r, c.g, c.b, bBlended ? fOpacity * pObj->m_Color.a : 1.0f);
    Set4(dc.vParams0, pFogVolume->fFogLayerZ, logf(100) / (2 * std::max(pFogVolume->fMaxFogDist, 0.01f)), bCap ? 1.0f : 0.0f,
         bBlended ? 1.0f : 0.0f);
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA | (bBlended || bCap ? 0 : GS_DEPTHFUNC_EQUAL);
    if (bCap || eDC == eDC_WaterBottom)
      Key.nCull = R_CULL_NONE;
    Draw();
    return;
  }

  //! Sun and ambient, or the lightmap; vertex colours when there are no normals. The templates
  //! without lights get their colour from LitColor alone; the glow templates add their gloss map,
  //! the alpha glow ones their texture times its alpha, unlit.
  auto Light = [&]()
  {
    if (Props.eGlow == EGlow::Alpha && !bLightPass)
      dc.nFlags2 |= DF2_ALPHA_GLOW;
    else if (Props.eGlow != EGlow::None && !bLightPass)
    {
      if (CTexture *pGlow = GetGlowMap(it.pRes))
      {
        const float t = iTimer ? iTimer->GetCurrTime() : 0.0f;
        const float g = (Props.eGlow == EGlow::Pulse ? std::clamp(0.75f + 0.75f * sinf(gf_PI * t), 0.0f, 1.0f) : bSelfIllum ? 2.0f : 1.0f) *
                        HdrBoost(CV_d3d12_HDRGlow);
        dc.nFlags2 |= DF2_ADD_TEX4;
        dc.nTex4 = pGlow->GetSRV();
        Set4(dc.vAddColor, g, g, g, 0);
      }
    }
    if (eLighting == ELighting::VColor)
      dc.nFlags |= DF_VCOLOR;
    if (eLighting == ELighting::Ambient || eLighting == ELighting::Ambient2 || eLighting == ELighting::None || eLighting == ELighting::VColor)
      return;
    if (!bLightPass && pSh->UsesLightmap() && pObj->m_nLMId > 0 && pObj->m_pLMTCBufferO && m_pTextures && m_pTextures->GetByID(pObj->m_nLMId))
      TexCoord1 = Geometry::GetTexCoordStream(pObj->m_pLMTCBufferO);
    if (TexCoord1)
    {
      // The light map is not tinted by the material's colour, only the ambient under it is
      // (LitColor is white).
      dc.nFlags |= DF_TEX1_LIGHTMAP;
      dc.nTex1 = m_pTextures->GetSRV(pObj->m_nLMId);
      dc.nSamplers |= XR_SAMPLER_LINEAR_CLAMP << 4;
      Set4(dc.vAmbient, dc.vAmbient[0] * MatColor.r, dc.vAmbient[1] * MatColor.g, dc.vAmbient[2] * MatColor.b, dc.vAmbient[3]);
      // A dot3 light map (bump mapping on): the dominant direction of its light beside it.
      if (pObj->m_nLMDirId > 0 && m_pTextures->GetByID(pObj->m_nLMDirId))
      {
        dc.nFlags |= DF_LIGHTMAP_DIR;
        dc.nTex2 = m_pTextures->GetSRV(pObj->m_nLMDirId);
      }
    }
    else if (HasNormal(nFormat))
    {
      dc.nFlags |= DF_LIGHTING;
      SetObjectLights(dc, pObj, World, pLB);
    }
    else if (HasColor(nFormat))
      dc.nFlags |= DF_VCOLOR;
    // d3d12_PixelLighting: an object with a light map gets the dynamic lights of its mask that the
    // light map does not have (no sun, no DLF_LM light; not one lit in passes, whose light passes
    // bring them), per pixel as the vertex lighting takes them and in place of the ambient under the
    // light map, as the original adds its vertex lighting there (DF2_LIGHTMAP_LIGHTS).
    if (CV_d3d12_PixelLighting && TexCoord1 && !(pObj->m_ObjFlags & FOB_ZPASS) && Props.eEnvMap != EEnvMap::Auto && HasNormal(nFormat))
    {
      const uint32 nFlags2 = dc.nFlags2;
      SetObjectLights(dc, pObj, World, pLB);
      dc.nFlags2 = nFlags2 | (dc.nLights ? DF2_LIGHTMAP_LIGHTS | DF2_VERTEX_LIGHTS | DF2_SMOOTH_FALLOFF : 0);
    }
    // Bump mapping on (r_Quality_BumpMapping): the materials of the bump, model and light diffuse
    // templates are lit per pixel, with the material's normal map over the leaf buffer's tangent
    // frames when it has one (also for the dot3 light maps). The normal maps' green runs along
    // the binormal (the direction of growing v). The specular templates add the highlights of the
    // lights' specular colours, with the material's specular colour (times the texture's alpha
    // for the gloss alpha ones).
    if (CV_r_Quality_BumpMapping > 0 && Props.bPixelLighting && (dc.nFlags & (DF_LIGHTING | DF_LIGHTMAP_DIR)))
    {
      if (dc.nFlags & DF_LIGHTING)
      {
        dc.nFlags |= DF_PIXEL_LIGHTING;
        if (Props.bSpecular && it.pRes && it.pRes->m_LMaterial)
        {
          const CFColor &Spec = it.pRes->m_LMaterial->Front.m_Specular;
          dc.nFlags |= DF_SPECULAR;
          Set4(dc.vSpecular, Spec.r, Spec.g, Spec.b, Props.bGlossAlpha ? 1.0f : 0.0f);
        }
      }
      CTexture *pBump = GetNormalMap(it.pRes);
      if (pBump && (Tangents = Geometry::GetTangentStream(pLB)))
      {
        dc.nFlags |= DF_BUMP;
        dc.nTex3 = pBump->GetSRV();
        dc.vParams1[1] = 1;
      }
    }
    // d3d12_PixelLighting: the other lit materials per pixel too, with the lights as their vertex
    // lighting has them (static objects falling off by the curve their attenuation is fitted to);
    // the auto cube map keeps the fixed function's lighting (DF2_MATERIAL_LIGHT, below).
    if (CV_d3d12_PixelLighting && (dc.nFlags & DF_LIGHTING) && !(dc.nFlags & DF_PIXEL_LIGHTING) && Props.eEnvMap != EEnvMap::Auto)
    {
      dc.nFlags |= DF_PIXEL_LIGHTING;
      dc.nFlags2 |= DF2_VERTEX_LIGHTS | (pObj->m_pCharInstance ? 0 : DF2_SMOOTH_FALLOFF);
    }
    // The sun hidden by the shadow maps of the object's casters; by the sun's cascades where they
    // reach (a light map by the live casters only: it has the shadows of the rest).
    if (dc.nFlags & (DF_LIGHTING | DF_TEX1_LIGHTMAP))
    {
      SetupShadowReceiver(dc, pObj);
      dc.nFlags2 |= CascadeReceiverFlags(!(dc.nFlags & DF_LIGHTING));
    }
  };

  //! What the texture is multiplied by besides the light: the material's colour (white under a
  //! light map), or for the templates without lights the object's ambient colour (twice for
  //! Ambient2) or white.
  auto LitColor = [&]() -> CFColor
  {
    const Vec3 &a = pObj->m_AmbColor;
    switch (eLighting)
    {
    case ELighting::Ambient:
      return CFColor(a.x, a.y, a.z, 1);
    case ELighting::Ambient2:
      return CFColor(2 * a.x, 2 * a.y, 2 * a.z, 1);
    case ELighting::None:
    case ELighting::VColor:
      return CFColor(1, 1, 1, 1);
    default:
      return (dc.nFlags & DF_TEX1_LIGHTMAP) ? CFColor(1, 1, 1, 1) : MatColor;
    }
  };

  // False: only the projected lights' passes below.
  bool bFirstPass = true;
  switch (eDC)
  {
  case eDC_Opaque:
  case eDC_AlphaTest:
  {
    Light();
    const CFColor Color = LitColor();
    Set4(dc.vColor, Color.r * pObj->m_Color.r, Color.g * pObj->m_Color.g, Color.b * pObj->m_Color.b, 1);
    // The auto cube map is lit as the fixed function lights the material: the object's ambient
    // colour plus the material's colour times the lights, and the highlights of its specular colour.
    if (Props.eEnvMap == EEnvMap::Auto && (dc.nFlags & DF_LIGHTING))
    {
      const SLightMaterial *pMat = it.pRes ? it.pRes->m_LMaterial : nullptr;
      dc.nFlags2 |= DF2_MATERIAL_LIGHT;
      Set4(dc.vAmbient, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z, 1);
      Set4(dc.vSpecular, pMat ? pMat->Front.m_Specular.r : 0, pMat ? pMat->Front.m_Specular.g : 0, pMat ? pMat->Front.m_Specular.b : 0, 0);
      dc.vParams3[0] = pMat ? pMat->Front.m_SpecShininess : 0;
    }
    if (eDC == eDC_AlphaTest || GetAlphaRef(pSh, pRes) > 0)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = GetAlphaRef(pSh, pRes) > 0 ? GetAlphaRef(pSh, pRes) : 0.5f;
    }
    break;
  }
  case eDC_AlphaBlend:
  {
    Light();
    const CFColor Color = LitColor();
    Set4(dc.vColor, Color.r * pObj->m_Color.r, Color.g * pObj->m_Color.g, Color.b * pObj->m_Color.b, fOpacity * pObj->m_Color.a);
    // Glass of opacity 1 is opaque, whatever its texture's alpha.
    const bool bOpaqueGlass = (Props.eEnvMap == EEnvMap::Glass || Props.eEnvMap == EEnvMap::GlassFixed) && dc.vColor[3] >= 1;
    if (!bOpaqueGlass)
      Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    if (GetAlphaRef(pSh, pRes) > 0)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = GetAlphaRef(pSh, pRes);
    }
    break;
  }
  case eDC_AlphaTestVColor:
  case eDC_VColor:
  {
    // texture * vertex colour * 2 * the object's colour * the world colour; alpha * the object's
    // alpha squared * the opacity.
    dc.nFlags |= DF_VCOLOR | DF_VCOLOR_X2;
    const float a = pObj->m_Color.a;
    Set4(dc.vColor, pObj->m_Color.r * m_WorldColor[0], pObj->m_Color.g * m_WorldColor[1], pObj->m_Color.b * m_WorldColor[2], a * a * fOpacity);
    if (eDC == eDC_AlphaTestVColor)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = GetAlphaRef(pSh, pRes) > 0 ? GetAlphaRef(pSh, pRes) : 128.0f / 255.0f;
    }
    if (IsBlendedVColor(eDC, pRes, pObj))
    {
      // Grass fading in or out, transparent plants: blended, no depth writes.
      Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = 1.0f / 255.0f;
    }
    break;
  }
  case eDC_Additive:
  {
    // The texture alone (times the object's ambient colour for TemplDecalAdd_VLight); TemplDecalAlphaAdd
    // the texture times the vertex colour, added by its alpha (SRCALPHA, ONE).
    // With r_HDRRendering brighter in its core than the game draws it (d3d12_HDRGlow), the stealth
    // effect not.
    const float s = GetColorScale(pSh, pObj);
    const CFColor Color = eLighting == ELighting::Ambient ? LitColor() : CFColor(1, 1, 1, 1);
    Set4(dc.vColor, s * Color.r, s * Color.g, s * Color.b, 1);
    Key.nState = GS_BLSRC_ONE | GS_BLDST_ONE;
    if (eLighting == ELighting::VColor)
    {
      dc.nFlags |= DF_VCOLOR;
      Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONE;
    }
    if (!Props.bStealth)
      SetHdrCore(dc, CV_d3d12_HDRGlow, Key.nState);
    break;
  }
  case eDC_Modulate:
  {
    // The texture multiplied into the frame (DESTCOLOR, ZERO), no depth writes; an object fading
    // out (its colour's alpha a below 1) fades the decal towards white: saturate(texture + 1 - a).
    dc.nFlags2 |= DF2_MODULATE;
    Set4(dc.vColor, 1, 1, 1, 1);
    Set4(dc.vAddColor, 0, 0, 0, 1 - std::clamp(pObj->m_Color.a, 0.0f, 1.0f));
    Key.nState = GS_BLSRC_DSTCOL | GS_BLDST_ZERO;
    break;
  }
  case eDC_Terrain:
  {
    // The sector texture over the sector: u = y * s + u0, v = x * s + v0 from the element's custom
    // data (u0, v0, s): s = 1 / 64 and the sector's corner with its own texture; the far sectors and
    // the low detail terrain get the texture of the whole terrain with s = 1 / terrain size and 0.
    // Without custom data: the corner of the sector's vertices. (m_nCountCustomData is not set for
    // the sectors.)
    const float *pCD = (const float *)pLeaf->m_CustomData;
    float fScale = 1.0f / TERRAIN_SECTOR_SIZE, fU0 = 0, fV0 = 0;
    if (pCD && pCD[2] > 0)
    {
      fU0 = pCD[0];
      fV0 = pCD[1];
      fScale = pCD[2];
    }
    else
    {
      float x0 = 0, y0 = 0;
      GetSectorCorner(pLB, x0, y0);
      fU0 = -y0 * fScale;
      fV0 = -x0 * fScale;
    }
    dc.nFlags |= DF_TEXGEN_WORLD;
    dc.nFlags &= ~DF_TEXMATRIX;
    Set4(dc.vTexGenU, 0, fScale, 0, fU0);
    Set4(dc.vTexGenV, fScale, 0, 0, fV0);
    Set4(dc.vColor, m_WorldColor[0], m_WorldColor[1], m_WorldColor[2], 1);
    if (pTex)
      dc.nSamplers = (dc.nSamplers == XR_SAMPLER_POINT_WRAP || dc.nSamplers == XR_SAMPLER_POINT_CLAMP) ? XR_SAMPLER_POINT_CLAMP : XR_SAMPLER_LINEAR_CLAMP;
    // The sun's cascades: the shadows of the live casters (its texture has the others'), in the
    // colour of the engine's shadows on the terrain, which fade out where the cascades reach.
    if (const uint32 nCascades = CascadeReceiverFlags(true))
      dc.nFlags2 |= nCascades | DF2_SHADOW_TINT;
    // With the shader "terrain" (the terrain's detail layers on) a sector gets no detail texture
    // but its cliff texture where the cliffs are: the engine puts it into the element's custom
    // texture binds 1 and 2, projected on two planes, with the texture coordinates of each in the
    // custom data after the sector's (u then v, 4 floats each, w unused), and marks the cliff
    // vertices of bind 1 with red, those of bind 2 with green in the vertex colour. Colour =
    // 2 * sector * world colour * lerp(1, 2 * cliff, weight) for each.
    if (nFormat != VERTEX_FORMAT_P3F && it.pShader && !strcmp(it.pShader->GetName(), "terrain"))
    {
      dc.nFlags |= DF_TERRAIN_CLIFF;
      const float *pCustom = (const float *)pLeaf->m_CustomData;
      float *pTexGen[4] = { dc.vParams0, dc.vParams1, dc.vParams2, dc.vParams3 };
      for (int i = 0; i < 2; i++)
      {
        CTexture *pCliff = pCustom && pLeaf->m_CustomTexBind[1 + i] > 0 && m_pTextures ? m_pTextures->GetByID(pLeaf->m_CustomTexBind[1 + i]) : nullptr;
        if (!pCliff)
          continue;
        dc.vTexGen1V[i] = 1;
        (i ? dc.nTex3 : dc.nTex2) = pCliff->GetSRV();
        for (int k = 0; k < 2; k++)
          Set4(pTexGen[i * 2 + k], pCustom[4 + i * 8 + k * 4], pCustom[5 + i * 8 + k * 4], pCustom[6 + i * 8 + k * 4], 0);
      }
      break;
    }
    // The sectors also get the detail texture, repeated every r_DetailScale metres:
    // u = y / scale, v = x / scale, colour = 2 * sector * world colour + detail - 0.5.
    CTexture *pDetail = nFormat != VERTEX_FORMAT_P3F ? GetDetailTexture() : nullptr;
    if (pDetail)
    {
      const float fDetail = 1.0f / std::max(CV_r_DetailScale, 0.01f);
      dc.nFlags |= DF_TEX1 | DF_TEX1_DETAIL | DF_TEXGEN1_WORLD;
      dc.nTex1 = pDetail->GetSRV();
      dc.nSamplers = (dc.nSamplers & 15) | (XR_SAMPLER_LINEAR_WRAP << 4);
      Set4(dc.vTexGen1U, 0, fDetail, 0, 0);
      Set4(dc.vTexGen1V, fDetail, 0, 0, 0);
    }
    break;
  }
  case eDC_TerrainLayer:
  {
    // A detail layer of the terrain near the camera (world positions, the terrain normal, the
    // layer's weight in the vertex alpha): the layer's texture coordinates from the world position
    // with the element's custom data (u row in floats 0..2, v row in 4..6, the layer's own scale;
    // u = y / 4, v = x / 4 without it), the weight faded out from a quarter of the distance in float
    // 3 of the custom data to that distance (13 to 52 m; r_DetailDistance to twice that without
    // it), multiplied in twice (DESTCOLOR, SRCCOLOR); no depth writes. A layer with a material of
    // its own (the template "terrainlayer": the cliffs, projected on a vertical plane, one element
    // for each plane) is the material's diffuse texture, bump mapped with its normal map
    // (EFTT_NORMALMAP, else EFTT_BUMP): the sun's light on the bumps relative to its light on the
    // terrain's own normal, which the terrain's texture already has; its weight fades out linearly
    // from the camera to the distance in float 3 (156 m).
    if (!pTex || !CV_r_DetailTextures)
      return;
    CTexture *pBump = GetNormalMap(it.pRes);
    float fStart = std::max(CV_r_DetailDistance, 0.01f), fEnd = 2 * fStart;
    if (const float *pCustom = (const float *)pLeaf->m_CustomData; pCustom && pCustom[3] > 0)
    {
      fEnd = pCustom[3];
      fStart = it.pRes ? 0 : fEnd / 4;
    }
    dc.nFlags = (dc.nFlags & ~DF_TEXMATRIX) | DF_TEX0 | DF_TEXGEN_WORLD | DF_VCOLOR | DF_DETAIL_LAYER;
    if (const float *pCustom = (const float *)pLeaf->m_CustomData)
    {
      Set4(dc.vTexGenU, pCustom[0], pCustom[1], pCustom[2], 0);
      Set4(dc.vTexGenV, pCustom[4], pCustom[5], pCustom[6], 0);
    }
    else
    {
      Set4(dc.vTexGenU, 0, 0.25f, 0, 0);
      Set4(dc.vTexGenV, 0.25f, 0, 0, 0);
    }
    Set4(dc.vParams0, fStart, fEnd, 1.0f / (fEnd - fStart), 0);
    dc.nSamplers = XR_SAMPLER_LINEAR_WRAP;
    if (pBump)
    {
      // Ambient 0.3; the normal maps' green runs against the v row.
      dc.nFlags |= DF_DETAIL_BUMP;
      dc.nTex1 = pBump->GetSRV();
      dc.nSamplers |= XR_SAMPLER_LINEAR_WRAP << 4;
      Set4(dc.vParams1, 0.3f, -1, 0, 0);
    }
    Key.nState = GS_BLSRC_DSTCOL | GS_BLDST_SRCCOL;
    break;
  }
  case eDC_Decal:
  {
    // A decal on a static object (a "BigDecal" buffer: the indices of the owner's triangles it
    // covers, over the owner's vertices, with the owner's matrix): the texture the buffer was
    // given, its coordinates from the position in the owner's object space with the u and v
    // planes in floats 0..3 and 4..7 of the element's custom data, clamped to the edge; colour
    // and alpha as the texture has them, the alpha fading out from the centre to the circle
    // inside the square (0.5 at half the radius), blended over the owner where alpha > 0, both
    // sides, no depth writes. The planes go to world space for the texgen.
    const float *pCustom = (const float *)pLeaf->m_CustomData;
    if (!pTex || !pCustom)
      return;
    const Matrix44 Inv = GetInverted44(World);
    float *pRows[2] = { dc.vTexGenU, dc.vTexGenV };
    for (int r = 0; r < 2; r++)
      for (int i = 0; i < 4; i++)
        pRows[r][i] = Inv(i, 0) * pCustom[r * 4] + Inv(i, 1) * pCustom[r * 4 + 1] + Inv(i, 2) * pCustom[r * 4 + 2] + Inv(i, 3) * pCustom[r * 4 + 3];
    dc.nFlags = (dc.nFlags & ~DF_TEXMATRIX) | DF_TEXGEN_WORLD | DF_DECAL_FADE | DF_ALPHATEST;
    dc.fAlphaRef = 1.0f / 255.0f;
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
    Set4(dc.vColor, 1, 1, 1, 1);
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    Key.nCull = R_CULL_NONE;
    Key.bDepthBias = 1;
    break;
  }
  case eDC_TerrainShadow:
  {
    // The shadow of casters on a terrain sector ("EntityShadowOnTerrain": triangles of the
    // sector under the casters, over the sector's vertices; the object's m_pShadowCasters lists
    // the casters): the sector's texture (the one the buffer was given) at the sector's texture
    // coordinates (u = y * float 2 + float 0, v = x * float 2 + float 1 of the custom data) as the
    // terrain draws it (twice the world colour) times the object's colour (the terrain in the
    // shadow), blended over the terrain where the casters' maps hide the sun, the more weakly the
    // farther behind its casters (by the depth across the map's range); no depth writes, before
    // the detail layers.
    const float *pCustom = (const float *)pLeaf->m_CustomData;
    if (!pTex || !pCustom || !SetupShadowReceiver(dc, pObj))
      return;
    dc.nFlags = (dc.nFlags & ~DF_TEXMATRIX) | DF_TEXGEN_WORLD | DF_SHADOW_PASS | DF_ALPHATEST;
    dc.fAlphaRef = 1.0f / 255.0f;
    dc.vShadowParams[1] = 1;
    dc.nFlags2 |= CascadeReceiverFlags(false);
    Set4(dc.vTexGenU, 0, pCustom[2], 0, pCustom[0]);
    Set4(dc.vTexGenV, pCustom[2], 0, 0, pCustom[1]);
    Set4(dc.vColor, 2 * m_WorldColor[0] * pObj->m_Color.r, 2 * m_WorldColor[1] * pObj->m_Color.g, 2 * m_WorldColor[2] * pObj->m_Color.b, 1);
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    if (dc.nFlags & DF_SHADOW_DEBUG)
      dc.nFlags &= ~DF_ALPHATEST;
    break;
  }
  case eDC_TerrainLight:
  {
    // The terrain under dynamic lights ("Dynamic": triangles of a sector near the lights, over the
    // sector's vertices; the lights in the object's m_DynLMMask): the sector's texture at the
    // sector's texture coordinates (as eDC_TerrainShadow) as 0.75 * texture + 0.25, lit by the
    // point lights alone per pixel with the vertex normal, falling off as saturate(1 - (d / R)^2),
    // added to the picture without depth writes, fogged towards black; the projected lights below.
    const float *pCustom = (const float *)pLeaf->m_CustomData;
    if (!(dc.nFlags & DF_TEX0) || !pCustom || !HasNormal(nFormat))
      return;
    dc.nFlags = DF_TEX0 | DF_TEXGEN_WORLD | DF_LIGHTING | DF_PIXEL_LIGHTING;
    dc.nFlags2 = DF2_NO_SUN | DF2_TERRAIN_LIGHT | DF2_FOG_BLACK;
    Set4(dc.vTexGenU, 0, pCustom[2], 0, pCustom[0]);
    Set4(dc.vTexGenV, pCustom[2], 0, 0, pCustom[1]);
    Set4(dc.vColor, 1, 1, 1, 1);
    Set4(dc.vAmbient, 0, 0, 0, 1);
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
    SetObjectLights(dc, pObj, World, pLB);
    Key.nState = GS_BLSRC_ONE | GS_BLDST_ONE;
    bFirstPass = dc.nLights > 0;
    break;
  }
  case eDC_Water:
    // Not in its own reflection; in the environment cubes only the ocean (SetupWaterDraw).
    if (m_bReflectionPass || !SetupWaterDraw(Key, dc, it))
      return;
    // The surface stays where it is, though the ocean's ring follows the camera.
    SetStaticMotion(dc);
    pTex = nullptr;
    break;
  case eDC_WaterBeach:
    if (m_bReflectionPass || m_bEnvCubePass || !SetupBeachDraw(Key, dc, 0))
      return;
    pTex = m_pBeachTexture;
    break;
  case eDC_WaterBottom:
    SetupWaterBottomDraw(Key, dc, (dc.nFlags & DF_TEX0) ? pTex : nullptr);
    break;
  case eDC_Caustics:
    if (!SetupCausticsDraw(Key, dc, it))
      return;
    pTex = nullptr;
    break;
  case eDC_ScopeLens:
    if (!SetupScopeLensDraw(Key, dc))
      return;
    pTex = nullptr;
    break;
  default:
    return;
  }
  // A light pass: the light alone (no ambient, sun or light map), added where the first pass is
  // (blended objects: where they are seen, by their alpha), fogged towards black.
  if (bLightPass)
  {
    if (!(dc.nFlags & DF_LIGHTING) || !dc.nLights)
      return;
    dc.nFlags &= ~(DF_SHADOWS | DF_SHADOW_DEBUG);
    dc.nFlags2 = (dc.nFlags2 & ~(DF2_SUN_CASCADES | DF2_CASCADES_LIVE)) | DF2_NO_SUN | DF2_FOG_BLACK;
    Set4(dc.vAmbient, 0, 0, 0, dc.vAmbient[3]);
    const bool bBlended = (Key.nState & GS_BLEND_MASK) != 0;
    Key.nState = bBlended ? GS_BLSRC_SRCALPHA | GS_BLDST_ONE : GS_BLSRC_ONE | GS_BLDST_ONE | GS_DEPTHFUNC_EQUAL;
  }
  if (m_bFog)
    dc.nFlags |= DF_FOG;
  // The picture of a far vegetation sprite: where the object is, whatever the texture's alpha.
  if (m_bSpritePass)
    dc.nFlags |= DF_ALPHA_ONE;
  // While the ambient occlusion's target is bound (General.hlsl, XR_AMBIENT): the ambient light's
  // part of a colour the shader cannot tell from its lighting, the light baked into the terrain and
  // the vegetation; all of it for the templates lit by the ambient colour alone and the terrain's
  // shadow.
  if (!bLightPass && m_pDevice->IsAmbientBound())
  {
    if (eDC == eDC_Terrain || eDC == eDC_VColor || eDC == eDC_AlphaTestVColor)
      dc.nFlags2 |= DF2_AO_BAKED;
    else if (eDC == eDC_TerrainShadow || ((eDC == eDC_Opaque || eDC == eDC_AlphaTest || eDC == eDC_AlphaBlend) &&
                                          (eLighting == ELighting::Ambient || eLighting == ELighting::Ambient2)))
      dc.nFlags2 |= DF2_AO_ALL;
  }
  if (bFirstPass)
    Draw();
  // The beach: the other waves and the wet sand.
  for (int nPass = 1; eDC == eDC_WaterBeach && nPass <= BEACH_WAVES && SetupBeachDraw(Key, dc, nPass); nPass++)
  {
    if (m_bFog)
      dc.nFlags |= DF_FOG;
    Draw();
  }

  // The projected lights of the object's m_DynLMMask (DLF_PROJECT with an image, a cube with the
  // picture on +X): the object once more for each, where its first pass is (blended objects: by
  // their alpha, where they are seen): the diffuse texture times the material's colour lit by the
  // light alone (no ambient, sun or light map) times twice the cube at the point's lookup vector
  // (the light's m_TextureMatrix), alpha times the cube's, fogged towards black. The light: per
  // pixel as the object's other lights where bump mapping lights it per pixel (with its normal
  // map and specular), else per vertex as a point light of its radius, clamped to 1 (per pixel
  // with d3d12_PixelLighting, the light as per vertex). The terrain
  // under the light (eDC_TerrainLight): 0.75 * texture + 0.25 lit per pixel, falling off linearly to
  // the radius, added.
  const bool bTerrain = eDC == eDC_TerrainLight;
  if (bLightPass || m_bSpritePass || m_bReflectionPass || !HasNormal(nFormat) || (eDC != eDC_Opaque && eDC != eDC_AlphaTest && eDC != eDC_AlphaBlend && !bTerrain))
    return;
  const std::span<CDLight *const> Lights = m_pScene->GetLights();
  const bool bBlended = (Key.nState & GS_BLEND_MASK) != 0 && !bTerrain;
  const bool bPixel = bTerrain || (CV_r_Quality_BumpMapping > 0 && Props.bPixelLighting && eLighting != ELighting::None);
  // d3d12_PixelLighting: the others per pixel too, the light as per vertex.
  const bool bVertexLights = !bPixel && CV_d3d12_PixelLighting;
  const uint32 nKeep = bTerrain ? DF_TEXGEN_WORLD : DF_TEXMATRIX | DF_ALPHATEST | DF_ALPHATEST_LESS | (bPixel ? DF_BUMP | DF_SPECULAR : 0);
  const uint32 nFlags = (dc.nFlags & nKeep) | DF_LIGHTING | (bPixel || bVertexLights ? DF_PIXEL_LIGHTING : 0) | (m_bFog ? DF_FOG : 0);
  const uint32 nFlags2 = bVertexLights ? DF2_VERTEX_LIGHTS | (pObj->m_pCharInstance ? 0 : DF2_SMOOTH_FALLOFF) : 0;
  Vec3 vCenter;
  float r;
  GetLightSphere(World, pLB, vCenter, r);
  for (size_t i = 0; i < Lights.size() && i < 32; i++)
  {
    const CDLight *pLight = Lights[i];
    if (!pLight || !(pLight->m_Flags & DLF_PROJECT) || !(pObj->m_DynLMMask & (1u << i)))
      continue;
    if ((pLight->m_Flags & (DLF_HEATSOURCE | DLF_LIGHTSOURCE)) == DLF_HEATSOURCE)
      continue;
    CTexture *pCube = static_cast<CTexture *>(pLight->m_pLightImage);
    if (!pCube || !pCube->IsCube() || !pCube->IsTextureLoaded())
      continue;
    dc.nFlags = nFlags;
    dc.nFlags2 = DF2_NO_SUN | DF2_PROJECTOR | DF2_FOG_BLACK | (bTerrain ? DF2_TERRAIN_LIGHT : 0) | nFlags2;
    if (pDiffuse)
    {
      dc.nFlags |= DF_TEX0;
      dc.nTex0 = pDiffuse->GetSRV();
      dc.nSamplers = bTerrain ? XR_SAMPLER_LINEAR_CLAMP : pDiffuse->GetSampler();
    }
    if (bTerrain)
      Set4(dc.vColor, 1, 1, 1, 1);
    else
      Set4(dc.vColor, MatColor.r * pObj->m_Color.r, MatColor.g * pObj->m_Color.g, MatColor.b * pObj->m_Color.b, 1);
    Set4(dc.vAmbient, 0, 0, 0, bTerrain ? 1.0f : 2.0f);
    SetPointLight(dc, 0, pLight, pObj, vCenter, r);
    dc.nLights = 1;
    dc.nTex5 = pCube->GetSRV();
    memcpy(dc.mParams0, pLight->m_TextureMatrix.data, sizeof(dc.mParams0));
    Key.nState = bTerrain ? GS_BLSRC_ONE | GS_BLDST_ONE
                 : bBlended ? GS_BLSRC_SRCALPHA | GS_BLDST_ONE
                            : GS_BLSRC_ONE | GS_BLDST_ONE | GS_DEPTHFUNC_EQUAL;
    TexCoord1.reset();
    if (!(dc.nFlags & DF_BUMP))
      Tangents.reset();
    pTex = pDiffuse;
    Draw();
  }
}

void CD3D12Renderer::DumpItem(int nIndex, const SRenderItem &it, int eList)
{
  static const char *const s_DrawClasses[] = { "NoDraw", "Opaque", "AlphaTest", "AlphaTestVColor", "VColor", "AlphaBlend", "Additive", "Terrain", "Sky", "Engine", "Water", "WaterBottom", "WaterBeach", "TerrainLayer", "Decal",
                                                "TerrainShadow", "Caustics", "FogLayer", "ScopeLens", "Flare", "Modulate", "TerrainLight" };
  const CCObject *pObj = it.pObj;
  const char *szLB = "";
  if (it.pRE->m_Type == eDATA_OcLeaf && static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer)
    szLB = static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer->m_sSource;
  const CTexture *pTex = GetDiffuseTexture(it.pRes);
  fprintf(m_pFrameDump.get(), "[%d] list %d sort 0x%x fog %d %s RE %d shader '%s' templ '%s' tex '%s' lb '%s' pos (%.2f %.2f %.2f) flags 0x%x LM %d (dir %d occl %d HDR %d) LMTC %d mask 0x%x amb (%.2f %.2f %.2f) col (%.2f %.2f %.2f %.2f) res aref %.3f opacity %.2f flags 0x%x casters %d\n",
          nIndex, eList, it.nSort, it.nFog, (unsigned)it.eDrawClass < std::size(s_DrawClasses) ? s_DrawClasses[it.eDrawClass] : "?", it.pRE->m_Type,
          it.pShader ? it.pShader->GetName() : "", it.pShader ? it.pShader->GetProps().Template.c_str() : "", pTex ? pTex->m_Name.c_str() : "",
          szLB ? szLB : "",
          pObj->m_Matrix.data[12], pObj->m_Matrix.data[13], pObj->m_Matrix.data[14], pObj->m_ObjFlags, pObj->m_nLMId,
          pObj->m_nLMDirId, pObj->m_nOcclId, pObj->m_nHDRLMId, pObj->m_pLMTCBufferO ? 1 : 0, pObj->m_DynLMMask, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z,
          pObj->m_Color.r, pObj->m_Color.g, pObj->m_Color.b, pObj->m_Color.a, it.pRes ? it.pRes->m_AlphaRef : 0.0f,
          it.pRes ? it.pRes->m_Opacity : 1.0f, it.pRes ? it.pRes->m_ResFlags : 0,
          pObj->m_pShadowCasters ? reinterpret_cast<const int *>(pObj->m_pShadowCasters)[2] : -1);
  // The rotation and scale of the object's matrix, when it has one.
  const float *m = pObj->m_Matrix.data;
  if (m[0] != 1 || m[1] != 0 || m[2] != 0 || m[4] != 0 || m[5] != 1 || m[6] != 0 || m[8] != 0 || m[9] != 0 || m[10] != 1)
    fprintf(m_pFrameDump.get(), "    matrix x (%.3f %.3f %.3f) y (%.3f %.3f %.3f) z (%.3f %.3f %.3f)\n", m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]);

  // Shader parameters of the object and the resources, the element's custom data.
  auto Params = [](const TArray<SShaderParam> *pParams)
  {
    std::string s;
    for (int i = 0; pParams && i < pParams->Num(); i++)
    {
      const SShaderParam &p = (*pParams)[i];
      char sz[128];
      switch (p.m_Type)
      {
      case eType_FLOAT:  snprintf(sz, sizeof(sz), " %s=%g", p.m_Name, p.m_Value.m_Float); break;
      case eType_STRING: snprintf(sz, sizeof(sz), " %s='%s'", p.m_Name, p.m_Value.m_String ? p.m_Value.m_String : ""); break;
      case eType_FCOLOR: snprintf(sz, sizeof(sz), " %s=(%g %g %g %g)", p.m_Name, p.m_Value.m_Color[0], p.m_Value.m_Color[1], p.m_Value.m_Color[2], p.m_Value.m_Color[3]); break;
      case eType_VECTOR: snprintf(sz, sizeof(sz), " %s=(%g %g %g)", p.m_Name, p.m_Value.m_Vector[0], p.m_Value.m_Vector[1], p.m_Value.m_Vector[2]); break;
      default:           snprintf(sz, sizeof(sz), " %s=%d", p.m_Name, p.m_Value.m_Int); break;
      }
      s += sz;
    }
    return s;
  };
  const std::string ObjParams = Params(pObj->m_ShaderParams), ResParams = Params(it.pRes ? &it.pRes->m_ShaderParams : nullptr);
  const CREOcLeaf *pLeaf = it.pRE->m_Type == eDATA_OcLeaf ? static_cast<const CREOcLeaf *>(it.pRE) : nullptr;
  if (!ObjParams.empty() || !ResParams.empty() || (pLeaf && pLeaf->m_CustomData))
  {
    fprintf(m_pFrameDump.get(), "    params: object {%s } resources {%s }", ObjParams.c_str(), ResParams.c_str());
    if (pLeaf && pLeaf->m_CustomData)
    {
      fprintf(m_pFrameDump.get(), " custom data %d:", pLeaf->m_nCountCustomData);
      for (int i = 0; i < std::min(pLeaf->m_nCountCustomData, 4) * 4; i++)
        fprintf(m_pFrameDump.get(), " %g", ((const float *)pLeaf->m_CustomData)[i]);
    }
    fprintf(m_pFrameDump.get(), "\n");
  }

  // The material's other texture slots by name, '*' when loaded.
  if (it.pRes)
  {
    std::string Slots;
    for (int i = 0; i < EFTT_MAX; i++)
    {
      const SEfResTexture *pT = it.pRes->m_Textures[i];
      if (i == EFTT_DIFFUSE || !pT || pT->m_Name.empty())
        continue;
      char sz[300];
      snprintf(sz, sizeof(sz), " %d '%s'%s", i, pT->m_Name.c_str(), pT->m_TU.m_ITexPic ? "*" : "");
      Slots += sz;
    }
    if (!Slots.empty())
      fprintf(m_pFrameDump.get(), "    texture slots:%s\n", Slots.c_str());
  }

  // The sky: its values, and how many fog layers and portals (areas open to the sky) it has.
  if (it.pRE->m_Type == eDATA_Sky)
  {
    const CRESky *pSky = static_cast<const CRESky *>(it.pRE);
    int nPortals = 0;
    for (const auto &Portal : pSky->m_arrvPortalVerts)
      nPortals += Portal[0].xyz != Portal[1].xyz ? 1 : 0;
    fprintf(m_pFrameDump.get(), "    sky: water level %.2f stretching %.2f alpha %.2f sphere list %d fog layers %d %d portals %d\n", pSky->m_fTerrainWaterLevel,
            pSky->m_fSkyBoxStretching, pSky->m_fAlpha, pSky->m_nSphereListId, pSky->m_parrFogLayer ? pSky->m_parrFogLayer->Count() : -1,
            pSky->m_parrFogLayer2 ? pSky->m_parrFogLayer2->Count() : -1, nPortals);
  }

  // The tangent frames of the video copy (bump mapping): how many, of how many vertices.
  if (pLeaf && pLeaf->m_pBuffer)
  {
    const CVertexBuffer *pVB = pLeaf->m_pBuffer->GetVertexContainer()->m_pVertexBuffer;
    if (pVB && pVB->m_VS[VSF_TANGENTS].m_VData)
      fprintf(m_pFrameDump.get(), "    tangents: %d of %d vertices\n", pVB->m_VS[VSF_TANGENTS].m_nItems, pVB->m_NumVerts);
  }

  // Terrain sectors: the element's custom texture binds and the texture coordinates in its custom
  // data (the sector's, then those of the cliff textures in binds 1 and 2).
  if (pLeaf && it.eDrawClass == eDC_Terrain && szLB && !strcmp(szLB, "TerrainSector"))
  {
    FILE *f = m_pFrameDump.get();
    fprintf(f, "    texture binds");
    for (int b : pLeaf->m_CustomTexBind)
    {
      const CTexture *pB = b > 0 && m_pTextures ? m_pTextures->GetByID(b) : nullptr;
      fprintf(f, " %d '%s'", b, pB ? pB->m_Name.c_str() : "");
    }
    if (const float *pCustom = (const float *)pLeaf->m_CustomData)
    {
      fprintf(f, ", custom data");
      const int nBinds = pLeaf->m_CustomTexBind[2] > 0 ? 2 : pLeaf->m_CustomTexBind[1] > 0 ? 1 : 0;
      for (int i = 0; i < 4 + 8 * nBinds; i++)
        fprintf(f, " %g", pCustom[i]);
    }
    fprintf(f, "\n");
  }

  // Light mapped elements: the chunk's vertices and indices, the object's light map texture
  // coordinates (count, those of the chunk's first vertex).
  if (pObj->m_nLMId > 0 && m_pTextures)
  {
    const CTexture *pLM = m_pTextures->GetByID(pObj->m_nLMId), *pDir = pObj->m_nLMDirId > 0 ? m_pTextures->GetByID(pObj->m_nLMDirId) : nullptr;
    fprintf(m_pFrameDump.get(), "    light map '%s' %dx%d, direction '%s'\n", pLM ? pLM->m_Name.c_str() : "?", pLM ? pLM->m_nWidth : 0, pLM ? pLM->m_nHeight : 0,
            pDir ? pDir->m_Name.c_str() : "");
  }
  if (pLeaf && pLeaf->m_pChunk && pObj->m_pLMTCBufferO)
  {
    const CMatInfo *pC = pLeaf->m_pChunk;
    const CVertexBuffer *pVB = pObj->m_pLMTCBufferO->m_pVertexBuffer;
    const CLeafBuffer *pVC = pLeaf->m_pBuffer ? pLeaf->m_pBuffer->GetVertexContainer() : nullptr;
    FILE *f = m_pFrameDump.get();
    fprintf(f, "    chunk: vertices %d + %d of %d, indices %d + %d; light map coordinates %d", pC->nFirstVertId, pC->nNumVerts,
            pVC && pVC->m_pSecVertBuffer ? pVC->m_pSecVertBuffer->m_NumVerts : 0, pC->nFirstIndexId, pC->nNumIndices, pVB ? pVB->m_NumVerts : 0);
    if (pVB && pVB->m_VS[VSF_GENERAL].m_VData && pC->nFirstVertId >= 0 && pC->nFirstVertId < pVB->m_NumVerts)
    {
      const float *uv = (const float *)pVB->m_VS[VSF_GENERAL].m_VData + 2 * (size_t)pC->nFirstVertId;
      fprintf(f, ", first (%g %g)", uv[0], uv[1]);
    }
    fprintf(f, "\n");
  }

  // Water elements: the object's matrix and the first vertices, word by word (float and hex).
  if (pLeaf && pLeaf->m_pBuffer && (it.eDrawClass == eDC_Water || it.eDrawClass == eDC_WaterBottom || it.eDrawClass == eDC_WaterBeach))
  {
    const CLeafBuffer *pVC = pLeaf->m_pBuffer->GetVertexContainer();
    const CVertexBuffer *pVB = pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData ? pVC->m_pSecVertBuffer : pVC->m_pVertexBuffer;
    FILE *f = m_pFrameDump.get();
    fprintf(f, "    matrix");
    for (float v : pObj->m_Matrix.data)
      fprintf(f, " %g", v);
    if (pLeaf->m_pChunk)
      fprintf(f, " chunk: vertices %d + %d, indices %d + %d", pLeaf->m_pChunk->nFirstVertId, pLeaf->m_pChunk->nNumVerts,
              pLeaf->m_pChunk->nFirstIndexId, pLeaf->m_pChunk->nNumIndices);
    fprintf(f, "\n");
    if (pVB && pVB->m_VS[VSF_GENERAL].m_VData)
    {
      const int nStride = CD3D12Pipeline::GetVertexSize(pVB->m_vertexformat);
      fprintf(f, "    vertices: format %d, %d of them\n", pVB->m_vertexformat, pVB->m_NumVerts);
      for (int i = 0; i < std::min(pVB->m_NumVerts, 8) && nStride > 0; i++)
      {
        const uint32 *w = (const uint32 *)((const byte *)pVB->m_VS[VSF_GENERAL].m_VData + (size_t)i * nStride);
        fprintf(f, "     ");
        for (int k = 0; k < nStride / 4; k++)
          fprintf(f, " %g/%08x", std::bit_cast<float>(w[k]), w[k]);
        fprintf(f, "\n");
      }
    }
  }
}

void CD3D12Renderer::DumpDraw(const SDrawConstants &dc, const SPipelineKey &Key, const CTexture *pTex)
{
  static const char *const s_Flags[] = { "TEX0", "TEX1", "VCOLOR", "VCOLOR_X2", "LIGHTING", "ALPHATEST", "TEXGEN_WORLD", "TEXMATRIX", "FOG",
                                         "TEX1_DETAIL", "TEX1_LIGHTMAP", "ALPHA_FROM_TEX", "CUBE0", "TEX0_ALPHA_ONLY", "SCREEN_POS", "ALPHATEST_LESS", "TEXGEN1_WORLD",
                                         "ALPHA_ONE", "TEX0_ARRAY", "DETAIL_LAYER", "TERRAIN_CLIFF", "DETAIL_BUMP", "DECAL_FADE", "SHADOWS",
                                         "SHADOW_PASS", "SHADOW_DEBUG", "LIGHTMAP_DIR", "VALPHA_ONLY", "CAUSTICS", "BUMP", "PIXEL_LIGHTING", "SPECULAR" };
  std::string Flags;
  for (size_t i = 0; i < std::size(s_Flags); i++)
    if (dc.nFlags & (1u << i))
      Flags += (Flags.empty() ? "" : "|") + std::string(s_Flags[i]);
  FILE *f = m_pFrameDump.get();
  if (dc.nFlags2 & DF2_ADD_TEX4)
    Flags += (Flags.empty() ? "" : "|") + std::string("ADD_TEX4");
  if (dc.nFlags2 & DF2_GLASS)
    Flags += (Flags.empty() ? "" : "|") + std::string("GLASS");
  if (dc.nFlags2 & DF2_PROJECTOR)
    Flags += (Flags.empty() ? "" : "|") + std::string("PROJECTOR");
  if (dc.nFlags2 & DF2_TERRAIN_LIGHT)
    Flags += (Flags.empty() ? "" : "|") + std::string("TERRAIN_LIGHT");
  if (dc.nFlags2 & DF2_NO_SUN)
    Flags += (Flags.empty() ? "" : "|") + std::string("NO_SUN");
  if (dc.nFlags2 & DF2_VERTEX_LIGHTS)
    Flags += (Flags.empty() ? "" : "|") + std::string("VERTEX_LIGHTS");
  if (dc.nFlags2 & DF2_SMOOTH_FALLOFF)
    Flags += (Flags.empty() ? "" : "|") + std::string("SMOOTH_FALLOFF");
  if (dc.nFlags2 & DF2_LIGHTMAP_LIGHTS)
    Flags += (Flags.empty() ? "" : "|") + std::string("LIGHTMAP_LIGHTS");
  if (dc.nFlags2 & DF2_AO_BAKED)
    Flags += (Flags.empty() ? "" : "|") + std::string("AO_BAKED");
  if (dc.nFlags2 & DF2_AO_ALL)
    Flags += (Flags.empty() ? "" : "|") + std::string("AO_ALL");
  fprintf(f, "    draw: fmt %d state 0x%x cull %d bias %d flags %s tex0 '%s' alpharef %.3f col (%.2f %.2f %.2f %.2f) amb (%.2f %.2f %.2f) x%g lights %u",
          Key.nVertexFormat, Key.nState, Key.nCull, Key.bDepthBias, Flags.c_str(), (dc.nFlags & DF_TEX0) && pTex ? pTex->m_Name.c_str() : "",
          dc.fAlphaRef, dc.vColor[0], dc.vColor[1], dc.vColor[2], dc.vColor[3], dc.vAmbient[0], dc.vAmbient[1], dc.vAmbient[2], dc.vAmbient[3], dc.nLights);
  if (dc.nFlags & DF_SHADOWS)
    fprintf(f, " shadow maps %d", (int)dc.vShadowParams[0]);
  for (uint32 i = 0; i < dc.nLights; i++)
    fprintf(f, " {(%.1f %.1f %.1f %.0f) col (%.2f %.2f %.2f) att (%.3f %.3f %.1f) 1/r %.4f}", dc.vLightPos[i][0], dc.vLightPos[i][1], dc.vLightPos[i][2],
            dc.vLightPos[i][3], dc.vLightColor[i][0], dc.vLightColor[i][1], dc.vLightColor[i][2], dc.vLightAtten[i][0], dc.vLightAtten[i][1], dc.vLightAtten[i][2],
            dc.vLightAtten[i][3]);
  fprintf(f, "\n");
}

void CD3D12Renderer::SetObjectLights(SDrawConstants &dc, const CCObject *pObj, const Matrix44 &World, CLeafBuffer *pLB)
{
  // The lights of the object's m_DynLMMask besides the sun (an empty mask: none, and no sun), at most
  // XR_MAX_DRAW_LIGHTS; projected lights are not among them (their pass is of its own, DrawLeafItem).
  // Point lights: SetPointLight. Directional lights come from their origin.
  Vec3 vCenter;
  float r;
  GetLightSphere(World, pLB, vCenter, r);
  const std::span<CDLight *const> Lights = m_pScene->GetLights();
  // The sun lights the object only with its bit in the mask: the engine leaves it out where the sun
  // does not reach (the first person weapon in a tunnel).
  const CDLight *pSun = m_pScene->GetSun();
  for (size_t i = 0; pSun && i < Lights.size() && i < 32; i++)
  {
    if (Lights[i] == pSun && !(pObj->m_DynLMMask & (1u << i)))
      dc.nFlags2 |= DF2_NO_SUN;
  }
  uint32 n = 0;
  for (size_t i = 0; i < Lights.size() && i < 32 && n < XR_MAX_DRAW_LIGHTS; i++)
  {
    const CDLight *pLight = Lights[i];
    if (!pLight || (pLight->m_Flags & (DLF_SUN | DLF_PROJECT)))
      continue;
    // A heat source alone gives no light (heat vision, ItemHeat).
    if ((pLight->m_Flags & (DLF_HEATSOURCE | DLF_LIGHTSOURCE)) == DLF_HEATSOURCE)
      continue;
    // A light baked into light maps (DLF_LM) lights an object with a light map through it only.
    if ((pLight->m_Flags & DLF_LM) && pObj->m_nLMId > 0)
      continue;
    if ((pObj->m_DynLMMask & (1u << i)) == 0)
      continue;
    if (pLight->m_Flags & DLF_DIRECTIONAL)
    {
      Vec3 vDir = pLight->m_Origin - vCenter;
      const float fLen = vDir.GetLength();
      vDir = fLen > 0 ? vDir / fLen : Vec3(0, 0, 1);
      Set4(dc.vLightColor[n], pLight->m_Color.r, pLight->m_Color.g, pLight->m_Color.b, 1);
      Set4(dc.vLightSpec[n], pLight->m_SpecColor.r, pLight->m_SpecColor.g, pLight->m_SpecColor.b, 1);
      Set4(dc.vLightPos[n], vDir.x, vDir.y, vDir.z, 2);
      Set4(dc.vLightAtten[n], 1, 0, 0, 0);
      n++;
      continue;
    }
    SetPointLight(dc, n++, pLight, pObj, vCenter, r);
  }
  dc.nLights = n;
}

CTexture *CD3D12Renderer::GetDetailTexture()
{
  if (!m_pTextures)
    return nullptr;
  if (!m_pDetailTexture)
    m_pDetailTexture = m_pTextures->LoadTexture("textures/terrain/detail/default", 0, 0, eTT_Base, -1, -1, -1, 0);
  return m_pDetailTexture && m_pDetailTexture->IsTextureLoaded() ? m_pDetailTexture : nullptr;
}

CTexture *CD3D12Renderer::GetNormalMap(SRenderShaderResources *pRes)
{
  if (!pRes || !m_pShaders)
    return nullptr;
  auto Usable = [](CTexture *p) { return p && !p->IsCube() && !(p->GetFlags() & FT_NOTFOUND) && p->IsTextureLoaded(); };
  CTexture *pMap = m_pShaders->LoadResourceTexture(pRes, EFTT_NORMALMAP);
  if (!Usable(pMap))
    pMap = m_pShaders->LoadResourceTexture(pRes, EFTT_BUMP);
  return Usable(pMap) ? pMap : nullptr;
}

CTexture *CD3D12Renderer::GetGlowMap(SRenderShaderResources *pRes)
{
  if (!pRes || !m_pShaders)
    return nullptr;
  CTexture *pMap = m_pShaders->LoadResourceTexture(pRes, EFTT_GLOSS);
  return pMap && !pMap->IsCube() && !(pMap->GetFlags() & FT_NOTFOUND) && pMap->IsTextureLoaded() ? pMap : nullptr;
}

CTexture *CD3D12Renderer::GetEnvCubeMap(SRenderShaderResources *pRes)
{
  if (!m_pShaders || !m_pTextures)
    return nullptr;
  // The material's cube is EFTT_CUBEMAP, the name of a face; EFTT_REFLECTION is not used.
  CTexture *pCube = pRes ? m_pShaders->LoadResourceTexture(pRes, EFTT_CUBEMAP) : nullptr;
  if (pCube && pCube->IsCube() && pCube->IsTextureLoaded())
    return pCube;
  if (!m_pDefaultCube)
    m_pDefaultCube = m_pTextures->LoadTexture("textures/defaults/defaultcm", 0, 0, eTT_Cubemap, -1, -1, -1, 0);
  return m_pDefaultCube && m_pDefaultCube->IsCube() && m_pDefaultCube->IsTextureLoaded() ? m_pDefaultCube : nullptr;
}

void CD3D12Renderer::DrawSkyItem(const SRenderItem &it)
{
  if (it.eDrawClass != eDC_Sky || !m_pShaders || !m_pTextures)
    return;
  const SSkyShader *pSkyShader = m_pShaders->FindSky(it.pShader->GetName());
  if (!pSkyShader)
    return;
  auto Box = std::find_if(m_SkyBoxes.begin(), m_SkyBoxes.end(), [&](const SSkyBoxTextures &b) { return b.Name == it.pShader->GetName(); });
  if (Box == m_SkyBoxes.end())
  {
    SSkyBoxTextures New;
    New.Name = it.pShader->GetName();
    static const char *const s_Suffixes[3] = { "_5", "_12", "_34" };
    for (int i = 0; i < 3; i++)
      New.pTex[i] = m_pTextures->LoadTexture(("textures/" + pSkyShader->Box + s_Suffixes[i]).c_str(), FT_CLAMP | FT_NOMIPS, 0, eTT_Base, -1, -1, -1, 0);
    for (const SSkyLayer &Layer : pSkyShader->Layers)
      New.Layers.push_back(m_pTextures->LoadTexture(Layer.Map.c_str(), FT_NOMIPS, 0, eTT_Base, -1, -1, -1, 0));
    m_SkyBoxes.push_back(std::move(New));
    Box = m_SkyBoxes.end() - 1;
  }
  m_nSceneSkyBox = (int)(Box - m_SkyBoxes.begin());

  // A box around the camera, of half size 32 in world axes: the top at z = 32 with u = (32 - y) / 64,
  // v = (x + 32) / 64, and the four sides from z = 32 down to z1 and on to z2. Seen from inside, the
  // sides make one picture from left to right: x = 32 (the upper half of _12), y = -32 (its lower
  // half, upside down), x = -32 (the upper half of _34), y = 32 (its lower half); along each side u
  // runs from 1 to 0 counter-clockwise (seen from above), and v from 0 (upper half) or 1 (lower
  // half) at the top to 0.5 at z1, which it keeps down to z2. With h the camera's height over the
  // water level, s the sky box stretching and F the far plane, z1 = -s h (30.72 / F + 1 / 128) and
  // z2 = -8 + h (30.72 / F - 0.018); both 0 below the water.
  const CRESky *pSky = static_cast<const CRESky *>(it.pRE);
  const float F = std::max(m_Camera.GetZMax(), 1.0f);
  const float h = m_Camera.GetPos().z - pSky->m_fTerrainWaterLevel;
  float z1 = 0, z2 = 0;
  if (h >= 0)
  {
    z1 = -pSky->m_fSkyBoxStretching * h * (30.72f / F + 1.0f / 128);
    z2 = -8 + h * (30.72f / F - 0.018f);
  }
  struct SSide
  {
    float x0, y0, x1, y1;   //!< from (u 1) to (u 0)
    float vTop;
    int nTex;
  };
  static const SSide s_Sides[4] =
  {
    {  32, -32,  32,  32, 0, 1 },
    { -32, -32,  32, -32, 1, 1 },
    { -32,  32, -32, -32, 0, 2 },
    {  32,  32, -32,  32, 1, 2 },
  };

  // The view without its translation; no depth test or writes, no fog; texture * (1, 1, 1, alpha),
  // blended below alpha 1.
  Matrix44 View = m_CameraView;
  View.data[12] = View.data[13] = View.data[14] = 0;
  const Matrix44 ViewProj = View * m_SkyProjMatrix;
  const float fAlpha = std::clamp(pSky->m_fAlpha, 0.0f, 1.0f);
  m_fSceneSkyZ1 = z1;
  m_fSceneSkyAlpha = fAlpha;
  SPipelineKey Key = {};
  Key.nState = GS_NODEPTHTEST | (fAlpha < 1 ? GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA : 0);
  Key.nProgram = eD3D12P_General;
  Key.nCull = R_CULL_NONE;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
  m_bItemScissor = false;
  // With r_HDRRendering the sky brighter towards the sun (d3d12_HDRSky), the box and its layers.
  const bool bSkyGlow = HdrSkyGlow() != 1;

  auto Draw = [&](int nTex, const struct_VERTEX_FORMAT_P3F_TEX2F *pVerts, int nVerts)
  {
    CTexture *pTex = Box->pTex[nTex];
    if (!pTex || !pTex->IsTextureLoaded() || pTex->IsCube())
      return;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    if (m_bUpscaling)
      memcpy(dc.mPrevWorldViewProj, m_PrevSkyViewProj.data, sizeof(dc.mPrevWorldViewProj));
    dc.nFlags = DF_TEX0;
    dc.nFlags2 = bSkyGlow ? DF2_SKY_GLOW : 0;
    dc.nTex0 = pTex->GetSRV();
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
    Set4(dc.vColor, 1, 1, 1, fAlpha);
    if (m_pFrameDump)
      DumpDraw(dc, Key, pTex);
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, VERTEX_FORMAT_P3F_TEX2F, pVerts, nVerts);
  };
  const struct_VERTEX_FORMAT_P3F_TEX2F Top[4] =
  {
    { Vec3(-32, -32, 32), { 1, 0 } },
    { Vec3( 32, -32, 32), { 1, 1 } },
    { Vec3(-32,  32, 32), { 0, 0 } },
    { Vec3( 32,  32, 32), { 0, 1 } },
  };
  Draw(0, Top, 4);
  for (const SSide &s : s_Sides)
  {
    const struct_VERTEX_FORMAT_P3F_TEX2F Verts[6] =
    {
      { Vec3(s.x0, s.y0, 32), { 1, s.vTop } },
      { Vec3(s.x1, s.y1, 32), { 0, s.vTop } },
      { Vec3(s.x0, s.y0, z1), { 1, 0.5f } },
      { Vec3(s.x1, s.y1, z1), { 0, 0.5f } },
      { Vec3(s.x0, s.y0, z2), { 1, 0.5f } },
      { Vec3(s.x1, s.y1, z2), { 0, 0.5f } },
    };
    Draw(s.nTex, Verts, 6);
  }

  // The layers over the box, in the sky's own axes: the world's turned a quarter about z (x_sky =
  // -y, y_sky = x; the box's top above has u = (x_sky + 32) / 64, v = (y_sky + 32) / 64 in them). A
  // dome around the camera, its top at z = 8 over a ring of radius 16 at z = 0 and one of radius 32
  // at z = -8, in 9 segments of 40 degrees from the y_sky axis towards x_sky; white, opaque up to the
  // inner ring and transparent at the outer one (the dome goes on to radius 48 at z = -16, all of it
  // transparent: not drawn). The texture * the vertex colour (not faded by the sky's alpha), u and v
  // from the layer's planes over the position in the sky's axes; blended as the layer's Blend, no
  // depth test or writes, no fog.
  const float t = iTimer ? iTimer->GetCurrTime() : 0.0f;
  for (size_t l = 0; l < pSkyShader->Layers.size() && l < Box->Layers.size(); l++)
  {
    const SSkyLayer &Layer = pSkyShader->Layers[l];
    CTexture *pTex = Box->Layers[l];
    if (!pTex || !pTex->IsTextureLoaded() || pTex->IsCube())
      continue;
    float PlaneS[4], PlaneT[4];
    for (int i = 0; i < 4; i++)
    {
      PlaneS[i] = Layer.PlaneS[i] + Layer.TimeS[i] * t;
      PlaneT[i] = Layer.PlaneT[i] + Layer.TimeT[i] * t;
    }
    const auto Vertex = [&](float fRadius, float fAngle, float z, uint8 nAlpha)
    {
      const Vec3 vSky(fRadius * cosf(fAngle), fRadius * sinf(fAngle), z);
      struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F v;
      v.xyz = Vec3(vSky.y, -vSky.x, vSky.z);
      v.color.dcolor = 0x00ffffff | (uint32)nAlpha << 24;
      v.st[0] = PlaneS[0] * vSky.x + PlaneS[1] * vSky.y + PlaneS[2] * vSky.z + PlaneS[3];
      v.st[1] = PlaneT[0] * vSky.x + PlaneT[1] * vSky.y + PlaneT[2] * vSky.z + PlaneT[3];
      return v;
    };
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Top[20], Ring[20];
    for (int i = 0; i < 10; i++)
    {
      const float fAngle = DEG2RAD(90.0f - 40.0f * i);
      Top[i * 2] = Vertex(0, 0, 8, 255);
      Top[i * 2 + 1] = Ring[i * 2] = Vertex(16, fAngle, 0, 255);
      Ring[i * 2 + 1] = Vertex(32, fAngle, -8, 0);
    }
    SPipelineKey LayerKey = Key;
    LayerKey.nState = GS_NODEPTHTEST | Layer.nBlend;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    if (m_bUpscaling)
      memcpy(dc.mPrevWorldViewProj, m_PrevSkyViewProj.data, sizeof(dc.mPrevWorldViewProj));
    dc.nFlags = DF_TEX0 | DF_VCOLOR;
    dc.nFlags2 = bSkyGlow ? DF2_SKY_GLOW : 0;
    dc.nTex0 = pTex->GetSRV();
    dc.nSamplers = XR_SAMPLER_LINEAR_WRAP;
    if (m_pFrameDump)
    {
      fprintf(m_pFrameDump.get(), "  sky layer %d '%s' u (%g %g %g %g) v (%g %g %g %g) at time %.3f\n", (int)l, Layer.Map.c_str(), PlaneS[0],
              PlaneS[1], PlaneS[2], PlaneS[3], PlaneT[0], PlaneT[1], PlaneT[2], PlaneT[3], t);
      DumpDraw(dc, LayerKey, pTex);
    }
    DrawImmediate(LayerKey, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Top, 20);
    DrawImmediate(LayerKey, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Ring, 20);
  }
}

void CD3D12Renderer::DrawSprites()
{
  // Particles: camera facing quads in world space, the object's texture (m_NumCM) and blending
  // (m_RenderState), depth tested without depth writes. Colour (the vertex colour + the object's
  // ambient colour, each channel at most 1) * texture, alpha vertex alpha * texture alpha.
  const std::span<const SSceneSprite> Sprites = m_pScene->GetSprites();
  if (Sprites.empty())
    return;
  const std::span<const SColorVert> Verts = m_pScene->GetSpriteVerts();
  const std::span<const ushort> Inds = m_pScene->GetSpriteIndices();
  std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> BatchVerts;
  std::vector<ushort> BatchInds;
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
  m_bItemScissor = false;
  // One draw per run of sprites with the same texture and blending (each sprite has an object of
  // its own; the ambient colour goes into the vertices).
  auto SameDraw = [](const CCObject *a, const CCObject *b)
  { return a && b && a->m_NumCM == b->m_NumCM && (a->m_RenderState & GS_BLEND_MASK) == (b->m_RenderState & GS_BLEND_MASK); };
  for (size_t nRun = 0; nRun < Sprites.size();)
  {
    const CCObject *pObj = Sprites[nRun].pObj;
    if (!pObj)
    {
      nRun++;
      continue;
    }
    BatchVerts.clear();
    BatchInds.clear();
    int nCount = 0;
    size_t nNext = nRun;
    for (; nNext < Sprites.size() && SameDraw(Sprites[nNext].pObj, pObj); nNext++)
    {
      const SSceneSprite &Sprite = Sprites[nNext];
      if (Sprite.nVerts <= 0 || Sprite.nIndices <= 0 ||
          Sprite.nFirstVert + Sprite.nVerts > (int)Verts.size() || Sprite.nFirstIndex + Sprite.nIndices > (int)Inds.size())
        continue;
      // The engine's colours are bytes R, G, B, A; the vertex format's B, G, R, A.
      const Vec3 &Amb = Sprite.pObj->m_AmbColor;
      const int nAmb[3] = { (int)(std::clamp(Amb.z, 0.0f, 1.0f) * 255 + 0.5f), (int)(std::clamp(Amb.y, 0.0f, 1.0f) * 255 + 0.5f),
                            (int)(std::clamp(Amb.x, 0.0f, 1.0f) * 255 + 0.5f) };
      // 16 bit indices.
      if (!BatchVerts.empty() && BatchVerts.size() + Sprite.nVerts > 0xFFFF)
        break;
      const size_t nBase = BatchVerts.size();
      BatchVerts.resize(nBase + Sprite.nVerts);
      for (int i = 0; i < Sprite.nVerts; i++)
      {
        const SColorVert &v = Verts[Sprite.nFirstVert + i];
        struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F &o = BatchVerts[nBase + i];
        o.xyz = v.vert;
        o.color.bcolor[0] = (uint8)std::min(v.color.bcolor[2] + nAmb[0], 255);
        o.color.bcolor[1] = (uint8)std::min(v.color.bcolor[1] + nAmb[1], 255);
        o.color.bcolor[2] = (uint8)std::min(v.color.bcolor[0] + nAmb[2], 255);
        o.color.bcolor[3] = v.color.bcolor[3];
        o.st[0] = v.dTC[0];
        o.st[1] = v.dTC[1];
      }
      for (int i = 0; i < Sprite.nIndices; i++)
        BatchInds.push_back((ushort)(nBase + Inds[Sprite.nFirstIndex + i]));
      nCount++;
    }
    nRun = nNext;
    if (BatchVerts.empty())
      continue;

    SPipelineKey Key = {};
    const uint32 nBlend = pObj->m_RenderState & GS_BLEND_MASK;
    Key.nState = nBlend ? nBlend : (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
    Key.nProgram = eD3D12P_General;
    Key.nCull = R_CULL_NONE;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    SetStaticMotion(dc);
    dc.nFlags = DF_VCOLOR;
    // An additive one (fire, sparks, explosions) with r_HDRRendering brighter in its core.
    if ((Key.nState & GS_BLDST_MASK) == GS_BLDST_ONE)
      SetHdrCore(dc, CV_d3d12_HDRGlow, Key.nState);
    CTexture *pTex = m_pTextures ? m_pTextures->GetByID(pObj->m_NumCM) : nullptr;
    if (pTex && !pTex->IsCube())
    {
      dc.nFlags |= DF_TEX0;
      dc.nTex0 = pTex->GetSRV();
      dc.nSamplers = pTex->GetSampler();
    }
    if (m_bFog)
      dc.nFlags |= DF_FOG;
    if (m_pFrameDump)
    {
      const UCol &c = Verts[Sprites[nNext - 1].nFirstVert].color;
      const Vec3 &Amb = pObj->m_AmbColor;
      fprintf(m_pFrameDump.get(), "  sprites: %d (%d verts), texture %d, state 0x%x, sort %g, first amb (%.2f %.2f %.2f), a vertex colour's bytes %u %u %u %u\n",
              nCount, (int)BatchVerts.size(), pObj->m_NumCM, pObj->m_RenderState, pObj->m_SortId, Amb.x, Amb.y, Amb.z,
              c.bcolor[0], c.bcolor[1], c.bcolor[2], c.bcolor[3]);
      DumpDraw(dc, Key, pTex);
    }
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, BatchVerts.data(), (int)BatchVerts.size(),
      BatchInds.data(), (int)BatchInds.size());
  }
}
