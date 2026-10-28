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
//	list after the water surfaces.
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

}

void CD3D12Renderer::BeginScene3D()
{
  // The sun of the scene as the directional light (black without one); each object can also get
  // lights of its own (SetObjectLights).
  Vec3 vDir(0, 0, 1);
  CFColor Color(0, 0, 0, 1);
  if (m_pScene->GetSun())
  {
    vDir = m_pScene->GetSunDirection();
    Color = m_pScene->GetSunColor();
  }
  vDir.Normalize();
  m_SunDir[0] = vDir.x; m_SunDir[1] = vDir.y; m_SunDir[2] = vDir.z;
  m_SunColor[0] = Color.r; m_SunColor[1] = Color.g; m_SunColor[2] = Color.b;
  m_NearestProj = MakeNearestProjection(m_Camera);
  m_nSceneSkyBox = -1;
  InvalidateFrameConstants();

  // The main view of the game clears colour and depth before its scene.
  if ((m_nSceneFlags & SHDF_ALLOWHDR) && m_nRecurseLevel <= 1)
  {
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
  if (eList == eRL_Blended)
    m_bSceneCopied = false;
  GpuSection(eList == eRL_Opaque ? "opaque" : eList == eRL_Nearest ? "nearest" : "water");
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "list %d: %d items, recursion %d, scene flags 0x%x\n", eList, nItems, m_nRecurseLevel, m_nSceneFlags);
  for (int i = 0; i < nItems; i++)
  {
    const SRenderItem &it = pItems[i];
    if (!it.pRE)
      continue;
    if (!bSprites && it.eDrawClass != eDC_Water && it.eDrawClass != eDC_WaterBeach)
    {
      GpuSection("blended");
      DrawSprites();
      bSprites = true;
    }
    if (m_pFrameDump)
      DumpItem(i, it, eList);
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
    DrawSprites();
  }
  m_bItemScissor = false;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
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
  if (eDC == eDC_NoDraw || eDC == eDC_Engine || eDC == eDC_Sky)
    return;
  // The fog volume pass: the element once more, with the ID of the fog volume it is in, in the
  // sort group eS_FogShader (the object of a static one has FOB_FOGPASS). The original draws
  // it only with r_VolumetricFog; not drawn yet, but for the caustics under the ocean.
  if (eDC != eDC_Caustics && ((it.nSort & ~EFSLIST_MASK) == eS_FogShader || (pObj->m_ObjFlags & FOB_FOGPASS)))
    return;

  // Transform, depth range and scissor.
  const Matrix44 World = GetWorldMatrix(pObj);
  const bool bNearest = (pObj->m_ObjFlags & FOB_NEAREST) != 0;
  const Matrix44 WorldViewProj = World * m_CameraView * (bNearest ? m_NearestProj : m_ProjMatrix);
  m_fDepthMin = 0;
  m_fDepthMax = bNearest ? 0.1f : 1.0f;
  m_bItemScissor = pObj->m_nScissorX2 > pObj->m_nScissorX1 && pObj->m_nScissorY2 > pObj->m_nScissorY1;
  m_ItemScissor = { pObj->m_nScissorX1, pObj->m_nScissorY1, pObj->m_nScissorX2, pObj->m_nScissorY2 };

  SPipelineKey Key = {};
  Key.nState = GS_DEPTHWRITE;
  Key.nProgram = eD3D12P_General;
  Key.nVertexFormat = (uint8)Geom->nVertexFormat;
  Key.nCull = CullOf(GetCullMode(pSh, pRes), Det3(World) < 0);
  // The water reflection sees the land from below the water: both sides, so that where the waves move
  // the reflection over a shore, it shows the land rather than the sky behind it.
  if (m_bReflectionPass)
    Key.nCull = R_CULL_NONE;
  Key.bWireframe = m_nPolygonMode == R_WIREFRAME_MODE ? 1 : 0;
  Key.bDepthBias = (it.nSort & ~EFSLIST_MASK) == eS_Decal ? 1 : 0;

  SDrawConstants dc = DefaultDrawConstants();
  memcpy(dc.mWorld, World.data, sizeof(dc.mWorld));
  memcpy(dc.mWorldViewProj, WorldViewProj.data, sizeof(dc.mWorldViewProj));

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
  Set4(dc.vAmbient, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z, 1);
  std::optional<D3D12_VERTEX_BUFFER_VIEW> TexCoord1;

  auto Draw = [&]()
  {
    if (m_pFrameDump)
      DumpDraw(dc, Key, pTex);
    if (Geom->IsIndexed())
      DrawGeometry(Key, dc, Geom->Topology, Geom->VB, &Geom->IB, Geom->nNumIndices, Geom->nFirstIndex, Geom->nBaseVertex, TexCoord1 ? &*TexCoord1 : nullptr);
    else
      DrawGeometry(Key, dc, Geom->Topology, Geom->VB, nullptr, Geom->nNumVertices, Geom->nFirstVertex, 0, TexCoord1 ? &*TexCoord1 : nullptr);
  };
  // A shadow map: the depth of the casters.
  if (m_bShadowMapPass)
  {
    if (SetupShadowCasterDraw(Key, dc, it))
      Draw();
    return;
  }

  //! Sun and ambient, or the lightmap; vertex colours when there are no normals.
  auto Light = [&]()
  {
    if (pSh->UsesLightmap() && pObj->m_nLMId > 0 && pObj->m_pLMTCBufferO && m_pTextures && m_pTextures->GetByID(pObj->m_nLMId))
      TexCoord1 = Geometry::GetTexCoordStream(pObj->m_pLMTCBufferO);
    if (TexCoord1)
    {
      dc.nFlags |= DF_TEX1_LIGHTMAP;
      dc.nTex1 = m_pTextures->GetSRV(pObj->m_nLMId);
      dc.nSamplers |= XR_SAMPLER_LINEAR_CLAMP << 4;
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
    // The sun hidden by the shadow maps of the object's casters.
    if (dc.nFlags & (DF_LIGHTING | DF_TEX1_LIGHTMAP))
      SetupShadowReceiver(dc, pObj);
  };

  switch (eDC)
  {
  case eDC_Opaque:
  case eDC_AlphaTest:
    Light();
    Set4(dc.vColor, MatColor.r * pObj->m_Color.r, MatColor.g * pObj->m_Color.g, MatColor.b * pObj->m_Color.b, 1);
    if (eDC == eDC_AlphaTest || GetAlphaRef(pSh, pRes) > 0)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = GetAlphaRef(pSh, pRes) > 0 ? GetAlphaRef(pSh, pRes) : 0.5f;
    }
    break;
  case eDC_AlphaBlend:
    Light();
    Set4(dc.vColor, MatColor.r * pObj->m_Color.r, MatColor.g * pObj->m_Color.g, MatColor.b * pObj->m_Color.b, fOpacity * pObj->m_Color.a);
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    if (GetAlphaRef(pSh, pRes) > 0)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = GetAlphaRef(pSh, pRes);
    }
    break;
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
    const float s = GetColorScale(pSh, pObj);
    Set4(dc.vColor, s, s, s, 1);
    Key.nState = GS_BLSRC_ONE | GS_BLDST_ONE;
    break;
  }
  case eDC_Terrain:
  {
    // The sector texture over the sector: u = (y - y0) / 64, v = (x - x0) / 64; the low detail
    // terrain gets its scale from its custom data (1 / terrain size).
    float fScale = 1.0f / TERRAIN_SECTOR_SIZE, x0 = 0, y0 = 0;
    if (nFormat == VERTEX_FORMAT_P3F && pLeaf->m_CustomData && pLeaf->m_nCountCustomData > 0)
      fScale = ((const float *)pLeaf->m_CustomData)[2];
    else
      GetSectorCorner(pLB, x0, y0);
    dc.nFlags |= DF_TEXGEN_WORLD;
    dc.nFlags &= ~DF_TEXMATRIX;
    Set4(dc.vTexGenU, 0, fScale, 0, -y0 * fScale);
    Set4(dc.vTexGenV, fScale, 0, 0, -x0 * fScale);
    Set4(dc.vColor, m_WorldColor[0], m_WorldColor[1], m_WorldColor[2], 1);
    if (pTex)
      dc.nSamplers = (dc.nSamplers == XR_SAMPLER_POINT_WRAP || dc.nSamplers == XR_SAMPLER_POINT_CLAMP) ? XR_SAMPLER_POINT_CLAMP : XR_SAMPLER_LINEAR_CLAMP;
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
    // u = y / 4, v = x / 4 without it), the weight faded out between r_DetailDistance and twice
    // that, multiplied in twice (DESTCOLOR, SRCCOLOR); no depth writes. A layer with a material of
    // its own (the template "terrainlayer": the cliffs, projected on a vertical plane, one element
    // for each plane) is the material's diffuse texture, bump mapped with its normal map
    // (EFTT_NORMALMAP, else EFTT_BUMP): the sun's light on the bumps relative to its light on the
    // terrain's own normal, which the terrain's texture already has; its weight fades out linearly
    // to the distance in float 3 of its custom data (156 m).
    if (!pTex || !CV_r_DetailTextures)
      return;
    CTexture *pBump = nullptr;
    if (it.pRes && m_pShaders)
    {
      pBump = m_pShaders->LoadResourceTexture(it.pRes, EFTT_NORMALMAP);
      if (!pBump)
        pBump = m_pShaders->LoadResourceTexture(it.pRes, EFTT_BUMP);
    }
    float fStart = std::max(CV_r_DetailDistance, 0.01f), fEnd = 2 * fStart;
    if (it.pRes && pLeaf->m_CustomData && ((const float *)pLeaf->m_CustomData)[3] > 0)
    {
      fStart = 0;
      fEnd = ((const float *)pLeaf->m_CustomData)[3];
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
    if (pBump && !pBump->IsCube())
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
    // The shadow of static objects on a terrain sector ("EntityShadowOnTerrain": triangles of the
    // sector under the casters, over the sector's vertices; the object's m_pShadowCasters lists
    // the casters): the sector's texture (the one the buffer was given) at the sector's texture
    // coordinates (u = y * float 2 + float 0, v = x * float 2 + float 1 of the custom data) times
    // twice the object's colour (the terrain in the shadow), blended over the terrain where the
    // casters' maps hide the sun; no depth writes, before the detail layers.
    const float *pCustom = (const float *)pLeaf->m_CustomData;
    if (!pTex || !pCustom || !SetupShadowReceiver(dc, pObj))
      return;
    dc.nFlags = (dc.nFlags & ~DF_TEXMATRIX) | DF_TEXGEN_WORLD | DF_SHADOW_PASS | DF_ALPHATEST;
    dc.fAlphaRef = 1.0f / 255.0f;
    Set4(dc.vTexGenU, 0, pCustom[2], 0, pCustom[0]);
    Set4(dc.vTexGenV, pCustom[2], 0, 0, pCustom[1]);
    Set4(dc.vColor, 2 * pObj->m_Color.r, 2 * pObj->m_Color.g, 2 * pObj->m_Color.b, 1);
    dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    if (dc.nFlags & DF_SHADOW_DEBUG)
      dc.nFlags &= ~DF_ALPHATEST;
    break;
  }
  case eDC_Water:
    // Not in its own reflection.
    if (m_bReflectionPass || !SetupWaterDraw(Key, dc, it))
      return;
    pTex = nullptr;
    break;
  case eDC_WaterBeach:
    if (m_bReflectionPass || !SetupBeachDraw(Key, dc, 0))
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
  default:
    return;
  }
  if (m_bFog)
    dc.nFlags |= DF_FOG;
  // The picture of a far vegetation sprite: where the object is, whatever the texture's alpha.
  if (m_bSpritePass)
    dc.nFlags |= DF_ALPHA_ONE;
  Draw();
  // The beach: the other waves.
  for (int nWave = 1; eDC == eDC_WaterBeach && nWave < BEACH_WAVES && SetupBeachDraw(Key, dc, nWave); nWave++)
  {
    if (m_bFog)
      dc.nFlags |= DF_FOG;
    Draw();
  }
}

void CD3D12Renderer::DumpItem(int nIndex, const SRenderItem &it, int eList)
{
  static const char *const s_DrawClasses[] = { "NoDraw", "Opaque", "AlphaTest", "AlphaTestVColor", "VColor", "AlphaBlend", "Additive", "Terrain", "Sky", "Engine", "Water", "WaterBottom", "WaterBeach", "TerrainLayer", "Decal",
                                                "TerrainShadow", "Caustics" };
  const CCObject *pObj = it.pObj;
  const char *szLB = "";
  if (it.pRE->m_Type == eDATA_OcLeaf && static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer)
    szLB = static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer->m_sSource;
  const CTexture *pTex = GetDiffuseTexture(it.pRes);
  fprintf(m_pFrameDump.get(), "[%d] list %d sort 0x%x %s RE %d shader '%s' tex '%s' lb '%s' pos (%.2f %.2f %.2f) flags 0x%x LM %d (dir %d occl %d HDR %d) LMTC %d mask 0x%x amb (%.2f %.2f %.2f) col (%.2f %.2f %.2f %.2f) res aref %.3f opacity %.2f flags 0x%x casters %d\n",
          nIndex, eList, it.nSort, (unsigned)it.eDrawClass < std::size(s_DrawClasses) ? s_DrawClasses[it.eDrawClass] : "?", it.pRE->m_Type,
          it.pShader ? it.pShader->GetName() : "", pTex ? pTex->m_Name.c_str() : "", szLB ? szLB : "",
          pObj->m_Matrix.data[12], pObj->m_Matrix.data[13], pObj->m_Matrix.data[14], pObj->m_ObjFlags, pObj->m_nLMId,
          pObj->m_nLMDirId, pObj->m_nOcclId, pObj->m_nHDRLMId, pObj->m_pLMTCBufferO ? 1 : 0, pObj->m_DynLMMask, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z,
          pObj->m_Color.r, pObj->m_Color.g, pObj->m_Color.b, pObj->m_Color.a, it.pRes ? it.pRes->m_AlphaRef : 0.0f,
          it.pRes ? it.pRes->m_Opacity : 1.0f, it.pRes ? it.pRes->m_ResFlags : 0,
          pObj->m_pShadowCasters ? reinterpret_cast<const int *>(pObj->m_pShadowCasters)[2] : -1);

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
                                         "SHADOW_PASS" };
  std::string Flags;
  for (size_t i = 0; i < std::size(s_Flags); i++)
    if (dc.nFlags & (1u << i))
      Flags += (Flags.empty() ? "" : "|") + std::string(s_Flags[i]);
  FILE *f = m_pFrameDump.get();
  fprintf(f, "    draw: fmt %d state 0x%x cull %d bias %d flags %s tex0 '%s' alpharef %.3f col (%.2f %.2f %.2f %.2f) amb (%.2f %.2f %.2f) lights %u",
          Key.nVertexFormat, Key.nState, Key.nCull, Key.bDepthBias, Flags.c_str(), (dc.nFlags & DF_TEX0) && pTex ? pTex->m_Name.c_str() : "",
          dc.fAlphaRef, dc.vColor[0], dc.vColor[1], dc.vColor[2], dc.vColor[3], dc.vAmbient[0], dc.vAmbient[1], dc.vAmbient[2], dc.nLights);
  if (dc.nFlags & DF_SHADOWS)
    fprintf(f, " shadow maps %d", (int)dc.vShadowParams[0]);
  for (uint32 i = 0; i < dc.nLights; i++)
    fprintf(f, " {(%.1f %.1f %.1f %.0f) col (%.2f %.2f %.2f) att (%.3f %.3f %.1f)}", dc.vLightPos[i][0], dc.vLightPos[i][1], dc.vLightPos[i][2],
            dc.vLightPos[i][3], dc.vLightColor[i][0], dc.vLightColor[i][1], dc.vLightColor[i][2], dc.vLightAtten[i][0], dc.vLightAtten[i][1], dc.vLightAtten[i][2]);
  fprintf(f, "\n");
}

void CD3D12Renderer::SetObjectLights(SDrawConstants &dc, const CCObject *pObj, const Matrix44 &World, CLeafBuffer *pLB)
{
  // The lights of the object's m_DynLMMask besides the sun (an empty mask: none), at most
  // XR_MAX_DRAW_LIGHTS; projected lights are not among them. Point lights are lit per vertex
  // with the attenuation 1 / (a0 + a1 d) up to their range. Characters get (0.01, 5.07756 / R)
  // up to 50.416 R. Static objects get a line through the falloff
  // f(x) = 2 (1 - 3 v^2 + 2 v^3), v = min(x / (0.9 R), 1), at the nearest and the farthest point
  // of the object's sphere (the centre of the leaf buffer's box plus the object's position, half
  // the box diagonal), up to where it reaches 1/256. Directional lights come from their origin.
  Vec3 vCenter(World.data[12], World.data[13], World.data[14]);
  float r = 0;
  const CLeafBuffer *pVC = pLB->GetVertexContainer();
  if (pVC->m_vBoxMin.x <= pVC->m_vBoxMax.x)
  {
    vCenter += (pVC->m_vBoxMin + pVC->m_vBoxMax) * 0.5f;
    r = (pVC->m_vBoxMax - pVC->m_vBoxMin).GetLength() * 0.5f;
  }
  const std::span<CDLight *const> Lights = m_pScene->GetLights();
  uint32 n = 0;
  for (size_t i = 0; i < Lights.size() && i < 32 && n < XR_MAX_DRAW_LIGHTS; i++)
  {
    const CDLight *pLight = Lights[i];
    if (!pLight || (pLight->m_Flags & (DLF_SUN | DLF_PROJECT)))
      continue;
    // A light baked into light maps (DLF_LM) lights an object with a light map through it only.
    if ((pLight->m_Flags & DLF_LM) && pObj->m_nLMId > 0)
      continue;
    if ((pObj->m_DynLMMask & (1u << i)) == 0)
      continue;
    Set4(dc.vLightColor[n], pLight->m_Color.r, pLight->m_Color.g, pLight->m_Color.b, 1);
    if (pLight->m_Flags & DLF_DIRECTIONAL)
    {
      Vec3 vDir = pLight->m_Origin - vCenter;
      const float fLen = vDir.GetLength();
      vDir = fLen > 0 ? vDir / fLen : Vec3(0, 0, 1);
      Set4(dc.vLightPos[n], vDir.x, vDir.y, vDir.z, 2);
      Set4(dc.vLightAtten[n], 1, 0, 0, 0);
      n++;
      continue;
    }
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
    Set4(dc.vLightPos[n], pLight->m_Origin.x, pLight->m_Origin.y, pLight->m_Origin.z, 1);
    Set4(dc.vLightAtten[n], a0, a1, fRange, 0);
    n++;
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

void CD3D12Renderer::DrawSkyItem(const SRenderItem &it)
{
  if (it.eDrawClass != eDC_Sky || !m_pShaders || !m_pTextures)
    return;
  const std::string *pName = m_pShaders->FindSkyBox(it.pShader->GetName());
  if (!pName)
    return;
  auto Box = std::find_if(m_SkyBoxes.begin(), m_SkyBoxes.end(), [&](const SSkyBoxTextures &b) { return b.Name == *pName; });
  if (Box == m_SkyBoxes.end())
  {
    SSkyBoxTextures New;
    New.Name = *pName;
    static const char *const s_Suffixes[3] = { "_5", "_12", "_34" };
    for (int i = 0; i < 3; i++)
      New.pTex[i] = m_pTextures->LoadTexture(("textures/" + *pName + s_Suffixes[i]).c_str(), FT_CLAMP | FT_NOMIPS, 0, eTT_Base, -1, -1, -1, 0);
    m_SkyBoxes.push_back(New);
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

  auto Draw = [&](int nTex, const struct_VERTEX_FORMAT_P3F_TEX2F *pVerts, int nVerts)
  {
    CTexture *pTex = Box->pTex[nTex];
    if (!pTex || !pTex->IsTextureLoaded() || pTex->IsCube())
      return;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    dc.nFlags = DF_TEX0;
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
    dc.nFlags = DF_VCOLOR;
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
