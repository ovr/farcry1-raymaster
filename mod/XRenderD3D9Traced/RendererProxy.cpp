//////////////////////////////////////////////////////////////////////
//
//	File: RendererProxy.cpp
//	Description: CRendererProxy - device, frame, 2D/3D drawing, matrices,
//	textures and fonts.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "FieldWatch.h"
#include "RendererProxy.h"
#include "LeafBufferHooks.h"
#include "D3DState.h"
#include "D3DDraws.h"
#include "VCallHooks.h"
#include <I3DEngine.h>
#include <IStatObj.h>

static const void *s_pProxyVTable = NULL;

CRendererProxy::CRendererProxy(IRenderer *pReal, int nRealType)
{
  m_pReal = pReal;
  m_nRealType = nRealType;
  m_nFrames = 0;
  s_pProxyVTable = *(const void *const *)this;

  // Lets a trace run reach a given state without input, e.g. XRENDERTRACED_EXEC="map training".
  m_szExec[0] = 0;
  m_nExecFrame = 60;
  DWORD nLen = GetEnvironmentVariableA("XRENDERTRACED_EXEC", m_szExec, sizeof(m_szExec));
  if (nLen >= sizeof(m_szExec))
    m_szExec[0] = 0;
  char szFrame[32];
  nLen = GetEnvironmentVariableA("XRENDERTRACED_EXEC_FRAME", szFrame, sizeof(szFrame));
  if (nLen && nLen < sizeof(szFrame))
    m_nExecFrame = atoi(szFrame);
  if (m_szExec[0])
    Trace_Write("Will execute '%s' after frame %d", m_szExec, m_nExecFrame);
  m_nExecLevelAfterLoad = 100;
  m_nExecLevelFrame = -1;
  nLen = GetEnvironmentVariableA("XRENDERTRACED_EXEC_LEVEL", m_szExecLevel, sizeof(m_szExecLevel));
  if (!nLen || nLen >= sizeof(m_szExecLevel))
    m_szExecLevel[0] = 0;
  nLen = GetEnvironmentVariableA("XRENDERTRACED_EXEC_LEVEL_FRAME", szFrame, sizeof(szFrame));
  if (nLen && nLen < sizeof(szFrame))
    m_nExecLevelAfterLoad = atoi(szFrame);
  if (m_szExecLevel[0])
    Trace_Write("Will execute '%s' %d frames after each level load", m_szExecLevel, m_nExecLevelAfterLoad);
  Watch_Init();

  // A complete frame of a level in the log, e.g. XRENDERTRACED_CAPTURE=300.
  m_nCaptureAfterLoad = 0;
  m_nCaptureFrame = -1;
  nLen = GetEnvironmentVariableA("XRENDERTRACED_CAPTURE", szFrame, sizeof(szFrame));
  if (nLen && nLen < sizeof(szFrame))
    m_nCaptureAfterLoad = atoi(szFrame);
  if (m_nCaptureAfterLoad > 0)
    Trace_Write("Will capture frame %d after each level load", m_nCaptureAfterLoad);
}

CRendererProxy::~CRendererProxy()
{
}

IRenderer *CRendererProxy::Unwrap(IRenderer *pRenderer)
{
  if (pRenderer && s_pProxyVTable && *(const void *const *)pRenderer == s_pProxyVTable)
    return ((CRendererProxy *)pRenderer)->m_pReal;
  return pRenderer;
}

//////////////////////////////////////////////////////////////////////////
// Device

WIN_HWND CRendererProxy::Init(int x, int y, int width, int height, unsigned int cbpp, int zbpp, int sbits, bool fullscreen, WIN_HINSTANCE hinst, WIN_HWND Glhwnd, WIN_HDC Glhdc, WIN_HGLRC hGLrc, bool bReInit)
{
  TRACE_CALL("x %d y %d %dx%d cbpp %u zbpp %d sbits %d fullscreen %d hinst %p hwnd %p hdc %p hglrc %p reinit %d",
    x, y, width, height, cbpp, zbpp, sbits, fullscreen, hinst, Glhwnd, Glhdc, hGLrc, bReInit);
  WIN_HWND hWnd = m_pReal->Init(x, y, width, height, cbpp, zbpp, sbits, fullscreen, hinst, Glhwnd, Glhdc, hGLrc, bReInit);
  TRACE_RESULT("hwnd %p, %dx%d, color %d depth %d stencil %d bits, features 0x%x", hWnd, m_pReal->GetWidth(), m_pReal->GetHeight(),
    m_pReal->GetColorBpp(), m_pReal->GetDepthBpp(), m_pReal->GetStencilBpp(), m_pReal->GetFeatures());
  // The console exists by now.
  Trace_RegisterCVars();
  return hWnd;
}

bool CRendererProxy::SetCurrentContext(WIN_HWND hWnd)
{
  TRACE_CALL("hwnd %p", hWnd);
  bool bRes = m_pReal->SetCurrentContext(hWnd);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::CreateContext(WIN_HWND hWnd, bool bAllowFSAA)
{
  TRACE_CALL("hwnd %p allowFSAA %d", hWnd, bAllowFSAA);
  bool bRes = m_pReal->CreateContext(hWnd, bAllowFSAA);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::DeleteContext(WIN_HWND hWnd)
{
  TRACE_CALL("hwnd %p", hWnd);
  bool bRes = m_pReal->DeleteContext(hWnd);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

int CRendererProxy::GetFeatures()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetFeatures();
  TRACE_RESULT("0x%x", nRes);
  return nRes;
}

int CRendererProxy::GetMaxTextureMemory()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetMaxTextureMemory();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

void CRendererProxy::ShutDown(bool bReInit)
{
  TRACE_CALL("reinit %d", bReInit);
  m_pReal->ShutDown(bReInit);
}

int CRendererProxy::EnumDisplayFormats(TArray<SDispFormat> &Formats, bool bReset)
{
  TRACE_CALL("formats %p (%d) reset %d", &Formats, Formats.Num(), bReset);
  int nRes = m_pReal->EnumDisplayFormats(Formats, bReset);
  TRACE_RESULT("%d, formats %d", nRes, Formats.Num());
  if (TRACE_DUMP)
  {
    for (int i = 0; i < Formats.Num() && i < 64; i++)
      Trace_Write("      %dx%dx%d", Formats[i].m_Width, Formats[i].m_Height, Formats[i].m_BPP);
  }
  return nRes;
}

bool CRendererProxy::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen)
{
  TRACE_CALL("%dx%dx%d %dHz fullscreen %d", nNewWidth, nNewHeight, nNewColDepth, nNewRefreshHZ, bFullScreen);
  bool bRes = m_pReal->ChangeResolution(nNewWidth, nNewHeight, nNewColDepth, nNewRefreshHZ, bFullScreen);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::Release()
{
  TRACE_CALL("");
  Watch_WriteSummary("renderer released");
  VCallHooks_WriteSummary("renderer released");
  LeafBufferHooks_Uninstall();
  VCallHooks_Uninstall();
  m_pReal->Release();
  m_pReal = NULL;
  Trace_Write("Renderer released after %d frames", m_nFrames);
  delete this;
}

void CRendererProxy::FreeResources(int nFlags)
{
  TRACE_CALL("flags 0x%x", nFlags);
  m_pReal->FreeResources(nFlags);
}

void CRendererProxy::RefreshResources(int nFlags)
{
  TRACE_CALL("flags 0x%x", nFlags);
  m_pReal->RefreshResources(nFlags);
}

void CRendererProxy::PreLoad()
{
  // Level loading: trace its calls from the start.
  Trace_Restart("PreLoad");
  TRACE_CALL("");
  m_pReal->PreLoad();
}

void CRendererProxy::PostLoad()
{
  TRACE_CALL("");
  m_pReal->PostLoad();
  // The first frames of the level.
  Trace_Restart("PostLoad");
  if (m_nCaptureAfterLoad > 0)
    m_nCaptureFrame = m_nFrames + m_nCaptureAfterLoad;
  if (m_szExecLevel[0])
    m_nExecLevelFrame = m_nFrames + m_nExecLevelAfterLoad;
}

//////////////////////////////////////////////////////////////////////////
// Frame

void CRendererProxy::BeginFrame()
{
  TRACE_CALL("frame %d", m_nFrames + 1);
  Watch_NewFrame();
  if (Trace_IsCapturing())
    D3DDraws_Begin(m_pReal);
  m_pReal->BeginFrame();
}

void CRendererProxy::Update()
{
  {
    TRACE_CALL("frame %d", m_nFrames + 1);
    m_pReal->Update();
    D3DDraws_End();
    Trace_FrameEnd(++m_nFrames);
    if (m_nFrames + 1 == m_nCaptureFrame)
      Trace_CaptureFrame();
  }
  // Outside the traced call: the commands run engine code (a level load) that calls the renderer,
  // and those calls are calls of the engine, not of the renderer to itself.
  if (m_nFrames == m_nExecFrame && m_szExec[0])
  {
    ExecuteCommands(m_szExec);
    m_szExec[0] = 0;
  }
  if (m_nFrames == m_nExecLevelFrame)
    ExecuteCommands(m_szExecLevel);
}

void CRendererProxy::ExecuteCommands(const char *szCommands)
{
  if (!iConsole)
    return;
  char szCopy[2048];
  strcpy_s(szCopy, szCommands);
  char *szContext = NULL;
  for (char *szCmd = strtok_s(szCopy, ";", &szContext); szCmd; szCmd = strtok_s(NULL, ";", &szContext))
  {
    while (*szCmd == ' ')
      szCmd++;
    Trace_Write("Executing '%s'", szCmd);
    if (!strncmp(szCmd, "screenfx ", 9))
      ScreenFxCommand(szCmd + 9);
    else if (!strncmp(szCmd, "particles", 9))
      ParticlesCommand(szCmd + 9);
    else if (!strncmp(szCmd, "cvars", 5) && (!szCmd[5] || szCmd[5] == ' '))
      CVarsCommand(szCmd + 5);
    else if (!strncmp(szCmd, "veggroup ", 9))
      VegGroupCommand(szCmd + 9);
    else if (!strncmp(szCmd, "lightradius ", 12))
    {
      m_fLightDirect = m_fLightColor = 0;
      sscanf_s(szCmd + 12, "%f %f %f", &m_fLightRadius, &m_fLightDirect, &m_fLightColor);
      Trace_Write("  point lights get radius %g direct factor %g colour scale %g", m_fLightRadius, m_fLightDirect, m_fLightColor);
    }
    else if (!strncmp(szCmd, "view ", 5))
    {
      // "view maxdist|- fogstart|- fogend|-": I3DEngine::SetMaxViewDistance, SetFogStart, SetFogEnd.
      I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : NULL;
      char szArg[3][32] = { "-", "-", "-" };
      sscanf_s(szCmd + 5, "%31s %31s %31s", szArg[0], (unsigned)sizeof(szArg[0]), szArg[1], (unsigned)sizeof(szArg[1]), szArg[2], (unsigned)sizeof(szArg[2]));
      if (p3DEngine && strcmp(szArg[0], "-"))
        p3DEngine->SetMaxViewDistance((float)atof(szArg[0]));
      if (p3DEngine && strcmp(szArg[1], "-"))
        p3DEngine->SetFogStart((float)atof(szArg[1]));
      if (p3DEngine && strcmp(szArg[2], "-"))
        p3DEngine->SetFogEnd((float)atof(szArg[2]));
      if (p3DEngine)
        Trace_Write("  view distance %g fog %g to %g", p3DEngine->GetMaxViewDistance(), p3DEngine->GetFogStart(), p3DEngine->GetFogEnd());
    }
    else if (!strncmp(szCmd, "sky ", 4))
    {
      char szWater[32] = "-", szStretching[32] = "-", szAlpha[32] = "-";
      sscanf_s(szCmd + 4, "%31s %31s %31s", szWater, (unsigned)sizeof(szWater), szStretching, (unsigned)sizeof(szStretching),
        szAlpha, (unsigned)sizeof(szAlpha));
      m_bSkyWater = strcmp(szWater, "-") != 0;
      m_bSkyStretching = strcmp(szStretching, "-") != 0;
      m_bSkyAlpha = strcmp(szAlpha, "-") != 0;
      m_fSkyWater = (float)atof(szWater);
      m_fSkyStretching = (float)atof(szStretching);
      m_fSkyAlpha = (float)atof(szAlpha);
      Trace_Write("  sky gets water level %s stretching %s alpha %s", szWater, szStretching, szAlpha);
    }
    else
      iConsole->ExecuteString(szCmd, false, true);
  }
}

//! Writes the console variables whose names start with a prefix (IConsole::DumpCVars).
struct SCVarTraceSink : public ICVarDumpSink
{
  const char *m_szPrefix;
  int m_nFound;

  virtual void OnElementFound(ICVar *pCVar)
  {
    const char *szName = pCVar ? pCVar->GetName() : NULL;
    if (!szName || _strnicmp(szName, m_szPrefix, strlen(m_szPrefix)))
      return;
    m_nFound++;
    const char *szValue = pCVar->GetString();
    Trace_Write("    cvar %s = '%s' flags 0x%x", szName, szValue ? szValue : "", pCVar->GetFlags());
  }
};

void CRendererProxy::CVarsCommand(const char *szArgs)
{
  while (*szArgs == ' ')
    szArgs++;
  SCVarTraceSink Sink;
  Sink.m_szPrefix = szArgs;
  Sink.m_nFound = 0;
  iConsole->DumpCVars(&Sink);
  Trace_Write("    %d console variables starting with '%s'", Sink.m_nFound, szArgs);
}

void CRendererProxy::VegGroupCommand(const char *szArgs)
{
  I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : NULL;
  if (!p3DEngine)
    return;
  char szGroup[16] = "", szField[32] = "";
  float fValue = 0;
  if (sscanf_s(szArgs, "%15s %31s %f", szGroup, (unsigned)sizeof(szGroup), szField, (unsigned)sizeof(szField), &fValue) < 3)
    return;
  const bool bAll = !strcmp(szGroup, "*");
  int nChanged = 0;
  for (int g = bAll ? 0 : atoi(szGroup); g < (bAll ? 256 : atoi(szGroup) + 1); g++)
  {
    IStatInstGroup Group;
    if (!p3DEngine->GetStatInstGroup(g, Group) || !Group.pStatObj)
      continue;
    if (!strcmp(szField, "fadeSize"))
      Group.bFadeSize = fValue != 0;
    else if (!strcmp(szField, "useSprites"))
      Group.bUseSprites = fValue != 0;
    else if (!strcmp(szField, "maxViewDistRatio"))
      Group.fMaxViewDistRatio = fValue;
    else if (!strcmp(szField, "spriteDistRatio"))
      Group.fSpriteDistRatio = fValue;
    else if (!strcmp(szField, "brightness"))
      Group.fBrightness = fValue;
    else
      return;
    if (!p3DEngine->SetStatInstGroup(g, Group))
      continue;
    nChanged++;
    IStatInstGroup Check;
    if (p3DEngine->GetStatInstGroup(g, Check))
      Trace_Write("    group %d now: fadeSize %d useSprites %d maxViewDistRatio %g spriteDistRatio %g brightness %g", g,
        Check.bFadeSize, Check.bUseSprites, Check.fMaxViewDistRatio, Check.fSpriteDistRatio, Check.fBrightness);
  }
  Trace_Write("  SetStatInstGroup(%s %s %g): %d groups", szGroup, szField, fValue, nChanged);
}

void CRendererProxy::ScreenFxCommand(const char *szArgs)
{
  I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : NULL;
  if (!p3DEngine)
    return;
  char szEffect[64] = "", szValue[32] = "";
  if (sscanf_s(szArgs, "%63s %31s", szEffect, (unsigned)sizeof(szEffect), szValue, (unsigned)sizeof(szValue)) < 1)
    return;
  const bool bGet = szEffect[0] == '?';
  char *szName = szEffect + (bGet ? 1 : 0);
  char *szParam = strchr(szName, '.');
  if (szParam)
    *szParam++ = 0;
  if (bGet && szParam)
  {
    void *pValue = NULL;
    int nRes = p3DEngine->GetScreenFxParam(szName, szParam, pValue);
    Trace_Write("  GetScreenFxParam('%s', '%s') -> %d, value %p", szName, szParam, nRes, pValue);
  }
  else if (bGet)
    Trace_Write("  GetScreenFx('%s') -> %d", szName, p3DEngine->GetScreenFx(szName));
  else if (szParam)
  {
    // "i5" passes an integer, anything else a float.
    union { int n; float f; } Value;
    if (szValue[0] == 'i')
      Value.n = atoi(szValue + 1);
    else
      Value.f = (float)atof(szValue);
    p3DEngine->SetScreenFxParam(szName, szParam, &Value);
  }
  else
    p3DEngine->SetScreenFx(szName, atoi(szValue));
}

//! "particles [count] [blend type] [size]": spawns particles around the camera with
//! I3DEngine::SpawnParticles (default texture, 10 s of life), to see how the engine draws them.
void CRendererProxy::ParticlesCommand(const char *szArgs)
{
  I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : NULL;
  if (!p3DEngine)
    return;
  int nCount = 50, nBlend = ParticleBlendType_Additive;
  float fSize = 0.3f;
  sscanf_s(szArgs, "%d %d %f", &nCount, &nBlend, &fSize);
  ParticleParams Params;
  Params.vPosition = m_pReal->GetCamera().GetPos();
  Params.vDirection = Vec3(0, 0, 1);
  Params.fFocus = 0;
  Params.vColorStart = Vec3(1, 0.5f, 0.2f);
  Params.vColorEnd = Vec3(0.2f, 0.5f, 1);
  Params.fSpeed = 1.0f;
  Params.nCount = nCount;
  Params.fSize = fSize;
  Params.fLifeTime = 10.0f;
  Params.eBlendType = (ParticleBlendType)nBlend;
  Params.fScale = 1.0f;
  Trace_Write("  SpawnParticles(pos " TRACE_V3 " count %d blend %d size %g)", TRACE_V3_ARGS(Params.vPosition), nCount, nBlend, fSize);
  p3DEngine->SpawnParticles(Params);
}

void CRendererProxy::ShareResources(IRenderer *renderer)
{
  TRACE_CALL("renderer %p", renderer);
  m_pReal->ShareResources(Unwrap(renderer));
}

void CRendererProxy::GetViewport(int *x, int *y, int *width, int *height)
{
  TRACE_CALL("%p %p %p %p", x, y, width, height);
  m_pReal->GetViewport(x, y, width, height);
  TRACE_RESULT("%d %d %dx%d", x ? *x : 0, y ? *y : 0, width ? *width : 0, height ? *height : 0);
}

void CRendererProxy::SetViewport(int x, int y, int width, int height)
{
  TRACE_CALL("%d %d %dx%d", x, y, width, height);
  m_pReal->SetViewport(x, y, width, height);
}

void CRendererProxy::SetScissor(int x, int y, int width, int height)
{
  TRACE_CALL("%d %d %dx%d", x, y, width, height);
  m_pReal->SetScissor(x, y, width, height);
}

void CRendererProxy::MakeCurrent()
{
  TRACE_CALL("");
  m_pReal->MakeCurrent();
}

void CRendererProxy::CheckError(const char *comment)
{
  TRACE_CALL("'%s'", comment);
  m_pReal->CheckError(comment);
}

//////////////////////////////////////////////////////////////////////////
// Debug primitives

void CRendererProxy::Draw3dBBox(const Vec3 &mins, const Vec3 &maxs, int nPrimType)
{
  TRACE_CALL(TRACE_V3 "-" TRACE_V3 " prim %d", TRACE_V3_ARGS(mins), TRACE_V3_ARGS(maxs), nPrimType);
  m_pReal->Draw3dBBox(mins, maxs, nPrimType);
}

void CRendererProxy::Draw3dPrim(const Vec3 &mins, const Vec3 &maxs, int nPrimType, const float *fRGBA)
{
  TRACE_CALL(TRACE_V3 "-" TRACE_V3 " prim %d rgba %p", TRACE_V3_ARGS(mins), TRACE_V3_ARGS(maxs), nPrimType, fRGBA);
  m_pReal->Draw3dPrim(mins, maxs, nPrimType, fRGBA);
}

//////////////////////////////////////////////////////////////////////////
// Camera, display

void CRendererProxy::SetCamera(const CCamera &cam)
{
  TRACE_CALL("cam %p", &cam);
  if (TRACE_DUMP)
    Dump_Camera(cam);
  CWatchArg WatchCamera(WT_CCamera, &cam);
  m_pReal->SetCamera(cam);
}

const CCamera &CRendererProxy::GetCamera()
{
  TRACE_CALL("");
  const CCamera &Cam = m_pReal->GetCamera();
  TRACE_RESULT("%p", &Cam);
  return Cam;
}

bool CRendererProxy::SetGammaDelta(const float fGamma)
{
  TRACE_CALL("%g", fGamma);
  bool bRes = m_pReal->SetGammaDelta(fGamma);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::ChangeDisplay(unsigned int width, unsigned int height, unsigned int cbpp)
{
  TRACE_CALL("%ux%ux%u", width, height, cbpp);
  bool bRes = m_pReal->ChangeDisplay(width, height, cbpp);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::ChangeViewport(unsigned int x, unsigned int y, unsigned int width, unsigned int height)
{
  TRACE_CALL("%u %u %ux%u", x, y, width, height);
  m_pReal->ChangeViewport(x, y, width, height);
}

bool CRendererProxy::SaveTga(unsigned char *sourcedata, int sourceformat, int w, int h, const char *filename, bool flip)
{
  TRACE_CALL("data %p format %d %dx%d '%s' flip %d", sourcedata, sourceformat, w, h, filename, flip);
  bool bRes = m_pReal->SaveTga(sourcedata, sourceformat, w, h, filename, flip);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::SetTexture(int tnum, ETexType Type)
{
  TRACE_CALL("tex %d type %d", tnum, Type);
  m_pReal->SetTexture(tnum, Type);
}

void CRendererProxy::SetWhiteTexture()
{
  TRACE_CALL("");
  m_pReal->SetWhiteTexture();
}

//////////////////////////////////////////////////////////////////////////
// 2D

void CRendererProxy::WriteXY(CXFont *currfont, int x, int y, float xscale, float yscale, float r, float g, float b, float a, const char *message, ...)
{
  PROXY_VFORMAT(szText, message);
  TRACE_CALL("font %p %d %d scale %g %g color (%g %g %g %g) '%s'", currfont, x, y, xscale, yscale, r, g, b, a, szText);
  m_pReal->WriteXY(currfont, x, y, xscale, yscale, r, g, b, a, "%s", szText);
}

void CRendererProxy::Draw2dText(float posX, float posY, const char *szText, SDrawTextInfo &info)
{
  TRACE_CALL("%g %g '%s' flags 0x%x color (%g %g %g %g) scale %g %g font %p", posX, posY, szText, info.flags,
    info.color[0], info.color[1], info.color[2], info.color[3], info.xscale, info.yscale, info.xfont);
  m_pReal->Draw2dText(posX, posY, szText, info);
}

void CRendererProxy::Draw2dImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float angle, float r, float g, float b, float a, float z)
{
  TRACE_CALL("%g %g %gx%g tex %d st (%g %g)-(%g %g) angle %g color (%g %g %g %g) z %g", xpos, ypos, w, h, texture_id,
    s0, t0, s1, t1, angle, r, g, b, a, z);
  m_pReal->Draw2dImage(xpos, ypos, w, h, texture_id, s0, t0, s1, t1, angle, r, g, b, a, z);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::DrawImage(float xpos, float ypos, float w, float h, int texture_id, float s0, float t0, float s1, float t1, float r, float g, float b, float a)
{
  TRACE_CALL("%g %g %gx%g tex %d st (%g %g)-(%g %g) color (%g %g %g %g)", xpos, ypos, w, h, texture_id, s0, t0, s1, t1, r, g, b, a);
  m_pReal->DrawImage(xpos, ypos, w, h, texture_id, s0, t0, s1, t1, r, g, b, a);
}

int CRendererProxy::SetPolygonMode(int mode)
{
  TRACE_CALL("%d", mode);
  int nRes = m_pReal->SetPolygonMode(mode);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

int CRendererProxy::GetWidth()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetWidth();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

int CRendererProxy::GetHeight()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetHeight();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

void CRendererProxy::GetMemoryUsage(ICrySizer *Sizer)
{
  TRACE_CALL("sizer %p", Sizer);
  m_pReal->GetMemoryUsage(Sizer);
}

void CRendererProxy::ScreenShot(const char *filename)
{
  TRACE_CALL("'%s'", filename);
  m_pReal->ScreenShot(filename);
}

int CRendererProxy::GetColorBpp()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetColorBpp();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

int CRendererProxy::GetDepthBpp()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetDepthBpp();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

int CRendererProxy::GetStencilBpp()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetStencilBpp();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

//////////////////////////////////////////////////////////////////////////
// Projection

void CRendererProxy::ProjectToScreen(float ptx, float pty, float ptz, float *sx, float *sy, float *sz)
{
  TRACE_CALL("(%g %g %g)", ptx, pty, ptz);
  m_pReal->ProjectToScreen(ptx, pty, ptz, sx, sy, sz);
  TRACE_RESULT("(%g %g %g)", sx ? *sx : 0.f, sy ? *sy : 0.f, sz ? *sz : 0.f);
}

int CRendererProxy::UnProject(float sx, float sy, float sz, float *px, float *py, float *pz, const float modelMatrix[16], const float projMatrix[16], const int viewport[4])
{
  TRACE_CALL("(%g %g %g) view %p proj %p viewport (%d %d %d %d)", sx, sy, sz, modelMatrix, projMatrix,
    viewport[0], viewport[1], viewport[2], viewport[3]);
  int nRes = m_pReal->UnProject(sx, sy, sz, px, py, pz, modelMatrix, projMatrix, viewport);
  TRACE_RESULT("%d (%g %g %g)", nRes, px ? *px : 0.f, py ? *py : 0.f, pz ? *pz : 0.f);
  return nRes;
}

int CRendererProxy::UnProjectFromScreen(float sx, float sy, float sz, float *px, float *py, float *pz)
{
  TRACE_CALL("(%g %g %g)", sx, sy, sz);
  int nRes = m_pReal->UnProjectFromScreen(sx, sy, sz, px, py, pz);
  TRACE_RESULT("%d (%g %g %g)", nRes, px ? *px : 0.f, py ? *py : 0.f, pz ? *pz : 0.f);
  return nRes;
}

static void Dump_MatrixD(const char *szName, const double *m)
{
  float f[16];
  for (int i = 0; i < 16; i++)
    f[i] = (float)m[i];
  Dump_Matrix(szName, f);
}

void CRendererProxy::GetModelViewMatrix(float *mat)
{
  TRACE_CALL("float %p", mat);
  m_pReal->GetModelViewMatrix(mat);
  if (TRACE_DUMP)
    Dump_Matrix("modelview", mat);
}

void CRendererProxy::GetModelViewMatrix(double *mat)
{
  TRACE_CALL("double %p", mat);
  m_pReal->GetModelViewMatrix(mat);
  if (TRACE_DUMP)
    Dump_MatrixD("modelview", mat);
}

void CRendererProxy::GetProjectionMatrix(double *mat)
{
  TRACE_CALL("double %p", mat);
  m_pReal->GetProjectionMatrix(mat);
  if (TRACE_DUMP)
    Dump_MatrixD("projection", mat);
}

void CRendererProxy::GetProjectionMatrix(float *mat)
{
  TRACE_CALL("float %p", mat);
  m_pReal->GetProjectionMatrix(mat);
  if (TRACE_DUMP)
    Dump_Matrix("projection", mat);
}

Vec3 CRendererProxy::GetUnProject(const Vec3 &WindowCoords, const CCamera &cam)
{
  TRACE_CALL(TRACE_V3 " cam %p", TRACE_V3_ARGS(WindowCoords), &cam);
  Vec3 vRes = m_pReal->GetUnProject(WindowCoords, cam);
  TRACE_RESULT(TRACE_V3, TRACE_V3_ARGS(vRes));
  return vRes;
}

void CRendererProxy::RenderToViewport(const CCamera &cam, float x, float y, float width, float height)
{
  TRACE_CALL("cam %p %g %g %gx%g", &cam, x, y, width, height);
  if (TRACE_DUMP)
    Dump_Camera(cam);
  m_pReal->RenderToViewport(cam, x, y, width, height);
}

//////////////////////////////////////////////////////////////////////////
// Image files

void CRendererProxy::WriteDDS(byte *dat, int wdt, int hgt, int Size, const char *name, EImFormat eF, int NumMips)
{
  TRACE_CALL("data %p %dx%d size %d '%s' format %d mips %d", dat, wdt, hgt, Size, name, eF, NumMips);
  m_pReal->WriteDDS(dat, wdt, hgt, Size, name, eF, NumMips);
}

void CRendererProxy::WriteTGA(byte *dat, int wdt, int hgt, const char *name, int bits)
{
  TRACE_CALL("data %p %dx%d '%s' bits %d", dat, wdt, hgt, name, bits);
  m_pReal->WriteTGA(dat, wdt, hgt, name, bits);
}

void CRendererProxy::WriteJPG(byte *dat, int wdt, int hgt, char *name)
{
  TRACE_CALL("data %p %dx%d '%s'", dat, wdt, hgt, name);
  m_pReal->WriteJPG(dat, wdt, hgt, name);
}

//////////////////////////////////////////////////////////////////////////
// Fonts

bool CRendererProxy::FontUploadTexture(class CFBitmap *pBmp, ETEX_Format eTF)
{
  TRACE_CALL("bitmap %p format %d", pBmp, eTF);
  bool bRes = m_pReal->FontUploadTexture(pBmp, eTF);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

//! The bytes of a font bitmap (4 per pixel) by their place in the pixel: how many are not 0 and
//! their sum, to compare with the texture the renderer makes of it.
static void DumpFontBitmap(const byte *pData, int nWidth, int nHeight)
{
  if (!pData || nWidth <= 0 || nHeight <= 0 || !Trace_IsReadable(pData, (size_t)nWidth * nHeight * 4))
    return;
  int nNonZero[4] = {};
  unsigned __int64 nSum[4] = {};
  for (int i = 0; i < nWidth * nHeight * 4; i++)
  {
    nNonZero[i & 3] += pData[i] != 0;
    nSum[i & 3] += pData[i];
  }
  Trace_Write("    bytes 0..3 of the pixels: not 0 %d %d %d %d, sum %llu %llu %llu %llu", nNonZero[0], nNonZero[1], nNonZero[2],
    nNonZero[3], nSum[0], nSum[1], nSum[2], nSum[3]);
}

int CRendererProxy::FontCreateTexture(int Width, int Height, byte *pData, ETEX_Format eTF)
{
  TRACE_CALL("%dx%d data %p format %d", Width, Height, pData, eTF);
  if (TRACE_DUMP && eTF == eTF_8888)
    DumpFontBitmap(pData, Width, Height);
  int nRes = m_pReal->FontCreateTexture(Width, Height, pData, eTF);
  TRACE_RESULT("tex %d", nRes);
  return nRes;
}

bool CRendererProxy::FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte *pData)
{
  TRACE_CALL("tex %d %d %d %dx%d data %p", nTexId, X, Y, USize, VSize, pData);
  if (TRACE_DUMP)
    DumpFontBitmap(pData, USize, VSize);
  bool bRes = m_pReal->FontUpdateTexture(nTexId, X, Y, USize, VSize, pData);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::FontReleaseTexture(class CFBitmap *pBmp)
{
  TRACE_CALL("bitmap %p", pBmp);
  m_pReal->FontReleaseTexture(pBmp);
}

void CRendererProxy::FontSetTexture(class CFBitmap *pBmp, int nFilterMode)
{
  TRACE_CALL("bitmap %p filter %d", pBmp, nFilterMode);
  m_pReal->FontSetTexture(pBmp, nFilterMode);
}

void CRendererProxy::FontSetTexture(int nTexId, int nFilterMode)
{
  TRACE_CALL("tex %d filter %d", nTexId, nFilterMode);
  m_pReal->FontSetTexture(nTexId, nFilterMode);
}

void CRendererProxy::FontSetRenderingState(unsigned long nVirtualScreenWidth, unsigned long nVirtualScreenHeight)
{
  TRACE_CALL("virtual screen %lux%lu", nVirtualScreenWidth, nVirtualScreenHeight);
  m_pReal->FontSetRenderingState(nVirtualScreenWidth, nVirtualScreenHeight);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::FontSetBlending(int src, int dst)
{
  TRACE_CALL("src 0x%x dst 0x%x", src, dst);
  // XRENDERTRACED_PROBE=FontSetBlending: the first 13 calls pass (i, 12 - i) instead, so that the
  // Direct3D state after the text is drawn (XRENDERTRACED_D3D_STATE=DrawDynVB) shows what each
  // value stands for.
  static int s_nProbe = 0;
  if (s_nProbe <= 12 && Trace_IsListed("XRENDERTRACED_PROBE", "FontSetBlending"))
  {
    Trace_Write("    probe: FontSetBlending(%d, %d) instead of (%d, %d)", s_nProbe, 12 - s_nProbe, src, dst);
    src = s_nProbe;
    dst = 12 - s_nProbe;
    s_nProbe++;
  }
  m_pReal->FontSetBlending(src, dst);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::FontRestoreRenderingState()
{
  TRACE_CALL("");
  m_pReal->FontRestoreRenderingState();
}

//////////////////////////////////////////////////////////////////////////
// Statistics, misc

int CRendererProxy::GetPolyCount()
{
  TRACE_CALL("");
  int nRes = m_pReal->GetPolyCount();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

void CRendererProxy::GetPolyCount(int &nPolygons, int &nShadowVolPolys)
{
  TRACE_CALL("%p %p", &nPolygons, &nShadowVolPolys);
  m_pReal->GetPolyCount(nPolygons, nShadowVolPolys);
  TRACE_RESULT("polygons %d shadow volume %d", nPolygons, nShadowVolPolys);
}

void CRendererProxy::SetClearColor(const Vec3 &vColor)
{
  TRACE_CALL(TRACE_V3, TRACE_V3_ARGS(vColor));
  m_pReal->SetClearColor(vColor);
}

int CRendererProxy::GetFrameID(bool bIncludeRecursiveCalls)
{
  TRACE_CALL("recursive %d", bIncludeRecursiveCalls);
  int nRes = m_pReal->GetFrameID(bIncludeRecursiveCalls);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

void CRendererProxy::MakeMatrix(const Vec3 &pos, const Vec3 &angles, const Vec3 &scale, Matrix44 *mat)
{
  TRACE_CALL("pos " TRACE_V3 " angles " TRACE_V3 " scale " TRACE_V3 " mat %p", TRACE_V3_ARGS(pos), TRACE_V3_ARGS(angles),
    TRACE_V3_ARGS(scale), mat);
  m_pReal->MakeMatrix(pos, angles, scale, mat);
  if (TRACE_DUMP && mat)
    Dump_Matrix("matrix", (const float *)mat);
}

//////////////////////////////////////////////////////////////////////////
// Labels, text

void CRendererProxy::DrawLabelImage(const Vec3 &vPos, float fSize, int nTextureId)
{
  TRACE_CALL(TRACE_V3 " size %g tex %d", TRACE_V3_ARGS(vPos), fSize, nTextureId);
  m_pReal->DrawLabelImage(vPos, fSize, nTextureId);
}

void CRendererProxy::DrawLabel(Vec3 pos, float font_size, const char *label_text, ...)
{
  PROXY_VFORMAT(szText, label_text);
  TRACE_CALL(TRACE_V3 " size %g '%s'", TRACE_V3_ARGS(pos), font_size, szText);
  m_pReal->DrawLabel(pos, font_size, "%s", szText);
}

void CRendererProxy::DrawLabelEx(Vec3 pos, float font_size, float *pfColor, bool bFixedSize, bool bCenter, const char *label_text, ...)
{
  PROXY_VFORMAT(szText, label_text);
  TRACE_CALL(TRACE_V3 " size %g color %p fixed %d center %d '%s'", TRACE_V3_ARGS(pos), font_size, pfColor, bFixedSize, bCenter, szText);
  m_pReal->DrawLabelEx(pos, font_size, pfColor, bFixedSize, bCenter, "%s", szText);
}

void CRendererProxy::Draw2dLabel(float x, float y, float font_size, float *pfColor, bool bCenter, const char *label_text, ...)
{
  PROXY_VFORMAT(szText, label_text);
  TRACE_CALL("%g %g size %g color %p center %d '%s'", x, y, font_size, pfColor, bCenter, szText);
  m_pReal->Draw2dLabel(x, y, font_size, pfColor, bCenter, "%s", szText);
}

float CRendererProxy::ScaleCoordX(float value)
{
  TRACE_CALL("%g", value);
  float fRes = m_pReal->ScaleCoordX(value);
  TRACE_RESULT("%g", fRes);
  return fRes;
}

float CRendererProxy::ScaleCoordY(float value)
{
  TRACE_CALL("%g", value);
  float fRes = m_pReal->ScaleCoordY(value);
  TRACE_RESULT("%g", fRes);
  return fRes;
}

//////////////////////////////////////////////////////////////////////////
// Fixed function state

void CRendererProxy::SetState(int State)
{
  TRACE_CALL("0x%08x", State);
  m_pReal->SetState(State);
  TRACE_D3D_STATE(m_pReal);
}

void CRendererProxy::SetCullMode(int mode)
{
  TRACE_CALL("%d", mode);
  m_pReal->SetCullMode(mode);
}

bool CRendererProxy::EnableFog(bool enable)
{
  TRACE_CALL("%d", enable);
  bool bRes = m_pReal->EnableFog(enable);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::SetFog(float density, float fogstart, float fogend, const float *color, int fogmode)
{
  TRACE_CALL("density %g start %g end %g color %p (%g %g %g %g) mode %d", density, fogstart, fogend, color,
    color ? color[0] : 0.f, color ? color[1] : 0.f, color ? color[2] : 0.f, color ? color[3] : 0.f, fogmode);
  m_pReal->SetFog(density, fogstart, fogend, color, fogmode);
}

void CRendererProxy::EnableTexGen(bool enable)
{
  TRACE_CALL("%d", enable);
  m_pReal->EnableTexGen(enable);
}

void CRendererProxy::SetTexgen(float scaleX, float scaleY, float translateX, float translateY)
{
  TRACE_CALL("scale %g %g translate %g %g", scaleX, scaleY, translateX, translateY);
  m_pReal->SetTexgen(scaleX, scaleY, translateX, translateY);
}

void CRendererProxy::SetTexgen3D(float x1, float y1, float z1, float x2, float y2, float z2)
{
  TRACE_CALL("(%g %g %g) (%g %g %g)", x1, y1, z1, x2, y2, z2);
  m_pReal->SetTexgen3D(x1, y1, z1, x2, y2, z2);
}

void CRendererProxy::SetLodBias(float value)
{
  TRACE_CALL("%g", value);
  m_pReal->SetLodBias(value);
}

void CRendererProxy::SetColorOp(byte eCo, byte eAo, byte eCa, byte eAa)
{
  TRACE_CALL("color op %d alpha op %d color arg 0x%x alpha arg 0x%x", eCo, eAo, eCa, eAa);
  m_pReal->SetColorOp(eCo, eAo, eCa, eAa);
}

void CRendererProxy::EnableVSync(bool enable)
{
  TRACE_CALL("%d", enable);
  m_pReal->EnableVSync(enable);
}

//////////////////////////////////////////////////////////////////////////
// Matrix stack

void CRendererProxy::PushMatrix()
{
  TRACE_CALL("");
  m_pReal->PushMatrix();
}

void CRendererProxy::RotateMatrix(float a, float x, float y, float z)
{
  TRACE_CALL("%g around (%g %g %g)", a, x, y, z);
  m_pReal->RotateMatrix(a, x, y, z);
}

void CRendererProxy::RotateMatrix(const Vec3 &angels)
{
  TRACE_CALL(TRACE_V3, TRACE_V3_ARGS(angels));
  m_pReal->RotateMatrix(angels);
}

void CRendererProxy::TranslateMatrix(float x, float y, float z)
{
  TRACE_CALL("(%g %g %g)", x, y, z);
  m_pReal->TranslateMatrix(x, y, z);
}

void CRendererProxy::ScaleMatrix(float x, float y, float z)
{
  TRACE_CALL("(%g %g %g)", x, y, z);
  m_pReal->ScaleMatrix(x, y, z);
}

void CRendererProxy::TranslateMatrix(const Vec3 &pos)
{
  TRACE_CALL(TRACE_V3, TRACE_V3_ARGS(pos));
  m_pReal->TranslateMatrix(pos);
}

void CRendererProxy::MultMatrix(float *mat)
{
  TRACE_CALL("%p", mat);
  if (TRACE_DUMP && mat)
    Dump_Matrix("matrix", mat);
  m_pReal->MultMatrix(mat);
}

void CRendererProxy::LoadMatrix(const Matrix44 *src)
{
  TRACE_CALL("%p", src);
  if (TRACE_DUMP && src)
    Dump_Matrix("matrix", (const float *)src);
  m_pReal->LoadMatrix(src);
}

void CRendererProxy::PopMatrix()
{
  TRACE_CALL("");
  m_pReal->PopMatrix();
}

void CRendererProxy::EnableTMU(bool enable)
{
  TRACE_CALL("%d", enable);
  m_pReal->EnableTMU(enable);
}

void CRendererProxy::SelectTMU(int tnum)
{
  TRACE_CALL("%d", tnum);
  m_pReal->SelectTMU(tnum);
}

//////////////////////////////////////////////////////////////////////////
// Textures

unsigned int CRendererProxy::DownLoadToVideoMemory(unsigned char *data, int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat, int filter, int Id, char *szCacheName, int flags)
{
  TRACE_CALL("data %p %dx%d format %d -> %d mips %d repeat %d filter %d id %d cache '%s' flags 0x%x", data, w, h, eTFSrc, eTFDst,
    nummipmap, repeat, filter, Id, szCacheName, flags);
  unsigned int nRes = m_pReal->DownLoadToVideoMemory(data, w, h, eTFSrc, eTFDst, nummipmap, repeat, filter, Id, szCacheName, flags);
  TRACE_RESULT("tex %u", nRes);
  return nRes;
}

void CRendererProxy::UpdateTextureInVideoMemory(uint tnum, unsigned char *newdata, int posx, int posy, int w, int h, ETEX_Format eTFSrc)
{
  TRACE_CALL("tex %u data %p %d %d %dx%d format %d", tnum, newdata, posx, posy, w, h, eTFSrc);
  m_pReal->UpdateTextureInVideoMemory(tnum, newdata, posx, posy, w, h, eTFSrc);
}

unsigned int CRendererProxy::LoadTexture(const char *filename, int *tex_type, unsigned int def_tid, bool compresstodisk, bool bWarn)
{
  TRACE_CALL("'%s' type %p default %u compress %d warn %d", filename, tex_type, def_tid, compresstodisk, bWarn);
  unsigned int nRes = m_pReal->LoadTexture(filename, tex_type, def_tid, compresstodisk, bWarn);
  TRACE_RESULT("tex %u type %d", nRes, tex_type ? *tex_type : -1);
  return nRes;
}

bool CRendererProxy::DXTCompress(byte *raw_data, int nWidth, int nHeight, ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback)
{
  TRACE_CALL("data %p %dx%d format %d hw %d mips %d bpp %d callback %s", raw_data, nWidth, nHeight, eTF, bUseHW, bGenMips,
    nSrcBytesPerPix, Trace_Addr((const void *)callback));
  bool bRes = m_pReal->DXTCompress(raw_data, nWidth, nHeight, eTF, bUseHW, bGenMips, nSrcBytesPerPix, callback);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::DXTDecompress(byte *srcData, byte *dstData, int nWidth, int nHeight, ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix)
{
  TRACE_CALL("src %p dst %p %dx%d format %d hw %d bpp %d", srcData, dstData, nWidth, nHeight, eSrcTF, bUseHW, nDstBytesPerPix);
  bool bRes = m_pReal->DXTDecompress(srcData, dstData, nWidth, nHeight, eSrcTF, bUseHW, nDstBytesPerPix);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

void CRendererProxy::RemoveTexture(unsigned int TextureId)
{
  TRACE_CALL("tex %u", TextureId);
  m_pReal->RemoveTexture(TextureId);
}

void CRendererProxy::RemoveTexture(ITexPic *pTexPic)
{
  TRACE_CALL("pic %p", pTexPic);
  if (TRACE_DUMP)
    Dump_TexPic(pTexPic);
  m_pReal->RemoveTexture(pTexPic);
}

void CRendererProxy::TextToScreen(float x, float y, const char *format, ...)
{
  PROXY_VFORMAT(szText, format);
  TRACE_CALL("%g %g '%s'", x, y, szText);
  m_pReal->TextToScreen(x, y, "%s", szText);
}

void CRendererProxy::TextToScreenColor(int x, int y, float r, float g, float b, float a, const char *format, ...)
{
  PROXY_VFORMAT(szText, format);
  TRACE_CALL("%d %d color (%g %g %g %g) '%s'", x, y, r, g, b, a, szText);
  m_pReal->TextToScreenColor(x, y, r, g, b, a, "%s", szText);
}

void CRendererProxy::ResetToDefault()
{
  TRACE_CALL("");
  m_pReal->ResetToDefault();
}

int CRendererProxy::GenerateAlphaGlowTexture(float k)
{
  TRACE_CALL("%g", k);
  int nRes = m_pReal->GenerateAlphaGlowTexture(k);
  TRACE_RESULT("tex %d", nRes);
  return nRes;
}

void CRendererProxy::SetMaterialColor(float r, float g, float b, float a)
{
  TRACE_CALL("(%g %g %g %g)", r, g, b, a);
  m_pReal->SetMaterialColor(r, g, b, a);
}

int CRendererProxy::LoadAnimatedTexture(const char *format, const int nCount)
{
  TRACE_CALL("'%s' count %d", format, nCount);
  int nRes = m_pReal->LoadAnimatedTexture(format, nCount);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

void CRendererProxy::RemoveAnimatedTexture(AnimTexInfo *pInfo)
{
  TRACE_CALL("%p", pInfo);
  m_pReal->RemoveAnimatedTexture(pInfo);
}

AnimTexInfo *CRendererProxy::GetAnimTexInfoFromId(int nId)
{
  TRACE_CALL("%d", nId);
  AnimTexInfo *pRes = m_pReal->GetAnimTexInfoFromId(nId);
  TRACE_RESULT("%p", pRes);
  return pRes;
}

//////////////////////////////////////////////////////////////////////////
// Lines, helpers

void CRendererProxy::Draw2dLine(float x1, float y1, float x2, float y2)
{
  TRACE_CALL("(%g %g)-(%g %g)", x1, y1, x2, y2);
  m_pReal->Draw2dLine(x1, y1, x2, y2);
}

void CRendererProxy::SetLineWidth(float fWidth)
{
  TRACE_CALL("%g", fWidth);
  m_pReal->SetLineWidth(fWidth);
}

void CRendererProxy::DrawLine(const Vec3 &vPos1, const Vec3 &vPos2)
{
  TRACE_CALL(TRACE_V3 "-" TRACE_V3, TRACE_V3_ARGS(vPos1), TRACE_V3_ARGS(vPos2));
  m_pReal->DrawLine(vPos1, vPos2);
}

void CRendererProxy::DrawLineColor(const Vec3 &vPos1, const CFColor &vColor1, const Vec3 &vPos2, const CFColor &vColor2)
{
  TRACE_CALL(TRACE_V3 " (%g %g %g %g) - " TRACE_V3 " (%g %g %g %g)", TRACE_V3_ARGS(vPos1), vColor1.r, vColor1.g, vColor1.b, vColor1.a,
    TRACE_V3_ARGS(vPos2), vColor2.r, vColor2.g, vColor2.b, vColor2.a);
  m_pReal->DrawLineColor(vPos1, vColor1, vPos2, vColor2);
}

void CRendererProxy::Graph(byte *g, int x, int y, int wdt, int hgt, int nC, int type, char *text, CFColor &color, float fScale)
{
  TRACE_CALL("data %p %d %d %dx%d count %d type %d '%s' color (%g %g %g %g) scale %g", g, x, y, wdt, hgt, nC, type, text,
    color.r, color.g, color.b, color.a, fScale);
  m_pReal->Graph(g, x, y, wdt, hgt, nC, type, text, color, fScale);
}

void CRendererProxy::DrawBall(float x, float y, float z, float radius)
{
  TRACE_CALL("(%g %g %g) radius %g", x, y, z, radius);
  m_pReal->DrawBall(x, y, z, radius);
}

void CRendererProxy::DrawBall(const Vec3 &pos, float radius)
{
  TRACE_CALL(TRACE_V3 " radius %g", TRACE_V3_ARGS(pos), radius);
  m_pReal->DrawBall(pos, radius);
}

void CRendererProxy::DrawPoint(float x, float y, float z, float fSize)
{
  TRACE_CALL("(%g %g %g) size %g", x, y, z, fSize);
  m_pReal->DrawPoint(x, y, z, fSize);
}

void CRendererProxy::FlushTextMessages()
{
  TRACE_CALL("");
  m_pReal->FlushTextMessages();
}

//! The sprites made so far (MakeSprite), with the object's box and the Direct3D texture each one
//! got, written by DrawObjSprites.
struct SSpriteRecord
{
  IStatObj *m_pStatObj;
  float m_fScale;
  int m_nTexSize;
  float m_fRadius;
  Vec3 m_vBoxMin, m_vBoxMax;
  float m_fAngle;
  unsigned int m_nID;
  void *m_pD3DTexture;
  int m_nOffset;
};
static SSpriteRecord s_Sprites[8192];
static int s_nSprites = 0;

//! The Direct3D texture behind a texture of the original renderer: the first pointer in the
//! first 0x200 bytes of its ITexPic to an object whose vtable is in d3d9.dll.
static void *FindD3DTextureUnsafe(ITexPic *pPic, int *pOffset)
{
  HMODULE hD3D9 = GetModuleHandleA("d3d9.dll");
  for (int nOff = 8; pPic && hD3D9 && nOff < 0x200; nOff += 8)
  {
    if (!Trace_IsReadable((char *)pPic + nOff, sizeof(void *)))
      break;
    void *p = *(void **)((char *)pPic + nOff);
    HMODULE hModule = NULL;
    if (p && Trace_IsReadable(p, sizeof(void *)) && Trace_IsReadable(*(void **)p, sizeof(void *))
      && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)**(void ***)p, &hModule)
      && hModule == hD3D9)
    {
      *pOffset = nOff;
      return p;
    }
  }
  return NULL;
}

static void *FindD3DTexture(IRenderer *pRenderer, unsigned int nID, int *pOffset)
{
  __try
  {
    return FindD3DTextureUnsafe(pRenderer->EF_GetTextureByID(nID), pOffset);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    return NULL;
  }
}

void CRendererProxy::DrawObjSprites(list2<CStatObjInst *> *pList, float fMaxViewDist, CObjManager *pObjMan)
{
  TRACE_CALL("list %p (%d) maxViewDist %g objManager %p", pList, pList ? pList->Count() : -1, fMaxViewDist, pObjMan);
  // CStatObjInst and CObjManager are 3D engine types the SDK only declares: their sizes (heap block
  // headers) and the first instances as they are passed.
  if (bTraced)
  {
    Trace_Write("    objManager heap prefix %s", Trace_HeapPrefix(pObjMan));
    for (int i = 0; pList && i < pList->Count() && i < 2; i++)
      Trace_Write("    instance %d %p heap prefix %s", i, (*pList)[i], Trace_HeapPrefix((*pList)[i]));
  }
  if (TRACE_DUMP && s_nSprites)
  {
    // The sprites made since the last traced call with their Direct3D textures, to tell which one a
    // draw of the far sprites uses.
    for (int i = 0; i < s_nSprites; i++)
    {
      const SSpriteRecord &S = s_Sprites[i];
      Trace_Write("    sprite statObj %p angle %g id %u -> d3d %p (+0x%x) scale %g size %d radius %g box (%g %g %g)-(%g %g %g)", S.m_pStatObj,
        S.m_fAngle, S.m_nID, S.m_pD3DTexture, S.m_nOffset, S.m_fScale, S.m_nTexSize, S.m_fRadius, S.m_vBoxMin.x, S.m_vBoxMin.y,
        S.m_vBoxMin.z, S.m_vBoxMax.x, S.m_vBoxMax.y, S.m_vBoxMax.z);
    }
    s_nSprites = 0;
  }
  if (TRACE_DUMP)
  {
    // Once: the object manager and the vegetation groups of the level (public I3DEngine interface).
    static bool s_bOnce = false;
    I3DEngine *p3DEngine = iSystem ? iSystem->GetI3DEngine() : NULL;
    if (!s_bOnce && p3DEngine)
    {
      s_bOnce = true;
      Dump_Unknown("CObjManager", pObjMan, 0x328);
      for (int g = 0; g < 256; g++)
      {
        IStatInstGroup Group;
        if (!p3DEngine->GetStatInstGroup(g, Group) || !Group.pStatObj)
          continue;
        Vec3 vMin = Group.pStatObj->GetBoxMin(), vMax = Group.pStatObj->GetBoxMax();
        Trace_Write("    group %d: statObj %p '%s' radius %g brightness %g spriteDistRatio %g maxViewDistRatio %g useSprites %d "
          "spriteTexRes %d bending %g fadeSize %d alphaBlend %d backSide %g calcLighting %d rndFlags 0x%x material %p "
          "box (%g %g %g)-(%g %g %g) lods %p %p %p", g, Group.pStatObj, Group.pStatObj->GetFileName(), Group.pStatObj->GetRadius(),
          Group.fBrightness, Group.fSpriteDistRatio, Group.fMaxViewDistRatio, Group.bUseSprites, Group.nSpriteTexRes, Group.fBending,
          Group.bFadeSize, Group.bUseAlphaBlending, Group.fBackSideLevel, Group.bCalcLighting, Group.m_dwRndFlags, Group.pMaterial,
          vMin.x, vMin.y, vMin.z, vMax.x, vMax.y, vMax.z, Group.pStatObj->GetLodObject(1), Group.pStatObj->GetLodObject(2),
          Group.pStatObj->GetLodObject(3));
        // The object manager starts with a pointer to an array of groups (0x48 bytes each in Bin64).
        const unsigned char *pGroups = Trace_IsReadable(pObjMan, sizeof(void *)) ? *(const unsigned char **)pObjMan : NULL;
        if (pGroups && Trace_IsReadable(pGroups + g * 0x48, 0x48))
          Dump_HexLine("grpmem", pGroups + g * 0x48, 0x48);
      }
    }
    for (int i = 0; pList && i < pList->Count(); i++)
    {
      if (i < 2)
        Dump_Unknown("CStatObjInst", (*pList)[i], 0x90);
      Dump_HexLine("inst", (*pList)[i], 0x90);
    }
  }
  m_pReal->DrawObjSprites(pList, fMaxViewDist, pObjMan);
}

void CRendererProxy::DrawQuad(const Vec3 &right, const Vec3 &up, const Vec3 &origin, int nFlipMode)
{
  TRACE_CALL("right " TRACE_V3 " up " TRACE_V3 " origin " TRACE_V3 " flip %d", TRACE_V3_ARGS(right), TRACE_V3_ARGS(up),
    TRACE_V3_ARGS(origin), nFlipMode);
  m_pReal->DrawQuad(right, up, origin, nFlipMode);
}

void CRendererProxy::DrawQuad(float dy, float dx, float dz, float x, float y, float z)
{
  TRACE_CALL("d (%g %g %g) pos (%g %g %g)", dx, dy, dz, x, y, z);
  m_pReal->DrawQuad(dy, dx, dz, x, y, z);
}

void CRendererProxy::ClearDepthBuffer()
{
  TRACE_CALL("");
  m_pReal->ClearDepthBuffer();
}

void CRendererProxy::ClearColorBuffer(const Vec3 vColor)
{
  TRACE_CALL(TRACE_V3, TRACE_V3_ARGS(vColor));
  m_pReal->ClearColorBuffer(vColor);
}

void CRendererProxy::ReadFrameBuffer(unsigned char *pRGB, int nSizeX, int nSizeY, bool bBackBuffer, bool bRGBA, int nScaledX, int nScaledY)
{
  TRACE_CALL("dst %p %dx%d back %d rgba %d scaled %dx%d", pRGB, nSizeX, nSizeY, bBackBuffer, bRGBA, nScaledX, nScaledY);
  m_pReal->ReadFrameBuffer(pRGB, nSizeX, nSizeY, bBackBuffer, bRGBA, nScaledX, nScaledY);
}

void CRendererProxy::SetFogColor(float *color)
{
  TRACE_CALL("%p (%g %g %g %g)", color, color ? color[0] : 0.f, color ? color[1] : 0.f, color ? color[2] : 0.f, color ? color[3] : 0.f);
  m_pReal->SetFogColor(color);
}

void CRendererProxy::TransformTextureMatrix(float x, float y, float angle, float scale)
{
  TRACE_CALL("%g %g angle %g scale %g", x, y, angle, scale);
  m_pReal->TransformTextureMatrix(x, y, angle, scale);
}

void CRendererProxy::ResetTextureMatrix()
{
  TRACE_CALL("");
  m_pReal->ResetTextureMatrix();
}

//////////////////////////////////////////////////////////////////////////
// Renderer type

char CRendererProxy::GetType()
{
  TRACE_CALL("");
  char nRes = m_pReal->GetType();
  TRACE_RESULT("%d", nRes);
  return nRes;
}

char *CRendererProxy::GetVertexProfile(bool bSupportedProfile)
{
  TRACE_CALL("supported %d", bSupportedProfile);
  char *szRes = m_pReal->GetVertexProfile(bSupportedProfile);
  TRACE_RESULT("'%s'", szRes);
  return szRes;
}

char *CRendererProxy::GetPixelProfile(bool bSupportedProfile)
{
  TRACE_CALL("supported %d", bSupportedProfile);
  char *szRes = m_pReal->GetPixelProfile(bSupportedProfile);
  TRACE_RESULT("'%s'", szRes);
  return szRes;
}

void CRendererProxy::SetType(char type)
{
  // CrySystem passes the type of the slot the proxy was loaded into (R_DX8_RENDERER for
  // XRenderD3D8.dll); the original renderer is told its own type.
  char nRealType = m_nRealType >= 0 ? (char)m_nRealType : type;
  TRACE_CALL("%d, forwarded as %d", type, nRealType);
  m_pReal->SetType(nRealType);
}

//////////////////////////////////////////////////////////////////////////
// Sprites, shadows

unsigned int CRendererProxy::MakeSprite(float object_scale, int tex_size, float angle, IStatObj *pStatObj, uchar *pTmpBuffer, uint def_tid)
{
  TRACE_CALL("scale %g size %d angle %g statObj %p buffer %p default %u", object_scale, tex_size, angle, pStatObj, pTmpBuffer, def_tid);
  unsigned int nRes = m_pReal->MakeSprite(object_scale, tex_size, angle, pStatObj, pTmpBuffer, def_tid);
  TRACE_RESULT("tex %u", nRes);
  if (s_nSprites < (int)(sizeof(s_Sprites) / sizeof(s_Sprites[0])))
  {
    s_Sprites[s_nSprites].m_pStatObj = pStatObj;
    s_Sprites[s_nSprites].m_fAngle = angle;
    s_Sprites[s_nSprites].m_fScale = object_scale;
    s_Sprites[s_nSprites].m_nTexSize = tex_size;
    // The object may be gone by the time the list is written.
    s_Sprites[s_nSprites].m_fRadius = pStatObj ? pStatObj->GetRadius() : 0;
    s_Sprites[s_nSprites].m_vBoxMin = pStatObj ? pStatObj->GetBoxMin() : Vec3(0, 0, 0);
    s_Sprites[s_nSprites].m_vBoxMax = pStatObj ? pStatObj->GetBoxMax() : Vec3(0, 0, 0);
    s_Sprites[s_nSprites].m_nID = nRes;
    s_Sprites[s_nSprites].m_nOffset = 0;
    s_Sprites[s_nSprites].m_pD3DTexture = FindD3DTexture(m_pReal, nRes, &s_Sprites[s_nSprites].m_nOffset);
    s_nSprites++;
  }
  return nRes;
}

unsigned int CRendererProxy::Make3DSprite(int nTexSize, float fAngleStep, IStatObj *pStatObj)
{
  TRACE_CALL("size %d angleStep %g statObj %p", nTexSize, fAngleStep, pStatObj);
  unsigned int nRes = m_pReal->Make3DSprite(nTexSize, fAngleStep, pStatObj);
  TRACE_RESULT("tex %u", nRes);
  return nRes;
}

ShadowMapFrustum *CRendererProxy::MakeShadowMapFrustum(ShadowMapFrustum *lof, ShadowMapLightSource *pLs, const Vec3 &obj_pos, list2<IStatObj *> *pStatObjects, int shadow_type)
{
  TRACE_CALL("frustum %p lightSource %p pos " TRACE_V3 " statObjs %p (%d) type %d", lof, pLs, TRACE_V3_ARGS(obj_pos), pStatObjects,
    pStatObjects ? pStatObjects->Count() : -1, shadow_type);
  if (TRACE_DUMP)
  {
    Dump_Opaque("ShadowMapFrustum (in)", lof);
    Dump_Opaque("ShadowMapLightSource", pLs);
  }
  ShadowMapFrustum *pRes = m_pReal->MakeShadowMapFrustum(lof, pLs, obj_pos, pStatObjects, shadow_type);
  // ShadowMapFrustum and ShadowMapLightSource are only declared in the SDK: their heap block headers.
  TRACE_RESULT("%p, heap prefix: frustum %s, light source %s", pRes, Trace_HeapPrefix(pRes), Trace_HeapPrefix(pLs));
  if (TRACE_DUMP)
    Dump_Opaque("ShadowMapFrustum (out)", pRes);
  return pRes;
}

void CRendererProxy::Set2DMode(bool enable, int ortox, int ortoy)
{
  TRACE_CALL("%d %dx%d", enable, ortox, ortoy);
  m_pReal->Set2DMode(enable, ortox, ortoy);
}

int CRendererProxy::ScreenToTexture()
{
  TRACE_CALL("");
  int nRes = m_pReal->ScreenToTexture();
  TRACE_RESULT("tex %d", nRes);
  return nRes;
}

void CRendererProxy::SetTexClampMode(bool clamp)
{
  TRACE_CALL("%d", clamp);
  m_pReal->SetTexClampMode(clamp);
}

void CRendererProxy::EnableSwapBuffers(bool bEnable)
{
  TRACE_CALL("%d", bEnable);
  m_pReal->EnableSwapBuffers(bEnable);
}

WIN_HWND CRendererProxy::GetHWND()
{
  TRACE_CALL("");
  WIN_HWND hWnd = m_pReal->GetHWND();
  TRACE_RESULT("%p", hWnd);
  return hWnd;
}

void CRendererProxy::OnEntityDeleted(IEntityRender *pEntityRender)
{
  TRACE_CALL("%p", pEntityRender);
  m_pReal->OnEntityDeleted(pEntityRender);
}

int CRendererProxy::EnumAAFormats(TArray<SAAFormat> &Formats, bool bReset)
{
  TRACE_CALL("formats %p (%d) reset %d", &Formats, Formats.Num(), bReset);
  int nRes = m_pReal->EnumAAFormats(Formats, bReset);
  TRACE_RESULT("%d, formats %d", nRes, Formats.Num());
  if (TRACE_DUMP)
  {
    for (int i = 0; i < Formats.Num() && i < 32; i++)
      Trace_Write("      '%s' samples %d quality %d api %d", Formats[i].szDescr, Formats[i].nSamples, Formats[i].nQuality, Formats[i].nAPIType);
  }
  return nRes;
}

//////////////////////////////////////////////////////////////////////////
// Render targets

int CRendererProxy::CreateRenderTarget(int nWidth, int nHeight, ETEX_Format eTF)
{
  TRACE_CALL("%dx%d format %d", nWidth, nHeight, eTF);
  int nRes = m_pReal->CreateRenderTarget(nWidth, nHeight, eTF);
  TRACE_RESULT("%d", nRes);
  return nRes;
}

bool CRendererProxy::DestroyRenderTarget(int nHandle)
{
  TRACE_CALL("%d", nHandle);
  bool bRes = m_pReal->DestroyRenderTarget(nHandle);
  TRACE_RESULT("%d", bRes);
  return bRes;
}

bool CRendererProxy::SetRenderTarget(int nHandle)
{
  TRACE_CALL("%d", nHandle);
  bool bRes = m_pReal->SetRenderTarget(nHandle);
  TRACE_RESULT("%d", bRes);
  return bRes;
}
