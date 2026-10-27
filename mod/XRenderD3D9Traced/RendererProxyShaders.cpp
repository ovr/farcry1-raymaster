//////////////////////////////////////////////////////////////////////
//
//	File: RendererProxyShaders.cpp
//	Description: CRendererProxy - EF_* methods: shaders, textures,
//	render elements, render objects, lights.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <map>
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

//! Names of the shaders by their IDs (IShader::GetID), for the methods that take a shader ID.
static char s_szShaderNames[4096][48];

static void RememberShader(IShader *pSH)
{
  if (!pSH)
    return;
  int nID = pSH->GetID();
  if (nID >= 0 && nID < (int)(sizeof(s_szShaderNames) / sizeof(s_szShaderNames[0])))
  {
    _snprintf(s_szShaderNames[nID], sizeof(s_szShaderNames[nID]) - 1, "%s", ShaderName(pSH));
    s_szShaderNames[nID][sizeof(s_szShaderNames[nID]) - 1] = 0;
  }
}

static const char *ShaderNameByID(int nID)
{
  if (nID >= 0 && nID < (int)(sizeof(s_szShaderNames) / sizeof(s_szShaderNames[0])) && s_szShaderNames[nID][0])
    return s_szShaderNames[nID];
  return "?";
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
  TRACE_CALL("shader %d '%s' points %d verts %p obj %p fog %d", Ef, ShaderNameByID(Ef), numPts, verts, obj, nFogID);
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
  TRACE_CALL("shader %d '%s' points %d verts %p obj %p inds %p (%d) fog %d", Ef, ShaderNameByID(Ef), numPts, verts, obj, inds, ninds, nFogID);
  if (TRACE_DUMP)
  {
    Dump_ColorVerts(verts, numPts);
    if (obj)
      Dump_Object(obj);
  }
  // XRENDERTRACED_SKIP_SPRITES=particlelight_fp,...: sprites of these shaders are not passed on,
  // to tell which Direct3D draw belongs to which sprite. The renderer returns the object it was
  // given, so the proxy does the same.
  static char s_szSkip[256] = "?";
  if (s_szSkip[0] == '?' && !GetEnvironmentVariableA("XRENDERTRACED_SKIP_SPRITES", s_szSkip, sizeof(s_szSkip)))
    s_szSkip[0] = 0;
  if (s_szSkip[0] && obj)
  {
    const char *szName = ShaderNameByID(Ef);
    size_t nLen = strlen(szName);
    for (const char *p = s_szSkip; nLen && (p = strstr(p, szName)) != NULL; p += nLen)
    {
      if ((p == s_szSkip || p[-1] == ',') && (p[nLen] == 0 || p[nLen] == ','))
      {
        TRACE_RESULT("obj %p (skipped)", obj);
        return obj;
      }
    }
  }
  CCObject *pRes = m_pReal->EF_AddSpriteToScene(Ef, numPts, verts, obj, inds, ninds, nFogID);
  TRACE_RESULT("obj %p", pRes);
  return pRes;
}

void CRendererProxy::EF_AddPolyToScene2D(int Ef, int numPts, SColorVert2D *verts)
{
  TRACE_CALL("shader %d '%s' points %d verts %p", Ef, ShaderNameByID(Ef), numPts, verts);
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
  RememberShader(pRes);
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
  // XRENDERTRACED_TEMPLATE=TemplDecalModulate: every material that names a template ("Templ...")
  // gets this one instead, to see how the renderer draws it; with XRENDERTRACED_TEMPLATE_ONLY
  // ("name,name,...") only the materials of those names, and XRENDERTRACED_TEMPLATE_OPACITY
  // replaces their opacity as well.
  static char s_szTemplate[64] = "?";
  static bool s_bOnlyListed = false;
  static float s_fOpacity = -1;
  if (s_szTemplate[0] == '?')
  {
    if (!GetEnvironmentVariableA("XRENDERTRACED_TEMPLATE", s_szTemplate, sizeof(s_szTemplate)))
      s_szTemplate[0] = 0;
    s_bOnlyListed = GetEnvironmentVariableA("XRENDERTRACED_TEMPLATE_ONLY", NULL, 0) != 0;
    char szOpacity[32];
    if (GetEnvironmentVariableA("XRENDERTRACED_TEMPLATE_OPACITY", szOpacity, sizeof(szOpacity)))
      s_fOpacity = (float)atof(szOpacity);
  }
  if (s_szTemplate[0] && templName && !_strnicmp(templName, "Templ", 5) &&
      (!s_bOnlyListed || name && Trace_IsListed("XRENDERTRACED_TEMPLATE_ONLY", name)))
  {
    templName = s_szTemplate;
    if (s_bOnlyListed && s_fOpacity >= 0 && Res)
      Res->m_Opacity = s_fOpacity;
  }
  SShaderItem Item = m_pReal->EF_LoadShaderItem(name, Class, bShare, templName, flags, Res, nMaskGen);
  TRACE_RESULT("shader %p res %p", Item.m_pShader, Item.m_pShaderResources);
  Watch_RendererObject(WT_SRenderShaderResources, Item.m_pShaderResources);
  VCallHooks_Install("IShader", Item.m_pShader);
  VCallHooks_Install("SRenderShaderResources", Item.m_pShaderResources);
  RememberShader(Item.m_pShader);
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
  VCallHooks_Install("CCObject", pRes);
  return pRes;
}

//! Probe values from the environment: groups of nCount numbers ("1.5 3 6" for one, "x y z x y z"
//! for three), taken in turn, one group per frame. Returns false (pOut unchanged) without any.
static bool ProbeValues(const char *szVar, float *pOut, int nCount, int nFrame)
{
  const char *szValue = getenv(szVar);
  if (!szValue)
    return false;
  float Values[96];
  int nValues = 0;
  for (const char *p = szValue; *p && nValues < 96;)
  {
    char *pEnd;
    Values[nValues] = (float)strtod(p, &pEnd);
    if (pEnd == p)
      break;
    nValues++;
    p = pEnd;
  }
  const int nGroups = nValues / nCount;
  if (!nGroups)
    return false;
  memcpy(pOut, &Values[(nFrame % nGroups) * nCount], nCount * sizeof(float));
  return true;
}

//! A probe value from the environment (ProbeValues with one number per frame).
static float ProbeValue(const char *szVar, float fDefault, int nFrame)
{
  ProbeValues(szVar, &fDefault, 1, nFrame);
  return fDefault;
}

//! The camera's line of sight, turned by XRENDERTRACED_FLARE_DIR="dx dy" (dx times the right and dy
//! times the up vector of the view; the view space looks down -z).
static Vec3 ProbeDirection(const CCamera &Cam)
{
  const Matrix44 View = Cam.GetVCMatrixD3D9();
  float fRight = 0, fUp = 0;
  if (const char *szDir = getenv("XRENDERTRACED_FLARE_DIR"))
    sscanf_s(szDir, "%f %f", &fRight, &fUp);
  Vec3 Dir = Vec3(-View(0, 2), -View(1, 2), -View(2, 2)) + Vec3(View(0, 0), View(1, 0), View(2, 0)) * fRight
    + Vec3(View(0, 1), View(1, 1), View(2, 1)) * fUp;
  Dir.Normalize();
  return Dir;
}

void CRendererProxy::EF_AddEf(int NumFog, CRendElement *re, IShader *ef, SRenderShaderResources *sr, CCObject *obj, int nTempl, IShader *efState, int nSort)
{
  TRACE_CALL("fog %d RE %p %s shader %p '%s' res %p obj %p templ %d state %p '%s' sort %d", NumFog, re, re ? Trace_RETypeName(re->m_Type) : "",
    ef, ShaderName(ef), sr, obj, nTempl, efState, efState ? ShaderName(efState) : "", nSort);
  if (re && re->m_Type == eDATA_ScreenProcess)
    m_pScreenRE = re;
  if (re && re->m_Type == eDATA_Sky)
  {
    if (m_bSkyWater)
      ((CRESky *)re)->m_fTerrainWaterLevel = m_fSkyWater;
    if (m_bSkyStretching)
      ((CRESky *)re)->m_fSkyBoxStretching = m_fSkyStretching;
    if (m_bSkyAlpha)
      ((CRESky *)re)->m_fAlpha = m_fSkyAlpha;
  }
  if (TRACE_DUMP)
  {
    Dump_RE(re);
    Dump_RenderResources(sr);
    Dump_Object(obj);
  }
  // XRENDERTRACED_PROBE=FlareInView: a flare is moved onto the line of sight of the camera, at its
  // own distance or XRENDERTRACED_FLARE_DIST (ProbeValue), to see how the renderer draws one in
  // view (ProbeDirection).
  if (re && re->m_Type == eDATA_Flare && obj && Trace_IsListed("XRENDERTRACED_PROBE", "FlareInView"))
  {
    const CCamera &Cam = m_pReal->GetCamera();
    const Vec3 Eye = Cam.GetPos();
    const Vec3 Pos(obj->m_Matrix(3, 0), obj->m_Matrix(3, 1), obj->m_Matrix(3, 2));
    const Vec3 New = Eye + ProbeDirection(Cam) * ProbeValue("XRENDERTRACED_FLARE_DIST", (Pos - Eye).Length(), m_nFrames);
    Trace_Write("    probe: flare from (%.3f %.3f %.3f) to (%.3f %.3f %.3f)", Pos.x, Pos.y, Pos.z, New.x, New.y, New.z);
    obj->m_Matrix(3, 0) = New.x;
    obj->m_Matrix(3, 1) = New.y;
    obj->m_Matrix(3, 2) = New.z;
  }
  // XRENDERTRACED_PROBE=UserTemplParams: an object added with a user template (nTempl >=
  // EFT_USER_FIRST) gets shader params of the proxy instead of the engine's: one float
  // XRENDERTRACED_PARAM_NAME = XRENDERTRACED_PARAM_VALUE (ProbeValue), none for a negative value.
  if (obj && nTempl >= EFT_USER_FIRST && Trace_IsListed("XRENDERTRACED_PROBE", "UserTemplParams"))
  {
    static TArray<SShaderParam> s_Params;
    const char *szName = getenv("XRENDERTRACED_PARAM_NAME");
    const float fValue = ProbeValue("XRENDERTRACED_PARAM_VALUE", -1, m_nFrames);
    if (fValue < 0)
      obj->m_ShaderParams = NULL;
    else
    {
      if (!s_Params.Num())
        s_Params.AddIndex(1);
      strncpy_s(s_Params[0].m_Name, szName ? szName : "refraction", _TRUNCATE);
      s_Params[0].m_Type = eType_FLOAT;
      s_Params[0].m_nMaterial = -1;
      s_Params[0].m_Value.m_Float = fValue;
      obj->m_ShaderParams = &s_Params;
    }
    Trace_Write("    probe: object params %s %g", fValue < 0 ? "none" : s_Params[0].m_Name, fValue);
  }
  // XRENDERTRACED_PROBE=Nearest: an element added with sort eS_Nearest or an object with
  // FOB_NEAREST (the first person weapon) gets the sort XRENDERTRACED_NEAREST_SORT (ProbeValue), and
  // FOB_NEAREST is cleared with XRENDERTRACED_NEAREST_FLAG=0, to see which of the two the renderer
  // goes by.
  if (obj && (nSort == eS_Nearest || (obj->m_ObjFlags & FOB_NEAREST)) && Trace_IsListed("XRENDERTRACED_PROBE", "Nearest"))
  {
    const int nOldSort = nSort;
    const int nOldFlags = obj->m_ObjFlags;
    nSort = (int)ProbeValue("XRENDERTRACED_NEAREST_SORT", (float)nSort, m_nFrames);
    if (ProbeValue("XRENDERTRACED_NEAREST_FLAG", 1, m_nFrames) == 0)
      obj->m_ObjFlags &= ~FOB_NEAREST;
    Trace_Write("    probe: sort %d -> %d, flags 0x%x -> 0x%x", nOldSort, nSort, nOldFlags, obj->m_ObjFlags);
  }
  // XRENDERTRACED_FLARES=1: the object of a flare as the engine passes it (with what the renderer
  // left in it the frame before: its fade), every frame, and again after EF_EndEf3D.
  if (re && re->m_Type == eDATA_Flare && obj && Trace_IsListed("XRENDERTRACED_FLARES", "1"))
  {
    m_pFlareObj = obj;
    WriteFlareObject(obj, "", ShaderName(ef));
  }
  // The elements of a shader (GetREs, e.g. the flares) reach the renderer only here.
  VCallHooks_InstallRE(re);
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
  if (m_pFlareObj)
    WriteFlareObject(m_pFlareObj, " after EF_EndEf3D", "");
  if (m_pCoronaObj)
    WriteFlareObject(m_pCoronaObj, " corona after EF_EndEf3D", "");
  m_pFlareObj = NULL;
  m_pCoronaObj = NULL;
}

void CRendererProxy::WriteFlareObject(const CCObject *obj, const char *szWhen, const char *szShader)
{
  static int s_nLines = 0;
  if (s_nLines++ >= 40000)
    return;
  const Vec3 Eye = m_pReal->GetCamera().GetPos();
  const Vec3 Pos(obj->m_Matrix(3, 0), obj->m_Matrix(3, 1), obj->m_Matrix(3, 2));
  Trace_Write("[flare%s] frame %d time %.4f '%s' obj %d flags 0x%x color (%g %g %g %g) amb (%g %g %g) fadeTime %g startTime %g "
    "tempVars (%g %g %g %g %g) trans2 (%g %g %g) angs2 (%g %g %g) RE %p dist %.2f", szWhen, m_nFrames + 1,
    iTimer ? iTimer->GetCurrTime() : 0.0f, szShader, obj->m_Id, obj->m_ObjFlags, obj->m_Color.r, obj->m_Color.g, obj->m_Color.b,
    obj->m_Color.a, obj->m_AmbColor.x, obj->m_AmbColor.y, obj->m_AmbColor.z, obj->m_fBump, obj->m_fRefract, obj->m_TempVars[0],
    obj->m_TempVars[1], obj->m_TempVars[2], obj->m_TempVars[3], obj->m_TempVars[4], obj->m_Trans2[0], obj->m_Trans2[1],
    obj->m_Trans2[2], obj->m_Angs2[0], obj->m_Angs2[1], obj->m_Angs2[2], obj->m_RE, (Pos - Eye).Length());
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
  if (m_fLightRadius > 0 && Source && (Source->m_Flags & DLF_POINT) && !(Source->m_Flags & DLF_SUN))
  {
    Source->m_fRadius = m_fLightRadius;
    if (m_fLightDirect > 0)
      Source->m_fDirectFactor = m_fLightDirect;
    // The engine passes the same light object every frame: scale the colour it had first.
    static std::map<CDLight *, CFColor> s_OrigColors;
    if (m_fLightColor > 0)
    {
      if (!s_OrigColors.count(Source))
        s_OrigColors[Source] = Source->m_Color;
      const CFColor &Orig = s_OrigColors[Source];
      Source->m_Color = CFColor(Orig.r * m_fLightColor, Orig.g * m_fLightColor, Orig.b * m_fLightColor, 1);
    }
  }
  // XRENDERTRACED_PROBE=CoronaInView: a light with a corona is moved onto the line of sight of the
  // camera (ProbeDirection), XRENDERTRACED_CORONA_DIST (5) units in front of it, to see how the
  // renderer draws its corona; XRENDERTRACED_CORONA_SCALE replaces its m_fCoronaScale (both
  // ProbeValue).
  if (Source && Source->m_fCoronaScale > 0 && Trace_IsListed("XRENDERTRACED_PROBE", "CoronaInView"))
  {
    const CCamera &Cam = m_pReal->GetCamera();
    const Vec3 New = Cam.GetPos() + ProbeDirection(Cam) * ProbeValue("XRENDERTRACED_CORONA_DIST", 5.0f, m_nFrames);
    Source->m_fCoronaScale = ProbeValue("XRENDERTRACED_CORONA_SCALE", Source->m_fCoronaScale, m_nFrames);
    Trace_Write("    probe: light from (%.3f %.3f %.3f) to (%.3f %.3f %.3f)", Source->m_Origin.x, Source->m_Origin.y,
      Source->m_Origin.z, New.x, New.y, New.z);
    Source->m_Origin = New;
  }
  // XRENDERTRACED_PROBE=LightAt: the point lights (not the sun) are put at the world positions of
  // XRENDERTRACED_LIGHT_POS ("x y z x y z ...", ProbeValues), to see how the renderer lights an
  // object from known places.
  if (Source && (Source->m_Flags & DLF_POINT) && !(Source->m_Flags & DLF_SUN) && Trace_IsListed("XRENDERTRACED_PROBE", "LightAt"))
  {
    float Pos[3];
    if (ProbeValues("XRENDERTRACED_LIGHT_POS", Pos, 3, m_nFrames))
    {
      Trace_Write("    probe: light from (%.3f %.3f %.3f) to (%.3f %.3f %.3f)", Source->m_Origin.x, Source->m_Origin.y,
        Source->m_Origin.z, Pos[0], Pos[1], Pos[2]);
      Source->m_Origin = Vec3(Pos[0], Pos[1], Pos[2]);
    }
  }
  if (TRACE_DUMP)
    Dump_Light(Source);
  CWatchArg WatchLight(WT_CDLight, Source);
  m_pReal->EF_ADDDlight(Source);
  TRACE_RESULT("id %d", Source ? Source->m_Id : -1);
  if (Source)
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
        if (Source->m_pObject[i][j])
        {
          if (TRACE_DUMP)
            Trace_Write("    object [%d][%d] %p after the call", i, j, Source->m_pObject[i][j]);
          if (Trace_IsListed("XRENDERTRACED_FLARES", "1"))
          {
            m_pCoronaObj = Source->m_pObject[i][j];
            WriteFlareObject(m_pCoronaObj, " corona", Source->m_pShader ? Source->m_pShader->GetName() : "");
          }
        }
}

void CRendererProxy::EF_ClearLightsList()
{
  TRACE_CALL("");
  m_pReal->EF_ClearLightsList();
}

//! XRENDERTRACED_PROJ_IMAGE (a picture file, loaded as the engine loads light images),
//! XRENDERTRACED_PROJ_ANGLES ("x y z", m_ProjAngles) and XRENDERTRACED_PROJ_POS ("x y z", m_Origin)
//! replace those of the projected lights (DLF_PROJECT), to see which way up the picture is cast.
void CRendererProxy::ProbeProjector(CDLight *pDL)
{
  if (!pDL || !(pDL->m_Flags & DLF_PROJECT))
    return;
  float Values[3];
  if (ProbeValues("XRENDERTRACED_PROJ_ANGLES", Values, 3, m_nFrames))
    pDL->m_ProjAngles = Vec3(Values[0], Values[1], Values[2]);
  if (ProbeValues("XRENDERTRACED_PROJ_POS", Values, 3, m_nFrames))
    pDL->m_Origin = Vec3(Values[0], Values[1], Values[2]);
  if (const char *szImage = getenv("XRENDERTRACED_PROJ_IMAGE"))
  {
    static ITexPic *s_pImage = NULL;
    if (!s_pImage)
    {
      s_pImage = m_pReal->EF_LoadTexture(szImage, 0, FT2_FORCECUBEMAP, eTT_Cubemap);
      Trace_Write("    probe: projector image '%s' -> %p (id %d, loaded %d)", szImage, s_pImage, s_pImage ? s_pImage->GetTextureID() : -1,
        s_pImage ? s_pImage->IsTextureLoaded() : 0);
    }
    pDL->m_pLightImage = s_pImage;
  }
}

bool CRendererProxy::EF_UpdateDLight(CDLight *pDL)
{
  TRACE_CALL("%p", pDL);
  ProbeProjector(pDL);
  if (TRACE_DUMP)
    Dump_Light(pDL);
  CWatchArg WatchLight(WT_CDLight, pDL);
  // XRENDERTRACED_PROBE=ProjectorFrustum: a projected light gets the frustum angles 15, 30, ..., 90
  // in turn for this call (restored after it), and the texture matrix the renderer makes is written.
  static int s_nProbe = 0;
  const bool bProbe = pDL && (pDL->m_Flags & DLF_PROJECT) && Trace_IsListed("XRENDERTRACED_PROBE", "ProjectorFrustum");
  const float fFrustum = pDL ? pDL->m_fLightFrustumAngle : 0;
  if (bProbe)
    pDL->m_fLightFrustumAngle = 15.0f * (1 + s_nProbe++ % 6);
  bool bRes = m_pReal->EF_UpdateDLight(pDL);
  if (bProbe)
  {
    const Matrix44 &M = pDL->m_TextureMatrix;
    Trace_Write("    probe: frustum %.0f -> textureMatrix [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g]",
      pDL->m_fLightFrustumAngle, M(0, 0), M(0, 1), M(0, 2), M(0, 3), M(1, 0), M(1, 1), M(1, 2), M(1, 3), M(2, 0), M(2, 1),
      M(2, 2), M(2, 3), M(3, 0), M(3, 1), M(3, 2), M(3, 3));
    pDL->m_fLightFrustumAngle = fFrustum;
  }
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
