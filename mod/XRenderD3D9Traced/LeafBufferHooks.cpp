//////////////////////////////////////////////////////////////////////
//
//	File: LeafBufferHooks.cpp
//	Description: Tracing of the CLeafBuffer methods of the original renderer.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "FieldWatch.h"
#include "LeafBufferHooks.h"
#include "VCallHooks.h"

using namespace RetailRenderer;

static void **s_pVTable = NULL;
static void *s_pOrig[LBS_NumSlots];

//! The tracing replacements of the CLeafBuffer virtual functions. 'this' is a CLeafBuffer of the
//! original renderer. The class has no bases, members or virtual functions, so pointers to its
//! member functions are plain code addresses with the calling convention of the originals.
class CLeafBufferHooks
{
public:
  void CalcFaceNormals();
  void AddRE(CCObject *pObj, IShader *pEf, int nNumSort, IShader *pStateEff);
  bool CheckUpdate(int VertFormat, int Flags, bool bNeedAddNormals);
  bool CreateTangBuffer();
  void ReleaseShaders();
  void InvalidateVideoBuffer(int flags);
  void CopyTo(CLeafBuffer *pDst, bool bUseSysBuf);
  bool CreateBufferFromSource(VertexBufferSource *pSource);
  void CreateBufferFromMesh(CIndexedMesh *pTriData, bool bStripifyAndShareVerts, bool bRemoveNoDrawFaces, bool bKeepRemapTable);
  int GetAllocatedBytes(bool bVideoMem);
  void Unload();
  void UpdateCustomLighting(float fBackSideLevel, Vec3 vStatObjAmbientColor, const Vec3 &vLight, bool bCalcLighting);
  void AddRenderElements(CCObject *pObj, int DLightMask, int nTemplate, int nFogVolumeID, int nSortId, IMatInfo *pIMatInfo);
  unsigned short *GetIndices(int *pIndicesCount);
  void DestroyIndices();
  void UpdateVidIndices(const ushort *pNewInds, int nInds);
  void UpdateSysIndices(const ushort *pNewInds, int nInds);
  void UpdateSysVertices(void *pNewVertices, int nNewVerticesCount);
  void UpdateVidVertices(void *pNewVertices, int nNewVerticesCount);
  bool CreateSysVertices(int nVerts, int VertFormat);
  void CreateVidVertices(int nVerts, int VertFormat);
  void SetRECustomData(float *pfCustomData, float fFogScale, float fAlpha);
  void SetChunk(IShader *pShader, int nFirstVertId, int nVertCount, int nFirstIndexId, int nIndexCount, int nMatID, bool bForceInitChunk);
  void SetShader(IShader *pShader, int nCustomTID);
  void *GetSecVerticesPtr(int *pVerticesCount);
  void AllocateSystemBuffer(int nVertCount);
  void FreeSystemBuffer();
  bool Serialize(int &nPos, uchar *pSaveBuffer, bool bSaveToBuffer, char *szFolderName, char *szFileName, double &dCIndexedMesh__LoadMaterial);
  void DrawImmediately();
  bool UpdateTangBuffer(SPipTangents *pBasis);
  void RenderDebugLightPass(const Matrix44 &mat, int nLightMask, float fAlpha);
  void Render(const SRendParams &rParams, CCObject *pObj, TArray<int> &ShaderTemplates, int e_overlay_geometry, bool bNotCurArea, IMatInfo *pMaterial, bool bSupportDefaultShaderTemplates);

private:
  CLeafBuffer *LB() { return (CLeafBuffer *)this; }
  const char *Source() { return LB()->m_sSource ? LB()->m_sSource : ""; }
};

template <class TPmf> static TPmf OrigFn(int nSlot)
{
  static_assert(sizeof(TPmf) == sizeof(void *), "member function pointers must be plain code addresses");
  TPmf pmf;
  memcpy(&pmf, &s_pOrig[nSlot], sizeof(pmf));
  return pmf;
}

template <class TPmf> static void *HookAddr(TPmf pmf)
{
  static_assert(sizeof(TPmf) == sizeof(void *), "member function pointers must be plain code addresses");
  void *p;
  memcpy(&p, &pmf, sizeof(p));
  return p;
}

//! Calls the original function of a slot: LB_ORIG(LBS_Unload, void, ())().
#define LB_ORIG(Slot, Ret, Args) (this->*OrigFn<Ret (CLeafBufferHooks::*)Args>(Slot))

static const char *ShaderName(IShader *pSH)
{
  const char *szName = pSH ? pSH->GetName() : NULL;
  return szName ? szName : "(null)";
}

//! The leaf buffer with its system copy of the geometry.
static void Dump_LeafBufferGeometry(CLeafBuffer *pLB, int nMaxVerts = 4)
{
  Dump_LeafBuffer(pLB);
  CVertexBuffer *pSys = pLB->m_pSecVertBuffer;
  if (pSys)
    Dump_Vertices("sys verts", pSys->m_VS[VSF_GENERAL].m_VData, pSys->m_vertexformat, pLB->m_SecVertCount, nMaxVerts);
  Dump_Indices("sys inds", pLB->m_SecIndices.Num() ? &pLB->m_SecIndices[0] : NULL, pLB->m_SecIndices.Num());
}

//////////////////////////////////////////////////////////////////////////
// Geometry

void CLeafBufferHooks::CreateBufferFromMesh(CIndexedMesh *pTriData, bool bStripifyAndShareVerts, bool bRemoveNoDrawFaces, bool bKeepRemapTable)
{
  TRACE_CALL("lb %p '%s' mesh %p stripify %d removeNoDraw %d keepRemap %d", LB(), Source(), pTriData, bStripifyAndShareVerts,
    bRemoveNoDrawFaces, bKeepRemapTable);
  if (TRACE_DUMP)
    Dump_IndexedMesh(pTriData);
  CWatchArg WatchMesh(WT_CIndexedMesh, pTriData);
  LB_ORIG(LBS_CreateBufferFromMesh, void, (CIndexedMesh *, bool, bool, bool))(pTriData, bStripifyAndShareVerts, bRemoveNoDrawFaces, bKeepRemapTable);
  if (TRACE_DUMP)
    Dump_LeafBufferGeometry(LB());
}

bool CLeafBufferHooks::CreateBufferFromSource(VertexBufferSource *pSource)
{
  TRACE_CALL("lb %p '%s' source %p", LB(), Source(), pSource);
  if (TRACE_DUMP)
    Dump_VertexBufferSource(pSource);
  CWatchArg WatchSource(WT_VertexBufferSource, pSource);
  bool bRes = LB_ORIG(LBS_CreateBufferFromSource, bool, (VertexBufferSource *))(pSource);
  TRACE_RESULT("%d", bRes);
  if (TRACE_DUMP)
    Dump_LeafBufferGeometry(LB());
  return bRes;
}

bool CLeafBufferHooks::Serialize(int &nPos, uchar *pSaveBuffer, bool bSaveToBuffer, char *szFolderName, char *szFileName, double &dCIndexedMesh__LoadMaterial)
{
  TRACE_CALL("lb %p '%s' pos %d buffer %p save %d folder '%s' file '%s'", LB(), Source(), nPos, pSaveBuffer, bSaveToBuffer, szFolderName,
    szFileName);
  bool bRes = LB_ORIG(LBS_Serialize, bool, (int &, uchar *, bool, char *, char *, double &))(nPos, pSaveBuffer, bSaveToBuffer, szFolderName,
    szFileName, dCIndexedMesh__LoadMaterial);
  TRACE_RESULT("%d pos %d", bRes, nPos);
  if (TRACE_DUMP && !bSaveToBuffer)
    Dump_LeafBufferGeometry(LB());
  return bRes;
}

void CLeafBufferHooks::CopyTo(CLeafBuffer *pDst, bool bUseSysBuf)
{
  TRACE_CALL("lb %p '%s' dst %p useSys %d", LB(), Source(), pDst, bUseSysBuf);
  LB_ORIG(LBS_CopyTo, void, (CLeafBuffer *, bool))(pDst, bUseSysBuf);
}

void CLeafBufferHooks::SetChunk(IShader *pShader, int nFirstVertId, int nVertCount, int nFirstIndexId, int nIndexCount, int nMatID, bool bForceInitChunk)
{
  TRACE_CALL("lb %p '%s' shader %p '%s' verts %d+%d inds %d+%d mat %d force %d", LB(), Source(), pShader, ShaderName(pShader),
    nFirstVertId, nVertCount, nFirstIndexId, nIndexCount, nMatID, bForceInitChunk);
  LB_ORIG(LBS_SetChunk, void, (IShader *, int, int, int, int, int, bool))(pShader, nFirstVertId, nVertCount, nFirstIndexId, nIndexCount,
    nMatID, bForceInitChunk);
  if (TRACE_DUMP && LB()->m_pMats && nMatID >= 0 && nMatID < LB()->m_pMats->Count())
    Dump_MatInfo(&(*LB()->m_pMats)[nMatID], nMatID);
}

void CLeafBufferHooks::SetShader(IShader *pShader, int nCustomTID)
{
  TRACE_CALL("lb %p '%s' shader %p '%s' customTex %d", LB(), Source(), pShader, ShaderName(pShader), nCustomTID);
  LB_ORIG(LBS_SetShader, void, (IShader *, int))(pShader, nCustomTID);
}

void CLeafBufferHooks::UpdateSysVertices(void *pNewVertices, int nNewVerticesCount)
{
  TRACE_CALL("lb %p '%s' verts %p (%d)", LB(), Source(), pNewVertices, nNewVerticesCount);
  if (TRACE_DUMP)
    Dump_Vertices("new verts", pNewVertices, LB()->m_nVertexFormat, nNewVerticesCount);
  LB_ORIG(LBS_UpdateSysVertices, void, (void *, int))(pNewVertices, nNewVerticesCount);
}

void CLeafBufferHooks::UpdateVidVertices(void *pNewVertices, int nNewVerticesCount)
{
  TRACE_CALL("lb %p '%s' verts %p (%d)", LB(), Source(), pNewVertices, nNewVerticesCount);
  if (TRACE_DUMP)
    Dump_Vertices("new verts", pNewVertices, LB()->m_nVertexFormat, nNewVerticesCount);
  LB_ORIG(LBS_UpdateVidVertices, void, (void *, int))(pNewVertices, nNewVerticesCount);
}

void CLeafBufferHooks::UpdateSysIndices(const ushort *pNewInds, int nInds)
{
  TRACE_CALL("lb %p '%s' inds %p (%d)", LB(), Source(), pNewInds, nInds);
  if (TRACE_DUMP)
    Dump_Indices("new inds", pNewInds, nInds);
  LB_ORIG(LBS_UpdateSysIndices, void, (const ushort *, int))(pNewInds, nInds);
}

void CLeafBufferHooks::UpdateVidIndices(const ushort *pNewInds, int nInds)
{
  TRACE_CALL("lb %p '%s' inds %p (%d)", LB(), Source(), pNewInds, nInds);
  if (TRACE_DUMP)
    Dump_Indices("new inds", pNewInds, nInds);
  LB_ORIG(LBS_UpdateVidIndices, void, (const ushort *, int))(pNewInds, nInds);
}

bool CLeafBufferHooks::CreateSysVertices(int nVerts, int VertFormat)
{
  TRACE_CALL("lb %p '%s' verts %d format %s(%d)", LB(), Source(), nVerts, Trace_VertexFormatName(VertFormat), VertFormat);
  bool bRes = LB_ORIG(LBS_CreateSysVertices, bool, (int, int))(nVerts, VertFormat);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CLeafBufferHooks::CreateVidVertices(int nVerts, int VertFormat)
{
  TRACE_CALL("lb %p '%s' verts %d format %s(%d)", LB(), Source(), nVerts, Trace_VertexFormatName(VertFormat), VertFormat);
  LB_ORIG(LBS_CreateVidVertices, void, (int, int))(nVerts, VertFormat);
}

void CLeafBufferHooks::AllocateSystemBuffer(int nVertCount)
{
  TRACE_CALL("lb %p '%s' verts %d", LB(), Source(), nVertCount);
  LB_ORIG(LBS_AllocateSystemBuffer, void, (int))(nVertCount);
}

void CLeafBufferHooks::FreeSystemBuffer()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  LB_ORIG(LBS_FreeSystemBuffer, void, ())();
}

unsigned short *CLeafBufferHooks::GetIndices(int *pIndicesCount)
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  unsigned short *pRes = LB_ORIG(LBS_GetIndices, unsigned short *, (int *))(pIndicesCount);
  TRACE_RESULT("%p (%d)", pRes, pIndicesCount ? *pIndicesCount : -1);
  return pRes;
}

void CLeafBufferHooks::DestroyIndices()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  LB_ORIG(LBS_DestroyIndices, void, ())();
}

void *CLeafBufferHooks::GetSecVerticesPtr(int *pVerticesCount)
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  void *pRes = LB_ORIG(LBS_GetSecVerticesPtr, void *, (int *))(pVerticesCount);
  TRACE_RESULT("%p (%d)", pRes, pVerticesCount ? *pVerticesCount : -1);
  return pRes;
}

bool CLeafBufferHooks::CreateTangBuffer()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  bool bRes = LB_ORIG(LBS_CreateTangBuffer, bool, ())();
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CLeafBufferHooks::UpdateTangBuffer(SPipTangents *pBasis)
{
  TRACE_CALL("lb %p '%s' basis %p", LB(), Source(), pBasis);
  bool bRes = LB_ORIG(LBS_UpdateTangBuffer, bool, (SPipTangents *))(pBasis);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CLeafBufferHooks::CalcFaceNormals()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  LB_ORIG(LBS_CalcFaceNormals, void, ())();
}

bool CLeafBufferHooks::CheckUpdate(int VertFormat, int Flags, bool bNeedAddNormals)
{
  TRACE_CALL("lb %p '%s' format %s(%d) flags 0x%x addNormals %d", LB(), Source(), Trace_VertexFormatName(VertFormat), VertFormat, Flags,
    bNeedAddNormals);
  bool bRes = LB_ORIG(LBS_CheckUpdate, bool, (int, int, bool))(VertFormat, Flags, bNeedAddNormals);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CLeafBufferHooks::InvalidateVideoBuffer(int flags)
{
  TRACE_CALL("lb %p '%s' flags 0x%x", LB(), Source(), flags);
  LB_ORIG(LBS_InvalidateVideoBuffer, void, (int))(flags);
}

void CLeafBufferHooks::Unload()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  LB_ORIG(LBS_Unload, void, ())();
}

int CLeafBufferHooks::GetAllocatedBytes(bool bVideoMem)
{
  TRACE_CALL("lb %p '%s' video %d", LB(), Source(), bVideoMem);
  int nRes = LB_ORIG(LBS_GetAllocatedBytes, int, (bool))(bVideoMem);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

void CLeafBufferHooks::ReleaseShaders()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  LB_ORIG(LBS_ReleaseShaders, void, ())();
}

//////////////////////////////////////////////////////////////////////////
// Drawing

//! The render elements of the materials are created by the renderer itself.
static void HookRenderElements(CLeafBuffer *pLB)
{
  if (!pLB->m_pMats)
    return;
  for (int i = 0; i < pLB->m_pMats->Count(); i++)
    VCallHooks_InstallRE((*pLB->m_pMats)[i].pRE);
}

void CLeafBufferHooks::AddRenderElements(CCObject *pObj, int DLightMask, int nTemplate, int nFogVolumeID, int nSortId, IMatInfo *pIMatInfo)
{
  TRACE_CALL("lb %p '%s' obj %p lights 0x%x templ %d fog %d sort %d material %p", LB(), Source(), pObj, DLightMask, nTemplate,
    nFogVolumeID, nSortId, pIMatInfo);
  if (TRACE_DUMP)
  {
    Dump_LeafBuffer(LB());
    Dump_Object(pObj);
  }
  LB_ORIG(LBS_AddRenderElements, void, (CCObject *, int, int, int, int, IMatInfo *))(pObj, DLightMask, nTemplate, nFogVolumeID, nSortId,
    pIMatInfo);
  HookRenderElements(LB());
}

void CLeafBufferHooks::Render(const SRendParams &rParams, CCObject *pObj, TArray<int> &ShaderTemplates, int e_overlay_geometry, bool bNotCurArea, IMatInfo *pMaterial, bool bSupportDefaultShaderTemplates)
{
  TRACE_CALL("lb %p '%s' params %p obj %p templates %d overlay %d notCurArea %d material %p defaultTemplates %d", LB(), Source(), &rParams,
    pObj, ShaderTemplates.Num(), e_overlay_geometry, bNotCurArea, pMaterial, bSupportDefaultShaderTemplates);
  if (TRACE_DUMP)
  {
    char szTemplates[256] = "";
    int nLen = 0;
    for (int i = 0; i < (int)ShaderTemplates.Num() && nLen < (int)sizeof(szTemplates) - 16; i++)
      nLen += sprintf_s(szTemplates + nLen, sizeof(szTemplates) - nLen, " %d", ShaderTemplates[i]);
    const char *szMaterial = pMaterial ? pMaterial->GetName() : NULL;
    Trace_Write("    templates:%s material '%s'", szTemplates, szMaterial ? szMaterial : "");
    Dump_RendParams(rParams);
    Dump_Object(pObj);
  }
  CWatchArg WatchParams(WT_SRendParams, &rParams);
  LB_ORIG(LBS_Render, void, (const SRendParams &, CCObject *, TArray<int> &, int, bool, IMatInfo *, bool))(rParams, pObj, ShaderTemplates,
    e_overlay_geometry, bNotCurArea, pMaterial, bSupportDefaultShaderTemplates);
  HookRenderElements(LB());
}

void CLeafBufferHooks::AddRE(CCObject *pObj, IShader *pEf, int nNumSort, IShader *pStateEff)
{
  TRACE_CALL("lb %p '%s' obj %p shader %p '%s' sort %d state %p", LB(), Source(), pObj, pEf, ShaderName(pEf), nNumSort, pStateEff);
  if (TRACE_DUMP)
    Dump_Object(pObj);
  LB_ORIG(LBS_AddRE, void, (CCObject *, IShader *, int, IShader *))(pObj, pEf, nNumSort, pStateEff);
}

void CLeafBufferHooks::DrawImmediately()
{
  TRACE_CALL("lb %p '%s'", LB(), Source());
  LB_ORIG(LBS_DrawImmediately, void, ())();
}

void CLeafBufferHooks::RenderDebugLightPass(const Matrix44 &mat, int nLightMask, float fAlpha)
{
  TRACE_CALL("lb %p '%s' lights 0x%x alpha %g", LB(), Source(), nLightMask, fAlpha);
  LB_ORIG(LBS_RenderDebugLightPass, void, (const Matrix44 &, int, float))(mat, nLightMask, fAlpha);
}

void CLeafBufferHooks::SetRECustomData(float *pfCustomData, float fFogScale, float fAlpha)
{
  TRACE_CALL("lb %p '%s' custom %p fogScale %g alpha %g", LB(), Source(), pfCustomData, fFogScale, fAlpha);
  // The SDK does not say how many floats the render elements read; the first 16 are written.
  if (TRACE_DUMP && pfCustomData && Trace_IsReadable(pfCustomData, 16 * sizeof(float)))
  {
    const float *f = pfCustomData;
    Trace_Write("    custom: %g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g", f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8],
      f[9], f[10], f[11], f[12], f[13], f[14], f[15]);
  }
  LB_ORIG(LBS_SetRECustomData, void, (float *, float, float))(pfCustomData, fFogScale, fAlpha);
}

void CLeafBufferHooks::UpdateCustomLighting(float fBackSideLevel, Vec3 vStatObjAmbientColor, const Vec3 &vLight, bool bCalcLighting)
{
  TRACE_CALL("lb %p '%s' backSide %g ambient " TRACE_V3 " light " TRACE_V3 " calc %d", LB(), Source(), fBackSideLevel,
    TRACE_V3_ARGS(vStatObjAmbientColor), TRACE_V3_ARGS(vLight), bCalcLighting);
  LB_ORIG(LBS_UpdateCustomLighting, void, (float, Vec3, const Vec3 &, bool))(fBackSideLevel, vStatObjAmbientColor, vLight, bCalcLighting);
}

//////////////////////////////////////////////////////////////////////////
// Installation

static void GetHooks(void *pHooks[LBS_NumSlots])
{
  pHooks[LBS_CalcFaceNormals] = HookAddr(&CLeafBufferHooks::CalcFaceNormals);
  pHooks[LBS_AddRE] = HookAddr(&CLeafBufferHooks::AddRE);
  pHooks[LBS_CheckUpdate] = HookAddr(&CLeafBufferHooks::CheckUpdate);
  pHooks[LBS_CreateTangBuffer] = HookAddr(&CLeafBufferHooks::CreateTangBuffer);
  pHooks[LBS_ReleaseShaders] = HookAddr(&CLeafBufferHooks::ReleaseShaders);
  pHooks[LBS_InvalidateVideoBuffer] = HookAddr(&CLeafBufferHooks::InvalidateVideoBuffer);
  pHooks[LBS_CopyTo] = HookAddr(&CLeafBufferHooks::CopyTo);
  pHooks[LBS_CreateBufferFromSource] = HookAddr(&CLeafBufferHooks::CreateBufferFromSource);
  pHooks[LBS_CreateBufferFromMesh] = HookAddr(&CLeafBufferHooks::CreateBufferFromMesh);
  pHooks[LBS_GetAllocatedBytes] = HookAddr(&CLeafBufferHooks::GetAllocatedBytes);
  pHooks[LBS_Unload] = HookAddr(&CLeafBufferHooks::Unload);
  pHooks[LBS_UpdateCustomLighting] = HookAddr(&CLeafBufferHooks::UpdateCustomLighting);
  pHooks[LBS_AddRenderElements] = HookAddr(&CLeafBufferHooks::AddRenderElements);
  pHooks[LBS_GetIndices] = HookAddr(&CLeafBufferHooks::GetIndices);
  pHooks[LBS_DestroyIndices] = HookAddr(&CLeafBufferHooks::DestroyIndices);
  pHooks[LBS_UpdateVidIndices] = HookAddr(&CLeafBufferHooks::UpdateVidIndices);
  pHooks[LBS_UpdateSysIndices] = HookAddr(&CLeafBufferHooks::UpdateSysIndices);
  pHooks[LBS_UpdateSysVertices] = HookAddr(&CLeafBufferHooks::UpdateSysVertices);
  pHooks[LBS_UpdateVidVertices] = HookAddr(&CLeafBufferHooks::UpdateVidVertices);
  pHooks[LBS_CreateSysVertices] = HookAddr(&CLeafBufferHooks::CreateSysVertices);
  pHooks[LBS_CreateVidVertices] = HookAddr(&CLeafBufferHooks::CreateVidVertices);
  pHooks[LBS_SetRECustomData] = HookAddr(&CLeafBufferHooks::SetRECustomData);
  pHooks[LBS_SetChunk] = HookAddr(&CLeafBufferHooks::SetChunk);
  pHooks[LBS_SetShader] = HookAddr(&CLeafBufferHooks::SetShader);
  pHooks[LBS_GetSecVerticesPtr] = HookAddr(&CLeafBufferHooks::GetSecVerticesPtr);
  pHooks[LBS_AllocateSystemBuffer] = HookAddr(&CLeafBufferHooks::AllocateSystemBuffer);
  pHooks[LBS_FreeSystemBuffer] = HookAddr(&CLeafBufferHooks::FreeSystemBuffer);
  pHooks[LBS_Serialize] = HookAddr(&CLeafBufferHooks::Serialize);
  pHooks[LBS_DrawImmediately] = HookAddr(&CLeafBufferHooks::DrawImmediately);
  pHooks[LBS_UpdateTangBuffer] = HookAddr(&CLeafBufferHooks::UpdateTangBuffer);
  pHooks[LBS_RenderDebugLightPass] = HookAddr(&CLeafBufferHooks::RenderDebugLightPass);
  pHooks[LBS_Render] = HookAddr(&CLeafBufferHooks::Render);
}

static bool WriteVTable(void **pVTable, void *const *pEntries)
{
  const SIZE_T nSize = LBS_NumSlots * sizeof(void *);
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(pVTable, &mbi, sizeof(mbi)))
    return false;
  // Keep the page executable if it was (the vtable may share a page with code).
  const DWORD nExec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  DWORD nOld;
  if (!VirtualProtect(pVTable, nSize, (mbi.Protect & nExec) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &nOld))
    return false;
  memcpy(pVTable, pEntries, nSize);
  VirtualProtect(pVTable, nSize, nOld, &nOld);
  return true;
}

void LeafBufferHooks_Install(CLeafBuffer *pLB)
{
  static bool s_bDisabled = false;
  static bool s_bOtherLogged = false;
  if (!pLB || s_bDisabled)
    return;
  void **pVTable = *(void ***)pLB;
  if (pVTable == s_pVTable)
    return;
  if (s_pVTable)
  {
    if (!s_bOtherLogged)
      Trace_Write("CLeafBuffer hooks: %p has another vtable %s, not hooked", pLB, Trace_Addr(pVTable));
    s_bOtherLogged = true;
    return;
  }

  char szEnv[8];
  if (GetEnvironmentVariableA("XRENDERTRACED_NO_LB_HOOKS", szEnv, sizeof(szEnv)) && szEnv[0] == '1')
  {
    Trace_Write("CLeafBuffer hooks: off (XRENDERTRACED_NO_LB_HOOKS)");
    s_bDisabled = true;
    return;
  }

  // All slots must point to code of the module that holds the vtable, or the class is not the one
  // the SDK header describes.
  HMODULE hVTable = NULL;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pVTable, &hVTable);
  for (int i = 0; i < LBS_NumSlots; i++)
  {
    HMODULE hSlot = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pVTable[i], &hSlot);
    if (!hVTable || hSlot != hVTable || !Trace_IsCode(pVTable[i]))
    {
      Trace_Write("CLeafBuffer hooks: slot %d of vtable %s points to %s, not hooking", i, Trace_Addr(pVTable), Trace_Addr(pVTable[i]));
      s_bDisabled = true;
      return;
    }
  }

  // The hooked vtable points into this DLL until the process exits.
  HMODULE hSelf;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)&LeafBufferHooks_Install, &hSelf);

  void *pHooks[LBS_NumSlots];
  GetHooks(pHooks);
  memcpy(s_pOrig, pVTable, sizeof(s_pOrig));
  if (!WriteVTable(pVTable, pHooks))
  {
    Trace_Write("CLeafBuffer hooks: can't write vtable %s (error %u)", Trace_Addr(pVTable), GetLastError());
    s_bDisabled = true;
    return;
  }
  s_pVTable = pVTable;
  Trace_Write("CLeafBuffer hooks: vtable %s hooked (%d slots)", Trace_Addr(pVTable), (int)LBS_NumSlots);
}

void LeafBufferHooks_Uninstall()
{
  if (!s_pVTable)
    return;
  WriteVTable(s_pVTable, s_pOrig);
  Trace_Write("CLeafBuffer hooks: vtable %s restored", Trace_Addr(s_pVTable));
  s_pVTable = NULL;
}
