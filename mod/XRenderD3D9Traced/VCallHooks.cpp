// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: VCallHooks.cpp
//	Description: Tracing of the virtual functions of the renderer objects the engine holds.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "VCallHooks.h"
#include "D3DDraws.h"

#include <map>
#include <string>

//! The virtual functions of an SDK class in slot order.
struct SVTableInfo
{
  const char *szClass;
  int nSlots;
  const char *szMethods[192];
};

#define XRC_VTABLE(Class, nSlots) { #Class, nSlots, {
#define XRC_SLOT(Class, nSlot, szName) szName,
#define XRC_VTABLE_END(Class) } },
static const SVTableInfo s_VTableInfos[] =
{
#include <VTables.inl>
};
#undef XRC_VTABLE
#undef XRC_SLOT
#undef XRC_VTABLE_END

static const int nMaxSlots = 192;
static const int nMaxTables = 64;
static const int nMaxModules = 31;
//! Calls of each function from the engine that are written out.
static const int nLogPerSlot = 3;
//! Calls written out of a method listed in XRENDERTRACED_TRACE_METHODS ("CCObject::AddWaves");
//! XRENDERTRACED_TRACE_METHODS_MAX changes it.
static int s_nLogListed = 4000;
//! Calls of each screen effect parameter that are written out (and then every nLogScreenEvery-th).
static const int nLogPerScreenParam = 8;
static const int nLogScreenEvery = 50;

enum ECaller
{
  CALLER_Engine,
  CALLER_Renderer,
  //! The proxy itself (its dumps call GetName and the like).
  CALLER_Proxy,
  CALLER_Num
};

enum ESpecial
{
  SPECIAL_None,
  SPECIAL_ScreenActivate,
  SPECIAL_ScreenSetParameter,
  SPECIAL_ScreenGetParameter,
  SPECIAL_ConsoleGetCVar,
};

struct SHookedVTable;

struct SVSlot
{
  SHookedVTable *pTable;
  int nSlot;
  const char *szMethod;
  void *pOrig;
  ESpecial eSpecial;
  //! CRendElement::mfDraw: the element is the one being drawn until it returns.
  bool bDraw;
  //! Listed in XRENDERTRACED_TRACE_METHODS: nLogListed calls are written out.
  bool bListed;
  //! Calls from the other threads than the one that loaded the renderer; nMainCalls, from it
  //! (counted without a locked instruction: nearly all calls are made there).
  volatile LONG nCalls[CALLER_Num];
  LONG nMainCalls[CALLER_Num];
  //! Bit i: called from module i of s_Modules.
  volatile LONG nEngineModules;
  volatile LONG nLogged;
  //! Calls (of the engine into the renderer, or of the renderer into the engine) from another
  //! thread than the one that loaded the renderer.
  volatile LONG nOtherThread;
};

struct SHookedVTable
{
  const SVTableInfo *pInfo;
  void **pVTable;
  char szName[64];
  //! An interface of the engine: the calls from the renderer are written out.
  bool bEngine;
  SVSlot Slots[nMaxSlots];
};

//! The frame VCall_Thunk builds (VCallThunk_x64.asm).
struct SVCallFrame
{
  UINT_PTR Args[4];       // rcx ('this'), rdx, r8, r9
  float Xmm[4][4];        // xmm0..xmm3
  SVSlot *pSlot;
  //! The function VCall_Thunk calls; set by VCall_Enter.
  void *pOrig;
  UINT_PTR nResult;       // rax
  //! Set by VCall_Enter: FRAME_LOG, VCall_Leave writes the result; FRAME_DRAW, nPad holds the
  //! element drawn before this mfDraw.
  UINT_PTR nFlags;
  float XmmResult[4];     // xmm0
  UINT_PTR nPad;
  //! The caller's return address, its home space and its stack arguments (5th and on).
  void *pReturn;
  UINT_PTR Home[4];
  UINT_PTR StackArgs[1];
};
#ifdef _WIN64
static_assert(offsetof(SVCallFrame, pSlot) == 0x60 && offsetof(SVCallFrame, nResult) == 0x70 && offsetof(SVCallFrame, XmmResult) == 0x80 &&
  offsetof(SVCallFrame, pReturn) == 0x98 && offsetof(SVCallFrame, StackArgs) == 0xC0, "SVCallFrame does not match VCallThunk_x64.asm");
#endif

struct SModule
{
  UINT_PTR nBase;
  UINT_PTR nEnd;
  char szName[32];
};

static SRWLOCK s_Lock = SRWLOCK_INIT;
static SHookedVTable s_Tables[nMaxTables];
static int s_nTables = 0;
static void **s_Rejected[nMaxTables];
static int s_nRejected = 0;
static void **volatile s_pLastVTable = NULL;
static bool s_bInitialized = false;
static bool s_bDisabled = false;

enum
{
  FRAME_LOG = 1,
  FRAME_DRAW = 2,
};

//! The render element whose mfDraw is running (the renderer draws on one thread).
static CRendElement *s_pDrawingRE = NULL;

static SModule s_Modules[nMaxModules];
static volatile LONG s_nModules = 0;
static int s_nRenderer = -1;
static int s_nSelf = -1;
//! The IConsole entry of s_VTableInfos, once the console is hooked.
static const SVTableInfo *s_pConsoleInfo = NULL;

//! The screen effect calls by method, process and parameter, and how often each was made.
static std::map<unsigned int, int> s_ScreenCalls;
//! XRENDERTRACED_SCREENFX_ALL=1: every screen effect call is written out, with the timer's time.
static int s_nScreenAll = -1;
//! The console variables looked up with IConsole::GetCVar ("module name"), and how often.
static std::map<std::string, int> s_CVarLookups;

//////////////////////////////////////////////////////////////////////////
// Modules

//! Adds the module to s_Modules (under s_Lock); returns its index.
static int AddModule(HMODULE hModule)
{
  for (int i = 0; i < s_nModules; i++)
  {
    if (s_Modules[i].nBase == (UINT_PTR)hModule)
      return i;
  }
  if (s_nModules == nMaxModules)
    return -1;
  const IMAGE_DOS_HEADER *pDos = (const IMAGE_DOS_HEADER *)hModule;
  const IMAGE_NT_HEADERS *pNt = (const IMAGE_NT_HEADERS *)((const BYTE *)hModule + pDos->e_lfanew);
  SModule &Module = s_Modules[s_nModules];
  Module.nBase = (UINT_PTR)hModule;
  Module.nEnd = Module.nBase + pNt->OptionalHeader.SizeOfImage;
  char szPath[MAX_PATH] = "";
  GetModuleFileNameA(hModule, szPath, sizeof(szPath));
  const char *szName = strrchr(szPath, '\\') ? strrchr(szPath, '\\') + 1 : szPath;
  _snprintf(Module.szName, sizeof(Module.szName) - 1, "%s", szName);
  if (char *pExt = strrchr(Module.szName, '.'))
    *pExt = 0;
  MemoryBarrier();
  return InterlockedIncrement(&s_nModules) - 1;
}

//! Index of the module holding the address in s_Modules, -1 if it is not in a module.
static int FindModule(const void *pAddr)
{
  const UINT_PTR nAddr = (UINT_PTR)pAddr;
  const LONG nModules = s_nModules;
  for (LONG i = 0; i < nModules; i++)
  {
    if (nAddr >= s_Modules[i].nBase && nAddr < s_Modules[i].nEnd)
      return i;
  }
  HMODULE hModule = NULL;
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pAddr, &hModule))
    return -1;
  AcquireSRWLockExclusive(&s_Lock);
  int nRes = AddModule(hModule);
  ReleaseSRWLockExclusive(&s_Lock);
  return nRes;
}

#ifdef _WIN64
//! The modules of the addresses found in one, as (address << 8) | index by a hash of the address:
//! the calls come from a few hundred places. An entry stays right, as the first module of
//! s_Modules holding an address does.
static const int nModuleCache = 4096;
static volatile UINT64 s_ModuleCache[nModuleCache];
#endif

//! FindModule, looked up in s_ModuleCache first.
static int ModuleIndex(const void *pAddr)
{
#ifdef _WIN64
  const UINT64 nAddr = (UINT_PTR)pAddr;
  volatile UINT64 &Cached = s_ModuleCache[(nAddr ^ nAddr >> 12) & (nModuleCache - 1)];
  const UINT64 nCached = Cached;
  if (nAddr && nCached >> 8 == nAddr)
    return (int)(nCached & 0xFF);
  const int nRes = FindModule(pAddr);
  if (nRes >= 0)
    Cached = nAddr << 8 | (UINT64)nRes;
  return nRes;
#else
  return FindModule(pAddr);
#endif
}

void VCallHooks_SetRenderer(HMODULE hRenderer)
{
  AcquireSRWLockExclusive(&s_Lock);
  s_nRenderer = AddModule(hRenderer);
  ReleaseSRWLockExclusive(&s_Lock);
}

//////////////////////////////////////////////////////////////////////////
// Calls

//! The value a screen effect parameter points to, as an integer and as a float.
static const char *ScreenValueText(const void *pValue)
{
  static char s_szBuf[4][96];
  static volatile LONG s_nBuf = 0;
  char *szOut = s_szBuf[InterlockedIncrement(&s_nBuf) & 3];
  if (!pValue)
    return "null";
  if (!Trace_IsReadable(pValue, 8))
    return "(not readable)";
  const int *pInt = (const int *)pValue;
  _snprintf(szOut, 95, "int %d / float %g, next int %d / float %g", pInt[0], *(const float *)&pInt[0], pInt[1], *(const float *)&pInt[1]);
  szOut[95] = 0;
  return szOut;
}

//! Counts a screen effect call; returns whether it is written out.
static bool CountScreenCall(ESpecial eSpecial, int nProcess, int nParam)
{
  unsigned int nKey = ((unsigned int)eSpecial << 24) | ((unsigned int)(nProcess & 0xFFF) << 12) | (unsigned int)(nParam & 0xFFF);
  AcquireSRWLockExclusive(&s_Lock);
  int nCount = ++s_ScreenCalls[nKey];
  ReleaseSRWLockExclusive(&s_Lock);
  if (s_nScreenAll < 0)
  {
    char szEnv[8];
    s_nScreenAll = GetEnvironmentVariableA("XRENDERTRACED_SCREENFX_ALL", szEnv, sizeof(szEnv)) && szEnv[0] == '1';
  }
  return s_nScreenAll || nCount <= nLogPerScreenParam || nCount % nLogScreenEvery == 0;
}

static void ScreenCall_Enter(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  const int nProcess = (int)pFrame->Args[1];
  const int nParam = (int)pFrame->Args[2];
  switch (pSlot->eSpecial)
  {
  case SPECIAL_ScreenActivate:
    if (CountScreenCall(pSlot->eSpecial, nProcess, 0))
      Trace_Write("[screenfx] %p mfActivate(process %d) from %s", (void *)pFrame->Args[0], nProcess, Trace_Addr(pFrame->pReturn));
    break;
  case SPECIAL_ScreenSetParameter:
    if (CountScreenCall(pSlot->eSpecial, nProcess, nParam))
    {
      Trace_Write("[screenfx] %p mfSetParameter(process %d, param %d, value %p: %s) from %s", (void *)pFrame->Args[0], nProcess, nParam,
        (void *)pFrame->Args[3], ScreenValueText((void *)pFrame->Args[3]), Trace_Addr(pFrame->pReturn));
      pFrame->nFlags |= FRAME_LOG;
    }
    break;
  case SPECIAL_ScreenGetParameter:
    if (CountScreenCall(pSlot->eSpecial, nProcess, nParam))
      pFrame->nFlags |= FRAME_LOG;
    break;
  }
}

static void ScreenCall_Leave(SVCallFrame *pFrame)
{
  const int nProcess = (int)pFrame->Args[1];
  const int nParam = (int)pFrame->Args[2];
  if (pFrame->pSlot->eSpecial == SPECIAL_ScreenSetParameter)
    Trace_Write("[screenfx]   mfSetParameter(process %d, param %d) -> %d", nProcess, nParam, (int)pFrame->nResult);
  else if (pFrame->pSlot->eSpecial == SPECIAL_ScreenGetParameter)
    Trace_Write("[screenfx] %p mfGetParameter(process %d, param %d) -> %p: %s, from %s, time %.4f", (void *)pFrame->Args[0], nProcess,
      nParam, (void *)pFrame->nResult, ScreenValueText((void *)pFrame->nResult), Trace_Addr(pFrame->pReturn),
      iTimer ? iTimer->GetCurrTime() : 0.0f);
}

//! An argument as a quoted string if it points to a printable one, else as a number.
static const char *ArgText(UINT_PTR nArg)
{
  // Whole strings up to 1023 characters (file paths, log lines).
  static char s_szBuf[8][1040];
  static volatile LONG s_nBuf = 0;
  char *szOut = s_szBuf[InterlockedIncrement(&s_nBuf) & 7];
  const char *sz = (const char *)nArg;
  int n = 0;
  if (nArg >= 0x10000)
  {
    for (; n < 1024; n++)
    {
      if ((n == 0 || ((UINT_PTR)(sz + n) & 0xFFF) == 0) && !Trace_IsReadable(sz + n, 1))
        break;
      if (sz[n] < 0x20 || sz[n] >= 0x7F)
        break;
    }
  }
  if (nArg >= 0x10000 && n < 1024 && Trace_IsReadable(sz + n, 1) && sz[n] == 0)
    _snprintf(szOut, sizeof(s_szBuf[0]) - 1, "'%s'", sz);
  else
    _snprintf(szOut, sizeof(s_szBuf[0]) - 1, "0x%llx", (unsigned long long)nArg);
  szOut[sizeof(s_szBuf[0]) - 1] = 0;
  return szOut;
}

//! The type, flags and value of a console variable.
static const char *CVarText(ICVar *pVar)
{
  static char s_szBuf[2048];
  if (!pVar || !Trace_IsReadable(pVar, sizeof(void *)))
    return "";
  const char *szValue = pVar->GetString();
  _snprintf(s_szBuf, sizeof(s_szBuf) - 1, " '%s' type %d flags 0x%x value '%s'", pVar->GetName(), pVar->GetType(), pVar->GetFlags(),
    szValue ? szValue : "(null)");
  s_szBuf[sizeof(s_szBuf) - 1] = 0;
  return s_szBuf;
}

//! IConsole::GetCVar, from any module: the first lookup of each name by each module is written.
static void ConsoleGetCVar_Enter(SVCallFrame *pFrame, int nModule)
{
  const char *szName = (const char *)pFrame->Args[1];
  if (CV_xrt_Trace <= 0 || !szName || !Trace_IsReadable(szName, 1))
    return;
  std::string Key = std::string(nModule >= 0 ? s_Modules[nModule].szName : "?") + " " + szName;
  AcquireSRWLockExclusive(&s_Lock);
  const int nCount = ++s_CVarLookups[Key];
  ReleaseSRWLockExclusive(&s_Lock);
  if (nCount == 1)
    pFrame->nFlags |= FRAME_LOG;
}

static void ConsoleGetCVar_Leave(SVCallFrame *pFrame)
{
  Trace_Write("[console] GetCVar(%s, case sensitive %d) from %s -> %p%s", ArgText(pFrame->Args[1]), (int)(BYTE)pFrame->Args[2],
    Trace_Addr(pFrame->pReturn), (void *)pFrame->nResult, CVarText((ICVar *)pFrame->nResult));
}

//! The engine's interfaces whose calls from the renderer are written with their arguments as text
//! (all but I3DEngine).
static bool IsTextCall(const SVSlot *pSlot)
{
  return pSlot->pTable->bEngine && strcmp(pSlot->pTable->szName, "I3DEngine");
}

//! "[console] Register" or "[engine] ILog::Log".
static const char *TextCallName(const SVSlot *pSlot)
{
  static char s_szBuf[96];
  if (pSlot->pTable->pInfo == s_pConsoleInfo)
    _snprintf(s_szBuf, sizeof(s_szBuf) - 1, "[console] %s", pSlot->szMethod);
  else
    _snprintf(s_szBuf, sizeof(s_szBuf) - 1, "[engine] %s::%s", pSlot->pTable->szName, pSlot->szMethod);
  s_szBuf[sizeof(s_szBuf) - 1] = 0;
  return s_szBuf;
}

//! The other IConsole methods and the other engine services the renderer calls: every register
//! argument and the first three stack arguments (the overloads share a name; the slot tells them
//! apart).
static void TextCall_Enter(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  const UINT_PTR *pStack = pFrame->StackArgs;
  Trace_Write("%s (slot %d) from %s: rdx %s, r8 %s, r9 %s; xmm1 %g, xmm2 %g, xmm3 %g; stack %s %s %s", TextCallName(pSlot), pSlot->nSlot,
    Trace_Addr(pFrame->pReturn), ArgText(pFrame->Args[1]), ArgText(pFrame->Args[2]), ArgText(pFrame->Args[3]), pFrame->Xmm[1][0],
    pFrame->Xmm[2][0], pFrame->Xmm[3][0], ArgText(pStack[0]), ArgText(pStack[1]), ArgText(pStack[2]));
}

static void TextCall_Leave(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  Trace_Write("%s -> 0x%llx (xmm0 %g)%s", TextCallName(pSlot), (unsigned long long)pFrame->nResult, pFrame->XmmResult[0],
    !strcmp(pSlot->szMethod, "CreateVariable") ? CVarText((ICVar *)pFrame->nResult) : "");
}

//! AddWaves(SWaveForm2 **wf): the renderer points wf[0] and wf[1] to the object's two new waves and
//! the engine fills them after the call, so the waves of a written call are written at the next one.
static const SWaveForm2 *s_pFilledWaves[2];

//! The arguments of the I3DEngine methods the renderer calls (written after the [vcall] line).
static void EngineCall_Enter(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  if (strcmp(pSlot->pTable->szName, "I3DEngine"))
    return;
  if (!strcmp(pSlot->szMethod, "DrawLowDetail"))
  {
    // DrawLowDetail(const int &DrawFlags).
    const int *pFlags = (const int *)pFrame->Args[1];
    if (Trace_IsReadable(pFlags, sizeof(*pFlags)))
      Trace_Write("[vcall]     DrawFlags 0x%x", *pFlags);
  }
  else if (!strcmp(pSlot->szMethod, "SetCamera"))
  {
    // SetCamera(const CCamera &cam, bool bToTheScreen).
    const CCamera *pCam = (const CCamera *)pFrame->Args[1];
    if (Trace_IsReadable(pCam, sizeof(*pCam)))
    {
      const Vec3 &vPos = pCam->GetPos();
      const Ang3 vAng = pCam->GetAngles();
      Trace_Write("[vcall]     cam pos (%.2f %.2f %.2f) angles (%.2f %.2f %.2f) fov %.3f zmin %.3f zmax %.1f, bToTheScreen %d",
        vPos.x, vPos.y, vPos.z, vAng.x, vAng.y, vAng.z, pCam->GetFov(), pCam->GetZMin(), pCam->GetZMax(), (int)(BYTE)pFrame->Args[2]);
    }
  }
}

//! The name of a shader of the original renderer (the proxy's own calls are not written).
static const char *ShaderName(UINT_PTR pShader)
{
  if (!pShader || !Trace_IsReadable((const void *)pShader, sizeof(void *)))
    return "?";
  const char *szName = ((IShader *)pShader)->GetName();
  return szName && Trace_IsReadable(szName, 1) ? szName : "?";
}

//! The arguments and results of the IShader template methods (written after the [vcall] lines).
static void ShaderCall_Enter(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  if (strcmp(pSlot->pTable->szName, "IShader"))
    return;
  if (!strcmp(pSlot->szMethod, "AddTemplate"))
  {
    // AddTemplate(SRenderShaderResources *Res, int &TemplId, const char *Name, bool bSetPreferred, uint64 nMaskGen).
    const int *pId = (const int *)pFrame->Args[2];
    const char *szName = (const char *)pFrame->Args[3];
    Trace_Write("[vcall]     shader '%s' res %p TemplId %d name '%s' preferred %d maskGen 0x%llx", ShaderName(pFrame->Args[0]),
      (void *)pFrame->Args[1], Trace_IsReadable(pId, sizeof(*pId)) ? *pId : -999, szName && Trace_IsReadable(szName, 1) ? szName : "(null)",
      (int)(BYTE)pFrame->StackArgs[0], (unsigned long long)pFrame->StackArgs[1]);
  }
  else if (!strcmp(pSlot->szMethod, "RemoveTemplate") || !strcmp(pSlot->szMethod, "GetTemplate"))
    Trace_Write("[vcall]     shader '%s' id %d", ShaderName(pFrame->Args[0]), (int)pFrame->Args[1]);
}

static void ShaderCall_Leave(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  if (strcmp(pSlot->pTable->szName, "IShader"))
    return;
  if (!strcmp(pSlot->szMethod, "AddTemplate"))
  {
    const int *pId = (const int *)pFrame->Args[2];
    if (Trace_IsReadable(pId, sizeof(*pId)))
      Trace_Write("[vcall]     -> TemplId %d", *pId);
  }
  else if (!strcmp(pSlot->szMethod, "GetTemplate"))
    Trace_Write("[vcall]     -> template '%s'", ShaderName(pFrame->nResult));
}

//! The arguments of the CCObject methods (written after the [vcall] line).
static void ObjectCall_Enter(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  if (strcmp(pSlot->pTable->szName, "CCObject"))
    return;
  if (!strcmp(pSlot->szMethod, "AddWaves"))
  {
    for (int i = 0; i < 2; i++)
    {
      const SWaveForm2 *pWF = s_pFilledWaves[i];
      if (pWF && Trace_IsReadable(pWF, sizeof(*pWF)))
        Trace_Write("[vcall]     wave %c of the call before: type %d level %g amp %g phase %g freq %g", 'X' + i, pWF->m_eWFType,
          pWF->m_Level, pWF->m_Amp, pWF->m_Phase, pWF->m_Freq);
      s_pFilledWaves[i] = NULL;
    }
  }
  else if (!strcmp(pSlot->szMethod, "SetShaderFloat"))
  {
    // SetShaderFloat(const char *Name, float Val).
    const char *szName = (const char *)pFrame->Args[1];
    Trace_Write("[vcall]     '%s' = %g", szName && Trace_IsReadable(szName, 1) ? szName : "?", pFrame->Xmm[2][0]);
  }
}

static void ObjectCall_Leave(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  if (strcmp(pSlot->pTable->szName, "CCObject") || strcmp(pSlot->szMethod, "AddWaves"))
    return;
  const CCObject *pObj = (const CCObject *)pFrame->Args[0];
  const SWaveForm2 *const *pWaves = (const SWaveForm2 *const *)pFrame->Args[1];
  if (!Trace_IsReadable(pObj, sizeof(*pObj)) || !Trace_IsReadable(pWaves, 2 * sizeof(*pWaves)))
    return;
  Trace_Write("[vcall]     -> m_NumWFX %d m_NumWFY %d m_fBending %g wf %p %p time %.4f", pObj->m_NumWFX, pObj->m_NumWFY, pObj->m_fBending,
    pWaves[0], pWaves[1], iTimer ? iTimer->GetCurrTime() : 0.0f);
  s_pFilledWaves[0] = pWaves[0];
  s_pFilledWaves[1] = pWaves[1];
}

#ifdef _WIN64
//! The return address of the call of the original function in VCall_Thunk.
extern "C" void VCall_ThunkReturn();
#endif

//! The rest of VCall_Enter for a call of the kind written out; out of line, so that VCall_Enter
//! stays short for the calls it only counts.
static __declspec(noinline) void VCall_EnterWritten(SVCallFrame *pFrame, bool bMainThread)
{
  SVSlot *pSlot = pFrame->pSlot;
  if (!bMainThread && InterlockedIncrement(&pSlot->nOtherThread) == 1)
    Trace_Write("[thread] %s::%s called from thread %u (the renderer was loaded by thread %u), from %s", pSlot->pTable->szName,
      pSlot->szMethod, GetCurrentThreadId(), Trace_MainThread(), Trace_Addr(pFrame->pReturn));
  if (CV_xrt_Trace <= 0)
    return;
  if (pSlot->eSpecial != SPECIAL_None)
  {
    ScreenCall_Enter(pFrame);
    return;
  }
  if (InterlockedIncrement(&pSlot->nLogged) > (pSlot->bListed ? s_nLogListed : nLogPerSlot))
    return;
  pFrame->nFlags |= FRAME_LOG;
  if (IsTextCall(pSlot))
  {
    TextCall_Enter(pFrame);
    return;
  }
  Trace_Write("[vcall] %s::%s(this %p, rdx 0x%llx, r8 0x%llx, r9 0x%llx; xmm1 %g, xmm2 %g, xmm3 %g; stack 0x%llx 0x%llx) from %s",
    pSlot->pTable->szName, pSlot->szMethod, (void *)pFrame->Args[0], (unsigned long long)pFrame->Args[1], (unsigned long long)pFrame->Args[2],
    (unsigned long long)pFrame->Args[3], pFrame->Xmm[1][0], pFrame->Xmm[2][0], pFrame->Xmm[3][0], (unsigned long long)pFrame->StackArgs[0],
    (unsigned long long)pFrame->StackArgs[1], Trace_Addr(pFrame->pReturn));
  ObjectCall_Enter(pFrame);
  ShaderCall_Enter(pFrame);
  EngineCall_Enter(pFrame);
}

extern "C" void VCall_Enter(SVCallFrame *pFrame)
{
  SVSlot *pSlot = pFrame->pSlot;
  pFrame->pOrig = pSlot->pOrig;
  pFrame->nFlags = 0;
  if (pSlot->bDraw)
  {
    pFrame->nPad = (UINT_PTR)s_pDrawingRE;
    s_pDrawingRE = (CRendElement *)pFrame->Args[0];
    pFrame->nFlags = FRAME_DRAW;
    D3DDraws_OnDrawRE();
  }
  const int nModule = ModuleIndex(pFrame->pReturn);
  ECaller eCaller = nModule == s_nRenderer ? CALLER_Renderer : nModule == s_nSelf ? CALLER_Proxy : CALLER_Engine;
#ifdef _WIN64
  // A hooked function that ends with a jump to another one (e.g. Release to the destructor); in an
  // engine interface the jump is made by the engine's own function (IConsole::Clear is reached so).
  if (pFrame->pReturn == (void *)&VCall_ThunkReturn)
    eCaller = pSlot->pTable->bEngine ? CALLER_Engine : CALLER_Renderer;
#endif
  const bool bMainThread = GetCurrentThreadId() == Trace_MainThread();
  if (bMainThread)
    pSlot->nMainCalls[eCaller]++;
  else
    InterlockedIncrement(&pSlot->nCalls[eCaller]);
  if (eCaller == CALLER_Engine && nModule >= 0 && !(pSlot->nEngineModules & 1 << nModule))
    InterlockedOr(&pSlot->nEngineModules, 1 << nModule);
  // The console variables every module looks up, the engine's modules as well.
  if (pSlot->eSpecial == SPECIAL_ConsoleGetCVar)
  {
    if (eCaller != CALLER_Proxy)
      ConsoleGetCVar_Enter(pFrame, nModule);
    return;
  }
  // The engine's interfaces: the calls of the renderer into the engine are the ones to write.
  if (eCaller != (pSlot->pTable->bEngine ? CALLER_Renderer : CALLER_Engine))
    return;
  if (!bMainThread || CV_xrt_Trace > 0)
    VCall_EnterWritten(pFrame, bMainThread);
}

extern "C" void VCall_Leave(SVCallFrame *pFrame)
{
  if (pFrame->nFlags & FRAME_DRAW)
    s_pDrawingRE = (CRendElement *)pFrame->nPad;
  if (!(pFrame->nFlags & FRAME_LOG))
    return;
  SVSlot *pSlot = pFrame->pSlot;
  if (pSlot->eSpecial == SPECIAL_ConsoleGetCVar)
    ConsoleGetCVar_Leave(pFrame);
  else if (pSlot->eSpecial != SPECIAL_None)
    ScreenCall_Leave(pFrame);
  else if (IsTextCall(pSlot))
    TextCall_Leave(pFrame);
  else
  {
    Trace_Write("[vcall]   %s::%s -> rax 0x%llx (xmm0 %g)", pSlot->pTable->szName, pSlot->szMethod, (unsigned long long)pFrame->nResult,
      pFrame->XmmResult[0]);
    ObjectCall_Leave(pFrame);
    ShaderCall_Leave(pFrame);
  }
}

//////////////////////////////////////////////////////////////////////////
// Installation

#ifdef _WIN64
extern "C" void VCall_Thunk();

//! mov r10, pSlot; jmp [rip]; VCall_Thunk
static const int nStubSize = 24;

static void WriteStub(BYTE *pStub, SVSlot *pSlot)
{
  void *pThunk = (void *)&VCall_Thunk;
  pStub[0] = 0x49;
  pStub[1] = 0xBA;
  memcpy(pStub + 2, &pSlot, 8);
  pStub[10] = 0xFF;
  pStub[11] = 0x25;
  memset(pStub + 12, 0, 4);
  memcpy(pStub + 16, &pThunk, 8);
}
#endif

static bool WriteVTable(void **pVTable, void *const *pEntries, int nSlots)
{
  const SIZE_T nSize = nSlots * sizeof(void *);
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

static ESpecial SpecialOf(const char *szClass, const char *szMethod)
{
  if (!strcmp(szClass, "IConsole") && !strcmp(szMethod, "GetCVar"))
    return SPECIAL_ConsoleGetCVar;
  if (strcmp(szClass, "CREScreenProcess"))
    return SPECIAL_None;
  if (!strcmp(szMethod, "mfActivate"))
    return SPECIAL_ScreenActivate;
  if (!strcmp(szMethod, "mfSetParameter"))
    return SPECIAL_ScreenSetParameter;
  if (!strcmp(szMethod, "mfGetParameter"))
    return SPECIAL_ScreenGetParameter;
  return SPECIAL_None;
}

static void Reject(void **pVTable)
{
  if (s_nRejected < nMaxTables)
    s_Rejected[s_nRejected++] = pVTable;
}

//! Hooks the vtable (under s_Lock); bEngine: an interface of the engine, not of the renderer.
static void InstallLocked(const char *szClass, void **pVTable, const char *szTag, bool bEngine = false)
{
#ifndef _WIN64
  if (!s_bDisabled)
    Trace_Write("vcall hooks: x64 only");
  s_bDisabled = true;
#else
  if (!s_bInitialized)
  {
    s_bInitialized = true;
    char szEnv[16];
    if (GetEnvironmentVariableA("XRENDERTRACED_TRACE_METHODS_MAX", szEnv, sizeof(szEnv)) && atoi(szEnv) > 0)
      s_nLogListed = atoi(szEnv);
    if (GetEnvironmentVariableA("XRENDERTRACED_NO_VCALL_HOOKS", szEnv, sizeof(szEnv)) && szEnv[0] == '1')
    {
      Trace_Write("vcall hooks: off (XRENDERTRACED_NO_VCALL_HOOKS)");
      s_bDisabled = true;
      return;
    }
    // The hooked vtables point into this DLL until the process exits.
    HMODULE hSelf = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)&VCallHooks_Install, &hSelf);
    s_nSelf = AddModule(hSelf);
    if (s_nRenderer < 0)
    {
      Trace_Write("vcall hooks: the module of the original renderer is not known, not hooking");
      s_bDisabled = true;
      return;
    }
  }
  if (s_bDisabled)
    return;
  for (int i = 0; i < s_nTables; i++)
  {
    if (s_Tables[i].pVTable == pVTable)
    {
      s_pLastVTable = pVTable;
      return;
    }
  }
  for (int i = 0; i < s_nRejected; i++)
  {
    if (s_Rejected[i] == pVTable)
      return;
  }

  const SVTableInfo *pInfo = NULL;
  for (int i = 0; i < (int)(sizeof(s_VTableInfos) / sizeof(s_VTableInfos[0])); i++)
  {
    if (!strcmp(s_VTableInfos[i].szClass, szClass))
      pInfo = &s_VTableInfos[i];
  }
  if (pInfo && !strcmp(pInfo->szClass, "IConsole"))
    s_pConsoleInfo = pInfo;
  if (!pInfo || s_nTables == nMaxTables)
  {
    Trace_Write("vcall hooks: %s vtable %s: %s, not hooked", szClass, Trace_Addr(pVTable), pInfo ? "too many vtables" : "unknown class");
    Reject(pVTable);
    return;
  }
  // The vtable and all its slots must be in the original renderer (for an engine interface: in the
  // module of the vtable, not the renderer), or the object is not the one the SDK header describes.
  int nOwner = s_nRenderer;
  if (bEngine)
  {
    HMODULE hOwner = NULL;
    nOwner = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pVTable,
      &hOwner) ? AddModule(hOwner) : -1;
  }
  bool bValid = nOwner >= 0 && (!bEngine || nOwner != s_nRenderer) && Trace_IsReadable(pVTable, pInfo->nSlots * sizeof(void *));
  const SModule &Owner = s_Modules[nOwner >= 0 ? nOwner : s_nRenderer];
  bValid = bValid && (UINT_PTR)pVTable >= Owner.nBase && (UINT_PTR)pVTable < Owner.nEnd;
  for (int i = 0; bValid && i < pInfo->nSlots; i++)
  {
    UINT_PTR nSlot = (UINT_PTR)pVTable[i];
    bValid = nSlot >= Owner.nBase && nSlot < Owner.nEnd && Trace_IsCode(pVTable[i]);
  }
  if (!bValid)
  {
    Trace_Write("vcall hooks: %s vtable %s is not one of the %s, not hooked", szClass, Trace_Addr(pVTable),
      bEngine ? "engine" : "original renderer");
    Reject(pVTable);
    return;
  }

  BYTE *pStubs = (BYTE *)VirtualAlloc(NULL, pInfo->nSlots * nStubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if (!pStubs)
  {
    Trace_Write("vcall hooks: can't allocate stubs (error %u)", GetLastError());
    Reject(pVTable);
    return;
  }
  SHookedVTable &Table = s_Tables[s_nTables];
  memset(&Table, 0, sizeof(Table));
  Table.pInfo = pInfo;
  Table.pVTable = pVTable;
  Table.bEngine = bEngine;
  int nSame = 0;
  for (int i = 0; i < s_nTables; i++)
    nSame += s_Tables[i].pInfo == pInfo;
  if (szTag)
    _snprintf(Table.szName, sizeof(Table.szName) - 1, "%s[%s]", szClass, szTag);
  else if (nSame)
    _snprintf(Table.szName, sizeof(Table.szName) - 1, "%s#%d", szClass, nSame + 1);
  else
    _snprintf(Table.szName, sizeof(Table.szName) - 1, "%s", szClass);
  void *pEntries[nMaxSlots];
  for (int i = 0; i < pInfo->nSlots; i++)
  {
    SVSlot &Slot = Table.Slots[i];
    Slot.pTable = &Table;
    Slot.nSlot = i;
    Slot.szMethod = pInfo->szMethods[i];
    Slot.pOrig = pVTable[i];
    Slot.eSpecial = SpecialOf(szClass, Slot.szMethod);
    Slot.bDraw = !strcmp(Slot.szMethod, "mfDraw");
    char szName[128];
    _snprintf(szName, sizeof(szName) - 1, "%s::%s", Table.szName, Slot.szMethod);
    szName[sizeof(szName) - 1] = 0;
    Slot.bListed = pInfo == s_pConsoleInfo || Trace_IsListed("XRENDERTRACED_TRACE_METHODS", szName);
    WriteStub(pStubs + i * nStubSize, &Slot);
    pEntries[i] = pStubs + i * nStubSize;
  }
  FlushInstructionCache(GetCurrentProcess(), pStubs, pInfo->nSlots * nStubSize);
  if (!WriteVTable(pVTable, pEntries, pInfo->nSlots))
  {
    Trace_Write("vcall hooks: can't write %s vtable %s (error %u)", szClass, Trace_Addr(pVTable), GetLastError());
    Reject(pVTable);
    return;
  }
  s_nTables++;
  s_pLastVTable = pVTable;
  Trace_Write("vcall hooks: %s vtable %s hooked (%d slots)", Table.szName, Trace_Addr(pVTable), pInfo->nSlots);
#endif
}

void VCallHooks_Install(const char *szClass, void *pObject, const char *szTag)
{
  if (!pObject || s_bDisabled)
    return;
  void **pVTable = *(void ***)pObject;
  if (pVTable == s_pLastVTable)
    return;
  AcquireSRWLockExclusive(&s_Lock);
  InstallLocked(szClass, pVTable, szTag);
  ReleaseSRWLockExclusive(&s_Lock);
}

void VCallHooks_InstallEngine(const char *szClass, void *pObject)
{
  if (!pObject || s_bDisabled)
    return;
  void **pVTable = *(void ***)pObject;
  AcquireSRWLockExclusive(&s_Lock);
  InstallLocked(szClass, pVTable, NULL, true);
  ReleaseSRWLockExclusive(&s_Lock);
}

void VCallHooks_InstallRE(CRendElement *pRE)
{
  if (!pRE)
    return;
  const char *szClass;
  switch (pRE->m_Type)
  {
  case eDATA_Dummy:                       szClass = "CREDummy"; break;
  case eDATA_Sky:                         szClass = "CRESky"; break;
  case eDATA_Poly:                        szClass = "CREPolyMesh"; break;
  case eDATA_TriMeshShadow:               szClass = "CRETriMeshShadow"; break;
  case eDATA_OcLeaf:                      szClass = "CREOcLeaf"; break;
  case eDATA_TerrainSector:               szClass = "CRECommon"; break;
  case eDATA_2DQuad:                      szClass = "CRE2DQuad"; break;
  case eDATA_FarTreeSprites:              szClass = "CREFarTreeSprites"; break;
  case eDATA_ShadowMapGen:                szClass = "CREShadowMapGen"; break;
  case eDATA_TerrainDetailTextureLayers:  szClass = "CRETerrainDetailTextureLayers"; break;
  case eDATA_TerrainParticles:            szClass = "CRETerrainParticles"; break;
  case eDATA_Glare:                       szClass = "CREGlare"; break;
  case eDATA_OcclusionQuery:              szClass = "CREOcclusionQuery"; break;
  case eDATA_ClearStencil:                szClass = "CREClearStencil"; break;
  case eDATA_FlashBang:                   szClass = "CREFlashBang"; break;
  case eDATA_ScreenProcess:               szClass = "CREScreenProcess"; break;
  default:
    // A type without an SDK class: only the CRendElement functions.
    VCallHooks_Install("CRendElement", pRE, Trace_RETypeName(pRE->m_Type));
    return;
  }
  VCallHooks_Install(szClass, pRE);
}

//////////////////////////////////////////////////////////////////////////

CRendElement *VCallHooks_DrawingRE()
{
  return s_pDrawingRE;
}

//////////////////////////////////////////////////////////////////////////

void VCallHooks_RestartListed()
{
  AcquireSRWLockExclusive(&s_Lock);
  for (int i = 0; i < s_nTables; i++)
    for (int j = 0; j < s_Tables[i].pInfo->nSlots; j++)
      if (s_Tables[i].Slots[j].bListed)
        s_Tables[i].Slots[j].nLogged = 0;
  ReleaseSRWLockExclusive(&s_Lock);
}

int VCallHooks_ModuleIndex(const void *pAddr)
{
  return ModuleIndex(pAddr);
}

const char *VCallHooks_ModuleName(int nModule)
{
  return nModule >= 0 && nModule < s_nModules ? s_Modules[nModule].szName : "?";
}

void VCallHooks_WriteSummary(const char *szReason)
{
  if (!s_nTables)
    return;
  AcquireSRWLockShared(&s_Lock);
  Trace_Write("==== virtual calls (%s): engine (modules) / renderer / proxy ====", szReason);
  // For the engine's interfaces (I3DEngine) the renderer column is the calls of the renderer into
  // the engine.
  for (int t = 0; t < s_nTables; t++)
  {
    const SHookedVTable &Table = s_Tables[t];
    Trace_Write("  %s (vtbl %s)", Table.szName, Trace_Addr(Table.pVTable));
    for (int i = 0; i < Table.pInfo->nSlots; i++)
    {
      const SVSlot &Slot = Table.Slots[i];
      LONG nCalls[CALLER_Num];
      for (int c = 0; c < CALLER_Num; c++)
        nCalls[c] = Slot.nCalls[c] + Slot.nMainCalls[c];
      if (!nCalls[CALLER_Engine] && !nCalls[CALLER_Renderer] && !nCalls[CALLER_Proxy])
        continue;
      char szModules[160] = "";
      int nLen = 0;
      for (int m = 0; m < s_nModules && nLen < (int)sizeof(szModules) - 40; m++)
      {
        if (Slot.nEngineModules & (1 << m))
          nLen += _snprintf(szModules + nLen, sizeof(szModules) - nLen - 1, "%s%s", nLen ? ", " : "", s_Modules[m].szName);
      }
      Trace_Write("    %2d %-28s %8d (%s) / %8d / %d", i, Slot.szMethod, nCalls[CALLER_Engine], szModules, nCalls[CALLER_Renderer],
        nCalls[CALLER_Proxy]);
    }
  }
  if (!s_ScreenCalls.empty())
  {
    static const char *s_Methods[] = { "", "mfActivate", "mfSetParameter", "mfGetParameter" };
    Trace_Write("  screen effect calls from the engine: method(process, param) count");
    for (std::map<unsigned int, int>::const_iterator it = s_ScreenCalls.begin(); it != s_ScreenCalls.end(); ++it)
      Trace_Write("    %s(%d, %d) %d", s_Methods[(it->first >> 24) & 3], (it->first >> 12) & 0xFFF, it->first & 0xFFF, it->second);
  }
  if (!s_CVarLookups.empty())
  {
    Trace_Write("  IConsole::GetCVar lookups: module, name, count");
    for (std::map<std::string, int>::const_iterator it = s_CVarLookups.begin(); it != s_CVarLookups.end(); ++it)
      Trace_Write("    %s %d", it->first.c_str(), it->second);
  }
  ReleaseSRWLockShared(&s_Lock);
}

void VCallHooks_Uninstall()
{
  AcquireSRWLockExclusive(&s_Lock);
  for (int t = 0; t < s_nTables; t++)
  {
    SHookedVTable &Table = s_Tables[t];
    void *pEntries[nMaxSlots];
    for (int i = 0; i < Table.pInfo->nSlots; i++)
      pEntries[i] = Table.Slots[i].pOrig;
    WriteVTable(Table.pVTable, pEntries, Table.pInfo->nSlots);
  }
  if (s_nTables)
    Trace_Write("vcall hooks: %d vtables restored", s_nTables);
  // The stubs stay: a call may still be on its way through one.
  s_nTables = 0;
  s_pLastVTable = NULL;
  s_bDisabled = true;
  ReleaseSRWLockExclusive(&s_Lock);
}
