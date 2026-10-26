//////////////////////////////////////////////////////////////////////
//
//	File: D3DDraws.cpp
//	Description: The Direct3D 9 draw calls of the original renderer in the
//	captured frame.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <d3d9.h>
#include <intrin.h>
#include <map>
#include <set>
#include <vector>
#include "RenderTrace.h"
#include "VCallHooks.h"
#include "D3DState.h"
#include "D3DDraws.h"

//! Slots of IDirect3DDevice9 (declaration order in d3d9.h).
enum
{
  DEV_DrawPrimitive = 81,
  DEV_DrawIndexedPrimitive = 82,
  DEV_DrawPrimitiveUP = 83,
  DEV_DrawIndexedPrimitiveUP = 84,
  DEV_Slots = 119,
  //! The class behind the device has more virtual functions than IDirect3DDevice9, and d3d9.dll
  //! calls them through the same vtable, so the copy goes on as far as the table can be read.
  DEV_CopiedSlots = 1024,
};

//! Slots of IDirect3DVertexBuffer9 and IDirect3DIndexBuffer9.
enum
{
  BUF_Lock = 11,
  BUF_Unlock = 12,
  BUF_Slots = 14,
  BUF_CopiedSlots = 256,
};

//! Vertices written per draw.
static const UINT DUMP_Vertices = 8;

typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawPrimitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawIndexedPrimitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawPrimitiveUP)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawIndexedPrimitiveUP)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT,
  const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Lock)(IUnknown *, UINT, UINT, void **, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Unlock)(IUnknown *);

//! A dynamic vertex or index buffer is write only, so what is written into it is kept from the
//! first draw that uses it to the end of the frame (it gets its own vtable meanwhile).
struct SBufferShadow
{
  void **m_pOrigVTable;
  std::vector<BYTE> m_Data;
  //! Bytes written since the buffer was hooked (and since the last discard).
  std::vector<BYTE> m_Written;
  BYTE *m_pLocked;
  UINT m_nLockOffset;
  UINT m_nLockSize;
};

//! The copy of a buffer vtable with Lock and Unlock replaced.
struct SBufferVTable
{
  void **m_pOrig;
  void *m_Slots[BUF_CopiedSlots];
};

static IDirect3DDevice9 *s_pDevice = NULL;
static void **s_pOrigVTable = NULL;
static void *s_HookedVTable[DEV_CopiedSlots];
static int s_nLevel = 0;
static int s_nDraws = 0;
//! Vertex declarations, shaders and textures already described.
static std::set<const void *> s_Described;
static std::map<IUnknown *, SBufferShadow> s_Shadows;
static std::vector<SBufferVTable *> s_BufferVTables;

static void Append(char *szBuf, size_t nSize, const char *szFormat, ...)
{
  size_t nLen = strlen(szBuf);
  if (nLen >= nSize - 1)
    return;
  va_list args;
  va_start(args, szFormat);
  _vsnprintf(szBuf + nLen, nSize - nLen - 1, szFormat, args);
  va_end(args);
  szBuf[nSize - 1] = 0;
}

static const char *PrimName(D3DPRIMITIVETYPE eType)
{
  switch (eType)
  {
  case D3DPT_POINTLIST:     return "points";
  case D3DPT_LINELIST:      return "lines";
  case D3DPT_LINESTRIP:     return "linestrip";
  case D3DPT_TRIANGLELIST:  return "triangles";
  case D3DPT_TRIANGLESTRIP: return "strip";
  case D3DPT_TRIANGLEFAN:   return "fan";
  default:                  return "?";
  }
}

static const char *DeclTypeName(int nType)
{
  static const char *s_Names[] =
  {
    "FLOAT1", "FLOAT2", "FLOAT3", "FLOAT4", "D3DCOLOR", "UBYTE4", "SHORT2", "SHORT4", "UBYTE4N", "SHORT2N", "SHORT4N", "USHORT2N",
    "USHORT4N", "UDEC3", "DEC3N", "FLOAT16_2", "FLOAT16_4",
  };
  return nType >= 0 && nType < (int)(sizeof(s_Names) / sizeof(s_Names[0])) ? s_Names[nType] : "?";
}

static const char *DeclUsageName(int nUsage)
{
  static const char *s_Names[] =
  {
    "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR",
    "FOG", "DEPTH", "SAMPLE",
  };
  return nUsage >= 0 && nUsage < (int)(sizeof(s_Names) / sizeof(s_Names[0])) ? s_Names[nUsage] : "?";
}

//! Whether p is new to s_Described (and adds it).
static bool FirstSeen(const void *p)
{
  return p && s_Described.insert(p).second;
}

static void DescribeDecl(IDirect3DVertexDeclaration9 *pDecl)
{
  D3DVERTEXELEMENT9 Elements[MAXD3DDECLLENGTH + 1];
  UINT nElements = MAXD3DDECLLENGTH + 1;
  if (FAILED(pDecl->GetDeclaration(Elements, &nElements)))
    return;
  char szLine[1024] = "";
  for (UINT i = 0; i < nElements && Elements[i].Stream != 0xFF; i++)
    Append(szLine, sizeof(szLine), " s%u+%u %s %s%u", Elements[i].Stream, Elements[i].Offset, DeclTypeName(Elements[i].Type),
      DeclUsageName(Elements[i].Usage), Elements[i].UsageIndex);
  Trace_Write("    [d3d] decl %p:%s", pDecl, szLine);
}

//! The version token and size of a shader's byte code.
static void DescribeShader(const char *szKind, const void *pShader, UINT nSize, const DWORD *pCode)
{
  const DWORD nVersion = pCode && nSize >= 4 ? pCode[0] : 0;
  Trace_Write("    [d3d] %s %p: %s_%u_%u, %u bytes", szKind, pShader, (nVersion >> 16) == 0xFFFF ? "ps" : "vs", (nVersion >> 8) & 0xFF,
    nVersion & 0xFF, nSize);
}

static void DescribeTexture(IDirect3DBaseTexture9 *pTex)
{
  D3DRESOURCETYPE eType = pTex->GetType();
  D3DSURFACE_DESC Desc = {};
  if (eType == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9 *)pTex)->GetLevelDesc(0, &Desc)))
    Trace_Write("    [d3d] tex %p: %ux%u format %u levels %u pool %u usage 0x%x", pTex, Desc.Width, Desc.Height, Desc.Format,
      pTex->GetLevelCount(), Desc.Pool, Desc.Usage);
  else if (eType == D3DRTYPE_CUBETEXTURE && SUCCEEDED(((IDirect3DCubeTexture9 *)pTex)->GetLevelDesc(0, &Desc)))
    Trace_Write("    [d3d] tex %p: cube %u format %u levels %u pool %u usage 0x%x", pTex, Desc.Width, Desc.Format, pTex->GetLevelCount(),
      Desc.Pool, Desc.Usage);
  else
    Trace_Write("    [d3d] tex %p: type %d levels %u", pTex, eType, pTex->GetLevelCount());
}

static HRESULT STDMETHODCALLTYPE Hook_Lock(IUnknown *pBuf, UINT nOffset, UINT nSize, void **ppData, DWORD nFlags)
{
  SBufferShadow &Shadow = s_Shadows[pBuf];
  HRESULT hr = ((PFN_Lock)Shadow.m_pOrigVTable[BUF_Lock])(pBuf, nOffset, nSize, ppData, nFlags);
  Shadow.m_pLocked = NULL;
  if (SUCCEEDED(hr) && ppData && *ppData && nOffset < Shadow.m_Data.size())
  {
    if (nFlags & D3DLOCK_DISCARD)
      memset(&Shadow.m_Written[0], 0, Shadow.m_Written.size());
    Shadow.m_pLocked = (BYTE *)*ppData;
    Shadow.m_nLockOffset = nOffset;
    Shadow.m_nLockSize = !nSize || nOffset + nSize > Shadow.m_Data.size() ? (UINT)Shadow.m_Data.size() - nOffset : nSize;
  }
  return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_Unlock(IUnknown *pBuf)
{
  SBufferShadow &Shadow = s_Shadows[pBuf];
  if (Shadow.m_pLocked)
  {
    memcpy(&Shadow.m_Data[Shadow.m_nLockOffset], Shadow.m_pLocked, Shadow.m_nLockSize);
    memset(&Shadow.m_Written[Shadow.m_nLockOffset], 1, Shadow.m_nLockSize);
    Shadow.m_pLocked = NULL;
  }
  return ((PFN_Unlock)Shadow.m_pOrigVTable[BUF_Unlock])(pBuf);
}

//! Starts keeping what is written into a dynamic buffer (holds a reference until the end of the
//! frame).
static void HookBuffer(IUnknown *pBuf, UINT nSize)
{
  if (!nSize || s_Shadows.find(pBuf) != s_Shadows.end())
    return;
  void **pVTable = *(void ***)pBuf;
  SBufferVTable *pHooked = NULL;
  for (size_t i = 0; i < s_BufferVTables.size() && !pHooked; i++)
    if (s_BufferVTables[i]->m_pOrig == pVTable)
      pHooked = s_BufferVTables[i];
  if (!pHooked)
  {
    int nSlots = BUF_CopiedSlots;
    while (nSlots > BUF_Slots && !Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
      nSlots--;
    if (!Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
      return;
    pHooked = new SBufferVTable;
    memset(pHooked->m_Slots, 0, sizeof(pHooked->m_Slots));
    memcpy(pHooked->m_Slots, pVTable, nSlots * sizeof(void *));
    pHooked->m_Slots[BUF_Lock] = (void *)&Hook_Lock;
    pHooked->m_Slots[BUF_Unlock] = (void *)&Hook_Unlock;
    pHooked->m_pOrig = pVTable;
    s_BufferVTables.push_back(pHooked);
  }
  SBufferShadow &Shadow = s_Shadows[pBuf];
  Shadow.m_pOrigVTable = pVTable;
  Shadow.m_Data.assign(nSize, 0);
  Shadow.m_Written.assign(nSize, 0);
  Shadow.m_pLocked = NULL;
  pBuf->AddRef();
  *(void ***)pBuf = pHooked->m_Slots;
}

//! The bytes of a hooked buffer, if all of them were written since it was hooked.
static const BYTE *ShadowData(IUnknown *pBuf, UINT nOffset, UINT nSize)
{
  std::map<IUnknown *, SBufferShadow>::iterator it = s_Shadows.find(pBuf);
  if (it == s_Shadows.end() || !nSize || nOffset + nSize > it->second.m_Data.size())
    return NULL;
  for (UINT i = 0; i < nSize; i++)
    if (!it->second.m_Written[nOffset + i])
      return NULL;
  return &it->second.m_Data[nOffset];
}

//! The number of vertices a non-indexed draw reads.
static UINT DrawVertexCount(D3DPRIMITIVETYPE eType, UINT nPrims)
{
  switch (eType)
  {
  case D3DPT_POINTLIST:     return nPrims;
  case D3DPT_LINELIST:      return nPrims * 2;
  case D3DPT_LINESTRIP:     return nPrims + 1;
  case D3DPT_TRIANGLELIST:  return nPrims * 3;
  default:                  return nPrims + 2;
  }
}

//! Writes the first vertices of a stream as the vertex declaration describes them.
static void DumpVertices(IDirect3DVertexDeclaration9 *pDecl, UINT nStream, const BYTE *pData, UINT nStride, UINT nCount)
{
  D3DVERTEXELEMENT9 Elements[MAXD3DDECLLENGTH + 1];
  UINT nElements = MAXD3DDECLLENGTH + 1;
  if (!pDecl || !pData || FAILED(pDecl->GetDeclaration(Elements, &nElements)))
    return;
  for (UINT nVert = 0; nVert < nCount && nVert < DUMP_Vertices; nVert++)
  {
    const BYTE *pVert = pData + nVert * nStride;
    char szLine[512] = "";
    for (UINT i = 0; i < nElements && Elements[i].Stream != 0xFF; i++)
    {
      if (Elements[i].Stream != nStream || Elements[i].Offset + 4u > nStride)
        continue;
      const BYTE *pElem = pVert + Elements[i].Offset;
      Append(szLine, sizeof(szLine), " %s%u", DeclUsageName(Elements[i].Usage), Elements[i].UsageIndex);
      if (Elements[i].Type <= D3DDECLTYPE_FLOAT4)
      {
        for (UINT c = 0; c <= (UINT)Elements[i].Type && Elements[i].Offset + (c + 1) * 4 <= nStride; c++)
          Append(szLine, sizeof(szLine), " %g", ((const float *)pElem)[c]);
      }
      else
        Append(szLine, sizeof(szLine), " 0x%08x", *(const DWORD *)pElem);
    }
    Trace_Write("    [d3d] v%u:%s", nVert, szLine);
  }
}

//! The render element whose mfDraw is running and, for a leaf buffer element, its buffer and
//! material.
static void AppendRE(char *szBuf, size_t nSize)
{
  CRendElement *pRE = VCallHooks_DrawingRE();
  if (!pRE || !Trace_IsReadable(pRE, sizeof(CRendElement)))
  {
    Append(szBuf, nSize, " | RE -");
    return;
  }
  Append(szBuf, nSize, " | RE %s %p", Trace_RETypeName(pRE->m_Type), pRE);
  if (pRE->m_Type != eDATA_OcLeaf || !Trace_IsReadable(pRE, sizeof(CREOcLeaf)))
    return;
  const CREOcLeaf *pLeaf = (const CREOcLeaf *)pRE;
  const CLeafBuffer *pLB = pLeaf->m_pBuffer;
  if (!pLB || !Trace_IsReadable(pLB, sizeof(CLeafBuffer)))
    return;
  const char *szSource = pLB->m_sSource && Trace_IsReadable(pLB->m_sSource, 1) ? pLB->m_sSource : "?";
  int nChunk = -1;
  if (pLB->m_pMats && Trace_IsReadable(pLB->m_pMats, sizeof(*pLB->m_pMats)) && pLB->m_pMats->Count())
  {
    const CMatInfo *pFirst = &(*pLB->m_pMats)[0];
    if (pLeaf->m_pChunk >= pFirst && pLeaf->m_pChunk < pFirst + pLB->m_pMats->Count())
      nChunk = (int)(pLeaf->m_pChunk - pFirst);
  }
  Append(szBuf, nSize, " lb %p '%s' chunk %d", pLB, szSource, nChunk);
  IShader *pShader = nChunk >= 0 ? pLeaf->m_pChunk->shaderItem.m_pShader : NULL;
  if (pShader)
    Append(szBuf, nSize, " shader '%s'", pShader->GetName());
}

//! Writes the line of a draw call (before it is made), the objects it uses for the first time and
//! the first vertices of a non-indexed draw (nStart is its first vertex, the first index of an
//! indexed draw; pUPVerts the vertices of a *UP draw).
static void WriteDraw(IDirect3DDevice9 *pDev, const char *szCall, bool bStreams, bool bIndexed, UINT nStart, UINT nVerts,
  const void *pUPVerts, UINT nUPStride, void *pCaller)
{
  const UINT nStartIndex = bIndexed ? nStart : 0;
  IUnknown *pVB0 = NULL;
  UINT nOffset0 = 0, nStride0 = 0;
  ++s_nDraws;
  char szLine[2048];
  _snprintf_s(szLine, _TRUNCATE, "[d3d] %d %s", s_nDraws, szCall);

  if (bStreams)
  {
    for (UINT nStream = 0; nStream < 4; nStream++)
    {
      IDirect3DVertexBuffer9 *pVB = NULL;
      UINT nOffset = 0, nStride = 0;
      if (FAILED(pDev->GetStreamSource(nStream, &pVB, &nOffset, &nStride)) || !pVB)
        continue;
      D3DVERTEXBUFFER_DESC Desc = {};
      pVB->GetDesc(&Desc);
      Append(szLine, sizeof(szLine), " s%u %p+%u/%u (%u bytes%s)", nStream, pVB, nOffset, nStride, Desc.Size,
        (Desc.Usage & D3DUSAGE_DYNAMIC) ? " dynamic" : "");
      if (Desc.Usage & D3DUSAGE_DYNAMIC)
        HookBuffer(pVB, Desc.Size);
      if (!nStream)
      {
        pVB0 = pVB;
        nOffset0 = nOffset;
        nStride0 = nStride;
      }
      pVB->Release();
    }
  }
  IDirect3DIndexBuffer9 *pIB = NULL;
  D3DINDEXBUFFER_DESC IBDesc = {};
  if (bIndexed && SUCCEEDED(pDev->GetIndices(&pIB)) && pIB)
  {
    pIB->GetDesc(&IBDesc);
    Append(szLine, sizeof(szLine), " ib %p (%u bytes, %s bit%s)", pIB, IBDesc.Size, IBDesc.Format == D3DFMT_INDEX32 ? "32" : "16",
      (IBDesc.Usage & D3DUSAGE_DYNAMIC) ? " dynamic" : "");
    if (IBDesc.Usage & D3DUSAGE_DYNAMIC)
      HookBuffer(pIB, IBDesc.Size);
  }

  IDirect3DVertexDeclaration9 *pDecl = NULL;
  DWORD nFVF = 0;
  pDev->GetFVF(&nFVF);
  pDev->GetVertexDeclaration(&pDecl);
  Append(szLine, sizeof(szLine), " decl %p fvf 0x%x", pDecl, nFVF);
  IDirect3DVertexShader9 *pVS = NULL;
  IDirect3DPixelShader9 *pPS = NULL;
  pDev->GetVertexShader(&pVS);
  pDev->GetPixelShader(&pPS);
  Append(szLine, sizeof(szLine), " vs %p ps %p", pVS, pPS);
  IDirect3DBaseTexture9 *pTextures[4] = {};
  for (DWORD nStage = 0; nStage < 4; nStage++)
  {
    pDev->GetTexture(nStage, &pTextures[nStage]);
    if (pTextures[nStage])
      Append(szLine, sizeof(szLine), " t%u %p", nStage, pTextures[nStage]);
  }
  AppendRE(szLine, sizeof(szLine));
  Append(szLine, sizeof(szLine), " | from %s", Trace_Addr(pCaller));
  Trace_Write("%s", szLine);

  if (pDecl)
  {
    if (FirstSeen(pDecl))
      DescribeDecl(pDecl);
    // The dynamic buffers keep what was written into them since the first draw that used them.
    const UINT nDumped = nVerts < DUMP_Vertices ? nVerts : DUMP_Vertices;
    if (!bIndexed && pUPVerts)
      DumpVertices(pDecl, 0, (const BYTE *)pUPVerts, nUPStride, nDumped);
    else if (!bIndexed && pVB0 && nStride0)
      DumpVertices(pDecl, 0, ShadowData(pVB0, nOffset0 + nStart * nStride0, nDumped * nStride0), nStride0, nDumped);
    pDecl->Release();
  }
  if (pVS)
  {
    UINT nSize = 0;
    if (FirstSeen(pVS) && SUCCEEDED(pVS->GetFunction(NULL, &nSize)))
    {
      DWORD *pCode = (DWORD *)malloc(nSize ? nSize : 4);
      DescribeShader("vs", pVS, nSize, pCode && SUCCEEDED(pVS->GetFunction(pCode, &nSize)) ? pCode : NULL);
      free(pCode);
    }
    pVS->Release();
  }
  if (pPS)
  {
    UINT nSize = 0;
    if (FirstSeen(pPS) && SUCCEEDED(pPS->GetFunction(NULL, &nSize)))
    {
      DWORD *pCode = (DWORD *)malloc(nSize ? nSize : 4);
      DescribeShader("ps", pPS, nSize, pCode && SUCCEEDED(pPS->GetFunction(pCode, &nSize)) ? pCode : NULL);
      free(pCode);
    }
    pPS->Release();
  }
  for (DWORD nStage = 0; nStage < 4; nStage++)
  {
    if (!pTextures[nStage])
      continue;
    if (FirstSeen(pTextures[nStage]))
      DescribeTexture(pTextures[nStage]);
    pTextures[nStage]->Release();
  }

  // The first indices of the draw, when the buffer can be read back.
  if (pIB)
  {
    const UINT nIndexSize = IBDesc.Format == D3DFMT_INDEX32 ? 4 : 2;
    const UINT nCount = 12;
    void *pData = NULL;
    const BYTE *pShadow = ShadowData(pIB, nStartIndex * nIndexSize, nCount * nIndexSize);
    if (pShadow || ((IBDesc.Pool != D3DPOOL_DEFAULT || !(IBDesc.Usage & D3DUSAGE_WRITEONLY))
      && (nStartIndex + nCount) * nIndexSize <= IBDesc.Size
      && SUCCEEDED(pIB->Lock(nStartIndex * nIndexSize, nCount * nIndexSize, &pData, D3DLOCK_READONLY))))
    {
      const void *pIndices = pShadow ? (const void *)pShadow : pData;
      char szIndices[256] = "";
      for (UINT i = 0; i < nCount; i++)
        Append(szIndices, sizeof(szIndices), " %u", nIndexSize == 4 ? ((const DWORD *)pIndices)[i] : ((const WORD *)pIndices)[i]);
      if (!pShadow)
        pIB->Unlock();
      Trace_Write("    [d3d] indices from %u:%s", nStartIndex, szIndices);
    }
    else
      Trace_Write("    [d3d] indices from %u: not readable (pool %u usage 0x%x)", nStartIndex, IBDesc.Pool, IBDesc.Usage);
    pIB->Release();
  }
  if (s_nLevel >= 2)
    D3DState_WriteDevice(pDev, s_nLevel >= 3);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitive(IDirect3DDevice9 *pDev, D3DPRIMITIVETYPE eType, UINT nStartVertex, UINT nPrims)
{
  char szCall[160];
  _snprintf_s(szCall, _TRUNCATE, "DrawPrimitive(%s start %u prims %u)", PrimName(eType), nStartVertex, nPrims);
  WriteDraw(pDev, szCall, true, false, nStartVertex, DrawVertexCount(eType, nPrims), NULL, 0, _ReturnAddress());
  return ((PFN_DrawPrimitive)s_pOrigVTable[DEV_DrawPrimitive])(pDev, eType, nStartVertex, nPrims);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitive(IDirect3DDevice9 *pDev, D3DPRIMITIVETYPE eType, INT nBaseVertex, UINT nMinIndex,
  UINT nVerts, UINT nStartIndex, UINT nPrims)
{
  char szCall[160];
  _snprintf_s(szCall, _TRUNCATE, "DrawIndexedPrimitive(%s base %d min %u verts %u start %u prims %u)", PrimName(eType), nBaseVertex, nMinIndex,
    nVerts, nStartIndex, nPrims);
  WriteDraw(pDev, szCall, true, true, nStartIndex, 0, NULL, 0, _ReturnAddress());
  return ((PFN_DrawIndexedPrimitive)s_pOrigVTable[DEV_DrawIndexedPrimitive])(pDev, eType, nBaseVertex, nMinIndex, nVerts, nStartIndex, nPrims);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitiveUP(IDirect3DDevice9 *pDev, D3DPRIMITIVETYPE eType, UINT nPrims, const void *pVerts,
  UINT nStride)
{
  char szCall[160];
  _snprintf_s(szCall, _TRUNCATE, "DrawPrimitiveUP(%s prims %u stride %u)", PrimName(eType), nPrims, nStride);
  WriteDraw(pDev, szCall, false, false, 0, DrawVertexCount(eType, nPrims), pVerts, nStride, _ReturnAddress());
  return ((PFN_DrawPrimitiveUP)s_pOrigVTable[DEV_DrawPrimitiveUP])(pDev, eType, nPrims, pVerts, nStride);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitiveUP(IDirect3DDevice9 *pDev, D3DPRIMITIVETYPE eType, UINT nMinIndex, UINT nVerts,
  UINT nPrims, const void *pIndices, D3DFORMAT eIndexFormat, const void *pVerts, UINT nStride)
{
  char szCall[160];
  _snprintf_s(szCall, _TRUNCATE, "DrawIndexedPrimitiveUP(%s min %u verts %u prims %u indices %s stride %u)", PrimName(eType), nMinIndex,
    nVerts, nPrims, eIndexFormat == D3DFMT_INDEX32 ? "32" : "16", nStride);
  WriteDraw(pDev, szCall, false, false, 0, 0, NULL, 0, _ReturnAddress());
  return ((PFN_DrawIndexedPrimitiveUP)s_pOrigVTable[DEV_DrawIndexedPrimitiveUP])(pDev, eType, nMinIndex, nVerts, nPrims, pIndices,
    eIndexFormat, pVerts, nStride);
}

void D3DDraws_Begin(IRenderer *pRenderer)
{
  if (s_pDevice)
    return;
  char szEnv[8] = "";
  if (!GetEnvironmentVariableA("XRENDERTRACED_D3D_DRAWS", szEnv, sizeof(szEnv)) || szEnv[0] < '1' || szEnv[0] > '9')
    return;
  s_nLevel = szEnv[0] - '0';
  IDirect3DDevice9 *pDev = D3DState_GetDevice(pRenderer);
  if (!pDev)
    return;
  void **pVTable = *(void ***)pDev;
  int nSlots = DEV_CopiedSlots;
  while (nSlots > DEV_Slots && !Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
    nSlots--;
  if (!Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
  {
    Trace_Write("d3d draws: the device vtable %p is not readable", pVTable);
    return;
  }
  memcpy(s_HookedVTable, pVTable, nSlots * sizeof(void *));
  s_HookedVTable[DEV_DrawPrimitive] = (void *)&Hook_DrawPrimitive;
  s_HookedVTable[DEV_DrawIndexedPrimitive] = (void *)&Hook_DrawIndexedPrimitive;
  s_HookedVTable[DEV_DrawPrimitiveUP] = (void *)&Hook_DrawPrimitiveUP;
  s_HookedVTable[DEV_DrawIndexedPrimitiveUP] = (void *)&Hook_DrawIndexedPrimitiveUP;
  s_pOrigVTable = pVTable;
  s_pDevice = pDev;
  s_nDraws = 0;
  *(void ***)pDev = s_HookedVTable;
  Trace_Write("==== Direct3D draws of this frame (device %p) ====", pDev);
}

void D3DDraws_OnDrawRE()
{
  if (!s_pDevice)
    return;
  char szLine[512] = "[d3d] mfDraw";
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s", szLine);
}

void D3DDraws_End()
{
  if (!s_pDevice)
    return;
  *(void ***)s_pDevice = s_pOrigVTable;
  s_pDevice = NULL;
  for (std::map<IUnknown *, SBufferShadow>::iterator it = s_Shadows.begin(); it != s_Shadows.end(); ++it)
  {
    *(void ***)it->first = it->second.m_pOrigVTable;
    it->first->Release();
  }
  s_Shadows.clear();
  Trace_Write("==== %d Direct3D draws ====", s_nDraws);
}
