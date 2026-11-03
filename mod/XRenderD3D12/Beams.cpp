// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Beams.cpp
//	Description: The beams of the lights: the cone of light of a light
//	whose shader's script has a "ClientEffect 'Beam'" block (SBeam; the
//	LightBeam* shaders of Lights.csl: the hanging lamps of the indoor
//	levels, the yellow cones under the lamps of Treehouse's corridors).
//
//	As the original draws them: in the main view's blended list, after
//	the rest of the 3D scene and before the flares (DrawFlares), whatever
//	r_Coronas and r_Beams; every light of the scene (and every DLF_FAKE
//	light) with such a shader gets its beam, its shader's cone model
//	(objects/indoor/lights/beam.cgf, loaded on first use, its vertices
//	kept, the object released) put at the light along its axes
//	(CScene::GetLightAxes, the projector's), every vertex made as the
//	original's vertex shader makes it (LightBeam.h): the cone from
//	StartRadius to EndRadius over Length, the colour from StartColor to
//	EndColor, its alpha fading where the camera sees the cone's sides edge
//	on and at the tip. Blended by that alpha, depth tested without depth
//	writes, both sides, fogged.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Pipeline.h"
#include "Geometry.h"
#include "LightBeam.h"
#include "Scene.h"
#include "Shader.h"

#include <algorithm>

namespace
{

//! The model's vertices and triangles (all of its chunks) into Model; its size from the object's
//! box: the length its largest x, the radius its largest y.
void ReadBeamModel(IStatObj *pObj, CD3D12Renderer::SBeamModel &Model)
{
  CLeafBuffer *pLB = pObj->GetLeafBuffer();
  CLeafBuffer *pVC = pLB ? pLB->GetVertexContainer() : nullptr;
  if (!pVC || !pLB->m_pMats)
    return;
  const CVertexBuffer *pVB = pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData ? pVC->m_pSecVertBuffer : pVC->m_pVertexBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
    return;
  const int nStride = CD3D12Pipeline::GetVertexSize(pVB->m_vertexformat);
  int nNormal, nColor0, nTex0, nTex1;
  if (nStride < (int)sizeof(Vec3) || !CD3D12Pipeline::GetVertexAttributes(pVB->m_vertexformat, nNormal, nColor0, nTex0, nTex1))
    return;
  if (nNormal < 0 && !pVC->m_TempNormals)
    return;
  const byte *pData = (const byte *)pVB->m_VS[VSF_GENERAL].m_VData;
  const int nVerts = pVB->m_NumVerts;
  Model.Pos.resize(nVerts);
  Model.Normals.resize(nVerts);
  for (int i = 0; i < nVerts; i++)
  {
    const byte *pVert = pData + (size_t)i * nStride;
    Model.Pos[i] = *(const Vec3 *)pVert;
    Model.Normals[i] = nNormal >= 0 ? *(const Vec3 *)(pVert + nNormal) : pVC->m_TempNormals[i];
  }
  for (int m = 0; m < pLB->m_pMats->Count(); m++)
  {
    for (uint16 i : Geometry::GetTriangles(pLB, &(*pLB->m_pMats)[m]))
    {
      if (i >= nVerts)
      {
        Model.Inds.clear();
        return;
      }
      Model.Inds.push_back(i);
    }
  }
  Model.vBoxMin = pObj->GetBoxMin();
  Model.vBoxMax = pObj->GetBoxMax();
  Model.fLength = Model.vBoxMax.x;
  Model.fRadius = Model.vBoxMax.y;
  if (Model.fLength <= 0 || Model.fRadius <= 0)
    Model.Inds.clear();
}

} // namespace

const CD3D12Renderer::SBeamModel *CD3D12Renderer::GetBeamModel(const std::string &Name)
{
  auto it = std::find_if(m_BeamModels.begin(), m_BeamModels.end(), [&](const auto &e) { return e.first == Name; });
  if (it == m_BeamModels.end())
  {
    SBeamModel Model;
    I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
    // Without shared vertices, as the original draws it (369 vertices: each face its own normals;
    // shared ones, 277, round the cone's sides off and its sides fade less).
    if (IStatObj *pObj = p3D && !Name.empty() ? p3D->MakeObject(Name.c_str(), nullptr, evs_NoSharing) : nullptr)
    {
      if (!pObj->IsDefaultObject())
        ReadBeamModel(pObj, Model);
      p3D->ReleaseObject(pObj);
    }
    if (Model.Inds.empty() && iLog)
      iLog->Log("XRenderD3D12: no beam model '%s'", Name.c_str());
    m_BeamModels.emplace_back(Name, std::move(Model));
    it = m_BeamModels.end() - 1;
  }
  return it->second.Inds.empty() ? nullptr : &it->second;
}

void CD3D12Renderer::DrawBeams()
{
  // The lights with a beam, each once.
  std::vector<std::pair<const CDLight *, const SBeam *>> Beams;
  auto Add = [&](std::span<CDLight *const> Lights)
  {
    for (const CDLight *pLight : Lights)
    {
      const CShader *pSh = pLight ? static_cast<const CShader *>(pLight->m_pShader) : nullptr;
      if (!pSh || !pSh->GetBeam())
        continue;
      if (std::none_of(Beams.begin(), Beams.end(), [pLight](const auto &b) { return b.first == pLight; }))
        Beams.emplace_back(pLight, pSh->GetBeam());
    }
  };
  Add(m_pScene->GetLights());
  Add(m_pScene->GetFakeLights());
  if (Beams.empty())
    return;

  GpuSection("beams");
  m_fDepthMin = 0;
  m_fDepthMax = 1;
  m_bItemScissor = false;
  m_fDrawHeat = 0;
  const Vec3 vCamPos = m_Camera.GetPos();
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> Verts;
  for (const auto &[pLight, pBeam] : Beams)
  {
    const SBeamModel *pModel = GetBeamModel(pBeam->Model);
    if (!pModel)
      continue;
    LightBeam::SParams b;
    b.fStartRadius = pBeam->fStartRadius;
    b.fEndRadius = pBeam->fEndRadius;
    b.fLength = pBeam->fLength;
    std::copy(pBeam->StartColor, pBeam->StartColor + 4, b.StartColor);
    std::copy(pBeam->EndColor, pBeam->EndColor + 4, b.EndColor);
    b.fModelLength = pModel->fLength;
    b.fModelRadius = pModel->fRadius;
    Vec3 F, U, V;
    CScene::GetLightAxes(pLight, F, U, V);
    const Vec3 &O = pLight->m_Origin;
    const Vec3 vToCam = vCamPos - O;
    const float vCamera[3] = { vToCam.Dot(F), vToCam.Dot(U), vToCam.Dot(V) };

    Verts.resize(pModel->Pos.size());
    auto Byte = [](float x) { return (uint8)std::clamp(x * 255.0f + 0.5f, 0.0f, 255.0f); };
    float fMaxAlpha = 0;
    for (size_t i = 0; i < Verts.size(); i++)
    {
      const LightBeam::SVertex v = LightBeam::Transform(b, &pModel->Pos[i].x, &pModel->Normals[i].x, vCamera);
      struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F &Out = Verts[i];
      Out.xyz = O + F * v.p[0] + U * v.p[1] + V * v.p[2];
      Out.color.bcolor[0] = Byte(v.Color[2]);
      Out.color.bcolor[1] = Byte(v.Color[1]);
      Out.color.bcolor[2] = Byte(v.Color[0]);
      Out.color.bcolor[3] = Byte(v.Color[3]);
      Out.st[0] = Out.st[1] = 0;
      fMaxAlpha = std::max(fMaxAlpha, v.Color[3]);
    }

    SPipelineKey Key = {};
    Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
    Key.nProgram = eD3D12P_General;
    Key.nCull = R_CULL_NONE;
    Key.nVertexFormat = VERTEX_FORMAT_P3F_COL4UB_TEX2F;
    SDrawConstants dc = DefaultDrawConstants();
    memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
    dc.nFlags = DF_VCOLOR | (m_bFog ? DF_FOG : 0);
    if (m_pFrameDump)
    {
      fprintf(m_pFrameDump.get(),
              "  beam of light %d '%s' at (%.2f %.2f %.2f) forward (%.3f %.3f %.3f) up (%.3f %.3f %.3f): model '%s' %d vertices %d triangles, "
              "box (%.3f %.3f %.3f)-(%.3f %.3f %.3f); radius %g to %g over %g, colour (%g %g %g %g) to (%g %g %g %g); camera in the light's axes "
              "(%.2f %.2f %.2f); largest alpha %.3f\n",
              pLight->m_Id, pLight->m_pShader ? pLight->m_pShader->GetName() : "", O.x, O.y, O.z, F.x, F.y, F.z, U.x, U.y, U.z, pBeam->Model.c_str(),
              (int)pModel->Pos.size(), (int)pModel->Inds.size() / 3, pModel->vBoxMin.x, pModel->vBoxMin.y, pModel->vBoxMin.z, pModel->vBoxMax.x,
              pModel->vBoxMax.y, pModel->vBoxMax.z, b.fStartRadius, b.fEndRadius, b.fLength, b.StartColor[0], b.StartColor[1], b.StartColor[2],
              b.StartColor[3], b.EndColor[0], b.EndColor[1], b.EndColor[2], b.EndColor[3], vCamera[0], vCamera[1], vCamera[2], fMaxAlpha);
      DumpDraw(dc, Key, nullptr);
    }
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Verts.data(), (int)Verts.size(), pModel->Inds.data(),
                  (int)pModel->Inds.size());
  }
}
