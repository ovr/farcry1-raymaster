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
#include "Dpi.h"

//! Slots of IDirect3DDevice9 (declaration order in d3d9.h).
enum
{
  DEV_Present = 17,
  DEV_UpdateSurface = 30,
  DEV_GetRenderTargetData = 32,
  DEV_GetFrontBufferData = 33,
  DEV_StretchRect = 34,
  DEV_ColorFill = 35,
  DEV_SetRenderTarget = 37,
  DEV_SetDepthStencilSurface = 39,
  DEV_GetDepthStencilSurface = 40,
  DEV_Clear = 43,
  DEV_DrawPrimitive = 81,
  DEV_DrawIndexedPrimitive = 82,
  DEV_DrawPrimitiveUP = 83,
  DEV_DrawIndexedPrimitiveUP = 84,
  DEV_CreateQuery = 118,
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

//! Slots of IDirect3DSurface9.
enum
{
  SURF_LockRect = 13,
  SURF_Slots = 17,
  SURF_CopiedSlots = 256,
};

//! Vertices written per draw (XRENDERTRACED_D3D_VERTS, default 8).
static UINT s_nDumpVertices = 8;
//! XRENDERTRACED_D3D_VERTS is set: the indexed draws write their vertices too.
static bool s_bDumpIndexedVertices = false;

typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawPrimitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawIndexedPrimitive)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawPrimitiveUP)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_DrawIndexedPrimitiveUP)(IDirect3DDevice9 *, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT,
  const void *, UINT);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Clear)(IDirect3DDevice9 *, DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetRenderTarget)(IDirect3DDevice9 *, DWORD, IDirect3DSurface9 *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetDepthStencilSurface)(IDirect3DDevice9 *, IDirect3DSurface9 *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_UpdateSurface)(IDirect3DDevice9 *, IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *,
  const POINT *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_StretchRect)(IDirect3DDevice9 *, IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *,
  const RECT *, D3DTEXTUREFILTERTYPE);
typedef HRESULT (STDMETHODCALLTYPE *PFN_ColorFill)(IDirect3DDevice9 *, IDirect3DSurface9 *, const RECT *, D3DCOLOR);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetRenderTargetData)(IDirect3DDevice9 *, IDirect3DSurface9 *, IDirect3DSurface9 *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetFrontBufferData)(IDirect3DDevice9 *, UINT, IDirect3DSurface9 *);
typedef HRESULT (STDMETHODCALLTYPE *PFN_GetDepthStencilSurface)(IDirect3DDevice9 *, IDirect3DSurface9 **);
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateQuery)(IDirect3DDevice9 *, D3DQUERYTYPE, IDirect3DQuery9 **);
typedef HRESULT (STDMETHODCALLTYPE *PFN_LockRect)(IDirect3DSurface9 *, D3DLOCKED_RECT *, const RECT *, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_Present)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
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

//! Colour of a 565 value as B, G, R, A bytes.
static void Color565(WORD c, BYTE *pOut)
{
  pOut[0] = (BYTE)((c & 31) * 255 / 31);
  pOut[1] = (BYTE)(((c >> 5) & 63) * 255 / 63);
  pOut[2] = (BYTE)((c >> 11) * 255 / 31);
  pOut[3] = 255;
}

//! One 4x4 block of DXT1 (or the colour half of DXT3/DXT5) into B, G, R, A rows of nPitch bytes.
static void DecodeColorBlock(const BYTE *pBlock, bool bDXT1, BYTE *pOut, UINT nPitch)
{
  BYTE Colors[4][4];
  const WORD c0 = (WORD)(pBlock[0] | (pBlock[1] << 8)), c1 = (WORD)(pBlock[2] | (pBlock[3] << 8));
  Color565(c0, Colors[0]);
  Color565(c1, Colors[1]);
  for (int i = 0; i < 4; i++)
  {
    if (!bDXT1 || c0 > c1)
    {
      Colors[2][i] = (BYTE)((2 * Colors[0][i] + Colors[1][i]) / 3);
      Colors[3][i] = (BYTE)((Colors[0][i] + 2 * Colors[1][i]) / 3);
    }
    else
    {
      Colors[2][i] = (BYTE)((Colors[0][i] + Colors[1][i]) / 2);
      Colors[3][i] = 0;
    }
  }
  const DWORD nBits = pBlock[4] | (pBlock[5] << 8) | (pBlock[6] << 16) | ((DWORD)pBlock[7] << 24);
  for (int n = 0; n < 16; n++)
    memcpy(pOut + (n / 4) * nPitch + (n % 4) * 4, Colors[(nBits >> (2 * n)) & 3], 4);
}

//! Whether level 0 of a texture of this format can be saved.
static bool CanSaveFormat(D3DFORMAT eFormat)
{
  return eFormat == D3DFMT_DXT1 || eFormat == D3DFMT_DXT3 || eFormat == D3DFMT_DXT5 || eFormat == D3DFMT_X8R8G8B8 ||
    eFormat == D3DFMT_A8R8G8B8;
}

//! A locked level of one of the CanSaveFormat formats into B, G, R, A rows of Desc.Width * 4 bytes.
static void DecodeLevel(const D3DSURFACE_DESC &Desc, const D3DLOCKED_RECT &Rect, BYTE *pPixels)
{
  const BYTE *pSrc = (const BYTE *)Rect.pBits;
  if (Desc.Format == D3DFMT_X8R8G8B8 || Desc.Format == D3DFMT_A8R8G8B8)
  {
    for (UINT y = 0; y < Desc.Height; y++)
      memcpy(pPixels + y * Desc.Width * 4, pSrc + y * Rect.Pitch, Desc.Width * 4);
    return;
  }
  const UINT nBlockSize = Desc.Format == D3DFMT_DXT1 ? 8 : 16;
  for (UINT by = 0; by < Desc.Height; by += 4)
  {
    for (UINT bx = 0; bx < Desc.Width; bx += 4)
    {
      const BYTE *pBlock = pSrc + (by / 4) * Rect.Pitch + (bx / 4) * nBlockSize;
      BYTE *pOut = pPixels + (by * Desc.Width + bx) * 4;
      DecodeColorBlock(pBlock + nBlockSize - 8, Desc.Format == D3DFMT_DXT1, pOut, Desc.Width * 4);
      for (int n = 0; n < 16 && Desc.Format != D3DFMT_DXT1; n++)
      {
        BYTE nAlpha;
        if (Desc.Format == D3DFMT_DXT3)
          nAlpha = (BYTE)(((pBlock[n / 2] >> (4 * (n & 1))) & 15) * 17);
        else
        {
          const BYTE a0 = pBlock[0], a1 = pBlock[1];
          const int nBit = 16 + 3 * n;
          const int i = (int)(((pBlock[nBit / 8] | (pBlock[nBit / 8 + 1] << 8)) >> (nBit % 8)) & 7);
          if (i < 2)
            nAlpha = i ? a1 : a0;
          else if (a0 > a1)
            nAlpha = (BYTE)(((8 - i) * a0 + (i - 1) * a1) / 7);
          else if (i < 6)
            nAlpha = (BYTE)(((6 - i) * a0 + (i - 1) * a1) / 5);
          else
            nAlpha = i == 6 ? 0 : 255;
        }
        pOut[(n / 4) * Desc.Width * 4 + (n % 4) * 4 + 3] = nAlpha;
      }
    }
  }
}

//! Writes B, G, R, A pixels as an uncompressed 32-bit TGA file.
static bool WriteTGA(const char *szFile, UINT nWidth, UINT nHeight, const BYTE *pPixels)
{
  FILE *pFile = NULL;
  if (fopen_s(&pFile, szFile, "wb"))
    return false;
  const BYTE Header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (BYTE)nWidth, (BYTE)(nWidth >> 8), (BYTE)nHeight, (BYTE)(nHeight >> 8),
    32, 0x28 };
  fwrite(Header, 1, sizeof(Header), pFile);
  fwrite(pPixels, 4, nWidth * nHeight, pFile);
  fclose(pFile);
  return true;
}

//! XRENDERTRACED_D3D_CUBESAVE=prefix: the first time a draw of the captured frame uses a cube
//! texture, level 0 of each face is saved as an uncompressed 32-bit TGA file
//! "<prefix><texture>_<face>.tga" (faces in D3DCUBEMAP_FACES order: +X, -X, +Y, -Y, +Z, -Z).
static void SaveCubeFaces(IDirect3DCubeTexture9 *pCube, const D3DSURFACE_DESC &Desc)
{
  char szPrefix[MAX_PATH] = "";
  if (!GetEnvironmentVariableA("XRENDERTRACED_D3D_CUBESAVE", szPrefix, sizeof(szPrefix)) || !szPrefix[0])
    return;
  if (!CanSaveFormat(Desc.Format))
  {
    Trace_Write("    [d3d] cube %p: format %u is not saved", pCube, Desc.Format);
    return;
  }
  BYTE *pPixels = (BYTE *)malloc(Desc.Width * Desc.Height * 4);
  for (int nFace = 0; nFace < 6 && pPixels; nFace++)
  {
    D3DLOCKED_RECT Rect = {};
    if (FAILED(pCube->LockRect((D3DCUBEMAP_FACES)nFace, 0, &Rect, NULL, D3DLOCK_READONLY)))
    {
      Trace_Write("    [d3d] cube %p face %d: LockRect failed", pCube, nFace);
      continue;
    }
    DecodeLevel(Desc, Rect, pPixels);
    pCube->UnlockRect((D3DCUBEMAP_FACES)nFace, 0);
    char szFile[MAX_PATH + 32];
    sprintf_s(szFile, "%s%p_%d.tga", szPrefix, pCube, nFace);
    if (WriteTGA(szFile, Desc.Width, Desc.Height, pPixels))
      Trace_Write("    [d3d] cube %p face %d saved to '%s'", pCube, nFace, szFile);
  }
  free(pPixels);
}

//! XRENDERTRACED_D3D_TEXSAVE=prefix: the first time a draw of the captured frame uses a 2D
//! texture no wider or higher than XRENDERTRACED_D3D_TEXSAVE_MAX (default 256), level 0 is saved
//! as an uncompressed 32-bit TGA file "<prefix><texture>.tga".
static void SaveTexture(IDirect3DTexture9 *pTex, const D3DSURFACE_DESC &Desc)
{
  char szPrefix[MAX_PATH] = "", szMax[16] = "";
  if (!GetEnvironmentVariableA("XRENDERTRACED_D3D_TEXSAVE", szPrefix, sizeof(szPrefix)) || !szPrefix[0])
    return;
  const UINT nMax = GetEnvironmentVariableA("XRENDERTRACED_D3D_TEXSAVE_MAX", szMax, sizeof(szMax)) ? (UINT)atoi(szMax) : 256;
  if (Desc.Width > nMax || Desc.Height > nMax || !CanSaveFormat(Desc.Format) || Desc.Pool == D3DPOOL_DEFAULT)
    return;
  D3DLOCKED_RECT Rect = {};
  if (FAILED(pTex->LockRect(0, &Rect, NULL, D3DLOCK_READONLY)))
  {
    Trace_Write("    [d3d] tex %p: LockRect failed", pTex);
    return;
  }
  BYTE *pPixels = (BYTE *)malloc(Desc.Width * Desc.Height * 4);
  if (pPixels)
    DecodeLevel(Desc, Rect, pPixels);
  pTex->UnlockRect(0);
  char szFile[MAX_PATH + 32];
  sprintf_s(szFile, "%s%p.tga", szPrefix, pTex);
  if (pPixels && WriteTGA(szFile, Desc.Width, Desc.Height, pPixels))
    Trace_Write("    [d3d] tex %p saved to '%s'", pTex, szFile);
  free(pPixels);
}

static void DescribeTexture(IDirect3DBaseTexture9 *pTex)
{
  D3DRESOURCETYPE eType = pTex->GetType();
  D3DSURFACE_DESC Desc = {};
  if (eType == D3DRTYPE_TEXTURE && SUCCEEDED(((IDirect3DTexture9 *)pTex)->GetLevelDesc(0, &Desc)))
  {
    Trace_Write("    [d3d] tex %p: %ux%u format %u levels %u pool %u usage 0x%x", pTex, Desc.Width, Desc.Height, Desc.Format,
      pTex->GetLevelCount(), Desc.Pool, Desc.Usage);
    SaveTexture((IDirect3DTexture9 *)pTex, Desc);
  }
  else if (eType == D3DRTYPE_CUBETEXTURE && SUCCEEDED(((IDirect3DCubeTexture9 *)pTex)->GetLevelDesc(0, &Desc)))
  {
    Trace_Write("    [d3d] tex %p: cube %u format %u levels %u pool %u usage 0x%x", pTex, Desc.Width, Desc.Format, pTex->GetLevelCount(),
      Desc.Pool, Desc.Usage);
    SaveCubeFaces((IDirect3DCubeTexture9 *)pTex, Desc);
  }
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

//! The dynamic vertex buffers of earlier captured frames: a later captured frame hooks them from
//! its start, so that what is written into them before its first draw is known (the flares'
//! visibility quads are drawn first). The references are never released, so a device Reset after
//! a capture would fail.
static std::vector<std::pair<IUnknown *, UINT> > s_KnownDynamicVBs;

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
  for (UINT nVert = 0; nVert < nCount && nVert < s_nDumpVertices; nVert++)
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

//! The box of the positions (FLOAT3 POSITION0 of stream 0) of the vertices an indexed draw may use,
//! when the buffer can be read back: the extent of the object in the space its vertices are in.
static void WriteVertexBox(IDirect3DVertexDeclaration9 *pDecl, IUnknown *pVB0, UINT nOffset, UINT nStride, UINT nFirst, UINT nCount)
{
  D3DVERTEXELEMENT9 Elements[MAXD3DDECLLENGTH + 1];
  UINT nElements = MAXD3DDECLLENGTH + 1;
  if (!pDecl || !pVB0 || !nStride || !nCount || FAILED(pDecl->GetDeclaration(Elements, &nElements)))
    return;
  int nPos = -1;
  for (UINT i = 0; i < nElements && Elements[i].Stream != 0xFF; i++)
  {
    if (Elements[i].Stream == 0 && Elements[i].Usage == D3DDECLUSAGE_POSITION && Elements[i].UsageIndex == 0
      && Elements[i].Type == D3DDECLTYPE_FLOAT3)
      nPos = Elements[i].Offset;
  }
  IDirect3DVertexBuffer9 *pVB = (IDirect3DVertexBuffer9 *)pVB0;
  D3DVERTEXBUFFER_DESC Desc = {};
  pVB->GetDesc(&Desc);
  const UINT nStart = nOffset + nFirst * nStride, nSize = nCount * nStride;
  if (nPos < 0 || nStart + nSize > Desc.Size)
    return;
  void *pData = NULL;
  const BYTE *pShadow = ShadowData(pVB0, nStart, nSize);
  if (!pShadow && ((Desc.Pool == D3DPOOL_DEFAULT && (Desc.Usage & D3DUSAGE_WRITEONLY))
    || FAILED(pVB->Lock(nStart, nSize, &pData, D3DLOCK_READONLY))))
    return;
  const BYTE *pVerts = pShadow ? pShadow : (const BYTE *)pData;
  float fMin[3] = { 1e30f, 1e30f, 1e30f }, fMax[3] = { -1e30f, -1e30f, -1e30f };
  for (UINT v = 0; v < nCount; v++)
  {
    const float *pPos = (const float *)(pVerts + v * nStride + nPos);
    for (int c = 0; c < 3; c++)
    {
      fMin[c] = min(fMin[c], pPos[c]);
      fMax[c] = max(fMax[c], pPos[c]);
    }
  }
  if (!pShadow)
    pVB->Unlock();
  Trace_Write("    [d3d] box (%g %g %g)-(%g %g %g) of %u vertices", fMin[0], fMin[1], fMin[2], fMax[0], fMax[1], fMax[2], nCount);
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
  if (!pShader)
    return;
  Append(szBuf, nSize, " shader '%s'", pShader->GetName());
  // The template the renderer picked for the material (with nTempl -2 the one it draws with).
  IShader *pTempl = pShader->GetTemplate(-1);
  if (pTempl && pTempl != pShader)
    Append(szBuf, nSize, " templ '%s'", pTempl->GetName());
}

//! Writes the line of a draw call (before it is made), the objects it uses for the first time and
//! the first vertices of a non-indexed draw (nStart is its first vertex, the first index of an
//! indexed draw; pUPVerts the vertices of a *UP draw).
static void WriteDraw(IDirect3DDevice9 *pDev, const char *szCall, bool bStreams, bool bIndexed, UINT nStart, UINT nVerts,
  const void *pUPVerts, UINT nUPStride, void *pCaller, UINT nBoxFirst = 0, UINT nBoxCount = 0)
{
  const UINT nStartIndex = bIndexed ? nStart : 0;
  IUnknown *pVB0 = NULL;
  UINT nOffset0 = 0, nStride0 = 0;
  ++s_nDraws;
  char szLine[8192];
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
      {
        HookBuffer(pVB, Desc.Size);
        bool bKnown = false;
        for (size_t i = 0; i < s_KnownDynamicVBs.size() && !bKnown; i++)
          bKnown = s_KnownDynamicVBs[i].first == pVB;
        if (!bKnown)
        {
          pVB->AddRef();
          s_KnownDynamicVBs.push_back(std::make_pair((IUnknown *)pVB, Desc.Size));
        }
      }
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
    const UINT nDumped = nVerts < s_nDumpVertices ? nVerts : s_nDumpVertices;
    if (!bIndexed && pUPVerts)
      DumpVertices(pDecl, 0, (const BYTE *)pUPVerts, nUPStride, nDumped);
    else if (!bIndexed && pVB0 && nStride0)
      DumpVertices(pDecl, 0, ShadowData(pVB0, nOffset0 + nStart * nStride0, nDumped * nStride0), nStride0, nDumped);
    // With XRENDERTRACED_D3D_VERTS set, also the first vertices of the range an indexed draw uses.
    else if (bIndexed && s_bDumpIndexedVertices && pVB0 && nStride0 && nBoxCount)
    {
      const UINT nRange = nBoxCount < s_nDumpVertices ? nBoxCount : s_nDumpVertices;
      DumpVertices(pDecl, 0, ShadowData(pVB0, nOffset0 + nBoxFirst * nStride0, nRange * nStride0), nStride0, nRange);
    }
    if (bIndexed && s_nLevel >= 2)
      WriteVertexBox(pDecl, pVB0, nOffset0, nStride0, nBoxFirst, nBoxCount);
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
  WriteDraw(pDev, szCall, true, true, nStartIndex, 0, NULL, 0, _ReturnAddress(), (UINT)(nBaseVertex + (INT)nMinIndex), nVerts);
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

static HRESULT STDMETHODCALLTYPE Hook_Clear(IDirect3DDevice9 *pDev, DWORD nRects, const D3DRECT *pRects, DWORD nFlags, D3DCOLOR nColor,
  float fZ, DWORD nStencil)
{
  char szLine[512] = "";
  _snprintf_s(szLine, _TRUNCATE, "[d3d] Clear(rects %u flags 0x%x colour 0x%08x z %g stencil %u)", nRects, nFlags, nColor, fZ, nStencil);
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s", szLine);
  return ((PFN_Clear)s_pOrigVTable[DEV_Clear])(pDev, nRects, pRects, nFlags, nColor, fZ, nStencil);
}

//! "back buffer", or the surface's size and format with the texture it belongs to (as the draws write it).
static const char *SurfaceName(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSurf, char *szBuf, size_t nBuf)
{
  if (!pSurf)
    return "null";
  IDirect3DSurface9 *pBack = NULL;
  pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBack);
  if (pBack)
    pBack->Release();
  D3DSURFACE_DESC Desc = {};
  pSurf->GetDesc(&Desc);
  IDirect3DTexture9 *pTex = NULL;
  pSurf->GetContainer(__uuidof(IDirect3DTexture9), (void **)&pTex);
  if (pTex)
    pTex->Release();
  _snprintf_s(szBuf, nBuf, _TRUNCATE, "%s%p %ux%u format %u usage 0x%x tex %p", pSurf == pBack ? "back buffer " : "", pSurf, Desc.Width,
    Desc.Height, Desc.Format, Desc.Usage, pTex);
  return szBuf;
}

static const char *RectName(const RECT *pRect, char *szBuf, size_t nBuf)
{
  if (!pRect)
    return "all";
  _snprintf_s(szBuf, nBuf, _TRUNCATE, "(%ld %ld)-(%ld %ld)", pRect->left, pRect->top, pRect->right, pRect->bottom);
  return szBuf;
}

static HRESULT STDMETHODCALLTYPE Hook_DrawsSetRenderTarget(IDirect3DDevice9 *pDev, DWORD nIndex, IDirect3DSurface9 *pSurf)
{
  char szSurf[160];
  Trace_Write("[d3d] SetRenderTarget(%u, %s)", nIndex, SurfaceName(pDev, pSurf, szSurf, sizeof(szSurf)));
  return ((PFN_SetRenderTarget)s_pOrigVTable[DEV_SetRenderTarget])(pDev, nIndex, pSurf);
}

static HRESULT STDMETHODCALLTYPE Hook_SetDepthStencilSurface(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSurf)
{
  char szSurf[160];
  Trace_Write("[d3d] SetDepthStencilSurface(%s)", SurfaceName(pDev, pSurf, szSurf, sizeof(szSurf)));
  return ((PFN_SetDepthStencilSurface)s_pOrigVTable[DEV_SetDepthStencilSurface])(pDev, pSurf);
}

static HRESULT STDMETHODCALLTYPE Hook_UpdateSurface(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSrc, const RECT *pSrcRect,
  IDirect3DSurface9 *pDst, const POINT *pDstPoint)
{
  char szSrc[160], szDst[160], szRect[64];
  Trace_Write("[d3d] UpdateSurface(%s rect %s -> %s at (%ld %ld))", SurfaceName(pDev, pSrc, szSrc, sizeof(szSrc)),
    RectName(pSrcRect, szRect, sizeof(szRect)), SurfaceName(pDev, pDst, szDst, sizeof(szDst)), pDstPoint ? pDstPoint->x : 0,
    pDstPoint ? pDstPoint->y : 0);
  return ((PFN_UpdateSurface)s_pOrigVTable[DEV_UpdateSurface])(pDev, pSrc, pSrcRect, pDst, pDstPoint);
}

static HRESULT STDMETHODCALLTYPE Hook_StretchRect(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSrc, const RECT *pSrcRect,
  IDirect3DSurface9 *pDst, const RECT *pDstRect, D3DTEXTUREFILTERTYPE eFilter)
{
  char szSrc[160], szDst[160], szRect1[64], szRect2[64];
  Trace_Write("[d3d] StretchRect(%s rect %s -> %s rect %s, filter %d)", SurfaceName(pDev, pSrc, szSrc, sizeof(szSrc)),
    RectName(pSrcRect, szRect1, sizeof(szRect1)), SurfaceName(pDev, pDst, szDst, sizeof(szDst)), RectName(pDstRect, szRect2, sizeof(szRect2)),
    eFilter);
  return ((PFN_StretchRect)s_pOrigVTable[DEV_StretchRect])(pDev, pSrc, pSrcRect, pDst, pDstRect, eFilter);
}

static HRESULT STDMETHODCALLTYPE Hook_ColorFill(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSurf, const RECT *pRect, D3DCOLOR nColor)
{
  char szSurf[160], szRect[64];
  Trace_Write("[d3d] ColorFill(%s rect %s colour 0x%08x)", SurfaceName(pDev, pSurf, szSurf, sizeof(szSurf)),
    RectName(pRect, szRect, sizeof(szRect)), nColor);
  return ((PFN_ColorFill)s_pOrigVTable[DEV_ColorFill])(pDev, pSurf, pRect, nColor);
}

//! Read backs and queries (how the renderer learns what was drawn, e.g. whether a flare is hidden).
static HRESULT STDMETHODCALLTYPE Hook_GetRenderTargetData(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSrc, IDirect3DSurface9 *pDst)
{
  char szSrc[160], szDst[160], szLine[512];
  _snprintf_s(szLine, _TRUNCATE, "[d3d] GetRenderTargetData(%s -> %s)", SurfaceName(pDev, pSrc, szSrc, sizeof(szSrc)),
    SurfaceName(pDev, pDst, szDst, sizeof(szDst)));
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s", szLine);
  return ((PFN_GetRenderTargetData)s_pOrigVTable[DEV_GetRenderTargetData])(pDev, pSrc, pDst);
}

static HRESULT STDMETHODCALLTYPE Hook_GetFrontBufferData(IDirect3DDevice9 *pDev, UINT nSwapChain, IDirect3DSurface9 *pDst)
{
  char szDst[160], szLine[512];
  _snprintf_s(szLine, _TRUNCATE, "[d3d] GetFrontBufferData(%u -> %s)", nSwapChain, SurfaceName(pDev, pDst, szDst, sizeof(szDst)));
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s", szLine);
  return ((PFN_GetFrontBufferData)s_pOrigVTable[DEV_GetFrontBufferData])(pDev, nSwapChain, pDst);
}

static HRESULT STDMETHODCALLTYPE Hook_GetDepthStencilSurface(IDirect3DDevice9 *pDev, IDirect3DSurface9 **ppSurf)
{
  const HRESULT hr = ((PFN_GetDepthStencilSurface)s_pOrigVTable[DEV_GetDepthStencilSurface])(pDev, ppSurf);
  char szSurf[160] = "-", szLine[512];
  if (SUCCEEDED(hr) && ppSurf && *ppSurf)
    SurfaceName(pDev, *ppSurf, szSurf, sizeof(szSurf));
  _snprintf_s(szLine, _TRUNCATE, "[d3d] GetDepthStencilSurface() -> %s", szSurf);
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s", szLine);
  return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateQuery(IDirect3DDevice9 *pDev, D3DQUERYTYPE eType, IDirect3DQuery9 **ppQuery)
{
  const HRESULT hr = ((PFN_CreateQuery)s_pOrigVTable[DEV_CreateQuery])(pDev, eType, ppQuery);
  char szLine[512];
  _snprintf_s(szLine, _TRUNCATE, "[d3d] CreateQuery(type %d) -> %p", eType, ppQuery ? *ppQuery : NULL);
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s", szLine);
  return hr;
}

//! The depth buffer and the back buffer of the frame, each with LockRect hooked in a vtable copy of
//! its own (a read back of depths or pixels on the CPU).
struct SSurfaceHook
{
  IDirect3DSurface9 *m_pSurf;
  void **m_pOrigVTable;
  void *m_Slots[SURF_CopiedSlots];
  const char *m_szName;
};
static SSurfaceHook s_SurfaceHooks[2];
static int s_nSurfaceHooks = 0;

static HRESULT STDMETHODCALLTYPE Hook_SurfaceLockRect(IDirect3DSurface9 *pSurf, D3DLOCKED_RECT *pLocked, const RECT *pRect, DWORD nFlags)
{
  const SSurfaceHook *pHook = &s_SurfaceHooks[0];
  for (int i = 0; i < s_nSurfaceHooks; i++)
    if (s_SurfaceHooks[i].m_pSurf == pSurf)
      pHook = &s_SurfaceHooks[i];
  char szRect[64], szLine[512];
  _snprintf_s(szLine, _TRUNCATE, "[d3d] LockRect(%s rect %s flags 0x%x)", pHook->m_szName, RectName(pRect, szRect, sizeof(szRect)), nFlags);
  AppendRE(szLine, sizeof(szLine));
  Trace_Write("%s | from %s", szLine, Trace_Addr(_ReturnAddress()));
  return ((PFN_LockRect)pHook->m_pOrigVTable[SURF_LockRect])(pSurf, pLocked, pRect, nFlags);
}

static void HookSurface(IDirect3DDevice9 *pDev, IDirect3DSurface9 *pSurf, const char *szName)
{
  if (!pSurf || s_nSurfaceHooks == sizeof(s_SurfaceHooks) / sizeof(s_SurfaceHooks[0]))
  {
    if (pSurf)
      pSurf->Release();
    return;
  }
  char szSurf[160];
  Trace_Write("[d3d] %s: %s", szName, SurfaceName(pDev, pSurf, szSurf, sizeof(szSurf)));
  void **pVTable = *(void ***)pSurf;
  int nSlots = SURF_CopiedSlots;
  while (nSlots > SURF_Slots && !Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
    nSlots--;
  if (!Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
  {
    pSurf->Release();
    return;
  }
  SSurfaceHook &Hook = s_SurfaceHooks[s_nSurfaceHooks++];
  memset(Hook.m_Slots, 0, sizeof(Hook.m_Slots));
  memcpy(Hook.m_Slots, pVTable, nSlots * sizeof(void *));
  Hook.m_Slots[SURF_LockRect] = (void *)&Hook_SurfaceLockRect;
  Hook.m_pOrigVTable = pVTable;
  Hook.m_pSurf = pSurf;
  Hook.m_szName = szName;
  *(void ***)pSurf = Hook.m_Slots;
}

static HRESULT STDMETHODCALLTYPE Hook_Present(IDirect3DDevice9 *pDev, const RECT *pSrcRect, const RECT *pDstRect, HWND hWnd,
  const RGNDATA *pDirty)
{
  char szSrc[64], szDst[64];
  const HRESULT hr = ((PFN_Present)s_pOrigVTable[DEV_Present])(pDev, pSrcRect, pDstRect, hWnd, pDirty);
  Trace_Write("[d3d] Present(src %s dst %s window %p) -> 0x%08x", RectName(pSrcRect, szSrc, sizeof(szSrc)),
    RectName(pDstRect, szDst, sizeof(szDst)), hWnd, hr);
  return hr;
}

void D3DDraws_Begin(IRenderer *pRenderer)
{
  if (s_pDevice)
    return;
  char szEnv[8] = "";
  if (!GetEnvironmentVariableA("XRENDERTRACED_D3D_DRAWS", szEnv, sizeof(szEnv)) || szEnv[0] < '1' || szEnv[0] > '9')
    return;
  s_nLevel = szEnv[0] - '0';
  char szVerts[16] = "";
  if (GetEnvironmentVariableA("XRENDERTRACED_D3D_VERTS", szVerts, sizeof(szVerts)) && atoi(szVerts) > 0)
  {
    s_nDumpVertices = (UINT)atoi(szVerts);
    s_bDumpIndexedVertices = true;
  }
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
  s_HookedVTable[DEV_Clear] = (void *)&Hook_Clear;
  s_HookedVTable[DEV_SetRenderTarget] = (void *)&Hook_DrawsSetRenderTarget;
  s_HookedVTable[DEV_SetDepthStencilSurface] = (void *)&Hook_SetDepthStencilSurface;
  s_HookedVTable[DEV_UpdateSurface] = (void *)&Hook_UpdateSurface;
  s_HookedVTable[DEV_StretchRect] = (void *)&Hook_StretchRect;
  s_HookedVTable[DEV_ColorFill] = (void *)&Hook_ColorFill;
  s_HookedVTable[DEV_GetRenderTargetData] = (void *)&Hook_GetRenderTargetData;
  s_HookedVTable[DEV_GetFrontBufferData] = (void *)&Hook_GetFrontBufferData;
  s_HookedVTable[DEV_GetDepthStencilSurface] = (void *)&Hook_GetDepthStencilSurface;
  s_HookedVTable[DEV_CreateQuery] = (void *)&Hook_CreateQuery;
  s_HookedVTable[DEV_Present] = (void *)&Hook_Present;
  s_HookedVTable[DEV_DrawPrimitive] = (void *)&Hook_DrawPrimitive;
  s_HookedVTable[DEV_DrawIndexedPrimitive] = (void *)&Hook_DrawIndexedPrimitive;
  s_HookedVTable[DEV_DrawPrimitiveUP] = (void *)&Hook_DrawPrimitiveUP;
  s_HookedVTable[DEV_DrawIndexedPrimitiveUP] = (void *)&Hook_DrawIndexedPrimitiveUP;
  s_pOrigVTable = pVTable;
  s_pDevice = pDev;
  s_nDraws = 0;
  *(void ***)pDev = s_HookedVTable;
  // The timer time, for the animated texture transforms of the frame.
  Trace_Write("==== Direct3D draws of this frame (device %p, time %.4f) ====", pDev, iTimer ? iTimer->GetCurrTime() : 0.0f);
  for (size_t i = 0; i < s_KnownDynamicVBs.size(); i++)
    HookBuffer(s_KnownDynamicVBs[i].first, s_KnownDynamicVBs[i].second);
  IDirect3DSurface9 *pSurf = NULL;
  if (SUCCEEDED(((PFN_GetDepthStencilSurface)s_pOrigVTable[DEV_GetDepthStencilSurface])(pDev, &pSurf)))
    HookSurface(pDev, pSurf, "depth buffer");
  pSurf = NULL;
  if (SUCCEEDED(pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pSurf)))
    HookSurface(pDev, pSurf, "back buffer");
}

//! XRENDERTRACED_D3D_RT: the device keeps a vtable copy with SetRenderTarget hooked in every frame.
static void **s_pRTOrigVTable = NULL;
static void *s_RTVTable[DEV_CopiedSlots];
static int s_nRTFrame = 0, s_nRTLastFrame = -1, s_nRTLines = 0;
static bool s_bRTLines = false;
//! XRENDERTRACED_CAPTURE_RT: frames after the level load (-1: off), the frame of the load (-1: none
//! yet), and whether this level's capture has been made.
static int s_nRTCaptureAfter = -1, s_nRTLoadFrame = -1;
static bool s_bRTCaptured = false;
static IRenderer *s_pRTRenderer = NULL;
//! The renderer of the current frame, for the captures that start inside it.
static IRenderer *s_pFrameRenderer = NULL;

static HRESULT STDMETHODCALLTYPE Hook_SetRenderTarget(IDirect3DDevice9 *pDev, DWORD nIndex, IDirect3DSurface9 *pSurf)
{
  const bool bLine = s_bRTLines && s_nRTFrame != s_nRTLastFrame && s_nRTLines < 64;
  const bool bCapture = s_nRTCaptureAfter >= 0 && s_nRTLoadFrame >= 0 && !s_bRTCaptured
    && s_nRTFrame >= s_nRTLoadFrame + s_nRTCaptureAfter && !Trace_IsCapturing();
  if (pSurf && (bLine || bCapture))
  {
    IDirect3DSurface9 *pBack = NULL;
    pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBack);
    D3DSURFACE_DESC Desc;
    if (pSurf != pBack && SUCCEEDED(pSurf->GetDesc(&Desc)))
    {
      if (bLine)
      {
        Trace_Write("[d3d rt] frame %d: SetRenderTarget(%u, %ux%u format %u usage 0x%x)", s_nRTFrame, nIndex, Desc.Width,
          Desc.Height, Desc.Format, Desc.Usage);
        s_nRTLastFrame = s_nRTFrame;
        s_nRTLines++;
      }
      if (bCapture)
      {
        s_bRTCaptured = true;
        char szReason[128];
        _snprintf_s(szReason, sizeof(szReason), _TRUNCATE, "capture of the rest of frame %d from SetRenderTarget(%u, %ux%u format %u)",
          s_nRTFrame, nIndex, Desc.Width, Desc.Height, Desc.Format);
        Trace_CaptureFrame(szReason);
        D3DDraws_Begin(s_pRTRenderer);
      }
    }
    if (pBack)
      pBack->Release();
  }
  return ((PFN_SetRenderTarget)s_pRTOrigVTable[DEV_SetRenderTarget])(pDev, nIndex, pSurf);
}

//! XRENDERTRACED_D3D_VSREG=N: the vertex shader constant register N at the indexed draws.
static int s_nVSReg = -1;
static int s_nVSRegLines = 0;
static float s_LastVSReg[4];
static const int nMaxVSRegLines = 60000;

static HRESULT STDMETHODCALLTYPE Hook_VSRegDrawIndexedPrimitive(IDirect3DDevice9 *pDev, D3DPRIMITIVETYPE eType, INT nBaseVertex,
  UINT nMinIndex, UINT nVerts, UINT nStartIndex, UINT nPrims)
{
  IDirect3DVertexShader9 *pVS = NULL;
  if (s_nVSRegLines < nMaxVSRegLines && SUCCEEDED(pDev->GetVertexShader(&pVS)) && pVS)
  {
    float fReg[4];
    if (SUCCEEDED(pDev->GetVertexShaderConstantF(s_nVSReg, fReg, 1)) && memcmp(fReg, s_LastVSReg, sizeof(fReg)))
    {
      memcpy(s_LastVSReg, fReg, sizeof(fReg));
      Trace_Write("[d3d vsreg] frame %d time %.4f: c%d (%g %g %g %g) vs %p prims %u", s_nRTFrame, iTimer ? iTimer->GetCurrTime() : 0.0f,
        s_nVSReg, fReg[0], fReg[1], fReg[2], fReg[3], pVS, nPrims);
      s_nVSRegLines++;
    }
    pVS->Release();
  }
  return ((PFN_DrawIndexedPrimitive)s_pRTOrigVTable[DEV_DrawIndexedPrimitive])(pDev, eType, nBaseVertex, nMinIndex, nVerts, nStartIndex,
    nPrims);
}

void D3DDraws_WatchRenderTargets(IRenderer *pRenderer, int nFrame)
{
  s_nRTFrame = nFrame;
  s_pFrameRenderer = pRenderer;
  if (s_pRTOrigVTable || s_nRTLines < 0)
    return;
  char szEnv[8] = "";
  s_bRTLines = GetEnvironmentVariableA("XRENDERTRACED_D3D_RT", szEnv, sizeof(szEnv)) && szEnv[0] == '1';
  if (GetEnvironmentVariableA("XRENDERTRACED_CAPTURE_RT", szEnv, sizeof(szEnv)) && szEnv[0] >= '0' && szEnv[0] <= '9')
    s_nRTCaptureAfter = atoi(szEnv);
  const bool bRT = s_bRTLines || s_nRTCaptureAfter >= 0;
  if (GetEnvironmentVariableA("XRENDERTRACED_D3D_VSREG", szEnv, sizeof(szEnv)) && szEnv[0] >= '0' && szEnv[0] <= '9')
    s_nVSReg = atoi(szEnv);
  IDirect3DDevice9 *pDev = NULL;
  if ((!bRT && s_nVSReg < 0) || !(pDev = D3DState_GetDevice(pRenderer)))
  {
    s_nRTLines = -1;
    return;
  }
  void **pVTable = *(void ***)pDev;
  int nSlots = DEV_CopiedSlots;
  while (nSlots > DEV_Slots && !Trace_IsReadable(pVTable, nSlots * sizeof(void *)))
    nSlots--;
  memcpy(s_RTVTable, pVTable, nSlots * sizeof(void *));
  if (bRT)
    s_RTVTable[DEV_SetRenderTarget] = (void *)&Hook_SetRenderTarget;
  if (s_nVSReg >= 0)
    s_RTVTable[DEV_DrawIndexedPrimitive] = (void *)&Hook_VSRegDrawIndexedPrimitive;
  s_pRTOrigVTable = pVTable;
  s_pRTRenderer = pRenderer;
  *(void ***)pDev = s_RTVTable;
  if (bRT)
    Trace_Write("d3d render targets: watching SetRenderTarget of device %p", pDev);
  if (s_nRTCaptureAfter >= 0)
    Trace_Write("d3d render targets: capturing the rest of the first frame that sets one, %d frames after each level load",
      s_nRTCaptureAfter);
  if (s_nVSReg >= 0)
    Trace_Write("d3d vs register: c%d at the indexed draws of device %p", s_nVSReg, pDev);
}

void D3DDraws_CaptureOnShader(IShader *pShader, const char *szWhere)
{
  static int s_nMax = -1;
  static int s_nDone = 0;
  if (s_nMax < 0)
  {
    char szEnv[8] = "";
    s_nMax = GetEnvironmentVariableA("XRENDERTRACED_CAPTURE_SHADER", szEnv, sizeof(szEnv)) ? 1 : 0;
    if (s_nMax && GetEnvironmentVariableA("XRENDERTRACED_CAPTURE_SHADER_MAX", szEnv, sizeof(szEnv)) && szEnv[0] >= '0' && szEnv[0] <= '9')
      s_nMax = atoi(szEnv);
  }
  if (s_nDone >= s_nMax || !pShader || !s_pFrameRenderer || Trace_IsCapturing())
    return;
  const char *szName = pShader->GetName();
  const char *szHit = szName && Trace_IsListed("XRENDERTRACED_CAPTURE_SHADER", szName) ? szName : NULL;
  if (!szHit)
  {
    IShader *pTempl = pShader->GetTemplate(-1);
    const char *szTempl = pTempl ? pTempl->GetName() : NULL;
    szHit = szTempl && Trace_IsListed("XRENDERTRACED_CAPTURE_SHADER", szTempl) ? szTempl : NULL;
  }
  if (!szHit)
    return;
  s_nDone++;
  char szReason[192];
  _snprintf_s(szReason, sizeof(szReason), _TRUNCATE, "capture of the rest of frame %d from %s with shader '%s' ('%s')", s_nRTFrame,
    szWhere, szName ? szName : "", szHit);
  Trace_CaptureFrame(szReason);
  D3DDraws_Begin(s_pFrameRenderer);
}

void D3DDraws_WatchDisplay(IRenderer *pRenderer, int nFrame)
{
  static int s_nEnabled = -1;
  if (s_nEnabled < 0)
  {
    char szEnv[8] = "";
    s_nEnabled = GetEnvironmentVariableA("XRENDERTRACED_DISPLAY", szEnv, sizeof(szEnv)) && szEnv[0] == '1';
  }
  if (!s_nEnabled)
    return;
  // Not D3DState_GetDevice: it keeps the first device, and a watcher may have replaced its vtable.
  IDirect3DDevice9 *pDev = (IDirect3DDevice9 *)pRenderer->EF_Query(EFQ_D3DDevice);
  if (!pDev || !Trace_IsReadable(pDev, sizeof(void *)))
    return;
  D3DPRESENT_PARAMETERS PP;
  memset(&PP, 0, sizeof(PP));
  IDirect3DSwapChain9 *pSC = NULL;
  if (SUCCEEDED(pDev->GetSwapChain(0, &pSC)) && pSC)
  {
    pSC->GetPresentParameters(&PP);
    pSC->Release();
  }
  RECT Client = { 0, 0, 0, 0 };
  HWND hWnd = (HWND)pRenderer->GetHWND();
  if (hWnd)
    GetClientRect(hWnd, &Client);
  char szDpi[64];
  Dpi_Describe(hWnd, szDpi, sizeof(szDpi));
  char szLine[512];
  _snprintf_s(szLine, _TRUNCATE, "device %p back buffer %ux%u format %d count %u windowed %d refresh %u swap %d interval 0x%x "
    "depth %d format %d; window %p client %dx%d, %s; GetWidth %d GetHeight %d", pDev, PP.BackBufferWidth, PP.BackBufferHeight,
    (int)PP.BackBufferFormat, PP.BackBufferCount, PP.Windowed, PP.FullScreen_RefreshRateInHz, (int)PP.SwapEffect,
    PP.PresentationInterval, PP.EnableAutoDepthStencil, (int)PP.AutoDepthStencilFormat, hWnd, Client.right - Client.left,
    Client.bottom - Client.top, szDpi, pRenderer->GetWidth(), pRenderer->GetHeight());
  static char s_szLast[512] = "";
  if (strcmp(szLine, s_szLast))
  {
    strcpy_s(s_szLast, szLine);
    Trace_Write("[display] frame %d: %s", nFrame, szLine);
  }
}

void D3DDraws_SaveBackBuffer(IRenderer *pRenderer, const char *szFile)
{
  IDirect3DDevice9 *pDev = D3DState_GetDevice(pRenderer);
  IDirect3DSurface9 *pBack = NULL, *pCopy = NULL;
  D3DSURFACE_DESC Desc = {};
  D3DLOCKED_RECT Rect = {};
  if (!pDev || FAILED(pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBack)))
    return;
  pBack->GetDesc(&Desc);
  HRESULT hr = E_FAIL;
  if ((Desc.Format == D3DFMT_X8R8G8B8 || Desc.Format == D3DFMT_A8R8G8B8) && Desc.MultiSampleType == D3DMULTISAMPLE_NONE
    && SUCCEEDED(pDev->CreateOffscreenPlainSurface(Desc.Width, Desc.Height, Desc.Format, D3DPOOL_SYSTEMMEM, &pCopy, NULL))
    && SUCCEEDED(pDev->GetRenderTargetData(pBack, pCopy)) && SUCCEEDED(pCopy->LockRect(&Rect, NULL, D3DLOCK_READONLY)))
  {
    FILE *pFile = NULL;
    if (!fopen_s(&pFile, szFile, "wb"))
    {
      // Uncompressed true colour, 32 bits, rows from the top.
      const BYTE Header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (BYTE)Desc.Width, (BYTE)(Desc.Width >> 8), (BYTE)Desc.Height,
        (BYTE)(Desc.Height >> 8), 32, 0x28 };
      fwrite(Header, 1, sizeof(Header), pFile);
      for (UINT y = 0; y < Desc.Height; y++)
        fwrite((const BYTE *)Rect.pBits + y * Rect.Pitch, 4, Desc.Width, pFile);
      fclose(pFile);
      hr = S_OK;
    }
    pCopy->UnlockRect();
  }
  Trace_Write("back buffer %ux%u format %u saved to '%s': %s", Desc.Width, Desc.Height, Desc.Format, szFile, SUCCEEDED(hr) ? "ok" : "failed");
  if (pCopy)
    pCopy->Release();
  pBack->Release();
}

void D3DDraws_RestartRenderTargets()
{
  if (s_nRTLines > 0)
    s_nRTLines = 0;
  s_nVSRegLines = 0;
  s_nRTLoadFrame = s_nRTFrame;
  s_bRTCaptured = false;
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
  for (int i = 0; i < s_nSurfaceHooks; i++)
  {
    *(void ***)s_SurfaceHooks[i].m_pSurf = s_SurfaceHooks[i].m_pOrigVTable;
    s_SurfaceHooks[i].m_pSurf->Release();
  }
  s_nSurfaceHooks = 0;
  for (std::map<IUnknown *, SBufferShadow>::iterator it = s_Shadows.begin(); it != s_Shadows.end(); ++it)
  {
    *(void ***)it->first = it->second.m_pOrigVTable;
    it->first->Release();
  }
  s_Shadows.clear();
  Trace_Write("==== %d Direct3D draws ====", s_nDraws);
}
