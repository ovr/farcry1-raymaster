//////////////////////////////////////////////////////////////////////
//
//	File: RendererProxyShaders.cpp
//	Description: CRendererProxy - EF_* methods: shaders, textures,
//	render elements, render objects, lights.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "FieldWatch.h"
#include "RendererProxy.h"
#include "VCallHooks.h"
#include <CREPolyMesh.h>

static const char *ShaderName(IShader *pSH)
{
  if (!pSH)
    return "(null)";
  const char *szName = pSH->GetName();
  return szName ? szName : "(null)";
}

static const char *QueryName(int nQuery)
{
  switch (nQuery)
  {
  case EFQ_NUMEFS: return "NUMEFS";
  case EFQ_LOADEDEFS: return "LOADEDEFS";
  case EFQ_NUMTEXTURES: return "NUMTEXTURES";
  case EFQ_LOADEDTEXTURES: return "LOADEDTEXTURES";
  case EFQ_NUMEFFILES0: return "NUMEFFILES0";
  case EFQ_NUMEFFILES1: return "NUMEFFILES1";
  case EFQ_EFFILENAMES0: return "EFFILENAMES0";
  case EFQ_EFFILENAMES1: return "EFFILENAMES1";
  case EFQ_VProgramms: return "VProgramms";
  case EFQ_PShaders: return "PShaders";
  case EFQ_LightSource: return "LightSource";
  case EFQ_RecurseLevel: return "RecurseLevel";
  case EFQ_Pointer2FrameID: return "Pointer2FrameID";
  case EFQ_RegisteredTemplates: return "RegisteredTemplates";
  case EFQ_NumRenderItems: return "NumRenderItems";
  case EFQ_DeviceLost: return "DeviceLost";
  case EFQ_CubeColor: return "CubeColor";
  case EFQ_D3DDevice: return "D3DDevice";
  case EFQ_glReadPixels: return "glReadPixels";
  case EFQ_Orients: return "Orients";
  case EFQ_NumOrients: return "NumOrients";
  case EFQ_SkyShader: return "SkyShader";
  case EFQ_SunFlares: return "SunFlares";
  case EFQ_CurSunFlare: return "CurSunFlare";
  case EFQ_Materials: return "Materials";
  case EFQ_LightMaterials: return "LightMaterials";
  }
  return "?";
}

//////////////////////////////////////////////////////////////////////////
// Precaching, heat vision

bool CRendererProxy::EF_PrecacheResource(IShader *pSH, float fDist, float fTimeToReady, int Flags)
{
  TRACE_CALL("shader %p '%s' dist %g timeToReady %g flags 0x%x", pSH, ShaderName(pSH), fDist, fTimeToReady, Flags);
  bool bRes = m_pReal->EF_PrecacheResource(pSH, fDist, fTimeToReady, Flags);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_PrecacheResource(ITexPic *pTP, float fDist, float fTimeToReady, int Flags)
{
  TRACE_CALL("pic %p '%s' dist %g timeToReady %g flags 0x%x", pTP, pTP ? pTP->GetName() : "", fDist, fTimeToReady, Flags);
  bool bRes = m_pReal->EF_PrecacheResource(pTP, fDist, fTimeToReady, Flags);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_PrecacheResource(CLeafBuffer *pPB, float fDist, float fTimeToReady, int Flags)
{
  TRACE_CALL("leafBuffer %p dist %g timeToReady %g flags 0x%x", pPB, fDist, fTimeToReady, Flags);
  bool bRes = m_pReal->EF_PrecacheResource(pPB, fDist, fTimeToReady, Flags);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_PrecacheResource(CDLight *pLS, float fDist, float fTimeToReady, int Flags)
{
  TRACE_CALL("light %p dist %g timeToReady %g flags 0x%x", pLS, fDist, fTimeToReady, Flags);
  bool bRes = m_pReal->EF_PrecacheResource(pLS, fDist, fTimeToReady, Flags);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::EF_EnableHeatVision(bool bEnable)
{
  TRACE_CALL("%d", bEnable);
  m_pReal->EF_EnableHeatVision(bEnable);
}

bool CRendererProxy::EF_GetHeatVision()
{
  TRACE_CALL("");
  bool bRes = m_pReal->EF_GetHeatVision();
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::EF_PolygonOffset(bool bEnable, float fFactor, float fUnits)
{
  TRACE_CALL("%d factor %g units %g", bEnable, fFactor, fUnits);
  m_pReal->EF_PolygonOffset(bEnable, fFactor, fUnits);
}

//////////////////////////////////////////////////////////////////////////
// Client polygons

void CRendererProxy::EF_AddPolyToScene3D(int Ef, int numPts, SColorVert *verts, CCObject *obj, int nFogID)
{
  TRACE_CALL("shader %d points %d verts %p obj %p fog %d", Ef, numPts, verts, obj, nFogID);
  if (TRACE_DUMP)
  {
    Dump_ColorVerts(verts, numPts);
    if (obj)
      Dump_Object(obj);
  }
  m_pReal->EF_AddPolyToScene3D(Ef, numPts, verts, obj, nFogID);
}

CCObject *CRendererProxy::EF_AddSpriteToScene(int Ef, int numPts, SColorVert *verts, CCObject *obj, byte *inds, int ninds, int nFogID)
{
  TRACE_CALL("shader %d points %d verts %p obj %p inds %p (%d) fog %d", Ef, numPts, verts, obj, inds, ninds, nFogID);
  if (TRACE_DUMP)
  {
    Dump_ColorVerts(verts, numPts);
    if (obj)
      Dump_Object(obj);
  }
  CCObject *pRes = m_pReal->EF_AddSpriteToScene(Ef, numPts, verts, obj, inds, ninds, nFogID);
  TRACE_RESULT("obj %p", pRes);
  return pRes;
}

void CRendererProxy::EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D *verts)
{
  TRACE_CALL("shader %d points %d verts %p", Ef, numPts, verts);
  if (TRACE_DUMP)
    Dump_ColorVerts2D(verts, numPts);
  m_pReal->EF_AddPolyToScene2D(Ef, numPts, verts);
}

void CRendererProxy::EF_AddPolyToScene2D(SShaderItem si, int nTempl, int numPts, SColorVert2D *verts)
{
  TRACE_CALL("shader %p '%s' res %p templ %d points %d verts %p", si.m_pShader, ShaderName(si.m_pShader), si.m_pShaderResources,
    nTempl, numPts, verts);
  if (TRACE_DUMP)
    Dump_ColorVerts2D(verts, numPts);
  m_pReal->EF_AddPolyToScene2D(si, nTempl, numPts, verts);
}

//////////////////////////////////////////////////////////////////////////
// Shaders

IShader *CRendererProxy::EF_LoadShader(const char *name, EShClass Class, int flags, uint64 nMaskGen)
{
  TRACE_CALL("'%s' class %d flags 0x%x maskGen 0x%llx", name, Class, flags, (unsigned long long)nMaskGen);
  IShader *pRes = m_pReal->EF_LoadShader(name, Class, flags, nMaskGen);
  TRACE_RESULT("%p", pRes);
  VCallHooks_Install("IShader", pRes);
  if (TRACE_DUMP)
    Dump_Shader(pRes);
  return pRes;
}

SShaderItem CRendererProxy::EF_LoadShaderItem(const char *name, EShClass Class, bool bShare, const char *templName, int flags, SInputShaderResources *Res, uint64 nMaskGen)
{
  TRACE_CALL("'%s' class %d share %d template '%s' flags 0x%x res %p maskGen 0x%llx", name, Class, bShare, templName, flags, Res,
    (unsigned long long)nMaskGen);
  if (TRACE_DUMP && Res)
    Dump_InputResources(Res);
  CWatchArg WatchRes(WT_SInputShaderResources, Res);
  SShaderItem Item = m_pReal->EF_LoadShaderItem(name, Class, bShare, templName, flags, Res, nMaskGen);
  TRACE_RESULT("shader %p res %p", Item.m_pShader, Item.m_pShaderResources);
  Watch_RendererObject(WT_SRenderShaderResources, Item.m_pShaderResources);
  VCallHooks_Install("IShader", Item.m_pShader);
  VCallHooks_Install("SRenderShaderResources", Item.m_pShaderResources);
  if (TRACE_DUMP)
  {
    Dump_Shader(Item.m_pShader);
    Dump_RenderResources(Item.m_pShaderResources);
  }
  return Item;
}

bool CRendererProxy::EF_ReloadFile(const char *szFileName)
{
  TRACE_CALL("'%s'", szFileName);
  bool bRes = m_pReal->EF_ReloadFile(szFileName);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::EF_ReloadShaderFiles(int nCategory)
{
  TRACE_CALL("%d", nCategory);
  m_pReal->EF_ReloadShaderFiles(nCategory);
}

void CRendererProxy::EF_ReloadTextures()
{
  TRACE_CALL("");
  m_pReal->EF_ReloadTextures();
}

IShader *CRendererProxy::EF_CopyShader(IShader *ef)
{
  TRACE_CALL("%p '%s'", ef, ShaderName(ef));
  IShader *pRes = m_pReal->EF_CopyShader(ef);
  TRACE_RESULT("%p", pRes);
  VCallHooks_Install("IShader", pRes);
  if (TRACE_DUMP)
    Dump_Shader(pRes);
  return pRes;
}

//////////////////////////////////////////////////////////////////////////
// Textures

ITexPic *CRendererProxy::EF_GetTextureByID(int Id)
{
  TRACE_CALL("%d", Id);
  ITexPic *pRes = m_pReal->EF_GetTextureByID(Id);
  TRACE_RESULT("%p", pRes);
  VCallHooks_Install("ITexPic", pRes);
  if (TRACE_DUMP)
    Dump_TexPic(pRes);
  return pRes;
}

ITexPic *CRendererProxy::EF_LoadTexture(const char *nameTex, uint flags, uint flags2, byte eTT, float fAmount1, float fAmount2, int Id, int BindId)
{
  TRACE_CALL("'%s' flags 0x%x flags2 0x%x type %d amount %g %g id %d bind %d", nameTex, flags, flags2, eTT, fAmount1, fAmount2, Id, BindId);
  ITexPic *pRes = m_pReal->EF_LoadTexture(nameTex, flags, flags2, eTT, fAmount1, fAmount2, Id, BindId);
  TRACE_RESULT("%p", pRes);
  VCallHooks_Install("ITexPic", pRes);
  if (TRACE_DUMP)
    Dump_TexPic(pRes);
  return pRes;
}

int CRendererProxy::EF_LoadLightmap(const char *name)
{
  TRACE_CALL("'%s'", name);
  int nRes = m_pReal->EF_LoadLightmap(name);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

bool CRendererProxy::EF_ScanEnvironmentCM(const char *name, int size, Vec3 &Pos)
{
  TRACE_CALL("'%s' size %d pos " TRACE_V3, name, size, TRACE_V3_ARGS(Pos));
  bool bRes = m_pReal->EF_ScanEnvironmentCM(name, size, Pos);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

int CRendererProxy::EF_ReadAllImgFiles(IShader *ef, SShaderTexUnit *tl, STexAnim *ta, char *name)
{
  TRACE_CALL("shader %p '%s' texUnit %p anim %p '%s'", ef, ShaderName(ef), tl, ta, name);
  int nRes = m_pReal->EF_ReadAllImgFiles(ef, tl, ta, name);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

char **CRendererProxy::EF_GetShadersForFile(const char *File, int num)
{
  TRACE_CALL("'%s' %d", File, num);
  char **pRes = m_pReal->EF_GetShadersForFile(File, num);
  TRACE_RESULT("%p", pRes);
  return pRes;
}

SLightMaterial *CRendererProxy::EF_GetLightMaterial(char *Str)
{
  TRACE_CALL("'%s'", Str);
  SLightMaterial *pRes = m_pReal->EF_GetLightMaterial(Str);
  TRACE_RESULT("%p", pRes);
  return pRes;
}

//////////////////////////////////////////////////////////////////////////
// Templates

bool CRendererProxy::EF_RegisterTemplate(int nTemplId, char *Name, bool bReplace)
{
  TRACE_CALL("%d '%s' replace %d", nTemplId, Name, bReplace);
  bool bRes = m_pReal->EF_RegisterTemplate(nTemplId, Name, bReplace);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::EF_AddSplash(Vec3 Pos, eSplashType eST, float fForce, int Id)
{
  TRACE_CALL(TRACE_V3 " type %d force %g id %d", TRACE_V3_ARGS(Pos), eST, fForce, Id);
  m_pReal->EF_AddSplash(Pos, eST, fForce, Id);
}

bool CRendererProxy::EF_HideTemplate(const char *name)
{
  TRACE_CALL("'%s'", name);
  bool bRes = m_pReal->EF_HideTemplate(name);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_UnhideTemplate(const char *name)
{
  TRACE_CALL("'%s'", name);
  bool bRes = m_pReal->EF_UnhideTemplate(name);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_UnhideAllTemplates()
{
  TRACE_CALL("");
  bool bRes = m_pReal->EF_UnhideAllTemplates();
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_SetLightHole(Vec3 vPos, Vec3 vNormal, int idTex, float fScale, bool bAdditive)
{
  TRACE_CALL("pos " TRACE_V3 " normal " TRACE_V3 " tex %d scale %g additive %d", TRACE_V3_ARGS(vPos), TRACE_V3_ARGS(vNormal), idTex,
    fScale, bAdditive);
  bool bRes = m_pReal->EF_SetLightHole(vPos, vNormal, idTex, fScale, bAdditive);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::SetGlobalShaderTemplateId(int nTemplateId)
{
  TRACE_CALL("%d", nTemplateId);
  m_pReal->SetGlobalShaderTemplateId(nTemplateId);
}

int CRendererProxy::GetGlobalShaderTemplateId()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetGlobalShaderTemplateId();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

//////////////////////////////////////////////////////////////////////////
// Render elements, objects, the scene

//! Size of the SDK class of a render element type in this build, 0 if the SDK has no class for it.
static int SdkRESize(EDataType edt)
{
  switch (edt)
  {
  case eDATA_Dummy:             return sizeof(CREDummy);
  case eDATA_Sky:               return sizeof(CRESky);
  case eDATA_Poly:              return sizeof(CREPolyMesh);
  case eDATA_TriMeshShadow:     return sizeof(CRETriMeshShadow);
  case eDATA_OcLeaf:            return sizeof(CREOcLeaf);
  case eDATA_TerrainSector:     return sizeof(CRECommon);
  case eDATA_2DQuad:            return sizeof(CRE2DQuad);
  case eDATA_Glare:             return sizeof(CREGlare);
  case eDATA_OcclusionQuery:    return sizeof(CREOcclusionQuery);
  case eDATA_FlashBang:         return sizeof(CREFlashBang);
  case eDATA_ScreenProcess:     return sizeof(CREScreenProcess);
  default:                      return 0;
  }
}

CRendElement *CRendererProxy::EF_CreateRE(EDataType edt)
{
  TRACE_CALL("%s(%d)", Trace_RETypeName(edt), edt);
  CRendElement *pRes = m_pReal->EF_CreateRE(edt);
  // The vtable identifies the CRE* class in the original renderer.
  TRACE_RESULT("%p (vtbl %s), retail size 0x%x, SDK size 0x%x, heap prefix %s", pRes, Trace_VTable(pRes), RetailRenderer::RESize(edt),
    SdkRESize(edt), Trace_HeapPrefix(pRes));
  // CScreenVars is only declared in the SDK.
  if (pRes && edt == eDATA_ScreenProcess)
    TRACE_RESULT("screen vars %p, heap prefix %s", ((CREScreenProcess *)pRes)->GetVars(), Trace_HeapPrefix(((CREScreenProcess *)pRes)->GetVars()));
  Watch_RenderElement(pRes);
  VCallHooks_InstallRE(pRes);
  return pRes;
}

void CRendererProxy::EF_StartEf()
{
  TRACE_CALL("");
  m_pReal->EF_StartEf();
}

CCObject *CRendererProxy::EF_GetObject(bool bTemp, int num)
{
  TRACE_CALL("temp %d num %d", bTemp, num);
  CCObject *pRes = m_pReal->EF_GetObject(bTemp, num);
  TRACE_RESULT("%p id %d", pRes, pRes ? pRes->m_Id : -1);
  Watch_RendererObject(WT_CCObject, pRes);
  return pRes;
}

void CRendererProxy::EF_AddEf(int NumFog, CRendElement *re, IShader *ef, SRenderShaderResources *sr, CCObject *obj, int nTempl, IShader *efState, int nSort)
{
  TRACE_CALL("fog %d RE %p %s shader %p '%s' res %p obj %p templ %d state %p '%s' sort %d", NumFog, re, re ? Trace_RETypeName(re->m_Type) : "",
    ef, ShaderName(ef), sr, obj, nTempl, efState, efState ? ShaderName(efState) : "", nSort);
  if (TRACE_DUMP)
  {
    Dump_RE(re);
    Dump_RenderResources(sr);
    Dump_Object(obj);
  }
  m_pReal->EF_AddEf(NumFog, re, ef, sr, obj, nTempl, efState, nSort);
  Watch_RenderElement(re);
  Watch_RendererObject(WT_CCObject, obj);
  Watch_RendererObject(WT_SRenderShaderResources, sr);
}

void CRendererProxy::EF_EndEf3D(int nFlags)
{
  TRACE_CALL("flags 0x%x", nFlags);
  // What drawing the frame changes in the shadow casters (frustums, light sources).
  const void *pCasters = TRACE_DUMP ? g_pTraceLastShadowCasters : NULL;
  Dump_OpaqueList("shadow casters before EF_EndEf3D", pCasters);
  m_pReal->EF_EndEf3D(nFlags);
  Dump_OpaqueList("shadow casters after EF_EndEf3D", pCasters);
  g_pTraceLastShadowCasters = NULL;
}

void CRendererProxy::EF_EndEf2D(bool bSort)
{
  TRACE_CALL("sort %d", bSort);
  m_pReal->EF_EndEf2D(bSort);
}

//////////////////////////////////////////////////////////////////////////
// Lights

bool CRendererProxy::EF_IsFakeDLight(CDLight *Source)
{
  TRACE_CALL("%p", Source);
  bool bRes = m_pReal->EF_IsFakeDLight(Source);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::EF_ADDDlight(CDLight *Source)
{
  TRACE_CALL("%p", Source);
  if (TRACE_DUMP)
    Dump_Light(Source);
  CWatchArg WatchLight(WT_CDLight, Source);
  m_pReal->EF_ADDDlight(Source);
  TRACE_RESULT("id %d", Source ? Source->m_Id : -1);
}

void CRendererProxy::EF_ClearLightsList()
{
  TRACE_CALL("");
  m_pReal->EF_ClearLightsList();
}

bool CRendererProxy::EF_UpdateDLight(CDLight *pDL)
{
  TRACE_CALL("%p", pDL);
  if (TRACE_DUMP)
    Dump_Light(pDL);
  CWatchArg WatchLight(WT_CDLight, pDL);
  bool bRes = m_pReal->EF_UpdateDLight(pDL);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

//////////////////////////////////////////////////////////////////////////
// 2D shader drawing

bool CRendererProxy::EF_DrawEfForName(char *name, float x, float y, float width, float height, CFColor &col, int nTempl)
{
  TRACE_CALL("'%s' %g %g %gx%g color (%g %g %g %g) templ %d", name, x, y, width, height, col.r, col.g, col.b, col.a, nTempl);
  bool bRes = m_pReal->EF_DrawEfForName(name, x, y, width, height, col, nTempl);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_DrawEfForNum(int num, float x, float y, float width, float height, CFColor &col, int nTempl)
{
  TRACE_CALL("%d %g %g %gx%g color (%g %g %g %g) templ %d", num, x, y, width, height, col.r, col.g, col.b, col.a, nTempl);
  bool bRes = m_pReal->EF_DrawEfForNum(num, x, y, width, height, col, nTempl);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_DrawEf(IShader *ef, float x, float y, float width, float height, CFColor &col, int nTempl)
{
  TRACE_CALL("shader %p '%s' %g %g %gx%g color (%g %g %g %g) templ %d", ef, ShaderName(ef), x, y, width, height, col.r, col.g, col.b, col.a, nTempl);
  bool bRes = m_pReal->EF_DrawEf(ef, x, y, width, height, col, nTempl);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_DrawEf(SShaderItem si, float x, float y, float width, float height, CFColor &col, int nTempl)
{
  TRACE_CALL("shader %p '%s' res %p %g %g %gx%g color (%g %g %g %g) templ %d", si.m_pShader, ShaderName(si.m_pShader), si.m_pShaderResources,
    x, y, width, height, col.r, col.g, col.b, col.a, nTempl);
  bool bRes = m_pReal->EF_DrawEf(si, x, y, width, height, col, nTempl);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_DrawPartialEfForName(char *name, SVrect *vr, SVrect *pr, CFColor &col)
{
  TRACE_CALL("'%s' rect %p part %p color (%g %g %g %g)", name, vr, pr, col.r, col.g, col.b, col.a);
  bool bRes = m_pReal->EF_DrawPartialEfForName(name, vr, pr, col);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_DrawPartialEfForNum(int num, SVrect *vr, SVrect *pr, CFColor &col)
{
  TRACE_CALL("%d rect %p part %p color (%g %g %g %g)", num, vr, pr, col.r, col.g, col.b, col.a);
  bool bRes = m_pReal->EF_DrawPartialEfForNum(num, vr, pr, col);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::EF_DrawPartialEf(IShader *ef, SVrect *vr, SVrect *pr, CFColor &col, float iwdt, float ihgt)
{
  TRACE_CALL("shader %p '%s' rect %p part %p color (%g %g %g %g) %gx%g", ef, ShaderName(ef), vr, pr, col.r, col.g, col.b, col.a, iwdt, ihgt);
  bool bRes = m_pReal->EF_DrawPartialEf(ef, vr, pr, col, iwdt, ihgt);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

//////////////////////////////////////////////////////////////////////////
// Queries, world

void *CRendererProxy::EF_Query(int Query, int Param)
{
  TRACE_CALL("%s(%d) param %d", QueryName(Query), Query, Param);
  void *pRes = m_pReal->EF_Query(Query, Param);
  if (bTraced)
  {
    switch (Query)
    {
    case EFQ_Pointer2FrameID:
    case EFQ_DeviceLost:
      // Pointers to ints inside the original renderer (see RetailRenderer.h).
      TRACE_RESULT("%p (%s) = %d", pRes, Trace_Addr(pRes), pRes ? *(int *)pRes : 0);
      break;
    case EFQ_LightSource:
      TRACE_RESULT("CDLight %p", pRes);
      if (TRACE_DUMP && pRes)
        Dump_Light((CDLight *)pRes);
      break;
    default:
      TRACE_RESULT("%p (%d)", pRes, (int)(INT_PTR)pRes);
      break;
    }
  }
  return pRes;
}

void CRendererProxy::EF_ConstructEf(IShader *Ef)
{
  TRACE_CALL("%p '%s'", Ef, ShaderName(Ef));
  m_pReal->EF_ConstructEf(Ef);
}

void CRendererProxy::EF_SetWorldColor(float r, float g, float b, float a)
{
  TRACE_CALL("(%g %g %g %g)", r, g, b, a);
  m_pReal->EF_SetWorldColor(r, g, b, a);
}

int CRendererProxy::EF_RegisterFogVolume(float fMaxFogDist, float fFogLayerZ, CFColor color, int nIndex, bool bCaustics)
{
  TRACE_CALL("maxDist %g layerZ %g color (%g %g %g %g) index %d caustics %d", fMaxFogDist, fFogLayerZ, color.r, color.g, color.b, color.a,
    nIndex, bCaustics);
  int nRes = m_pReal->EF_RegisterFogVolume(fMaxFogDist, fFogLayerZ, color, nIndex, bCaustics);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

float CRendererProxy::EF_GetWaterZElevation(float fX, float fY)
{
  TRACE_CALL("%g %g", fX, fY);
  float fRes = m_pReal->EF_GetWaterZElevation(fX, fY);
  TRACE_RESULT("%g", fRes);
  return fRes;
}
