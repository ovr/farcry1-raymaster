//////////////////////////////////////////////////////////////////////
//
//	File: RendererProxyBuffers.cpp
//	Description: CRendererProxy - vertex, index and leaf buffers.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "FieldWatch.h"
#include "RendererProxy.h"
#include "LeafBufferHooks.h"
#include "D3DState.h"

//////////////////////////////////////////////////////////////////////////
// Vertex buffers

void CRendererProxy::DrawTriStrip(CVertexBuffer *src, int vert_num)
{
  TRACE_CALL("buffer %p verts %d", src, vert_num);
  if (TRACE_DUMP)
    Dump_VertexBuffer("buffer", src, vert_num);
  m_pReal->DrawTriStrip(src, vert_num);
  TRACE_D3D_STATE(m_pReal);
}

//! The last pointer GetDynVBPtr returned for pool 0.
static void *s_pDynVB0 = NULL;

void *CRendererProxy::GetDynVBPtr(int nVerts, int &nOffs, int Pool)
{
  TRACE_CALL("verts %d pool %d", nVerts, Pool);
  void *pRes = m_pReal->GetDynVBPtr(nVerts, nOffs, Pool);
  TRACE_RESULT("%p offset %d", pRes, nOffs);
  if (Pool == 0)
    s_pDynVB0 = pRes;
  return pRes;
}

void CRendererProxy::DrawDynVB(int nOffs, int Pool, int nVerts)
{
  TRACE_CALL("offset %d pool %d verts %d", nOffs, Pool, nVerts);
  // Pool 0 holds P3F_COL4UB_TEX2F vertices; GetDynVBPtr returned the place of the first one.
  if (TRACE_DUMP && Pool == 0 && s_pDynVB0)
    Dump_Vertices("verts", s_pDynVB0, VERTEX_FORMAT_P3F_COL4UB_TEX2F, nVerts, 6);
  m_pReal->DrawDynVB(nOffs, Pool, nVerts);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::DrawDynVB(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F *pBuf, ushort *pInds, int nVerts, int nInds, int nPrimType)
{
  TRACE_CALL("verts %p (%d) inds %p (%d) prim %d", pBuf, nVerts, pInds, nInds, nPrimType);
  if (TRACE_DUMP)
  {
    Dump_Vertices("verts", pBuf, VERTEX_FORMAT_P3F_COL4UB_TEX2F, nVerts);
    Dump_Indices("inds", pInds, nInds);
  }
  m_pReal->DrawDynVB(pBuf, pInds, nVerts, nInds, nPrimType);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::SetFenceCompleted(CVertexBuffer *buffer)
{
  TRACE_CALL("%p", buffer);
  m_pReal->SetFenceCompleted(buffer);
}

CVertexBuffer *CRendererProxy::CreateBuffer(int vertexcount, int vertexformat, const char *szSource, bool bDynamic)
{
  TRACE_CALL("verts %d format %s(%d) '%s' dynamic %d", vertexcount, Trace_VertexFormatName(vertexformat), vertexformat, szSource, bDynamic);
  CVertexBuffer *pRes = m_pReal->CreateBuffer(vertexcount, vertexformat, szSource, bDynamic);
  TRACE_RESULT("%p", pRes);
  Watch_RendererObject(WT_CVertexBuffer, pRes);
  if (TRACE_DUMP)
    Dump_VertexBuffer("created", pRes, 0);
  return pRes;
}

void CRendererProxy::ReleaseBuffer(CVertexBuffer *bufptr)
{
  TRACE_CALL("%p", bufptr);
  Watch_Forget(bufptr);
  m_pReal->ReleaseBuffer(bufptr);
}

void CRendererProxy::DrawBuffer(CVertexBuffer *src, SVertexStream *indicies, int numindices, int offsindex, int prmode, int vert_start, int vert_stop, CMatInfo *mi)
{
  TRACE_CALL("buffer %p inds %p count %d offset %d prim %d verts %d-%d mat %p", src, indicies, numindices, offsindex, prmode,
    vert_start, vert_stop, mi);
  if (TRACE_DUMP)
  {
    Dump_VertexBuffer("buffer", src, 0);
    if (indicies)
      Trace_Write("    index stream: data %p hw %p items %d dynamic %d offs %d", indicies->m_VData, indicies->m_VertBuf.m_pPtr,
        indicies->m_nItems, indicies->m_bDynamic, indicies->m_nBufOffset);
    if (mi)
      Dump_MatInfo(mi, -1);
  }
  m_pReal->DrawBuffer(src, indicies, numindices, offsindex, prmode, vert_start, vert_stop, mi);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::UpdateBuffer(CVertexBuffer *dest, const void *src, int vertexcount, bool bUnLock, int nOffs, int Type)
{
  TRACE_CALL("buffer %p src %p verts %d unlock %d offset %d stream %d", dest, src, vertexcount, bUnLock, nOffs, Type);
  if (TRACE_DUMP)
  {
    Dump_VertexBuffer("buffer", dest, 0);
    if (dest && src && Type == VSF_GENERAL)
      Dump_Vertices("src", src, dest->m_vertexformat, vertexcount);
  }
  m_pReal->UpdateBuffer(dest, src, vertexcount, bUnLock, nOffs, Type);
}

//////////////////////////////////////////////////////////////////////////
// Index buffers

void CRendererProxy::CreateIndexBuffer(SVertexStream *dest, const void *src, int indexcount)
{
  TRACE_CALL("stream %p src %p count %d", dest, src, indexcount);
  if (TRACE_DUMP)
    Dump_Indices("src", (const ushort *)src, indexcount);
  m_pReal->CreateIndexBuffer(dest, src, indexcount);
  TRACE_RESULT("data %p hw %p items %d", dest ? dest->m_VData : NULL, dest ? dest->m_VertBuf.m_pPtr : NULL, dest ? dest->m_nItems : 0);
}

void CRendererProxy::UpdateIndexBuffer(SVertexStream *dest, const void *src, int indexcount, bool bUnLock)
{
  TRACE_CALL("stream %p src %p count %d unlock %d", dest, src, indexcount, bUnLock);
  if (TRACE_DUMP)
    Dump_Indices("src", (const ushort *)src, indexcount);
  m_pReal->UpdateIndexBuffer(dest, src, indexcount, bUnLock);
}

void CRendererProxy::ReleaseIndexBuffer(SVertexStream *dest)
{
  TRACE_CALL("stream %p", dest);
  m_pReal->ReleaseIndexBuffer(dest);
}

//////////////////////////////////////////////////////////////////////////
// Leaf buffers

CLeafBuffer *CRendererProxy::CreateLeafBuffer(bool bDynamic, const char *szSource, CIndexedMesh *pIndexedMesh)
{
  TRACE_CALL("dynamic %d '%s' mesh %p", bDynamic, szSource, pIndexedMesh);
  if (TRACE_DUMP && pIndexedMesh)
    Dump_IndexedMesh(pIndexedMesh);
  CWatchArg WatchMesh(WT_CIndexedMesh, pIndexedMesh);
  CLeafBuffer *pRes = m_pReal->CreateLeafBuffer(bDynamic, szSource, pIndexedMesh);
  TRACE_RESULT("%p (vtbl %s) heap prefix %s, SDK size 0x%x", pRes, Trace_VTable(pRes), Trace_HeapPrefix(pRes), (int)sizeof(CLeafBuffer));
  if (TRACE_DUMP)
    Dump_LeafBuffer(pRes);
  LeafBufferHooks_Install(pRes);
  Watch_RendererObject(WT_CLeafBuffer, pRes);
  return pRes;
}

CLeafBuffer *CRendererProxy::CreateLeafBufferInitialized(void *pVertBuffer, int nVertCount, int nVertFormat, ushort *pIndices, int nIndices, int nPrimetiveType, const char *szSource, EBufferType eBufType, int nMatInfoCount, int nClientTextureBindID, bool (*PrepareBufferCallback)(CLeafBuffer *, bool), void *CustomData, bool bOnlyVideoBuffer, bool bPrecache)
{
  TRACE_CALL("verts %p (%d) format %s(%d) inds %p (%d) prim %d '%s' type %d mats %d tex %d callback %s custom %p videoOnly %d precache %d",
    pVertBuffer, nVertCount, Trace_VertexFormatName(nVertFormat), nVertFormat, pIndices, nIndices, nPrimetiveType, szSource, eBufType,
    nMatInfoCount, nClientTextureBindID, Trace_Addr((const void *)PrepareBufferCallback), CustomData, bOnlyVideoBuffer, bPrecache);
  if (TRACE_DUMP)
  {
    Dump_Vertices("verts", pVertBuffer, nVertFormat, nVertCount);
    Dump_Indices("inds", pIndices, nIndices);
  }
  CLeafBuffer *pRes = m_pReal->CreateLeafBufferInitialized(pVertBuffer, nVertCount, nVertFormat, pIndices, nIndices, nPrimetiveType,
    szSource, eBufType, nMatInfoCount, nClientTextureBindID, PrepareBufferCallback, CustomData, bOnlyVideoBuffer, bPrecache);
  TRACE_RESULT("%p (vtbl %s) heap prefix %s, SDK size 0x%x", pRes, Trace_VTable(pRes), Trace_HeapPrefix(pRes), (int)sizeof(CLeafBuffer));
  if (TRACE_DUMP)
    Dump_LeafBuffer(pRes);
  LeafBufferHooks_Install(pRes);
  Watch_RendererObject(WT_CLeafBuffer, pRes);
  return pRes;
}

void CRendererProxy::DeleteLeafBuffer(CLeafBuffer *pLBuffer)
{
  TRACE_CALL("%p '%s'", pLBuffer, pLBuffer && pLBuffer->m_sSource ? pLBuffer->m_sSource : "");
  Watch_Forget(pLBuffer);
  m_pReal->DeleteLeafBuffer(pLBuffer);
}
