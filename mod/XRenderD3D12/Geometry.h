//////////////////////////////////////////////////////////////////////
//
//	File: Geometry.h
//	Description: Leaf buffers (CLeafBuffer), vertex and index buffers
//	(CVertexBuffer, SVertexStream) and the draw data the 3D code takes
//	from them.
//
//	Storage (LeafBuffer.cpp, RendererBuffers.cpp):
//	- Every CVertexBuffer the renderer makes keeps its vertices in system
//	  memory, m_VS[VSF_GENERAL].m_VData (m_NumVerts vertices of
//	  m_vertexformat; tangents, when made, in m_VS[VSF_TANGENTS].m_VData).
//	  m_VS[n].m_VertBuf.m_pPtr points to the renderer's stream object,
//	  which holds the GPU copy. Index streams (SVertexStream) are the same:
//	  ushort indices at m_VData, m_nItems of them.
//	- A leaf buffer keeps the engine's data as the engine gave it: the
//	  system vertices m_pSecVertBuffer, the system indices m_SecIndices
//	  (they stay after the video copy exists: GetIndices), the materials
//	  m_pMats with their ranges. Its "video" copy is m_pVertexBuffer (in
//	  the format CheckUpdate chose) and m_Indices (the indices as given,
//	  in m_nPrimetiveType order).
//	- The GPU copy is made when a draw asks for it (GetDraw). Data that
//	  changes in consecutive frames moves to per-frame memory, the rest
//	  lives in a static buffer of the default heap.
//	- The indices of the GPU copy are triangle lists: strips, fans, quads
//	  and multi strips are converted (the engine's indices stay as given).
//	  In an R_PRIMV_MULTI_STRIPS buffer a material with a render element
//	  is drawn together with the following materials that have none (the
//	  terrain sectors: one strip per material, only the first one has the
//	  shader and the render element).
//
//	Vertex formats: for a buffer with a system copy, CheckUpdate(VertFormat,
//	...) gives the video copy every component of VertFormat and every
//	component of the system format (it never drops one; components the
//	system copy lacks come from m_TempNormals / m_TempColors /
//	m_TempTexCoords or are defaults); VertFormat 0 keeps the system format.
//	A buffer with only a video copy keeps the format it was made with (the
//	engine writes into it). SGeomDraw::nVertexFormat is the format to bind.
//
//	Ownership of m_pMats: a list the engine assigned (StatObj) is the
//	engine's; the renderer frees only lists it made or took over
//	(m_bMaterialsWasCreatedInRenderer), with their render elements.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <span>

//! What a draw of leaf buffer geometry needs; valid for the current frame.
struct SGeomDraw
{
  D3D12_VERTEX_BUFFER_VIEW VB = {};                           //!< input slot 0, in nVertexFormat
  int nVertexFormat = 0;                                      //!< VERTEX_FORMAT_*
  D3D12_INDEX_BUFFER_VIEW IB = {};                            //!< R16_UINT; BufferLocation 0: not indexed
  D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  uint32 nFirstIndex = 0, nNumIndices = 0;                    //!< indexed draws
  int nBaseVertex = 0;
  uint32 nFirstVertex = 0, nNumVertices = 0;                  //!< not indexed: the vertices; indexed: the range used

  bool IsIndexed() const { return IB.BufferLocation != 0; }
};

namespace Geometry
{
  //! The draw of one material (an entry of pLB->m_pMats, e.g. CREOcLeaf::m_pChunk) of a leaf
  //! buffer, after its CheckUpdate: an indexed triangle list. The vertices are those of the buffer's
  //! vertex container (CLeafBuffer::GetVertexContainer). Empty when there is nothing to draw (no
  //! indices, no vertices, a format that is not drawn, pChunk not in m_pMats).
  std::optional<SGeomDraw> GetDraw(CLeafBuffer *pLB, const CMatInfo *pChunk);

  //! The triangles of that draw on the CPU: a list of indices of the vertex container's vertices.
  //! Empty when there are none; valid until the buffer's indices or materials change.
  std::span<const uint16> GetTriangles(CLeafBuffer *pLB, const CMatInfo *pChunk);

  //! Vertices nFirstVert .. nFirstVert + nVerts - 1 of a CVertexBuffer as primitives of type
  //! nPrimType (R_PRIMV_TRIANGLES, R_PRIMV_TRIANGLE_STRIP, R_PRIMV_TRIANGLE_FAN, R_PRIMV_QUADS),
  //! e.g. DrawTriStrip. Strips stay strips (not indexed); fans and quads get an index list. The
  //! buffer may be the engine's own (system memory only): its vertices are copied for this frame.
  std::optional<SGeomDraw> GetDraw(CVertexBuffer *pVB, int nPrimType, int nFirstVert, int nVerts);

  //! An indexed draw of a CVertexBuffer (DrawBuffer): nInds indices of pInds from nFirstIndex, of
  //! type nPrimType (converted to a list). nVertStart / nVertStop: the vertex range the indices use
  //! when known (0, 0: found from the indices).
  std::optional<SGeomDraw> GetDraw(CVertexBuffer *pVB, SVertexStream *pInds, int nPrimType, int nFirstIndex, int nInds,
    int nVertStart = 0, int nVertStop = 0);

  //! The lightmap texture coordinates of an object (its "LMapTexCoords" leaf buffer,
  //! CCObject::m_pLMTCBufferO / SRendParams::pLMTCBuffer): VERTEX_FORMAT_TEX2F for input slot 2,
  //! one coordinate per vertex of the object's own leaf buffer. Empty when it has none.
  std::optional<D3D12_VERTEX_BUFFER_VIEW> GetTexCoordStream(CLeafBuffer *pLMTC);

  //! The tangent frames (SPipTangents, one per vertex) of the video copy of a leaf buffer's vertex
  //! container, for input slot 3, after its CheckUpdate. Empty when it has none for every vertex.
  std::optional<D3D12_VERTEX_BUFFER_VIEW> GetTangentStream(CLeafBuffer *pLB);

  //! Deletes every leaf buffer that is left (IRenderer::Release; the engine does not delete the
  //! terrain's). Call before CD3D12Upload::Shutdown.
  void Shutdown();
}

//////////////////////////////////////////////////////////////////////////
// Shared by LeafBuffer.cpp and RendererBuffers.cpp only.

namespace GeometryImpl
{
  //! Bytes per vertex of a VERTEX_FORMAT_* (all of them, 0 for an invalid one).
  int VertexSize(int nFormat);
  //! Whether the input layouts draw the format (every one but the tangent and the bare texture
  //! coordinate format).
  bool IsDrawableFormat(int nFormat);

  //! A vertex buffer of the renderer: nVerts zeroed vertices in system memory.
  CVertexBuffer *NewVertexBuffer(int nFormat, int nVerts, bool bDynamic);
  //! Frees a buffer made by NewVertexBuffer / IRenderer::CreateBuffer (NULL is fine).
  void DeleteVertexBuffer(CVertexBuffer *pVB);
  //! Room for at least nVerts vertices in stream nStream (VSF_*); keeps the data, zeroes the rest.
  //! Raises m_NumVerts for the general stream.
  bool ReserveVertices(CVertexBuffer *pVB, int nStream, int nVerts);
  //! The data of a stream changed: its GPU copy is made again before the next draw.
  void Invalidate(SVertexStream &Stream);

  //! An index stream with nInds indices (pSrc copied when not NULL, else zeroes kept).
  void SetIndices(SVertexStream &Stream, const ushort *pSrc, int nInds);
  //! Frees what the renderer made for a stream (data and GPU copy) and resets it.
  void FreeStream(SVertexStream &Stream);
  //! IRenderer::ReleaseIndexBuffer: FreeStream, but the index stream of a leaf buffer is only
  //! emptied (it stays the leaf buffer's).
  void ReleaseIndices(SVertexStream &Stream);

  //! True for a leaf buffer that exists (made by the renderer and not deleted).
  bool IsLeafBuffer(CLeafBuffer *pLB);
  //! Creates the leaf buffer's material list (renderer owned, nMats entries reserved, none used).
  void CreateMaterialList(CLeafBuffer *pLB, int nMats);
}
