// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RendererBuffers.cpp
//	Description: Vertex, index and leaf buffers (IRenderer methods).
//	How the data is kept: Geometry.h.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Geometry.h"

void CD3D12Renderer::SetFenceCompleted(CVertexBuffer *buffer)
{
  // The GPU copies are versioned per frame: nothing to wait for.
}

CVertexBuffer *CD3D12Renderer::CreateBuffer(int vertexcount, int vertexformat, const char *szSource, bool bDynamic)
{
  if (GeometryImpl::VertexSize(vertexformat) <= 0 || vertexcount < 0)
    return nullptr;
  return GeometryImpl::NewVertexBuffer(vertexformat, vertexcount, bDynamic);
}

void CD3D12Renderer::ReleaseBuffer(CVertexBuffer *bufptr)
{
  GeometryImpl::DeleteVertexBuffer(bufptr);
}

void CD3D12Renderer::DrawBuffer(CVertexBuffer *src, SVertexStream *indicies, int numindices, int offsindex, int prmode, int vert_start, int vert_stop, CMatInfo *mi)
{
  // numindices indices from offsindex, of the primitive type prmode, with the current render
  // state and the immediate transform (as DrawTriStrip).
  const std::optional<SGeomDraw> sd = Geometry::GetDraw(src, indicies, prmode, offsindex, numindices, vert_start, vert_stop);
  if (!sd)
    return;
  SPipelineKey Key;
  SDrawConstants dc;
  ApplyRenderState(Key, dc, sd->nVertexFormat, true);
  memcpy(dc.mWorldViewProj, GetImmediateViewProj().data, 16 * sizeof(float));
  if (sd->IsIndexed())
    DrawGeometry(Key, dc, sd->Topology, sd->VB, &sd->IB, sd->nNumIndices, sd->nFirstIndex, sd->nBaseVertex);
  else
    DrawGeometry(Key, dc, sd->Topology, sd->VB, nullptr, sd->nNumVertices, sd->nFirstVertex);
}

void CD3D12Renderer::UpdateBuffer(CVertexBuffer *dest, const void *src, int vertexcount, bool bUnLock, int nOffs, int Type)
{
  if (!dest)
    return;
  const int nStream = Type == VSF_TANGENTS ? VSF_TANGENTS : VSF_GENERAL;
  SVertexStream &s = dest->m_VS[nStream];
  if (src)
  {
    // vertexcount vertices of the stream's layout from vertex nOffs on.
    if (vertexcount > 0 && nOffs >= 0 && GeometryImpl::ReserveVertices(dest, nStream, nOffs + vertexcount) && s.m_VData)
    {
      const int nSize = nStream == VSF_TANGENTS ? (int)sizeof(SPipTangents) : GeometryImpl::VertexSize(dest->m_vertexformat);
      memcpy((uint8 *)s.m_VData + (size_t)nOffs * nSize, src, (size_t)vertexcount * nSize);
      GeometryImpl::Invalidate(s);
    }
  }
  else if (!bUnLock)
  {
    // Lock: m_VData is writable memory for the whole buffer until the unlock.
    GeometryImpl::ReserveVertices(dest, nStream, std::max(dest->m_NumVerts, std::max(nOffs, 0) + std::max(vertexcount, 0)));
    s.m_bLocked = true;
  }
  if (bUnLock)
  {
    // The unlock may name either stream: both are taken as written.
    for (auto &vs : dest->m_VS)
    {
      vs.m_bLocked = false;
      GeometryImpl::Invalidate(vs);
    }
  }
}

void CD3D12Renderer::CreateIndexBuffer(SVertexStream *dest, const void *src, int indexcount)
{
  if (dest)
    GeometryImpl::SetIndices(*dest, (const ushort *)src, indexcount);
}

void CD3D12Renderer::UpdateIndexBuffer(SVertexStream *dest, const void *src, int indexcount, bool bUnLock)
{
  if (!dest)
    return;
  if (src)
    GeometryImpl::SetIndices(*dest, (const ushort *)src, indexcount);
  else if (!bUnLock)
  {
    // Lock: room for indexcount indices (the current ones kept) at m_VData.
    if (indexcount > 0 && (indexcount > dest->m_nItems || !dest->m_VData))
      GeometryImpl::SetIndices(*dest, nullptr, indexcount);
    dest->m_bLocked = true;
  }
  if (bUnLock)
  {
    dest->m_bLocked = false;
    GeometryImpl::Invalidate(*dest);
  }
}

void CD3D12Renderer::ReleaseIndexBuffer(SVertexStream *dest)
{
  if (dest)
    GeometryImpl::ReleaseIndices(*dest);
}

bool CD3D12Renderer::EF_PrecacheResource(CLeafBuffer *pPB, float fDist, float fTimeToReady, int Flags)
{
  // The GPU copy is made by the first draw.
  return true;
}

CLeafBuffer *CD3D12Renderer::CreateLeafBuffer(bool bDynamic, const char *szSource, class CIndexedMesh *pIndexedMesh)
{
  auto *pLB = new CLeafBuffer(szSource);
  pLB->m_bDynamic = bDynamic;
  pLB->m_Indices.m_bDynamic = bDynamic;
  pLB->m_nVertexFormat = VERTEX_FORMAT_P3F;
  if (pIndexedMesh)
    pLB->CreateBuffer(pIndexedMesh, false, true, false);
  return pLB;
}

CLeafBuffer *CD3D12Renderer::CreateLeafBufferInitialized(void *pVertBuffer, int nVertCount, int nVertFormat, ushort *pIndices, int nIndices,
  int nPrimetiveType, const char *szSource, EBufferType eBufType, int nMatInfoCount, int nClientTextureBindID,
  bool (*PrepareBufferCallback)(CLeafBuffer *, bool), void *CustomData, bool bOnlyVideoBuffer, bool bPrecache)
{
  const bool bDynamic = eBufType == eBT_Dynamic;
  const int nFormat = GeometryImpl::VertexSize(nVertFormat) > 0 ? nVertFormat : VERTEX_FORMAT_P3F;
  nVertCount = std::max(nVertCount, 0);
  nIndices = std::max(nIndices, 0);

  auto *pLB = new CLeafBuffer(szSource);
  pLB->m_bDynamic = bDynamic;
  pLB->m_Indices.m_bDynamic = bDynamic;
  pLB->m_bOnlyVideoBuffer = bOnlyVideoBuffer;
  pLB->m_nPrimetiveType = nPrimetiveType;
  pLB->m_nClientTextureBindID = nClientTextureBindID;
  pLB->PrepareBufferCallback = PrepareBufferCallback;
  pLB->m_pCustomData = CustomData;
  pLB->m_nVertexFormat = nFormat;
  // Room for the materials; SetChunk adds them.
  GeometryImpl::CreateMaterialList(pLB, nMatInfoCount);

  // Vertices: a system copy when the engine gives them, else (or bOnlyVideoBuffer) only the video
  // copy, which the engine fills itself (UpdateBuffer, locks).
  const size_t nBytes = (size_t)nVertCount * GeometryImpl::VertexSize(nFormat);
  CVertexBuffer *pVB = nullptr;
  if (pVertBuffer && !bOnlyVideoBuffer)
  {
    pLB->CreateSysVertices(nVertCount, nFormat);
    pVB = pLB->m_pSecVertBuffer;
  }
  else
  {
    pLB->CreateVidVertices(nVertCount, nFormat);
    pLB->m_SecVertCount = nVertCount;
    pVB = pLB->m_pVertexBuffer;
  }
  if (pVB && pVertBuffer && nBytes && pVB->m_VS[VSF_GENERAL].m_VData)
  {
    memcpy(pVB->m_VS[VSF_GENERAL].m_VData, pVertBuffer, nBytes);
    GeometryImpl::Invalidate(pVB->m_VS[VSF_GENERAL]);
  }

  // The box of the vertices.
  if (pVertBuffer && nVertCount)
  {
    const int nSize = GeometryImpl::VertexSize(nFormat);
    const bool bPos = nFormat != VERTEX_FORMAT_T3F_B3F_N3F && nFormat != VERTEX_FORMAT_TEX2F;
    if (bPos)
    {
      Vec3 vMin(1e30f, 1e30f, 1e30f), vMax(-1e30f, -1e30f, -1e30f);
      for (int i = 0; i < nVertCount; i++)
      {
        const float *v = (const float *)((const uint8 *)pVertBuffer + (size_t)i * nSize);
        vMin.Set(std::min(vMin.x, v[0]), std::min(vMin.y, v[1]), std::min(vMin.z, v[2]));
        vMax.Set(std::max(vMax.x, v[0]), std::max(vMax.y, v[1]), std::max(vMax.z, v[2]));
      }
      pLB->m_vBoxMin = vMin;
      pLB->m_vBoxMax = vMax;
    }
  }

  // Indices: kept as system indices too, unless only video buffers were asked for.
  if (bOnlyVideoBuffer)
  {
    pLB->UpdateVidIndices(pIndices, nIndices);
    pLB->m_NumIndices = nIndices;
  }
  else
    pLB->UpdateSysIndices(pIndices, nIndices);

  // The callback is called once now, after the video copy exists.
  if (PrepareBufferCallback)
    pLB->CheckUpdate(0, 0, false);
  return pLB;
}

void CD3D12Renderer::DeleteLeafBuffer(CLeafBuffer *pLBuffer)
{
  // NULL and buffers deleted before are fine (the engine repeats deletes).
  if (!GeometryImpl::IsLeafBuffer(pLBuffer))
    return;
  delete pLBuffer;
}
