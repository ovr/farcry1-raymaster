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
//	position; the water by Water.cpp. FOB_NEAREST objects (the first
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
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "list %d: %d items, recursion %d, scene flags 0x%x\n", eList, nItems, m_nRecurseLevel, m_nSceneFlags);
  for (int i = 0; i < nItems; i++)
  {
    const SRenderItem &it = pItems[i];
    if (!it.pRE)
      continue;
    if (!bSprites && it.eDrawClass != eDC_Water && it.eDrawClass != eDC_WaterBeach)
    {
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
    DrawSprites();
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
  // it only with r_VolumetricFog; not drawn yet.
  if ((it.nSort & ~EFSLIST_MASK) == eS_FogShader || (pObj->m_ObjFlags & FOB_FOGPASS))
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
    }
    else if (HasNormal(nFormat))
    {
      dc.nFlags |= DF_LIGHTING;
      SetObjectLights(dc, pObj, World, pLB);
    }
    else if (HasColor(nFormat))
      dc.nFlags |= DF_VCOLOR;
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
    // texture * vertex colour * 2 * the object's colour * the world colour.
    dc.nFlags |= DF_VCOLOR | DF_VCOLOR_X2;
    const float a = pObj->m_Color.a;
    Set4(dc.vColor, pObj->m_Color.r * m_WorldColor[0], pObj->m_Color.g * m_WorldColor[1], pObj->m_Color.b * m_WorldColor[2], a * a);
    if (eDC == eDC_AlphaTestVColor)
    {
      dc.nFlags |= DF_ALPHATEST;
      dc.fAlphaRef = GetAlphaRef(pSh, pRes) > 0 ? GetAlphaRef(pSh, pRes) : 128.0f / 255.0f;
      if (a < 1)
      {
        // Grass fading out: blended, no depth writes.
        Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
        dc.fAlphaRef = 1.0f / 255.0f;
      }
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
  case eDC_Water:
    if (!SetupWaterDraw(Key, dc, it))
      return;
    pTex = nullptr;
    break;
  case eDC_WaterBeach:
    if (!SetupBeachDraw(Key, dc, 0))
      return;
    pTex = m_pBeachTexture;
    break;
  case eDC_WaterBottom:
    SetupWaterBottomDraw(Key, dc, (dc.nFlags & DF_TEX0) ? pTex : nullptr);
    break;
  default:
    return;
  }
  if (m_bFog)
    dc.nFlags |= DF_FOG;

  auto Draw = [&]()
  {
    if (m_pFrameDump)
      DumpDraw(dc, Key, pTex);
    if (Geom->IsIndexed())
      DrawGeometry(Key, dc, Geom->Topology, Geom->VB, &Geom->IB, Geom->nNumIndices, Geom->nFirstIndex, Geom->nBaseVertex, TexCoord1 ? &*TexCoord1 : nullptr);
    else
      DrawGeometry(Key, dc, Geom->Topology, Geom->VB, nullptr, Geom->nNumVertices, Geom->nFirstVertex, 0, TexCoord1 ? &*TexCoord1 : nullptr);
  };
  Draw();
  // The beach: the second wave.
  if (eDC == eDC_WaterBeach && SetupBeachDraw(Key, dc, 1))
  {
    if (m_bFog)
      dc.nFlags |= DF_FOG;
    Draw();
  }
}

void CD3D12Renderer::DumpItem(int nIndex, const SRenderItem &it, int eList)
{
  static const char *const s_DrawClasses[] = { "NoDraw", "Opaque", "AlphaTest", "AlphaTestVColor", "VColor", "AlphaBlend", "Additive", "Terrain", "Sky", "Engine", "Water", "WaterBottom", "WaterBeach" };
  const CCObject *pObj = it.pObj;
  const char *szLB = "";
  if (it.pRE->m_Type == eDATA_OcLeaf && static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer)
    szLB = static_cast<CREOcLeaf *>(it.pRE)->m_pBuffer->m_sSource;
  const CTexture *pTex = GetDiffuseTexture(it.pRes);
  fprintf(m_pFrameDump.get(), "[%d] list %d sort 0x%x %s RE %d shader '%s' tex '%s' lb '%s' pos (%.2f %.2f %.2f) flags 0x%x LM %d LMTC %d mask 0x%x amb (%.2f %.2f %.2f) col (%.2f %.2f %.2f %.2f) res aref %.3f opacity %.2f flags 0x%x\n",
          nIndex, eList, it.nSort, (unsigned)it.eDrawClass < std::size(s_DrawClasses) ? s_DrawClasses[it.eDrawClass] : "?", it.pRE->m_Type,
          it.pShader ? it.pShader->GetName() : "", pTex ? pTex->m_Name.c_str() : "", szLB ? szLB : "",
          pObj->m_Matrix.data[12], pObj->m_Matrix.data[13], pObj->m_Matrix.data[14], pObj->m_ObjFlags, pObj->m_nLMId,
          pObj->m_pLMTCBufferO ? 1 : 0, pObj->m_DynLMMask, pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z,
          pObj->m_Color.r, pObj->m_Color.g, pObj->m_Color.b, pObj->m_Color.a, it.pRes ? it.pRes->m_AlphaRef : 0.0f,
          it.pRes ? it.pRes->m_Opacity : 1.0f, it.pRes ? it.pRes->m_ResFlags : 0);

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
                                         "TEX1_DETAIL", "TEX1_LIGHTMAP", "ALPHA_FROM_TEX", "CUBE0", "TEX0_ALPHA_ONLY", "SCREEN_POS", "ALPHATEST_LESS", "TEXGEN1_WORLD" };
  std::string Flags;
  for (size_t i = 0; i < std::size(s_Flags); i++)
    if (dc.nFlags & (1u << i))
      Flags += (Flags.empty() ? "" : "|") + std::string(s_Flags[i]);
  FILE *f = m_pFrameDump.get();
  fprintf(f, "    draw: fmt %d state 0x%x cull %d bias %d flags %s tex0 '%s' alpharef %.3f col (%.2f %.2f %.2f %.2f) amb (%.2f %.2f %.2f) lights %u",
          Key.nVertexFormat, Key.nState, Key.nCull, Key.bDepthBias, Flags.c_str(), (dc.nFlags & DF_TEX0) && pTex ? pTex->m_Name.c_str() : "",
          dc.fAlphaRef, dc.vColor[0], dc.vColor[1], dc.vColor[2], dc.vColor[3], dc.vAmbient[0], dc.vAmbient[1], dc.vAmbient[2], dc.nLights);
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
  const Matrix44 ViewProj = View * m_ProjMatrix;
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
  // (m_RenderState), depth tested without depth writes.
  const std::span<const SSceneSprite> Sprites = m_pScene->GetSprites();
  if (Sprites.empty())
    return;
  const std::span<const SColorVert> Verts = m_pScene->GetSpriteVerts();
  const std::span<const ushort> Inds = m_pScene->GetSpriteIndices();
  std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> Converted;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
  m_bItemScissor = false;
  for (const SSceneSprite &Sprite : Sprites)
  {
    if (!Sprite.pObj || Sprite.nVerts <= 0 || Sprite.nIndices <= 0 ||
        Sprite.nFirstVert + Sprite.nVerts > (int)Verts.size() || Sprite.nFirstIndex + Sprite.nIndices > (int)Inds.size())
      continue;
    Converted.resize(Sprite.nVerts);
    for (int i = 0; i < Sprite.nVerts; i++)
    {
      const SColorVert &v = Verts[Sprite.nFirstVert + i];
      Converted[i].xyz = v.vert;
      Converted[i].color = v.color;
      Converted[i].st[0] = v.dTC[0];
      Converted[i].st[1] = v.dTC[1];
    }
    SPipelineKey Key = {};
    const uint32 nBlend = Sprite.pObj->m_RenderState & GS_BLEND_MASK;
    Key.nState = nBlend ? nBlend : (GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
    Key.nProgram = eD3D12P_General;
    Key.nCull = R_CULL_NONE;
    SDrawConstants dc = DefaultDrawConstants();
    const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    dc.nFlags = DF_VCOLOR;
    if (CTexture *pTex = m_pTextures ? m_pTextures->GetByID(Sprite.pObj->m_NumCM) : nullptr; pTex && !pTex->IsCube())
    {
      dc.nFlags |= DF_TEX0;
      dc.nTex0 = pTex->GetSRV();
      dc.nSamplers = pTex->GetSampler();
    }
    if (m_bFog)
      dc.nFlags |= DF_FOG;
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Converted.data(), Sprite.nVerts,
      Inds.data() + Sprite.nFirstIndex, Sprite.nIndices);
  }
}
