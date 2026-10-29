// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: LeafBuffer.cpp
//	Description: CLeafBuffer (the SDK's leaf buffer: constructor, statics
//	and every virtual function), the renderer's vertex and index streams
//	and the draw data of Geometry.h.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "Geometry.h"
#include "Shader.h"
#include <CryCompiledFile.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <unordered_set>

CLeafBuffer CLeafBuffer::m_Root("Root");
CLeafBuffer CLeafBuffer::m_RootGlobal("RootGlobal");

namespace
{

//////////////////////////////////////////////////////////////////////////
// Vertex formats

//! Where the components of a vertex format are: byte offsets, -1 for none.
struct SFormatInfo
{
  int nSize;
  int nPos;
  int nNormal;
  int nColor;
  int nSecColor;
  int nTC;
  int nTC2;
};

constexpr SFormatInfo s_Formats[] =
{
  { 0, -1, -1, -1, -1, -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F), 0, -1, -1, -1, -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_COL4UB), 0, -1, offsetof(struct_VERTEX_FORMAT_P3F_COL4UB, color), -1, -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_TEX2F), 0, -1, -1, -1, offsetof(struct_VERTEX_FORMAT_P3F_TEX2F, st), -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F), 0, -1, offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F, color), -1,
    offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F, st), -1 },
  { sizeof(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F), 0, -1, offsetof(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F, color), -1,
    offsetof(struct_VERTEX_FORMAT_TRP3F_COL4UB_TEX2F, st), -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB), 0, -1, offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB, color),
    offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB, seccolor), -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_N), 0, offsetof(struct_VERTEX_FORMAT_P3F_N, normal), -1, -1, -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB), 0, offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB, normal),
    offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB, color), -1, -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_N_TEX2F), 0, offsetof(struct_VERTEX_FORMAT_P3F_N_TEX2F, normal), -1, -1,
    offsetof(struct_VERTEX_FORMAT_P3F_N_TEX2F, st), -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F), 0, offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, normal),
    offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, color), -1, offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, st), -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB), 0, offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, normal),
    offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, color), offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, seccolor), -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F), 0, -1, offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F, color),
    offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F, seccolor), offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_COL4UB_TEX2F, st), -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F), 0, offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, normal),
    offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, color), offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, seccolor),
    offsetof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F, st), -1 },
  { sizeof(SPipTangents), -1, -1, -1, -1, -1, -1 },
  { sizeof(struct_VERTEX_FORMAT_TEX2F), -1, -1, -1, -1, 0, -1 },
  { sizeof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F), 0, -1, offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F, color), -1,
    offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F, st0), offsetof(struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F_TEX2F, st1) },
};
static_assert(std::size(s_Formats) == VERTEX_FORMAT_NUMS, "one entry per vertex format");
static_assert(sizeof(struct_VERTEX_FORMAT_P3F_N_TEX2F) == 32 && sizeof(struct_VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F) == 40,
  "vertex formats as the engine has them");

constexpr uint MASK_GENERAL = 1u << VSF_GENERAL;     //!< m_UpdateVBufferMask: vertices to copy from the system buffer
constexpr uint MASK_TANGENTS = 1u << VSF_TANGENTS;   //!< m_UpdateVBufferMask: tangents to copy from the system buffer

constexpr uint COMP_NORMAL = 1, COMP_COLOR = 2, COMP_SECCOLOR = 4, COMP_TC = 8;

const SFormatInfo *FormatInfo(int nFormat)
{
  if (nFormat <= 0 || nFormat >= VERTEX_FORMAT_NUMS)
    return nullptr;
  return &s_Formats[nFormat];
}

//! The formats that are a position plus optional normal, colours and one texture coordinate set.
bool IsComponentFormat(int nFormat)
{
  return nFormat >= VERTEX_FORMAT_P3F && nFormat <= VERTEX_FORMAT_P3F_N_COL4UB_COL4UB_TEX2F && nFormat != VERTEX_FORMAT_TRP3F_COL4UB_TEX2F;
}

uint Components(int nFormat)
{
  const SFormatInfo *p = FormatInfo(nFormat);
  if (!p)
    return 0;
  return (p->nNormal >= 0 ? COMP_NORMAL : 0) | (p->nColor >= 0 ? COMP_COLOR : 0) | (p->nSecColor >= 0 ? COMP_SECCOLOR : 0) |
    (p->nTC >= 0 ? COMP_TC : 0);
}

//! The format of a video copy: everything the buffer has and everything the draw asks for.
int ChooseVideoFormat(int nOwn, int nRequested)
{
  if (!IsComponentFormat(nRequested) || nRequested == nOwn || !IsComponentFormat(nOwn))
    return nOwn;
  uint c = Components(nOwn) | Components(nRequested);
  if (c == Components(nOwn))
    return nOwn;
  if (c & COMP_SECCOLOR)
    c |= COMP_COLOR;
  return VertFormatForComponents((c & COMP_COLOR) != 0, (c & COMP_SECCOLOR) != 0, (c & COMP_NORMAL) != 0, (c & COMP_TC) != 0);
}

//! Per-vertex components kept outside the vertices (CLeafBuffer::m_TempNormals ...).
struct SExtra
{
  const Vec3 *pNormals = nullptr;
  const UCol *pColors = nullptr;
  const UCol *pSecColors = nullptr;
  const SMRendTexVert *pTexCoords = nullptr;
  int nCount = 0;
};

SExtra ExtraOf(const CLeafBuffer *pLB)
{
  SExtra e;
  e.pNormals = pLB->m_TempNormals;
  e.pColors = pLB->m_TempColors;
  e.pSecColors = pLB->m_TempSecColors;
  e.pTexCoords = pLB->m_TempTexCoords;
  e.nCount = pLB->m_SecVertCount;
  return e;
}

//! Copies nVerts vertices from one format to another. Components the source lacks come from
//! Extra, else normal (0, 0, 1), white, texture coordinates (0, 0).
void ConvertVertices(uint8 *pDst, int nDstFormat, const uint8 *pSrc, int nSrcFormat, int nVerts, const SExtra &Extra)
{
  const SFormatInfo *pD = FormatInfo(nDstFormat);
  const SFormatInfo *pS = FormatInfo(nSrcFormat);
  if (!pD || !pS || nVerts <= 0 || !pDst || !pSrc)
    return;
  if (nDstFormat == nSrcFormat)
  {
    memcpy(pDst, pSrc, (size_t)nVerts * pD->nSize);
    return;
  }
  const SFormatInfo &D = *pD, &S = *pS;
  constexpr float vUp[3] = { 0.f, 0.f, 1.f };
  constexpr float vNoTC[2] = { 0.f, 0.f };
  constexpr uint32 nWhite = 0xFFFFFFFF;
  for (int i = 0; i < nVerts; i++, pDst += D.nSize, pSrc += S.nSize)
  {
    const bool bExtra = i < Extra.nCount;
    memset(pDst, 0, D.nSize);
    if (D.nPos >= 0 && S.nPos >= 0)
      memcpy(pDst + D.nPos, pSrc + S.nPos, 3 * sizeof(float));
    if (nDstFormat == VERTEX_FORMAT_TRP3F_COL4UB_TEX2F)
    {
      const float fRHW = 1.f;
      memcpy(pDst + 3 * sizeof(float), &fRHW, sizeof(float));
    }
    if (D.nNormal >= 0)
    {
      const void *p = S.nNormal >= 0 ? (const void *)(pSrc + S.nNormal) : (Extra.pNormals && bExtra ? (const void *)&Extra.pNormals[i] : vUp);
      memcpy(pDst + D.nNormal, p, 3 * sizeof(float));
    }
    if (D.nColor >= 0)
    {
      const void *p = S.nColor >= 0 ? (const void *)(pSrc + S.nColor) : (Extra.pColors && bExtra ? (const void *)&Extra.pColors[i] : &nWhite);
      memcpy(pDst + D.nColor, p, 4);
    }
    if (D.nSecColor >= 0)
    {
      const void *p = S.nSecColor >= 0 ? (const void *)(pSrc + S.nSecColor) : (Extra.pSecColors && bExtra ? (const void *)&Extra.pSecColors[i] : &nWhite);
      memcpy(pDst + D.nSecColor, p, 4);
    }
    if (D.nTC >= 0)
    {
      const void *p = S.nTC >= 0 ? (const void *)(pSrc + S.nTC) : (Extra.pTexCoords && bExtra ? (const void *)Extra.pTexCoords[i].vert : vNoTC);
      memcpy(pDst + D.nTC, p, 2 * sizeof(float));
    }
    if (D.nTC2 >= 0)
    {
      const int nSrcTC = S.nTC2 >= 0 ? S.nTC2 : S.nTC;
      memcpy(pDst + D.nTC2, nSrcTC >= 0 ? (const void *)(pSrc + nSrcTC) : vNoTC, 2 * sizeof(float));
    }
  }
}

//! Primitives of nPrimType (R_PRIMV_*) appended as a triangle list.
void AppendAsList(std::vector<uint16> &Out, const uint16 *p, int n, int nPrimType)
{
  if (!p || n < 3)
    return;
  switch (nPrimType)
  {
  case R_PRIMV_TRIANGLE_STRIP:
  case R_PRIMV_MULTI_STRIPS:
    for (int i = 0; i + 2 < n; i++)
    {
      uint16 a = p[i], b = p[i + 1];
      const uint16 c = p[i + 2];
      if (a == b || b == c || a == c)
        continue;
      if (i & 1)
        std::swap(a, b);
      Out.push_back(a);
      Out.push_back(b);
      Out.push_back(c);
    }
    break;
  case R_PRIMV_TRIANGLE_FAN:
    for (int i = 1; i + 1 < n; i++)
    {
      Out.push_back(p[0]);
      Out.push_back(p[i]);
      Out.push_back(p[i + 1]);
    }
    break;
  case R_PRIMV_QUADS:
    for (int i = 0; i + 3 < n; i += 4)
    {
      const uint16 q[6] = { p[i], p[i + 1], p[i + 2], p[i], p[i + 2], p[i + 3] };
      Out.insert(Out.end(), q, q + 6);
    }
    break;
  default:    // R_PRIMV_TRIANGLES, R_PRIMV_MULTI_GROUPS (lists here)
    Out.insert(Out.end(), p, p + (n - n % 3));
    break;
  }
}

//////////////////////////////////////////////////////////////////////////
// GPU copies

bool HaveGPU()
{
  return g_pRenderer && g_pRenderer->m_pDevice && g_pRenderer->m_pUpload;
}

//! The GPU copy of a block of system memory: a static buffer in the default heap, or per-frame
//! memory once the data changes in consecutive frames (or after it was drawn in the same frame).
class CGeomGPUCopy
{
public:
  CGeomGPUCopy() = default;
  CGeomGPUCopy(const CGeomGPUCopy &) = delete;
  CGeomGPUCopy &operator=(const CGeomGPUCopy &) = delete;
  ~CGeomGPUCopy() { Release(); }

  void Invalidate() { m_bDirty = true; }

  //! Frees the static buffer (deferred until the GPU is done with it).
  void Release()
  {
    if (m_pStatic)
    {
      if (g_pRenderer && g_pRenderer->m_pUpload)
        g_pRenderer->m_pUpload->ReleaseBuffer(m_pStatic);
      else
      {
        if (m_pStatic->m_pResource)
          m_pStatic->m_pResource->Release();
        delete m_pStatic;
      }
      m_pStatic = nullptr;
      m_nStaticSize = 0;
    }
    m_nFrameGPU = 0;
    m_nFrameFence = 0;
    m_bDirty = true;
  }

  //! The GPU address of nSize bytes of pData for a draw of the current frame; 0 on failure.
  D3D12_GPU_VIRTUAL_ADDRESS Get(const void *pData, uint32 nSize)
  {
    if (!pData || !nSize || !HaveGPU())
      return 0;
    const uint64 nFence = g_pRenderer->m_pDevice->GetFrameFence();
    if (!m_bPerFrame && m_pStatic && !m_bDirty && nSize <= m_nStaticSize)
    {
      m_nUseFence = nFence;
      return m_pStatic->m_nGPU;
    }
    if (!m_bPerFrame && m_pStatic && (nFence - m_nUploadFence <= 2 || m_nUseFence == nFence))
    {
      // Changes often, or again in a frame that already drew it: per-frame memory from now on.
      Release();
      m_bPerFrame = true;
    }
    if (!m_bPerFrame)
    {
      if (!m_pStatic || nSize > m_nStaticSize)
      {
        Release();
        m_pStatic = g_pRenderer->m_pUpload->CreateBuffer(pData, nSize);
        m_nStaticSize = m_pStatic ? nSize : 0;
      }
      else
        g_pRenderer->m_pUpload->UpdateBuffer(m_pStatic, 0, pData, nSize);
      if (m_pStatic)
      {
        m_nUploadFence = nFence;
        m_nUseFence = nFence;
        m_bDirty = false;
        return m_pStatic->m_nGPU;
      }
      m_bPerFrame = true;
    }
    if (m_bDirty || !m_nFrameGPU || m_nFrameFence != nFence || m_nFrameSize != nSize)
    {
      const SD3D12FrameAlloc a = g_pRenderer->m_pUpload->AllocFrame(nSize, 16);
      if (!a.pCPU)
        return 0;
      memcpy(a.pCPU, pData, nSize);
      m_nFrameGPU = a.nGPU;
      m_nFrameFence = nFence;
      m_nFrameSize = nSize;
      m_bDirty = false;
    }
    return m_nFrameGPU;
  }

  uint32 GetVideoBytes() const { return m_pStatic ? m_nStaticSize : 0; }

private:
  CD3D12Buffer *m_pStatic = nullptr;
  uint32 m_nStaticSize = 0;
  D3D12_GPU_VIRTUAL_ADDRESS m_nFrameGPU = 0;
  uint64 m_nFrameFence = 0;
  uint32 m_nFrameSize = 0;
  uint64 m_nUploadFence = 0;      //!< frame of the last upload into m_pStatic
  uint64 m_nUseFence = 0;         //!< last frame m_pStatic was handed to a draw
  bool m_bDirty = true;
  bool m_bPerFrame = false;
};

//! A draw range in the converted index list.
struct SGeomRange
{
  uint32 nFirst = 0, nCount = 0;
};

//! A material's index range when the list was made (to see changes).
struct SChunkSource
{
  int nFirst = 0, nCount = 0;
  bool bRE = false;
};

//! What SVertexStream::m_VertBuf.m_pPtr points to for every stream the renderer made. m_VData
//! is owned (nCapacity bytes) unless nCapacity is 0.
struct SGeomStream
{
  CGeomGPUCopy GPU;
  uint32 nCapacity = 0;

  // The index stream of a leaf buffer (CLeafBuffer::m_Indices):
  std::vector<uint16> List;             //!< the indices as triangle lists, material after material
  std::vector<SGeomRange> Ranges;       //!< per m_pMats entry: what its draw covers in List
  std::vector<SChunkSource> Sources;
  int nListPrimType = -1;
  bool bListDirty = true;
  bool bMatRefs = false;                //!< the m_pMats shader items hold references of the renderer
  bool bLeaf = false;                   //!< this is a leaf buffer's m_Indices
  //! What CMatInfo::m_pPrimitiveGroups points to (SPrimitiveGroup is not declared in the SDK):
  //! per material one group of the words { first index in the material, index count, 0,
  //! triangle count }, the rest 0.
  std::vector<std::array<uint32, 8>> Groups;
};

SGeomStream *StreamOf(const SVertexStream &s)
{
  return static_cast<SGeomStream *>(s.m_VertBuf.m_pPtr);
}

SGeomStream &MakeStream(SVertexStream &s)
{
  if (!s.m_VertBuf.m_pPtr)
    s.m_VertBuf.m_pPtr = new SGeomStream;
  return *StreamOf(s);
}

//! At least nBytes at s.m_VData; keeps the data, zeroes the new part. Memory the renderer did not
//! allocate is left alone.
bool ReserveBytes(SVertexStream &s, uint32 nBytes)
{
  const bool bForeign = !s.m_VertBuf.m_pPtr && s.m_VData;
  if (bForeign)
    return true;
  SGeomStream &st = MakeStream(s);
  if (nBytes <= st.nCapacity && (s.m_VData || !nBytes))
    return true;
  void *p = st.nCapacity ? realloc(s.m_VData, nBytes) : malloc(nBytes);
  if (!p)
    return false;
  memset((uint8 *)p + st.nCapacity, 0, nBytes - st.nCapacity);
  s.m_VData = p;
  st.nCapacity = nBytes;
  return true;
}

//////////////////////////////////////////////////////////////////////////
// State of all leaf buffers

struct SGeomState
{
  std::unordered_set<CLeafBuffer *> Live;
};

SGeomState *s_pState = nullptr;

SGeomState &State()
{
  if (!s_pState)
    s_pState = new SGeomState;
  return *s_pState;
}

bool IsRoot(const CLeafBuffer *p)
{
  return p == &CLeafBuffer::m_Root || p == &CLeafBuffer::m_RootGlobal;
}

//////////////////////////////////////////////////////////////////////////
// Leaf buffer helpers

int MatCount(const CLeafBuffer *pLB)
{
  return pLB->m_pMats ? pLB->m_pMats->Count() : 0;
}

SGeomStream &LeafIndices(CLeafBuffer *pLB)
{
  SGeomStream &s = MakeStream(pLB->m_Indices);
  s.bLeaf = true;
  return s;
}

//! In a multi strip buffer only the first material has a shader and a render element (it draws
//! the strips of the others too).
bool IsMainChunk(const CLeafBuffer *pLB, int nMat)
{
  return pLB->m_nPrimetiveType != R_PRIMV_MULTI_STRIPS || nMat == 0;
}

//! The render elements point to their entry of m_pMats, which moves when the list grows.
void FixChunkPointers(CLeafBuffer *pLB)
{
  for (int i = 0; i < MatCount(pLB); i++)
  {
    CMatInfo &m = (*pLB->m_pMats)[i];
    if (m.pRE)
    {
      m.pRE->m_pChunk = &m;
      m.pRE->m_pBuffer = pLB;
    }
  }
}

CREOcLeaf *NewRE(CLeafBuffer *pLB, CMatInfo &Mat)
{
  auto *pRE = new CREOcLeaf;
  pRE->m_pChunk = &Mat;
  pRE->m_pBuffer = pLB;
  if (pLB->m_nClientTextureBindID > 0)
    pRE->m_CustomTexBind[0] = pLB->m_nClientTextureBindID;
  return pRE;
}

void SetShaderRef(SGeomStream &IS, SShaderItem &si, IShader *pShader)
{
  if (si.m_pShader == pShader)
    return;
  if (IS.bMatRefs)
  {
    if (pShader)
      pShader->AddRef();
    if (si.m_pShader)
      si.m_pShader->Release();
  }
  si.m_pShader = pShader;
}

//! The vertices of a leaf buffer: the system copy, else the video copy.
const uint8 *VertexData(const CLeafBuffer *pLB, int &nFormat, int &nVerts)
{
  const CVertexBuffer *pVB = pLB->m_pSecVertBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
    pVB = pLB->m_pVertexBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
  {
    nFormat = 0;
    nVerts = 0;
    return nullptr;
  }
  nFormat = pVB->m_vertexformat;
  nVerts = pVB->m_NumVerts;
  return (const uint8 *)pVB->m_VS[VSF_GENERAL].m_VData;
}

//! m_vCenter and m_fRadius of a material from its vertex range (vertices of the container).
void CalcChunkBounds(CLeafBuffer *pLB, CMatInfo &Mat)
{
  int nFormat, nVerts;
  const uint8 *p = VertexData(pLB->GetVertexContainer(), nFormat, nVerts);
  const SFormatInfo *pF = FormatInfo(nFormat);
  if (!p || !pF || pF->nPos < 0)
    return;
  const int nFirst = std::max(Mat.nFirstVertId, 0);
  const int nEnd = std::min(nFirst + std::max(Mat.nNumVerts, 0), nVerts);
  if (nFirst >= nEnd)
    return;
  Vec3 vMin(1e30f, 1e30f, 1e30f), vMax(-1e30f, -1e30f, -1e30f);
  for (int i = nFirst; i < nEnd; i++)
  {
    const float *v = (const float *)(p + (size_t)i * pF->nSize + pF->nPos);
    vMin.Set(std::min(vMin.x, v[0]), std::min(vMin.y, v[1]), std::min(vMin.z, v[2]));
    vMax.Set(std::max(vMax.x, v[0]), std::max(vMax.y, v[1]), std::max(vMax.z, v[2]));
  }
  const Vec3 c((vMin.x + vMax.x) * 0.5f, (vMin.y + vMax.y) * 0.5f, (vMin.z + vMax.z) * 0.5f);
  float fR2 = 0;
  for (int i = nFirst; i < nEnd; i++)
  {
    const float *v = (const float *)(p + (size_t)i * pF->nSize + pF->nPos);
    const float dx = v[0] - c.x, dy = v[1] - c.y, dz = v[2] - c.z;
    fR2 = std::max(fR2, dx * dx + dy * dy + dz * dz);
  }
  Mat.m_vCenter = c;
  Mat.m_fRadius = sqrtf(fR2);
}

//! m_vBoxMin / m_vBoxMax from the positions.
void CalcBox(CLeafBuffer *pLB, const uint8 *p, int nFormat, int nVerts)
{
  const SFormatInfo *pF = FormatInfo(nFormat);
  if (!p || !pF || pF->nPos < 0 || nVerts <= 0)
    return;
  Vec3 vMin(1e30f, 1e30f, 1e30f), vMax(-1e30f, -1e30f, -1e30f);
  for (int i = 0; i < nVerts; i++)
  {
    const float *v = (const float *)(p + (size_t)i * pF->nSize + pF->nPos);
    vMin.Set(std::min(vMin.x, v[0]), std::min(vMin.y, v[1]), std::min(vMin.z, v[2]));
    vMax.Set(std::max(vMax.x, v[0]), std::max(vMax.y, v[1]), std::max(vMax.z, v[2]));
  }
  pLB->m_vBoxMin = vMin;
  pLB->m_vBoxMax = vMax;
}

//! Replaces the vertices of pVB by nVerts vertices of nFormat, the old ones converted when bKeep.
void ReformatVertices(CVertexBuffer *pVB, int nFormat, int nVerts, bool bKeep, const SExtra &Extra)
{
  SVertexStream &s = pVB->m_VS[VSF_GENERAL];
  const int nSize = GeometryImpl::VertexSize(nFormat);
  const uint32 nBytes = (uint32)std::max(nVerts, 0) * nSize;
  uint8 *pNew = nBytes ? (uint8 *)malloc(nBytes) : nullptr;
  if (nBytes && !pNew)
    return;
  if (pNew)
  {
    memset(pNew, 0, nBytes);
    if (bKeep && s.m_VData)
      ConvertVertices(pNew, nFormat, (const uint8 *)s.m_VData, pVB->m_vertexformat, std::min(nVerts, pVB->m_NumVerts), Extra);
  }
  SGeomStream &st = MakeStream(s);
  if (st.nCapacity)
    free(s.m_VData);
  s.m_VData = pNew;
  st.nCapacity = nBytes;
  s.m_nItems = nVerts;
  pVB->m_vertexformat = nFormat;
  pVB->m_NumVerts = nVerts;
  st.GPU.Invalidate();
}

CVertexBuffer *CloneVertexBuffer(const CVertexBuffer *pSrc)
{
  if (!pSrc)
    return nullptr;
  CVertexBuffer *pVB = GeometryImpl::NewVertexBuffer(pSrc->m_vertexformat, pSrc->m_NumVerts, pSrc->m_bDynamic);
  if (pSrc->m_VS[VSF_GENERAL].m_VData && pVB->m_VS[VSF_GENERAL].m_VData)
    memcpy(pVB->m_VS[VSF_GENERAL].m_VData, pSrc->m_VS[VSF_GENERAL].m_VData, (size_t)pSrc->m_NumVerts * GeometryImpl::VertexSize(pSrc->m_vertexformat));
  if (pSrc->m_VS[VSF_TANGENTS].m_VData && GeometryImpl::ReserveVertices(pVB, VSF_TANGENTS, pSrc->m_NumVerts))
    memcpy(pVB->m_VS[VSF_TANGENTS].m_VData, pSrc->m_VS[VSF_TANGENTS].m_VData, (size_t)pSrc->m_NumVerts * sizeof(SPipTangents));
  return pVB;
}

//! Makes the video copy current: created when missing, in the format ChooseVideoFormat gives,
//! refilled from the system copy when m_UpdateVBufferMask asks for it.
void PrepareVideo(CLeafBuffer *pLB, int nRequested)
{
  CVertexBuffer *pSys = pLB->m_pSecVertBuffer;
  if (pSys && !pSys->m_VS[VSF_GENERAL].m_VData)
    pSys = nullptr;
  CVertexBuffer *pVid = pLB->m_pVertexBuffer;
  const int nOwn = pSys ? pSys->m_vertexformat : (pVid ? pVid->m_vertexformat : pLB->m_nVertexFormat);
  // The components only grow: what the video copy has stays, what is asked for is added.
  int nFormat = nOwn;
  if (pSys && pVid)
    nFormat = ChooseVideoFormat(nFormat, pVid->m_vertexformat);
  nFormat = ChooseVideoFormat(nFormat, nRequested);
  if (GeometryImpl::VertexSize(nFormat) <= 0)
    return;
  if (pSys)
  {
    const int nVerts = pSys->m_NumVerts;
    if (!pVid)
    {
      pLB->CreateVidVertices(nVerts, nFormat);
      pVid = pLB->m_pVertexBuffer;
      if (!pVid)
        return;
    }
    else if (pVid->m_vertexformat != nFormat || pVid->m_NumVerts != nVerts)
    {
      ReformatVertices(pVid, nFormat, nVerts, false, SExtra());
      pLB->m_UpdateVBufferMask |= MASK_GENERAL | MASK_TANGENTS;
    }
    if (pLB->m_UpdateVBufferMask & MASK_GENERAL)
    {
      ConvertVertices((uint8 *)pVid->m_VS[VSF_GENERAL].m_VData, nFormat, (const uint8 *)pSys->m_VS[VSF_GENERAL].m_VData,
        pSys->m_vertexformat, std::min(nVerts, pVid->m_NumVerts), ExtraOf(pLB));
      GeometryImpl::Invalidate(pVid->m_VS[VSF_GENERAL]);
    }
    if ((pLB->m_UpdateVBufferMask & MASK_TANGENTS) && pSys->m_VS[VSF_TANGENTS].m_VData &&
      GeometryImpl::ReserveVertices(pVid, VSF_TANGENTS, nVerts))
    {
      memcpy(pVid->m_VS[VSF_TANGENTS].m_VData, pSys->m_VS[VSF_TANGENTS].m_VData, (size_t)nVerts * sizeof(SPipTangents));
      GeometryImpl::Invalidate(pVid->m_VS[VSF_TANGENTS]);
    }
  }
  else if (!pVid)
  {
    if (pLB->m_SecVertCount <= 0)
      return;
    pLB->CreateVidVertices(pLB->m_SecVertCount, nOwn > 0 ? nOwn : nFormat);
    pVid = pLB->m_pVertexBuffer;
    if (!pVid)
      return;
  }
  // A buffer with only a video copy keeps its format: the engine writes into it (UpdateBuffer,
  // locks) in the format it was made with.
  pLB->m_UpdateVBufferMask = 0;
  pLB->m_nVertexFormat = pVid->m_vertexformat;
}

//! Makes the triangle lists of the leaf buffer's indices again when they or the materials changed.
void UpdateList(CLeafBuffer *pLB, SGeomStream &IS)
{
  const int nMats = MatCount(pLB);
  bool bDirty = IS.bListDirty || IS.nListPrimType != pLB->m_nPrimetiveType || (int)IS.Sources.size() != nMats;
  for (int i = 0; i < nMats && !bDirty; i++)
  {
    const CMatInfo &m = (*pLB->m_pMats)[i];
    const SChunkSource &s = IS.Sources[i];
    bDirty = s.nFirst != m.nFirstIndexId || s.nCount != m.nNumIndices || s.bRE != (m.pRE != nullptr);
  }
  if (!bDirty)
    return;

  const uint16 *pInds = (const uint16 *)pLB->m_Indices.m_VData;
  const int nInds = pInds ? pLB->m_Indices.m_nItems : 0;
  const int nPrim = pLB->m_nPrimetiveType;
  IS.List.clear();
  IS.Ranges.assign(nMats, SGeomRange());
  IS.Sources.assign(nMats, SChunkSource());
  for (int i = 0; i < nMats; i++)
  {
    const CMatInfo &m = (*pLB->m_pMats)[i];
    IS.Sources[i] = { m.nFirstIndexId, m.nNumIndices, m.pRE != nullptr };
    const int nFirst = std::clamp(m.nFirstIndexId, 0, nInds);
    const int nCount = std::clamp(m.nNumIndices, 0, nInds - nFirst);
    const uint32 nStart = (uint32)IS.List.size();
    if (nCount)
      AppendAsList(IS.List, pInds + nFirst, nCount, nPrim);
    IS.Ranges[i] = { nStart, (uint32)IS.List.size() - nStart };
  }
  if (nPrim == R_PRIMV_MULTI_STRIPS)
  {
    // A material with a render element also draws the strips of the following ones without.
    for (int i = 0; i < nMats; i++)
    {
      if (!(*pLB->m_pMats)[i].pRE)
        continue;
      int j = i + 1;
      while (j < nMats && !(*pLB->m_pMats)[j].pRE)
        j++;
      const uint32 nEnd = j < nMats ? IS.Ranges[j].nFirst : (uint32)IS.List.size();
      IS.Ranges[i].nCount = nEnd - IS.Ranges[i].nFirst;
    }
  }
  IS.nListPrimType = nPrim;
  IS.bListDirty = false;
  IS.GPU.Invalidate();
}

//! The GPU address of a vertex buffer of the renderer (whole buffer).
D3D12_GPU_VIRTUAL_ADDRESS VertexGPU(CVertexBuffer *pVB, uint32 &nBytes)
{
  SVertexStream &s = pVB->m_VS[VSF_GENERAL];
  SGeomStream *pS = StreamOf(s);
  nBytes = (uint32)std::max(pVB->m_NumVerts, 0) * GeometryImpl::VertexSize(pVB->m_vertexformat);
  if (!pS || !s.m_VData || !nBytes)
    return 0;
  return pS->GPU.Get(s.m_VData, nBytes);
}

//! A copy for the current frame.
D3D12_GPU_VIRTUAL_ADDRESS FrameCopy(const void *pData, uint32 nBytes)
{
  if (!pData || !nBytes || !HaveGPU())
    return 0;
  const SD3D12FrameAlloc a = g_pRenderer->m_pUpload->AllocFrame(nBytes, 16);
  if (!a.pCPU)
    return 0;
  memcpy(a.pCPU, pData, nBytes);
  return a.nGPU;
}

//! The shader item a material is drawn with: with a material of the object (pIMat), the SDK's
//! CMatInfo::OverrideShaderItem rule (chunk id 0 or none: the material's own item, n: sub material
//! n - 1), read from the members (the engine also passes blocks with the CMatInfo layout and no
//! vtable). Unlike the SDK's inline version, chunks without an id (-1) take the material's own item
//! too: the engine relies on it (the fog volumes inside buildings, among others). Falls back to the
//! chunk's own item.
SShaderItem ResolveShaderItem(const CMatInfo &Chunk, IMatInfo *pIMat)
{
  SShaderItem si = Chunk.shaderItem;
  if (!pIMat)
    return si;
  const CMatInfo *pMat = static_cast<const CMatInfo *>(pIMat);
  const SShaderItem *pOver = &pMat->shaderItem;
  const int nId = Chunk.m_nCGFMaterialID;
  if (nId >= 1 && pMat->pSubMtls && nId - 1 < pMat->pSubMtls->Num())
  {
    const IMatInfo *pSub = (*pMat->pSubMtls)[nId - 1];
    if (pSub)
      pOver = &static_cast<const CMatInfo *>(pSub)->shaderItem;
  }
  if (pOver->m_pShader)
    si = *pOver;
  return si;
}

bool IsNoDrawShader(IShader *pShader)
{
  if (!pShader)
    return false;
  if (pShader->GetFlags3() & EF3_NODRAW)
    return true;
  IShader *pTempl = pShader->GetTemplate(-1);
  return pTempl && pTempl != pShader && (pTempl->GetFlags3() & EF3_NODRAW);
}

//! Frees the material list if the renderer owns it (with its render elements and the references
//! the renderer took).
void FreeMaterials(CLeafBuffer *pLB)
{
  if (!pLB->m_pMats || !pLB->m_bMaterialsWasCreatedInRenderer)
    return;
  SGeomStream *pIS = StreamOf(pLB->m_Indices);
  const bool bRefs = pIS && pIS->bMatRefs;
  for (int i = 0; i < MatCount(pLB); i++)
  {
    CMatInfo &m = (*pLB->m_pMats)[i];
    if (m.pRE)
    {
      m.pRE->m_pChunk = nullptr;
      m.pRE->m_pBuffer = nullptr;
      m.pRE->Release();
      m.pRE = nullptr;
    }
    if (bRefs)
    {
      SAFE_RELEASE(m.shaderItem.m_pShader);
      SAFE_RELEASE(m.shaderItem.m_pShaderResources);
    }
  }
  delete pLB->m_pMats;
  pLB->m_pMats = nullptr;
  pLB->m_bMaterialsWasCreatedInRenderer = 0;
  if (pIS)
  {
    pIS->bMatRefs = false;
    pIS->bListDirty = true;
  }
}

//! Frees the vertex data the leaf buffer owns (not the indices or the materials).
void FreeVertexData(CLeafBuffer *pLB)
{
  GeometryImpl::DeleteVertexBuffer(pLB->m_pSecVertBuffer);
  pLB->m_pSecVertBuffer = nullptr;
  GeometryImpl::DeleteVertexBuffer(pLB->m_pVertexBuffer);
  pLB->m_pVertexBuffer = nullptr;
  free(pLB->m_TempNormals);
  free(pLB->m_TempTexCoords);
  free(pLB->m_TempColors);
  free(pLB->m_TempSecColors);
  free(pLB->m_pLoadedColors);
  pLB->m_TempNormals = nullptr;
  pLB->m_TempTexCoords = nullptr;
  pLB->m_TempColors = nullptr;
  pLB->m_TempSecColors = nullptr;
  pLB->m_pLoadedColors = nullptr;
  delete[] pLB->m_arrVtxMap;
  pLB->m_arrVtxMap = nullptr;
  delete[] pLB->m_arrVertStripMap;
  pLB->m_arrVertStripMap = nullptr;
}

//! One vertex of a mesh while CreateBuffer merges equal ones.
struct SMeshVert
{
  float pos[3];
  float normal[3];
  float tc[2];
  uint32 color;

  bool operator==(const SMeshVert &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
};
static_assert(sizeof(SMeshVert) == 36, "no padding: compared as bytes");

struct SMeshVertHash
{
  size_t operator()(const SMeshVert &v) const
  {
    uint64 h = 14695981039346656037ull;
    const uint8 *p = (const uint8 *)&v;
    for (size_t i = 0; i < sizeof(v); i++)
      h = (h ^ p[i]) * 1099511628211ull;
    return (size_t)h;
  }
};

void Cross(const float *a, const float *b, float *r)
{
  r[0] = a[1] * b[2] - a[2] * b[1];
  r[1] = a[2] * b[0] - a[0] * b[2];
  r[2] = a[0] * b[1] - a[1] * b[0];
}

float Normalize3(float *v)
{
  const float f = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (f > 1e-12f)
  {
    v[0] /= f;
    v[1] /= f;
    v[2] /= f;
  }
  return f;
}

//! The triangles of every material, as lists (system indices, else the video ones).
std::vector<uint16> TriangleList(CLeafBuffer *pLB)
{
  std::vector<uint16> Out;
  const uint16 *pInds = pLB->m_SecIndices.Num() ? pLB->m_SecIndices.Data() : (const uint16 *)pLB->m_Indices.m_VData;
  const int nInds = pLB->m_SecIndices.Num() ? pLB->m_SecIndices.Num() : (pInds ? pLB->m_Indices.m_nItems : 0);
  if (!pInds || !nInds)
    return Out;
  if (!MatCount(pLB))
  {
    AppendAsList(Out, pInds, nInds, pLB->m_nPrimetiveType);
    return Out;
  }
  for (int i = 0; i < MatCount(pLB); i++)
  {
    const CMatInfo &m = (*pLB->m_pMats)[i];
    const int nFirst = std::clamp(m.nFirstIndexId, 0, nInds);
    const int nCount = std::clamp(m.nNumIndices, 0, nInds - nFirst);
    AppendAsList(Out, pInds + nFirst, nCount, pLB->m_nPrimetiveType);
  }
  return Out;
}

} // namespace

//////////////////////////////////////////////////////////////////////////
// GeometryImpl

int GeometryImpl::VertexSize(int nFormat)
{
  const SFormatInfo *p = FormatInfo(nFormat);
  return p ? p->nSize : 0;
}

bool GeometryImpl::IsDrawableFormat(int nFormat)
{
  return FormatInfo(nFormat) && nFormat != VERTEX_FORMAT_T3F_B3F_N3F && nFormat != VERTEX_FORMAT_TEX2F;
}

CVertexBuffer *GeometryImpl::NewVertexBuffer(int nFormat, int nVerts, bool bDynamic)
{
  auto *pVB = new CVertexBuffer;
  pVB->m_vertexformat = nFormat;
  pVB->m_bDynamic = bDynamic;
  pVB->m_bFenceSet = 0;
  pVB->m_fence = 0;
  for (auto &s : pVB->m_VS)
    s.m_bDynamic = bDynamic;
  MakeStream(pVB->m_VS[VSF_GENERAL]);
  ReserveVertices(pVB, VSF_GENERAL, std::max(nVerts, 0));
  pVB->m_NumVerts = std::max(nVerts, 0);
  pVB->m_VS[VSF_GENERAL].m_nItems = pVB->m_NumVerts;
  return pVB;
}

void GeometryImpl::DeleteVertexBuffer(CVertexBuffer *pVB)
{
  if (!pVB)
    return;
  for (auto &s : pVB->m_VS)
    FreeStream(s);
  delete pVB;
}

bool GeometryImpl::ReserveVertices(CVertexBuffer *pVB, int nStream, int nVerts)
{
  if (!pVB || nStream < 0 || nStream >= VSF_NUM || nVerts < 0)
    return false;
  const int nSize = nStream == VSF_TANGENTS ? (int)sizeof(SPipTangents) : VertexSize(pVB->m_vertexformat);
  if (nSize <= 0)
    return false;
  SVertexStream &s = pVB->m_VS[nStream];
  if (!ReserveBytes(s, (uint32)nVerts * nSize))
    return false;
  s.m_nItems = std::max(s.m_nItems, nVerts);
  if (nStream == VSF_GENERAL)
    pVB->m_NumVerts = std::max(pVB->m_NumVerts, nVerts);
  return true;
}

void GeometryImpl::Invalidate(SVertexStream &Stream)
{
  if (SGeomStream *p = StreamOf(Stream))
  {
    p->GPU.Invalidate();
    p->bListDirty = true;
  }
}

void GeometryImpl::SetIndices(SVertexStream &Stream, const ushort *pSrc, int nInds)
{
  nInds = std::max(nInds, 0);
  if (!StreamOf(Stream) && Stream.m_VData)
  {
    // Memory of the engine's: this stream becomes the renderer's.
    Stream.m_VData = nullptr;
  }
  if (!ReserveBytes(Stream, (uint32)nInds * sizeof(ushort)))
    return;
  if (pSrc && nInds)
    memcpy(Stream.m_VData, pSrc, (size_t)nInds * sizeof(ushort));
  Stream.m_nItems = nInds;
  Invalidate(Stream);
}

void GeometryImpl::FreeStream(SVertexStream &Stream)
{
  if (SGeomStream *p = StreamOf(Stream))
  {
    if (p->nCapacity)
      free(Stream.m_VData);
    delete p;
  }
  Stream.Reset();
}

void GeometryImpl::ReleaseIndices(SVertexStream &Stream)
{
  SGeomStream *p = StreamOf(Stream);
  if (!p || !p->bLeaf)
  {
    FreeStream(Stream);
    return;
  }
  if (p->nCapacity)
    free(Stream.m_VData);
  Stream.m_VData = nullptr;
  Stream.m_nItems = 0;
  Stream.m_bLocked = false;
  p->nCapacity = 0;
  p->GPU.Release();
  p->List.clear();
  p->bListDirty = true;
}

bool GeometryImpl::IsLeafBuffer(CLeafBuffer *pLB)
{
  return pLB && s_pState && s_pState->Live.count(pLB) != 0;
}

void GeometryImpl::CreateMaterialList(CLeafBuffer *pLB, int nMats)
{
  FreeMaterials(pLB);
  pLB->m_pMats = new list2<CMatInfo>;
  pLB->m_pMats->PreAllocate(std::max(nMats, 1));
  pLB->m_bMaterialsWasCreatedInRenderer = 1;
  LeafIndices(pLB).bListDirty = true;
}

//////////////////////////////////////////////////////////////////////////
// CVertexBuffer (out-of-line members of IRenderer.h)

void *CVertexBuffer::GetStream(int nStream, int *nOffs)
{
  if (nOffs)
    *nOffs = 0;
  if (nStream < 0 || nStream >= VSF_NUM)
    return nullptr;
  if (nOffs)
    *nOffs = m_VS[nStream].m_nBufOffset;
  return m_VS[nStream].m_VData;
}

int CVertexBuffer::Size(int Flags, int nVerts)
{
  int nSize = nVerts * GeometryImpl::VertexSize(m_vertexformat);
  if (m_VS[VSF_TANGENTS].m_VData)
    nSize += nVerts * (int)sizeof(SPipTangents);
  return nSize;
}

//////////////////////////////////////////////////////////////////////////
// CLeafBuffer

CLeafBuffer::CLeafBuffer(const char *szSource)
{
  m_Next = m_Prev = nullptr;
  m_sSource = (char *)szSource;
  m_NextGlobal = m_PrevGlobal = nullptr;
  m_TempNormals = nullptr;
  m_TempTexCoords = nullptr;
  m_TempColors = nullptr;
  m_TempSecColors = nullptr;
  m_pCustomData = nullptr;
  PrepareBufferCallback = nullptr;
  m_pVertexBuffer = nullptr;
  m_pVertexContainer = nullptr;
  m_bMaterialsWasCreatedInRenderer = 0;
  m_bOnlyVideoBuffer = 0;
  m_bDynamic = 0;
  m_UpdateVBufferMask = 0;
  m_UpdateFrame = 0;
  m_SortFrame = 0;
  m_SecVertCount = 0;
  m_pSecVertBuffer = nullptr;
  m_NumIndices = 0;
  m_pIndicesPreStrip = nullptr;
  m_arrVertStripMap = nullptr;
  m_nVertexFormat = VERTEX_FORMAT_P3F;
  m_nPrimetiveType = R_PRIMV_TRIANGLES;
  m_pMats = nullptr;
  m_arrVtxMap = nullptr;
  m_fMinU = m_fMinV = 0;
  m_fMaxU = m_fMaxV = 1;
  m_nClientTextureBindID = 0;
  m_vBoxMin.Set(0, 0, 0);
  m_vBoxMax.Set(0, 0, 0);
  m_pLoadedColors = nullptr;

  if (IsRoot(this))
  {
    // The list heads: static objects, nothing allocated.
    if (this == &m_Root)
      m_Next = m_Prev = this;
    else
      m_NextGlobal = m_PrevGlobal = this;
    return;
  }
  if (!m_RootGlobal.m_NextGlobal)
    m_RootGlobal.m_NextGlobal = m_RootGlobal.m_PrevGlobal = &m_RootGlobal;
  LinkGlobal(&m_RootGlobal);
  State().Live.insert(this);
  LeafIndices(this);
}

CLeafBuffer::~CLeafBuffer()
{
  if (IsRoot(this))
    return;
  if (s_pState)
    s_pState->Live.erase(this);
  Unlink();
  UnlinkGlobal();

  // An engine-owned m_pMats is NULL by now; a list of the renderer goes with its render elements.
  FreeMaterials(this);
  FreeVertexData(this);
  m_SecIndices.Free();
  GeometryImpl::FreeStream(m_Indices);
  delete m_pIndicesPreStrip;
  m_pIndicesPreStrip = nullptr;
}

void CLeafBuffer::CalcFaceNormals()
{
  int nFormat, nVerts;
  const uint8 *p = VertexData(GetVertexContainer(), nFormat, nVerts);
  const SFormatInfo *pF = FormatInfo(nFormat);
  if (!p || !pF || pF->nPos < 0)
    return;
  const uint16 *pInds = m_SecIndices.Num() ? m_SecIndices.Data() : (const uint16 *)m_Indices.m_VData;
  const int nInds = m_SecIndices.Num() ? m_SecIndices.Num() : (pInds ? m_Indices.m_nItems : 0);
  for (int i = 0; i < MatCount(this); i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    if (!m.pRE)
      continue;
    std::vector<uint16> Tris;
    const int nFirst = std::clamp(m.nFirstIndexId, 0, nInds);
    AppendAsList(Tris, pInds + nFirst, std::clamp(m.nNumIndices, 0, nInds - nFirst), m_nPrimetiveType);
    if (!m.pRE->m_Faces)
      m.pRE->m_Faces = new TArray<SMeshFace>;
    m.pRE->m_Faces->Free();
    for (size_t t = 0; t + 2 < Tris.size(); t += 3)
    {
      if (Tris[t] >= nVerts || Tris[t + 1] >= nVerts || Tris[t + 2] >= nVerts)
        continue;
      const float *a = (const float *)(p + (size_t)Tris[t] * pF->nSize + pF->nPos);
      const float *b = (const float *)(p + (size_t)Tris[t + 1] * pF->nSize + pF->nPos);
      const float *c = (const float *)(p + (size_t)Tris[t + 2] * pF->nSize + pF->nPos);
      const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
      const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
      float n[3];
      Cross(e1, e2, n);
      Normalize3(n);
      SMeshFace f;
      f.m_Normal.Set(n[0], n[1], n[2]);
      f.m_Middle.Set((a[0] + b[0] + c[0]) / 3.f, (a[1] + b[1] + c[1]) / 3.f, (a[2] + b[2] + c[2]) / 3.f);
      m.pRE->m_Faces->AddElem(f);
    }
  }
}

void CLeafBuffer::AddRE(CCObject *pObj, IShader *pEf, int nNumSort, IShader *pStateEff)
{
  if (!g_pRenderer)
    return;
  for (int i = 0; i < MatCount(this); i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    if (!m.pRE || m.nNumIndices <= 0)
      continue;
    IShader *pShader = pEf ? pEf : m.shaderItem.m_pShader;
    if (!pShader)
      continue;
    g_pRenderer->EF_AddEf(0, m.pRE, pShader, m.shaderItem.m_pShaderResources, pObj, -1, pStateEff, nNumSort);
  }
}

bool CLeafBuffer::CheckUpdate(int VertFormat, int Flags, bool bNeedAddNormals)
{
  CLeafBuffer *pVC = GetVertexContainer();
  if (pVC != this && GeometryImpl::IsLeafBuffer(pVC))
    PrepareVideo(pVC, 0);
  PrepareVideo(this, VertFormat);
  bool bResult = false;
  if (PrepareBufferCallback)
    bResult = PrepareBufferCallback(this, (Flags & MASK_TANGENTS) != 0);
  if (g_pRenderer)
    m_UpdateFrame = g_pRenderer->m_nFrameID;
  return bResult;
}

bool CLeafBuffer::CreateTangBuffer()
{
  CVertexBuffer *pVB = (m_pSecVertBuffer && m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData) ? m_pSecVertBuffer : m_pVertexBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
    return false;
  const SFormatInfo *pF = FormatInfo(pVB->m_vertexformat);
  const int nVerts = pVB->m_NumVerts;
  if (!pF || pF->nPos < 0 || nVerts <= 0 || !GeometryImpl::ReserveVertices(pVB, VSF_TANGENTS, nVerts))
    return false;

  const uint8 *p = (const uint8 *)pVB->m_VS[VSF_GENERAL].m_VData;
  std::vector<float> Acc((size_t)nVerts * 9, 0.f);   // tangent, binormal, face normal sums
  const std::vector<uint16> Tris = TriangleList(this);
  for (size_t t = 0; t + 2 < Tris.size(); t += 3)
  {
    const int i0 = Tris[t], i1 = Tris[t + 1], i2 = Tris[t + 2];
    if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts)
      continue;
    const float *p0 = (const float *)(p + (size_t)i0 * pF->nSize + pF->nPos);
    const float *p1 = (const float *)(p + (size_t)i1 * pF->nSize + pF->nPos);
    const float *p2 = (const float *)(p + (size_t)i2 * pF->nSize + pF->nPos);
    float uv[3][2] = {};
    if (pF->nTC >= 0)
    {
      memcpy(uv[0], p + (size_t)i0 * pF->nSize + pF->nTC, 8);
      memcpy(uv[1], p + (size_t)i1 * pF->nSize + pF->nTC, 8);
      memcpy(uv[2], p + (size_t)i2 * pF->nSize + pF->nTC, 8);
    }
    const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
    const float du1 = uv[1][0] - uv[0][0], dv1 = uv[1][1] - uv[0][1];
    const float du2 = uv[2][0] - uv[0][0], dv2 = uv[2][1] - uv[0][1];
    const float fDet = du1 * dv2 - du2 * dv1;
    const float r = fabsf(fDet) > 1e-12f ? 1.f / fDet : 0.f;
    float T[3], B[3], N[3];
    for (int k = 0; k < 3; k++)
    {
      T[k] = (e1[k] * dv2 - e2[k] * dv1) * r;
      B[k] = (e2[k] * du1 - e1[k] * du2) * r;
    }
    Cross(e1, e2, N);
    for (const int v : { i0, i1, i2 })
    {
      float *a = &Acc[(size_t)v * 9];
      for (int k = 0; k < 3; k++)
      {
        a[k] += T[k];
        a[3 + k] += B[k];
        a[6 + k] += N[k];
      }
    }
  }

  auto *pOut = (SPipTangents *)pVB->m_VS[VSF_TANGENTS].m_VData;
  for (int v = 0; v < nVerts; v++)
  {
    float *a = &Acc[(size_t)v * 9];
    float N[3] = { a[6], a[7], a[8] };
    if (pF->nNormal >= 0)
      memcpy(N, p + (size_t)v * pF->nSize + pF->nNormal, 12);
    else if (m_TempNormals && v < m_SecVertCount)
      memcpy(N, &m_TempNormals[v], 12);
    if (Normalize3(N) <= 1e-12f)
    {
      N[0] = 0; N[1] = 0; N[2] = 1;
    }
    float T[3] = { a[0], a[1], a[2] };
    const float d = T[0] * N[0] + T[1] * N[1] + T[2] * N[2];
    for (int k = 0; k < 3; k++)
      T[k] -= N[k] * d;
    if (Normalize3(T) <= 1e-12f)
    {
      const float x[3] = { 1, 0, 0 }, y[3] = { 0, 1, 0 };
      Cross(fabsf(N[0]) < 0.9f ? x : y, N, T);
      Normalize3(T);
    }
    float B[3] = { a[3], a[4], a[5] };
    float BN[3];
    Cross(N, T, BN);
    if (BN[0] * B[0] + BN[1] * B[1] + BN[2] * B[2] < 0)
      for (float &f : BN)
        f = -f;
    pOut[v].m_Tangent.Set(T[0], T[1], T[2]);
    pOut[v].m_Binormal.Set(BN[0], BN[1], BN[2]);
    pOut[v].m_TNormal.Set(N[0], N[1], N[2]);
  }
  GeometryImpl::Invalidate(pVB->m_VS[VSF_TANGENTS]);
  if (pVB == m_pSecVertBuffer)
    m_UpdateVBufferMask |= MASK_TANGENTS;
  return true;
}

void CLeafBuffer::ReleaseShaders()
{
  // Only references the renderer took for a list of its own (CreateBuffer from a mesh); the
  // engine releases those of its materials and of CopyTo copies.
  SGeomStream *pIS = StreamOf(m_Indices);
  if (!pIS || !pIS->bMatRefs)
    return;
  for (int i = 0; i < MatCount(this); i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    SAFE_RELEASE(m.shaderItem.m_pShader);
    SAFE_RELEASE(m.shaderItem.m_pShaderResources);
  }
}

void CLeafBuffer::CopyTo(CLeafBuffer *pDst, bool bUseSysBuf)
{
  if (!pDst || pDst == this)
    return;
  FreeMaterials(pDst);
  FreeVertexData(pDst);

  pDst->m_nVertexFormat = m_nVertexFormat;
  pDst->m_nPrimetiveType = m_nPrimetiveType;
  pDst->m_bDynamic = m_bDynamic;
  pDst->m_bOnlyVideoBuffer = m_bOnlyVideoBuffer;
  pDst->m_SecVertCount = m_SecVertCount;
  pDst->m_vBoxMin = m_vBoxMin;
  pDst->m_vBoxMax = m_vBoxMax;
  pDst->m_fMinU = m_fMinU;
  pDst->m_fMinV = m_fMinV;
  pDst->m_fMaxU = m_fMaxU;
  pDst->m_fMaxV = m_fMaxV;
  pDst->m_nClientTextureBindID = m_nClientTextureBindID;
  pDst->m_pVertexBuffer = CloneVertexBuffer(m_pVertexBuffer);
  if (bUseSysBuf)
    pDst->m_pSecVertBuffer = CloneVertexBuffer(m_pSecVertBuffer);
  if (m_TempNormals && m_SecVertCount > 0)
  {
    pDst->m_TempNormals = (Vec3 *)malloc(sizeof(Vec3) * m_SecVertCount);
    if (pDst->m_TempNormals)
      memcpy(pDst->m_TempNormals, m_TempNormals, sizeof(Vec3) * m_SecVertCount);
  }
  pDst->m_UpdateVBufferMask = MASK_GENERAL | MASK_TANGENTS;

  pDst->m_SecIndices.Free();
  if (m_SecIndices.Num())
    pDst->m_SecIndices.Copy(m_SecIndices);
  pDst->m_NumIndices = m_NumIndices;
  pDst->m_Indices.m_bDynamic = m_Indices.m_bDynamic;
  GeometryImpl::SetIndices(pDst->m_Indices, (const ushort *)m_Indices.m_VData, m_Indices.m_VData ? m_Indices.m_nItems : 0);

  if (m_pMats)
  {
    const int nMats = MatCount(this);
    GeometryImpl::CreateMaterialList(pDst, nMats);
    // The copy's shader items get references of their own, which the engine gives back itself
    // (it releases the copy's shader items before DeleteLeafBuffer), so they are not bMatRefs.
    for (int i = 0; i < nMats; i++)
    {
      const CMatInfo &src = (*m_pMats)[i];
      pDst->m_pMats->Add(src);
      CMatInfo &m = (*pDst->m_pMats)[i];
      m.pRE = nullptr;
      m.pSubMtls = nullptr;
      if (m.shaderItem.m_pShader)
        m.shaderItem.m_pShader->AddRef();
      if (m.shaderItem.m_pShaderResources)
        m.shaderItem.m_pShaderResources->AddRef();
    }
    for (int i = 0; i < nMats; i++)
    {
      const CMatInfo &src = (*m_pMats)[i];
      CMatInfo &m = (*pDst->m_pMats)[i];
      if (!src.pRE)
        continue;
      m.pRE = NewRE(pDst, m);
      m.pRE->m_Flags |= src.pRE->m_Flags & ~(FCEF_ALLOC_CUST_FLOAT_DATA);
      memcpy(m.pRE->m_CustomTexBind, src.pRE->m_CustomTexBind, sizeof(m.pRE->m_CustomTexBind));
    }
  }
}

void CLeafBuffer::CreateBuffer(CIndexedMesh *pMesh, bool bStripifyAndShareVerts, bool bRemoveNoDrawFaces, bool bKeepRemapTable)
{
  if (!pMesh)
    return;

  // Materials: the list the engine assigned, else one of the renderer's from the mesh.
  if (!m_pMats)
  {
    const int nMeshMats = pMesh->m_lstMatTable.Count();
    GeometryImpl::CreateMaterialList(this, nMeshMats);
    for (int i = 0; i < nMeshMats; i++)
    {
      m_pMats->Add(pMesh->m_lstMatTable[i]);
      CMatInfo &m = (*m_pMats)[i];
      m.pRE = nullptr;
      m.pSubMtls = nullptr;
      if (m.shaderItem.m_pShader)
        m.shaderItem.m_pShader->AddRef();
      if (m.shaderItem.m_pShaderResources)
        m.shaderItem.m_pShaderResources->AddRef();
    }
    LeafIndices(this).bMatRefs = true;
    if (!m_pMats->Count())
    {
      CMatInfo mi;
      m_pMats->Add(mi);
    }
  }
  const int nMats = MatCount(this);
  FreeVertexData(this);

  // Vertex colours when the mesh has them or a material is drawn with them (vegetation, which
  // UpdateCustomLighting lights; white until then); the normals then stay in m_TempNormals.
  const bool bMeshColors = pMesh->m_pColor != nullptr;
  bool bColors = bMeshColors;
  for (int m = 0; m < nMats && !bColors; m++)
  {
    const auto *pShader = static_cast<const CShader *>((*m_pMats)[m].shaderItem.m_pShader);
    bColors = pShader && (pShader->GetDrawClass() == eDC_AlphaTestVColor || pShader->GetDrawClass() == eDC_VColor);
  }
  const int nFormat = bColors ? VERTEX_FORMAT_P3F_COL4UB_TEX2F : VERTEX_FORMAT_P3F_N_TEX2F;

  // The faces of every material, in mesh order. Only the material decides whether a face is
  // dropped (nodraw with bRemoveNoDrawFaces); m_dwFlags is not looked at (not always initialised,
  // and the engine expects the faces it passed, e.g. 3 indices per face in m_pIndicesPreStrip).
  std::vector<std::vector<int>> MatFaces(nMats);
  std::vector<bool> NoDraw(nMats, false);
  if (bRemoveNoDrawFaces)
    for (int m = 0; m < nMats; m++)
      NoDraw[m] = IsNoDrawShader((*m_pMats)[m].shaderItem.m_pShader);
  for (int f = 0; f < pMesh->m_nFaceCount && pMesh->m_pFaces && pMesh->m_pVerts; f++)
  {
    const CObjFace &face = pMesh->m_pFaces[f];
    const int m = face.shader_id;
    if (m >= nMats || NoDraw[m])
      continue;
    if (face.v[0] >= pMesh->m_nVertCount || face.v[1] >= pMesh->m_nVertCount || face.v[2] >= pMesh->m_nVertCount)
      continue;
    MatFaces[m].push_back(f);
  }

  // The vertices of every material: with bStripifyAndShareVerts equal ones (position, normal,
  // texture coordinates and colour) are merged, else every face corner is a vertex of its own,
  // in face order (per-instance data such as lightmap coordinates is stored in that order).
  std::vector<SMeshVert> Verts;
  std::vector<int> VertSrc;
  std::vector<ushort> Inds;
  Verts.reserve(pMesh->m_nVertCount + pMesh->m_nVertCount / 4);
  Inds.reserve((size_t)pMesh->m_nFaceCount * 3);
  std::unordered_map<SMeshVert, uint16, SMeshVertHash> Map;
  std::vector<SGeomRange> IndRange(nMats), VertRange(nMats);
  bool bOverflow = false;
  for (int m = 0; m < nMats; m++)
  {
    Map.clear();
    IndRange[m].nFirst = (uint32)Inds.size();
    VertRange[m].nFirst = (uint32)Verts.size();
    for (const int f : MatFaces[m])
    {
      const CObjFace &face = pMesh->m_pFaces[f];
      const Vec3d &a = pMesh->m_pVerts[face.v[0]], &b = pMesh->m_pVerts[face.v[1]], &c = pMesh->m_pVerts[face.v[2]];
      const float e1[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
      const float e2[3] = { c.x - a.x, c.y - a.y, c.z - a.z };
      float vFaceN[3];
      Cross(e1, e2, vFaceN);
      Normalize3(vFaceN);
      uint16 tri[3];
      bool bOK = true;
      for (int k = 0; k < 3 && bOK; k++)
      {
        const int v = face.v[k];
        SMeshVert mv;
        memset(&mv, 0, sizeof(mv));
        mv.pos[0] = pMesh->m_pVerts[v].x;
        mv.pos[1] = pMesh->m_pVerts[v].y;
        mv.pos[2] = pMesh->m_pVerts[v].z;
        const int n = pMesh->m_nNormCount == pMesh->m_nVertCount ? v : face.n[k];
        if (pMesh->m_pNorms && n < pMesh->m_nNormCount)
        {
          mv.normal[0] = pMesh->m_pNorms[n].x;
          mv.normal[1] = pMesh->m_pNorms[n].y;
          mv.normal[2] = pMesh->m_pNorms[n].z;
        }
        else
          memcpy(mv.normal, vFaceN, sizeof(vFaceN));
        if (pMesh->m_pCoors && face.t[k] < pMesh->m_nCoorCount)
        {
          mv.tc[0] = pMesh->m_pCoors[face.t[k]].s;
          mv.tc[1] = pMesh->m_pCoors[face.t[k]].t;
        }
        mv.color = 0xFFFFFFFF;
        if (bMeshColors)
        {
          const UColor &col = pMesh->m_pColor[v];
          UCol u;
          u.bcolor[0] = col.b;
          u.bcolor[1] = col.g;
          u.bcolor[2] = col.r;
          u.bcolor[3] = col.a;
          mv.color = u.dcolor;
        }
        auto it = bStripifyAndShareVerts ? Map.find(mv) : Map.end();
        if (it != Map.end())
          tri[k] = it->second;
        else if (Verts.size() < 0x10000)
        {
          tri[k] = (uint16)Verts.size();
          if (bStripifyAndShareVerts)
            Map.emplace(mv, tri[k]);
          Verts.push_back(mv);
          VertSrc.push_back(v);
        }
        else
          bOK = false;
      }
      if (!bOK)
      {
        bOverflow = true;
        break;
      }
      Inds.insert(Inds.end(), tri, tri + 3);
    }
    IndRange[m].nCount = (uint32)Inds.size() - IndRange[m].nFirst;
    VertRange[m].nCount = (uint32)Verts.size() - VertRange[m].nFirst;
  }
  if (bOverflow && iLog)
    iLog->Log("Warning: XRenderD3D12: leaf buffer '%s' has more than 65536 vertices; faces dropped", m_sSource ? m_sSource : "");

  // The system copy.
  const int nVerts = (int)Verts.size();
  CreateSysVertices(nVerts, nFormat);
  const SFormatInfo &F = s_Formats[nFormat];
  auto *pOut = (uint8 *)m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData;
  // The normals and the colours of the mesh (0..255) UpdateCustomLighting starts from.
  if (bColors && nVerts)
    m_TempNormals = (Vec3 *)malloc(sizeof(Vec3) * nVerts);
  if (bMeshColors && nVerts)
    m_pLoadedColors = (Vec3 *)malloc(sizeof(Vec3) * nVerts);
  m_fMinU = m_fMinV = 1e30f;
  m_fMaxU = m_fMaxV = -1e30f;
  for (int i = 0; i < nVerts && pOut; i++)
  {
    const SMeshVert &mv = Verts[i];
    uint8 *p = pOut + (size_t)i * F.nSize;
    memcpy(p + F.nPos, mv.pos, 12);
    if (F.nNormal >= 0)
      memcpy(p + F.nNormal, mv.normal, 12);
    if (F.nColor >= 0)
      memcpy(p + F.nColor, &mv.color, 4);
    memcpy(p + F.nTC, mv.tc, 8);
    if (m_TempNormals)
      m_TempNormals[i].Set(mv.normal[0], mv.normal[1], mv.normal[2]);
    if (m_pLoadedColors)
    {
      UCol u;
      u.dcolor = mv.color;
      m_pLoadedColors[i].Set(u.bcolor[2], u.bcolor[1], u.bcolor[0]);
    }
    m_fMinU = std::min(m_fMinU, mv.tc[0]);
    m_fMaxU = std::max(m_fMaxU, mv.tc[0]);
    m_fMinV = std::min(m_fMinV, mv.tc[1]);
    m_fMaxV = std::max(m_fMaxV, mv.tc[1]);
  }
  if (!nVerts)
  {
    m_fMinU = m_fMinV = 0;
    m_fMaxU = m_fMaxV = 1;
  }
  if (bKeepRemapTable && nVerts)
  {
    m_arrVtxMap = new uint[nVerts];
    for (int i = 0; i < nVerts; i++)
      m_arrVtxMap[i] = (uint)VertSrc[i];
  }
  // A buffer made with bStripifyAndShareVerts maps each vertex the triangle list
  // (m_pIndicesPreStrip) refers to onto its own; the engine reads it. Here the order stays.
  if (bStripifyAndShareVerts && nVerts)
  {
    m_arrVertStripMap = new uint[nVerts];
    for (int i = 0; i < nVerts; i++)
      m_arrVertStripMap[i] = (uint)i;
  }

  // Indices: triangle lists, 3 per face, material after material. The engine also reads them
  // as m_pIndicesPreStrip, and the colours of a format without them through m_TempColors
  // (GetColorPtr): white until UpdateCustomLighting lights them.
  m_nPrimetiveType = R_PRIMV_TRIANGLES;
  UpdateSysIndices(Inds.data(), (int)Inds.size());
  delete m_pIndicesPreStrip;
  m_pIndicesPreStrip = new list2<ushort>;
  if (!Inds.empty())
    m_pIndicesPreStrip->AddList(Inds.data(), (int)Inds.size());
  if (F.nColor < 0 && nVerts)
  {
    m_TempColors = (UCol *)malloc(sizeof(UCol) * nVerts);
    if (m_TempColors)
      memset(m_TempColors, 0xff, sizeof(UCol) * nVerts);
  }

  // A buffer made with bStripifyAndShareVerts is an R_PRIMV_MULTI_GROUPS one: one group of
  // triangles per material with faces.
  SGeomStream &IS = LeafIndices(this);
  IS.Groups.assign(bStripifyAndShareVerts ? nMats : 0, {});
  if (bStripifyAndShareVerts)
    m_nPrimetiveType = R_PRIMV_MULTI_GROUPS;
  for (int m = 0; m < nMats; m++)
  {
    CMatInfo &mat = (*m_pMats)[m];
    mat.nFirstIndexId = IndRange[m].nCount ? (int)IndRange[m].nFirst : 0;
    mat.nNumIndices = (int)IndRange[m].nCount;
    mat.nFirstVertId = VertRange[m].nCount ? (int)VertRange[m].nFirst : 0;
    mat.nNumVerts = (int)VertRange[m].nCount;
    mat.m_dwNumSections = 0;
    mat.m_pPrimitiveGroups = nullptr;
    if (mat.nNumIndices && !IS.Groups.empty())
    {
      IS.Groups[m] = { 0, (uint32)mat.nNumIndices, 0, (uint32)mat.nNumIndices / 3 };
      mat.m_dwNumSections = 1;
      mat.m_pPrimitiveGroups = reinterpret_cast<SPrimitiveGroup *>(IS.Groups[m].data());
    }
    if (!mat.nNumIndices)
    {
      mat.m_vCenter.Set(0, 0, 0);
      mat.m_fRadius = 0;
      continue;
    }
    CalcChunkBounds(this, mat);
    if (!mat.pRE)
      mat.pRE = NewRE(this, mat);
  }
  FixChunkPointers(this);

  // The box of the mesh (also what nodraw faces cover), else of the vertices.
  if (pMesh->m_vBoxMin.x <= pMesh->m_vBoxMax.x && pMesh->m_vBoxMin.y <= pMesh->m_vBoxMax.y && pMesh->m_vBoxMin.z <= pMesh->m_vBoxMax.z &&
    !(pMesh->m_vBoxMin.x == 0 && pMesh->m_vBoxMax.x == 0 && pMesh->m_vBoxMin.y == 0 && pMesh->m_vBoxMax.y == 0))
  {
    m_vBoxMin = pMesh->m_vBoxMin;
    m_vBoxMax = pMesh->m_vBoxMax;
  }
  else
    CalcBox(this, pOut, nFormat, nVerts);

  m_UpdateVBufferMask |= MASK_GENERAL;
  m_UpdateFrame = 0;
  LeafIndices(this).bListDirty = true;

  // The engine reads the tangents of the system copy (e.g. when it merges static objects), the
  // face normals of the render elements (m_Faces) and the video copy of a new buffer, before it
  // is drawn.
  CreateTangBuffer();
  CalcFaceNormals();
  CheckUpdate(nFormat, 0, false);
}

bool CLeafBuffer::CreateBuffer(VertexBufferSource *pSource)
{
  if (!pSource || !pSource->pVertices || !pSource->numVertices || pSource->numVertices > 0x10000)
    return false;

  // Materials: the source's list is moved here, else one is made from its shader items.
  list2<CMatInfo> *pMats = nullptr;
  if (pSource->pMats)
  {
    pMats = pSource->pMats;
    pSource->pMats = nullptr;
  }
  else
  {
    pMats = new list2<CMatInfo>;
    pMats->PreAllocate(std::max((int)pSource->numMaterials, 1));
    for (unsigned i = 0; i < pSource->numMaterials; i++)
    {
      CMatInfo mi;
      if (pSource->pShaders)
        mi.shaderItem = pSource->pShaders[i];
      pMats->Add(mi);
    }
  }
  FreeMaterials(this);
  FreeVertexData(this);
  m_pMats = pMats;
  m_bMaterialsWasCreatedInRenderer = 1;
  const int nMats = MatCount(this);
  for (int i = 0; i < nMats; i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    if (pSource->pOrFlags)
      m.m_Flags |= (int)pSource->pOrFlags[i];
    if (pSource->pAndFlags)
      m.m_Flags &= (int)pSource->pAndFlags[i];
    m.nFirstIndexId = m.nNumIndices = 0;
    m.nFirstVertId = m.nNumVerts = 0;
  }

  // The vertices go into the video copy only (no system copy), as positions, normals and
  // texture coordinates; the engine sends the skinned ones with UpdateBuffer.
  const int nVerts = (int)pSource->numVertices;
  m_nVertexFormat = VERTEX_FORMAT_P3F_N_TEX2F;
  m_SecVertCount = nVerts;
  CreateVidVertices(nVerts, VERTEX_FORMAT_P3F_N_TEX2F);
  if (!m_pVertexBuffer)
    return false;
  auto *pOut = (struct_VERTEX_FORMAT_P3F_N_TEX2F *)m_pVertexBuffer->m_VS[VSF_GENERAL].m_VData;
  for (int i = 0; i < nVerts && pOut; i++)
  {
    pOut[i].xyz = pSource->pVertices[i];
    pOut[i].normal.Set(0, 0, 1);
    pOut[i].st[0] = pSource->pUVs ? pSource->pUVs[i].u : 0.f;
    pOut[i].st[1] = pSource->pUVs ? pSource->pUVs[i].v : 0.f;
  }
  GeometryImpl::Invalidate(m_pVertexBuffer->m_VS[VSF_GENERAL]);
  m_UpdateVBufferMask = 0;
  CalcBox(this, (const uint8 *)pOut, VERTEX_FORMAT_P3F_N_TEX2F, nVerts);

  // Indices: triangle lists; a material's range from its primitive groups.
  m_nPrimetiveType = R_PRIMV_TRIANGLES;
  UpdateSysIndices(pSource->pIndices, pSource->pIndices ? (int)pSource->numIndices : 0);
  const int nInds = m_SecIndices.Num();
  if (pSource->pPrimGroups && pSource->numPrimGroups)
  {
    for (unsigned g = 0; g < pSource->numPrimGroups; g++)
    {
      const CCFMaterialGroup &grp = pSource->pPrimGroups[g];
      if ((int)grp.nMaterial >= nMats || !grp.numIndices)
        continue;
      CMatInfo &m = (*m_pMats)[grp.nMaterial];
      const int nFirst = std::min((int)grp.nIndexBase, nInds);
      const int nEnd = std::min(nFirst + (int)grp.numIndices, nInds);
      if (!m.nNumIndices)
      {
        m.nFirstIndexId = nFirst;
        m.nNumIndices = nEnd - nFirst;
      }
      else
      {
        // Several groups of one material: one range over all of them.
        const int nStart = std::min(m.nFirstIndexId, nFirst);
        m.nNumIndices = std::max(m.nFirstIndexId + m.nNumIndices, nEnd) - nStart;
        m.nFirstIndexId = nStart;
      }
    }
  }
  else if (nMats)
  {
    (*m_pMats)[0].nFirstIndexId = 0;
    (*m_pMats)[0].nNumIndices = nInds;
  }

  for (int i = 0; i < nMats; i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    if (!m.nNumIndices)
      continue;
    int nMin = nVerts, nMax = -1;
    for (int k = m.nFirstIndexId; k < m.nFirstIndexId + m.nNumIndices; k++)
    {
      nMin = std::min(nMin, (int)m_SecIndices[k]);
      nMax = std::max(nMax, (int)m_SecIndices[k]);
    }
    m.nFirstVertId = nMax >= nMin ? nMin : 0;
    m.nNumVerts = nMax >= nMin ? nMax - nMin + 1 : 0;
    CalcChunkBounds(this, m);
    if (!m.pRE)
      m.pRE = NewRE(this, m);
    m.pRE->m_Flags |= pSource->nREFlags & ~(uint)FCEF_ALLOC_CUST_FLOAT_DATA;
  }
  FixChunkPointers(this);
  LeafIndices(this).bListDirty = true;
  return true;
}

int CLeafBuffer::GetAllocatedBytes(bool bVideoMem)
{
  auto StreamBytes = [bVideoMem](const SVertexStream &s) -> int
  {
    const SGeomStream *p = StreamOf(s);
    if (!p)
      return 0;
    return bVideoMem ? (int)p->GPU.GetVideoBytes() : (int)p->nCapacity + (int)(p->List.capacity() * sizeof(uint16));
  };
  int nSize = 0;
  for (const CVertexBuffer *pVB : { (const CVertexBuffer *)m_pVertexBuffer, (const CVertexBuffer *)m_pSecVertBuffer })
    if (pVB)
      for (const auto &s : pVB->m_VS)
        nSize += StreamBytes(s);
  nSize += StreamBytes(m_Indices);
  if (!bVideoMem)
  {
    nSize += sizeof(*this) + m_SecIndices.GetSize() * (int)sizeof(ushort);
    if (m_pMats)
      nSize += m_pMats->GetMemoryUsage();
    const int n = std::max(m_SecVertCount, 0);
    if (m_TempNormals)
      nSize += n * (int)sizeof(Vec3);
    if (m_pLoadedColors)
      nSize += n * (int)sizeof(Vec3);
    if (m_TempColors)
      nSize += n * (int)sizeof(UCol);
    if (m_TempSecColors)
      nSize += n * (int)sizeof(UCol);
    if (m_TempTexCoords)
      nSize += n * (int)sizeof(SMRendTexVert);
  }
  return nSize;
}

int CLeafBuffer::Size(int Flags)
{
  return GetAllocatedBytes(false);
}

void CLeafBuffer::Unload()
{
  // The GPU copies only; the next draw makes them again from the system memory.
  for (CVertexBuffer *pVB : { m_pVertexBuffer, m_pSecVertBuffer })
    if (pVB)
      for (auto &s : pVB->m_VS)
        if (SGeomStream *p = StreamOf(s))
          p->GPU.Release();
  if (SGeomStream *p = StreamOf(m_Indices))
    p->GPU.Release();
}

//! The triangles of Tris (a list) that a ray from o along the unit vector d crosses past a small
//! distance: the self shadow of UpdateCustomLighting.
static int CountRayHits(const uint8 *pVerts, const SFormatInfo &F, int nVerts, const std::vector<uint16> &Tris, const float *o, const float *d)
{
  int nHits = 0;
  for (size_t t = 0; t + 2 < Tris.size(); t += 3)
  {
    if (Tris[t] >= nVerts || Tris[t + 1] >= nVerts || Tris[t + 2] >= nVerts)
      continue;
    const float *a = (const float *)(pVerts + (size_t)Tris[t] * F.nSize + F.nPos);
    const float *b = (const float *)(pVerts + (size_t)Tris[t + 1] * F.nSize + F.nPos);
    const float *c = (const float *)(pVerts + (size_t)Tris[t + 2] * F.nSize + F.nPos);
    const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float h[3], q[3];
    Cross(d, e2, h);
    const float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
    if (fabsf(det) < 1e-12f)
      continue;
    const float f = 1 / det;
    const float s[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
    const float u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
    if (u < 0 || u > 1)
      continue;
    Cross(s, e1, q);
    const float v = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
    if (v < 0 || u + v > 1)
      continue;
    if (f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) > 0.01f)
      nHits++;
  }
  return nHits;
}

void CLeafBuffer::UpdateCustomLighting(float fBackSideLevel, Vec3 vStatObjAmbientColor, const Vec3 &vLight, bool bCalcLighting)
{
  CVertexBuffer *pSys = m_pSecVertBuffer;
  if (!pSys || !pSys->m_VS[VSF_GENERAL].m_VData)
    return;
  const SFormatInfo *pF = FormatInfo(pSys->m_vertexformat);
  const int nVerts = pSys->m_NumVerts;
  if (!pF || nVerts <= 0)
    return;
  uint8 *p = (uint8 *)pSys->m_VS[VSF_GENERAL].m_VData;
  UCol *pTempColors = nullptr;
  if (pF->nColor < 0)
  {
    if (!m_TempColors)
      m_TempColors = (UCol *)calloc(nVerts, sizeof(UCol));
    pTempColors = m_TempColors;
    if (!pTempColors)
      return;
  }

  Vec3 vSun(1, 1, 1);
  if (bCalcLighting && iSystem && iSystem->GetI3DEngine())
    vSun = iSystem->GetI3DEngine()->GetSunColor();
  float L[3] = { vLight.x, vLight.y, vLight.z };
  Normalize3(L);
  const float amb[3] = { vStatObjAmbientColor.x, vStatObjAmbientColor.y, vStatObjAmbientColor.z };
  const float sun[3] = { vSun.x, vSun.y, vSun.z };

  // The vertices of the chunks drawn without culling (leaves, grass) are lit from both sides.
  std::vector<uint8> TwoSided(nVerts, 0);
  for (int m = 0; m < MatCount(this); m++)
  {
    const CMatInfo &mat = (*m_pMats)[m];
    if (GetCullMode(static_cast<const CShader *>(mat.shaderItem.m_pShader), mat.shaderItem.m_pShaderResources) != eCULL_None)
      continue;
    const int nFirst = std::clamp(mat.nFirstVertId, 0, nVerts);
    std::fill_n(TwoSided.begin() + nFirst, std::clamp(mat.nNumVerts, 0, nVerts - nFirst), uint8(1));
  }
  // An object with a back side level below 1 shadows itself: each of its triangles between a
  // vertex and the sun takes (1 - level) * 0.15 off the sun light of the vertex.
  const float fShadowStep = bCalcLighting && fBackSideLevel < 1 ? (1 - std::max(fBackSideLevel, 0.f)) * 0.15f : 0;
  std::vector<uint16> Tris;
  if (fShadowStep > 0)
    Tris = TriangleList(this);

  for (int i = 0; i < nVerts; i++)
  {
    uint8 *pV = p + (size_t)i * pF->nSize;
    uint8 *pCol = pTempColors ? pTempColors[i].bcolor : pV + pF->nColor;
    // The colours of the mesh (0..255), white without.
    float rgb[3] = { 1, 1, 1 };
    if (m_pLoadedColors && i < m_SecVertCount)
    {
      rgb[0] = m_pLoadedColors[i].x / 255.f;
      rgb[1] = m_pLoadedColors[i].y / 255.f;
      rgb[2] = m_pLoadedColors[i].z / 255.f;
    }
    if (bCalcLighting)
    {
      // colour * (ambient / 2 + the sun light minus the self shadow, at least 0): on one side the
      // sun times N.L, on two sides ambient / 2 + sun / 2 times |N.L|. The shaders take vertex
      // colours times 2.
      float N[3] = { 0, 0, 1 };
      if (pF->nNormal >= 0)
        memcpy(N, pV + pF->nNormal, 12);
      else if (m_TempNormals && i < m_SecVertCount)
        memcpy(N, &m_TempNormals[i], 12);
      const float d = N[0] * L[0] + N[1] * L[1] + N[2] * L[2];
      const float fShadow = fShadowStep > 0 ? fShadowStep * CountRayHits(p, *pF, nVerts, Tris, (const float *)(pV + pF->nPos), L) : 0;
      for (int c = 0; c < 3; c++)
      {
        const float fSun = TwoSided[i] ? 0.5f * (amb[c] + sun[c] * fabsf(d)) : sun[c] * std::max(d, 0.f);
        rgb[c] *= 0.5f * amb[c] + std::max(fSun - fShadow, 0.f);
      }
    }
    pCol[2] = (uint8)(std::clamp(rgb[0], 0.f, 1.f) * 255.f + 0.5f);
    pCol[1] = (uint8)(std::clamp(rgb[1], 0.f, 1.f) * 255.f + 0.5f);
    pCol[0] = (uint8)(std::clamp(rgb[2], 0.f, 1.f) * 255.f + 0.5f);
  }
  m_UpdateVBufferMask |= MASK_GENERAL;
}

void CLeafBuffer::AddRenderElements(CCObject *pObj, int DLightMask, int nTemplate, int nFogVolumeID, int nSortId, IMatInfo *pIMatInfo)
{
  if (!g_pRenderer)
    return;
  for (int i = 0; i < MatCount(this); i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    if (!m.pRE || m.nNumIndices <= 0)
      continue;
    const SShaderItem si = ResolveShaderItem(m, pIMatInfo);
    if (!si.m_pShader)
      continue;
    g_pRenderer->EF_AddEf(nFogVolumeID, m.pRE, si.m_pShader, si.m_pShaderResources, pObj, nTemplate, nullptr, nSortId);
  }
}

unsigned short *CLeafBuffer::GetIndices(int *pIndicesCount)
{
  if (m_SecIndices.Num())
  {
    if (pIndicesCount)
      *pIndicesCount = m_SecIndices.Num();
    return m_SecIndices.Data();
  }
  if (m_Indices.m_VData && m_Indices.m_nItems > 0)
  {
    if (pIndicesCount)
      *pIndicesCount = m_Indices.m_nItems;
    return (unsigned short *)m_Indices.m_VData;
  }
  if (pIndicesCount)
    *pIndicesCount = 0;
  return nullptr;
}

void CLeafBuffer::DestroyIndices()
{
  // The system copy; the video indices stay for drawing.
  m_SecIndices.Free();
}

void CLeafBuffer::UpdateVidIndices(const ushort *pNewInds, int nInds)
{
  GeometryImpl::SetIndices(m_Indices, pNewInds, nInds);
}

void CLeafBuffer::UpdateSysIndices(const ushort *pNewInds, int nInds)
{
  m_SecIndices.Free();
  if (nInds > 0)
  {
    m_SecIndices.Create(nInds);
    if (pNewInds)
      memcpy(m_SecIndices.Data(), pNewInds, (size_t)nInds * sizeof(ushort));
  }
  m_NumIndices = std::max(nInds, 0);
  UpdateVidIndices(m_SecIndices.Num() ? m_SecIndices.Data() : nullptr, m_SecIndices.Num());
}

void CLeafBuffer::UpdateSysVertices(void *pNewVertices, int nNewVerticesCount)
{
  if (nNewVerticesCount < 0)
    return;
  if (!m_pSecVertBuffer && m_pVertexBuffer)
  {
    // No system copy (characters, buffers of the video memory only): into the video copy.
    UpdateVidVertices(pNewVertices, nNewVerticesCount);
    return;
  }
  if (!m_pSecVertBuffer && !CreateSysVertices(nNewVerticesCount, m_nVertexFormat))
    return;
  CVertexBuffer *pVB = m_pSecVertBuffer;
  if (!GeometryImpl::ReserveVertices(pVB, VSF_GENERAL, nNewVerticesCount))
    return;
  if (pNewVertices && nNewVerticesCount)
    memcpy(pVB->m_VS[VSF_GENERAL].m_VData, pNewVertices, (size_t)nNewVerticesCount * GeometryImpl::VertexSize(pVB->m_vertexformat));
  pVB->m_NumVerts = nNewVerticesCount;
  pVB->m_VS[VSF_GENERAL].m_nItems = nNewVerticesCount;
  m_SecVertCount = nNewVerticesCount;
  GeometryImpl::Invalidate(pVB->m_VS[VSF_GENERAL]);
  m_UpdateVBufferMask |= MASK_GENERAL;
}

void CLeafBuffer::UpdateVidVertices(void *pNewVertices, int nNewVerticesCount)
{
  if (nNewVerticesCount < 0)
    return;
  if (!m_pVertexBuffer)
    CreateVidVertices(nNewVerticesCount, m_nVertexFormat);
  CVertexBuffer *pVB = m_pVertexBuffer;
  if (!pVB || !GeometryImpl::ReserveVertices(pVB, VSF_GENERAL, nNewVerticesCount))
    return;
  if (pNewVertices && nNewVerticesCount)
    memcpy(pVB->m_VS[VSF_GENERAL].m_VData, pNewVertices, (size_t)nNewVerticesCount * GeometryImpl::VertexSize(pVB->m_vertexformat));
  GeometryImpl::Invalidate(pVB->m_VS[VSF_GENERAL]);
}

bool CLeafBuffer::CreateSysVertices(int nVerts, int VertFormat)
{
  if (GeometryImpl::VertexSize(VertFormat) <= 0 || nVerts < 0)
    return false;
  GeometryImpl::DeleteVertexBuffer(m_pSecVertBuffer);
  m_pSecVertBuffer = GeometryImpl::NewVertexBuffer(VertFormat, nVerts, false);
  m_SecVertCount = nVerts;
  m_nVertexFormat = VertFormat;
  m_UpdateVBufferMask |= MASK_GENERAL;
  return true;
}

void CLeafBuffer::CreateVidVertices(int nVerts, int VertFormat)
{
  if (GeometryImpl::VertexSize(VertFormat) <= 0)
    VertFormat = GeometryImpl::VertexSize(m_nVertexFormat) > 0 ? m_nVertexFormat : VERTEX_FORMAT_P3F;
  nVerts = std::max(nVerts, 0);
  if (m_pVertexBuffer)
    ReformatVertices(m_pVertexBuffer, VertFormat, nVerts, false, SExtra());
  else
    m_pVertexBuffer = GeometryImpl::NewVertexBuffer(VertFormat, nVerts, m_bDynamic);
  m_nVertexFormat = VertFormat;
  m_UpdateVBufferMask |= MASK_GENERAL | MASK_TANGENTS;
}

void CLeafBuffer::SetRECustomData(float *pfCustomData, float fFogScale, float fAlpha)
{
  // The pointer is the engine's (it keeps the data alive); read when the elements are drawn.
  for (int i = 0; i < MatCount(this); i++)
  {
    CREOcLeaf *pRE = (*m_pMats)[i].pRE;
    if (!pRE)
      continue;
    pRE->m_Flags &= ~(uint)FCEF_ALLOC_CUST_FLOAT_DATA;
    pRE->m_CustomData = pfCustomData;
    pRE->m_fFogScale = fFogScale;
    pRE->m_Color.a = fAlpha;
  }
}

void CLeafBuffer::SetChunk(IShader *pShader, int nFirstVertId, int nVertCount, int nFirstIndexId, int nIndexCount, int nMatID, bool bForceInitChunk)
{
  if (nMatID < 0)
    return;
  if (!m_pMats)
  {
    if (nIndexCount <= 0 && !bForceInitChunk)
      return;
    GeometryImpl::CreateMaterialList(this, nMatID + 1);
  }
  if (nMatID >= m_pMats->Count())
  {
    // A chunk without indices adds no material.
    if (nIndexCount <= 0 && !bForceInitChunk)
      return;
    while (m_pMats->Count() <= nMatID)
    {
      CMatInfo mi;
      m_pMats->Add(mi);
    }
    FixChunkPointers(this);
  }
  SGeomStream &IS = LeafIndices(this);
  CMatInfo &m = (*m_pMats)[nMatID];
  const bool bMain = IsMainChunk(this, nMatID);
  if (bMain && pShader)
    SetShaderRef(IS, m.shaderItem, pShader);
  m.nFirstVertId = nFirstVertId;
  m.nNumVerts = nVertCount;
  m.nFirstIndexId = nFirstIndexId;
  m.nNumIndices = nIndexCount;
  if (bMain)
  {
    if (!m.pRE)
      m.pRE = NewRE(this, m);
    CalcChunkBounds(this, m);
  }
  IS.bListDirty = true;
}

void CLeafBuffer::SetShader(IShader *pShader, int nCustomTID)
{
  m_nClientTextureBindID = nCustomTID;
  SGeomStream &IS = LeafIndices(this);
  for (int i = 0; i < MatCount(this); i++)
  {
    if (!IsMainChunk(this, i))
      continue;
    CMatInfo &m = (*m_pMats)[i];
    if (pShader)
      SetShaderRef(IS, m.shaderItem, pShader);
    if (m.pRE && nCustomTID > 0)
      m.pRE->m_CustomTexBind[0] = nCustomTID;
  }
}

void *CLeafBuffer::GetSecVerticesPtr(int *pVerticesCount)
{
  int nFormat, nVerts;
  const uint8 *p = VertexData(this, nFormat, nVerts);
  if (pVerticesCount)
    *pVerticesCount = p ? (m_pSecVertBuffer ? m_SecVertCount : nVerts) : 0;
  return (void *)p;
}

void CLeafBuffer::AllocateSystemBuffer(int nVertCount)
{
  CreateSysVertices(nVertCount, GeometryImpl::VertexSize(m_nVertexFormat) > 0 ? m_nVertexFormat : VERTEX_FORMAT_P3F);
}

void CLeafBuffer::FreeSystemBuffer()
{
  if (!m_pSecVertBuffer)
    return;
  PrepareVideo(this, 0);    // the video copy keeps the vertices
  GeometryImpl::DeleteVertexBuffer(m_pSecVertBuffer);
  m_pSecVertBuffer = nullptr;
}

bool CLeafBuffer::Serialize(int &nPos, uchar *pSaveBuffer, bool bSaveToBuffer, char *szFolderName, char *szFileName, double &dCIndexedMesh__LoadMaterial)
{
  return false;
}

void CLeafBuffer::DrawImmediately()
{
}

bool CLeafBuffer::UpdateTangBuffer(SPipTangents *pBasis)
{
  CVertexBuffer *pVB = (m_pSecVertBuffer && m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData) ? m_pSecVertBuffer : m_pVertexBuffer;
  if (!pVB || !pBasis || pVB->m_NumVerts <= 0 || !GeometryImpl::ReserveVertices(pVB, VSF_TANGENTS, pVB->m_NumVerts))
    return false;
  memcpy(pVB->m_VS[VSF_TANGENTS].m_VData, pBasis, (size_t)pVB->m_NumVerts * sizeof(SPipTangents));
  GeometryImpl::Invalidate(pVB->m_VS[VSF_TANGENTS]);
  if (pVB == m_pSecVertBuffer)
    m_UpdateVBufferMask |= MASK_TANGENTS;
  return true;
}

void CLeafBuffer::RenderDebugLightPass(const Matrix44 &mat, int nLightMask, float fAlpha)
{
}

void CLeafBuffer::Render(const SRendParams &rParams, CCObject *pObj, TArray<int> &ShaderTemplates, int e_overlay_geometry, bool bNotCurArea,
  IMatInfo *pMaterial, bool bSupportDefaultShaderTemplates)
{
  if (!g_pRenderer)
    return;
  if (pObj && rParams.fAlpha < 1.f)
    pObj->m_ObjFlags |= FOB_HASALPHA;
  for (int i = 0; i < MatCount(this); i++)
  {
    CMatInfo &m = (*m_pMats)[i];
    if (!m.pRE || m.nNumIndices <= 0)
      continue;
    const SShaderItem si = ResolveShaderItem(m, pMaterial);
    if (!si.m_pShader)
      continue;
    int nTempl = i < ShaderTemplates.Num() ? ShaderTemplates[i] : -1;
    if (nTempl < 0)
      nTempl = rParams.nShaderTemplate >= 0 ? rParams.nShaderTemplate : -1;
    g_pRenderer->EF_AddEf(rParams.nFogVolumeID, m.pRE, si.m_pShader, si.m_pShaderResources, pObj, nTempl, rParams.pStateShader, rParams.nSortValue);
  }
}

//////////////////////////////////////////////////////////////////////////
// Geometry.h

std::optional<SGeomDraw> Geometry::GetDraw(CLeafBuffer *pLB, const CMatInfo *pChunk)
{
  if (!pLB || !pChunk || !GeometryImpl::IsLeafBuffer(pLB) || !MatCount(pLB))
    return std::nullopt;
  const ptrdiff_t nMat = pChunk - &(*pLB->m_pMats)[0];
  if (nMat < 0 || nMat >= MatCount(pLB))
    return std::nullopt;
  CLeafBuffer *pVC = pLB->GetVertexContainer();
  if (pVC != pLB && !GeometryImpl::IsLeafBuffer(pVC))
    return std::nullopt;
  PrepareVideo(pVC, 0);
  CVertexBuffer *pVB = pVC->m_pVertexBuffer;
  if (!pVB || !GeometryImpl::IsDrawableFormat(pVB->m_vertexformat) || pVB->m_NumVerts <= 0)
    return std::nullopt;

  SGeomStream &IS = LeafIndices(pLB);
  UpdateList(pLB, IS);
  const SGeomRange r = IS.Ranges[nMat];
  if (!r.nCount)
    return std::nullopt;
  uint32 nVBBytes = 0;
  const D3D12_GPU_VIRTUAL_ADDRESS nVB = VertexGPU(pVB, nVBBytes);
  const uint32 nIBBytes = (uint32)(IS.List.size() * sizeof(uint16));
  const D3D12_GPU_VIRTUAL_ADDRESS nIB = nVB ? IS.GPU.Get(IS.List.data(), nIBBytes) : 0;
  if (!nVB || !nIB)
    return std::nullopt;

  SGeomDraw d;
  d.VB.BufferLocation = nVB;
  d.VB.SizeInBytes = nVBBytes;
  d.VB.StrideInBytes = GeometryImpl::VertexSize(pVB->m_vertexformat);
  d.nVertexFormat = pVB->m_vertexformat;
  d.IB.BufferLocation = nIB;
  d.IB.SizeInBytes = nIBBytes;
  d.IB.Format = DXGI_FORMAT_R16_UINT;
  d.Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  d.nFirstIndex = r.nFirst;
  d.nNumIndices = r.nCount;
  d.nBaseVertex = 0;
  d.nFirstVertex = (uint32)std::max(pChunk->nFirstVertId, 0);
  d.nNumVertices = (uint32)std::max(pChunk->nNumVerts, 0);
  return d;
}

std::span<const uint16> Geometry::GetTriangles(CLeafBuffer *pLB, const CMatInfo *pChunk)
{
  if (!pLB || !pChunk || !GeometryImpl::IsLeafBuffer(pLB) || !MatCount(pLB))
    return {};
  const ptrdiff_t nMat = pChunk - &(*pLB->m_pMats)[0];
  if (nMat < 0 || nMat >= MatCount(pLB))
    return {};
  SGeomStream &IS = LeafIndices(pLB);
  UpdateList(pLB, IS);
  if (nMat >= (ptrdiff_t)IS.Ranges.size())
    return {};
  const SGeomRange r = IS.Ranges[nMat];
  if ((size_t)r.nFirst + r.nCount > IS.List.size())
    return {};
  return { IS.List.data() + r.nFirst, r.nCount };
}

std::optional<SGeomDraw> Geometry::GetDraw(CVertexBuffer *pVB, int nPrimType, int nFirstVert, int nVerts)
{
  if (!pVB || nVerts < 3 || nFirstVert < 0 || !GeometryImpl::IsDrawableFormat(pVB->m_vertexformat) || !pVB->m_VS[VSF_GENERAL].m_VData)
    return std::nullopt;
  const int nStride = GeometryImpl::VertexSize(pVB->m_vertexformat);
  SGeomDraw d;
  d.nVertexFormat = pVB->m_vertexformat;
  d.VB.StrideInBytes = nStride;
  int nBase = nFirstVert;
  if (StreamOf(pVB->m_VS[VSF_GENERAL]))
  {
    if (nFirstVert + nVerts > pVB->m_NumVerts)
      nVerts = pVB->m_NumVerts - nFirstVert;
    if (nVerts < 3)
      return std::nullopt;
    uint32 nBytes = 0;
    d.VB.BufferLocation = VertexGPU(pVB, nBytes);
    d.VB.SizeInBytes = nBytes;
  }
  else
  {
    // The engine's own buffer (system memory only): these vertices for this frame.
    const uint32 nBytes = (uint32)nVerts * nStride;
    d.VB.BufferLocation = FrameCopy((const uint8 *)pVB->m_VS[VSF_GENERAL].m_VData + (size_t)nFirstVert * nStride, nBytes);
    d.VB.SizeInBytes = nBytes;
    nBase = 0;
  }
  if (!d.VB.BufferLocation)
    return std::nullopt;

  switch (nPrimType)
  {
  case R_PRIMV_TRIANGLE_STRIP:
  case R_PRIMV_MULTI_STRIPS:
    d.Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    d.nFirstVertex = nBase;
    d.nNumVertices = nVerts;
    return d;
  case R_PRIMV_TRIANGLE_FAN:
  case R_PRIMV_QUADS:
  {
    std::vector<uint16> Seq(nVerts), List;
    for (int i = 0; i < nVerts; i++)
      Seq[i] = (uint16)i;
    AppendAsList(List, Seq.data(), nVerts, nPrimType);
    if (List.empty())
      return std::nullopt;
    const uint32 nBytes = (uint32)(List.size() * sizeof(uint16));
    d.IB.BufferLocation = FrameCopy(List.data(), nBytes);
    if (!d.IB.BufferLocation)
      return std::nullopt;
    d.IB.SizeInBytes = nBytes;
    d.IB.Format = DXGI_FORMAT_R16_UINT;
    d.Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    d.nFirstIndex = 0;
    d.nNumIndices = (uint32)List.size();
    d.nBaseVertex = nBase;
    d.nFirstVertex = nBase;
    d.nNumVertices = nVerts;
    return d;
  }
  default:
    d.Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    d.nFirstVertex = nBase;
    d.nNumVertices = nVerts - nVerts % 3;
    return d;
  }
}

std::optional<SGeomDraw> Geometry::GetDraw(CVertexBuffer *pVB, SVertexStream *pInds, int nPrimType, int nFirstIndex, int nInds, int nVertStart, int nVertStop)
{
  if (!pVB || !pInds || !pInds->m_VData || nInds < 3 || nFirstIndex < 0 || !GeometryImpl::IsDrawableFormat(pVB->m_vertexformat) ||
    !pVB->m_VS[VSF_GENERAL].m_VData)
    return std::nullopt;
  if (pInds->m_nItems > 0 && nFirstIndex + nInds > pInds->m_nItems)
    nInds = pInds->m_nItems - nFirstIndex;
  std::vector<uint16> List;
  AppendAsList(List, (const uint16 *)pInds->m_VData + nFirstIndex, nInds, nPrimType);
  if (List.empty())
    return std::nullopt;
  const int nMaxIndex = *std::max_element(List.begin(), List.end());
  const int nMinIndex = *std::min_element(List.begin(), List.end());

  const int nStride = GeometryImpl::VertexSize(pVB->m_vertexformat);
  SGeomDraw d;
  d.nVertexFormat = pVB->m_vertexformat;
  d.VB.StrideInBytes = nStride;
  if (StreamOf(pVB->m_VS[VSF_GENERAL]))
  {
    uint32 nBytes = 0;
    d.VB.BufferLocation = VertexGPU(pVB, nBytes);
    d.VB.SizeInBytes = nBytes;
  }
  else
  {
    const int nCount = std::max(nMaxIndex + 1, nVertStop);
    const uint32 nBytes = (uint32)nCount * nStride;
    d.VB.BufferLocation = FrameCopy(pVB->m_VS[VSF_GENERAL].m_VData, nBytes);
    d.VB.SizeInBytes = nBytes;
  }
  const uint32 nIBBytes = (uint32)(List.size() * sizeof(uint16));
  d.IB.BufferLocation = d.VB.BufferLocation ? FrameCopy(List.data(), nIBBytes) : 0;
  if (!d.VB.BufferLocation || !d.IB.BufferLocation)
    return std::nullopt;
  d.IB.SizeInBytes = nIBBytes;
  d.IB.Format = DXGI_FORMAT_R16_UINT;
  d.Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  d.nFirstIndex = 0;
  d.nNumIndices = (uint32)List.size();
  d.nBaseVertex = 0;
  d.nFirstVertex = nVertStop > nVertStart ? (uint32)nVertStart : (uint32)nMinIndex;
  d.nNumVertices = nVertStop > nVertStart ? (uint32)(nVertStop - nVertStart) : (uint32)(nMaxIndex - nMinIndex + 1);
  return d;
}

std::optional<D3D12_VERTEX_BUFFER_VIEW> Geometry::GetTexCoordStream(CLeafBuffer *pLMTC)
{
  if (!pLMTC || !GeometryImpl::IsLeafBuffer(pLMTC))
    return std::nullopt;
  PrepareVideo(pLMTC, 0);
  CVertexBuffer *pVB = pLMTC->m_pVertexBuffer;
  if (!pVB || pVB->m_vertexformat != VERTEX_FORMAT_TEX2F || pVB->m_NumVerts <= 0)
    return std::nullopt;
  uint32 nBytes = 0;
  const D3D12_GPU_VIRTUAL_ADDRESS nGPU = VertexGPU(pVB, nBytes);
  if (!nGPU)
    return std::nullopt;
  D3D12_VERTEX_BUFFER_VIEW vb;
  vb.BufferLocation = nGPU;
  vb.SizeInBytes = nBytes;
  vb.StrideInBytes = sizeof(struct_VERTEX_FORMAT_TEX2F);
  return vb;
}

std::optional<D3D12_VERTEX_BUFFER_VIEW> Geometry::GetTangentStream(CLeafBuffer *pLB)
{
  if (!pLB || !GeometryImpl::IsLeafBuffer(pLB))
    return std::nullopt;
  CVertexBuffer *pVB = pLB->GetVertexContainer()->m_pVertexBuffer;
  if (!pVB || pVB->m_NumVerts <= 0)
    return std::nullopt;
  SVertexStream &s = pVB->m_VS[VSF_TANGENTS];
  SGeomStream *pS = StreamOf(s);
  const uint32 nBytes = (uint32)pVB->m_NumVerts * sizeof(SPipTangents);
  if (!pS || !s.m_VData || s.m_nItems < pVB->m_NumVerts || pS->nCapacity < nBytes)
    return std::nullopt;
  const D3D12_GPU_VIRTUAL_ADDRESS nGPU = pS->GPU.Get(s.m_VData, nBytes);
  if (!nGPU)
    return std::nullopt;
  D3D12_VERTEX_BUFFER_VIEW vb;
  vb.BufferLocation = nGPU;
  vb.SizeInBytes = nBytes;
  vb.StrideInBytes = sizeof(SPipTangents);
  return vb;
}

void Geometry::Shutdown()
{
  CLeafBuffer &Root = CLeafBuffer::m_RootGlobal;
  while (Root.m_NextGlobal && Root.m_NextGlobal != &Root)
    delete Root.m_NextGlobal;    // the destructor unlinks it
  delete s_pState;
  s_pState = nullptr;
}
