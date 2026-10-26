//////////////////////////////////////////////////////////////////////
//
//	File: RenderTrace.cpp
//	Description: Trace log of everything the engine passes to the renderer.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "FieldWatch.h"
#include "VCallHooks.h"
#include <LMCompStructures.h>
#include <map>
#include <string>

int CV_xrt_Trace = 2;
int CV_xrt_TraceCalls = 30;
int CV_xrt_TraceFrames = 3;

static FILE *s_pTraceFile = NULL;
static STraceSite *s_pSites = NULL;
static PVOID s_pExceptionHandler = NULL;
static int s_nExceptionsLogged = 0;
static int s_nLastFrame = 0;
static int s_nRestartFrame = 0;
static bool s_bCapture = false;
//! The thread that loaded the renderer (the engine's main thread).
static DWORD s_dwMainThread = 0;

//////////////////////////////////////////////////////////////////////////
static void Trace_ModuleAddress(const void *pAddr, char *szOut, size_t nOut)
{
  HMODULE hMod = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)pAddr, &hMod) && hMod)
  {
    char szPath[MAX_PATH];
    GetModuleFileNameA(hMod, szPath, sizeof(szPath));
    const char *szName = strrchr(szPath, '\\');
    szName = szName ? szName + 1 : szPath;
    _snprintf(szOut, nOut, "%s+0x%x", szName, (unsigned)((const char *)pAddr - (const char *)hMod));
  }
  else
    _snprintf(szOut, nOut, "%p", pAddr);
  szOut[nOut - 1] = 0;
}

static bool Trace_IsCodeAddress(UINT_PTR nAddr)
{
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery((LPCVOID)nAddr, &mbi, sizeof(mbi)))
    return false;
  if (mbi.Type != MEM_IMAGE || mbi.State != MEM_COMMIT)
    return false;
  return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

//! Logs first-chance faults (crashes are what the trace is mostly needed for), with a heuristic
//! call stack: every dword on the stack that points into executable code of a loaded module.
static LONG CALLBACK Trace_ExceptionHandler(EXCEPTION_POINTERS *pEx)
{
  DWORD nCode = pEx->ExceptionRecord->ExceptionCode;
  if (nCode != EXCEPTION_ACCESS_VIOLATION && nCode != EXCEPTION_ILLEGAL_INSTRUCTION && nCode != EXCEPTION_INT_DIVIDE_BY_ZERO &&
      nCode != EXCEPTION_STACK_OVERFLOW && nCode != EXCEPTION_PRIV_INSTRUCTION)
    return EXCEPTION_CONTINUE_SEARCH;
  if (!s_pTraceFile || s_nExceptionsLogged >= 8)
    return EXCEPTION_CONTINUE_SEARCH;
  s_nExceptionsLogged++;

  char szWhere[MAX_PATH + 32];
  Trace_ModuleAddress(pEx->ExceptionRecord->ExceptionAddress, szWhere, sizeof(szWhere));
  Trace_Write("!!! Exception 0x%08x at %s", nCode, szWhere);
  if (nCode == EXCEPTION_ACCESS_VIOLATION && pEx->ExceptionRecord->NumberParameters >= 2)
    Trace_Write("!!!   %s address %p", pEx->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
      (void *)pEx->ExceptionRecord->ExceptionInformation[1]);
  const CONTEXT *c = pEx->ContextRecord;
#if defined(_M_IX86)
  Trace_Write("!!!   eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x",
    c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
  const UINT_PTR *pStack = (const UINT_PTR *)(UINT_PTR)c->Esp;
#else
  Trace_Write("!!!   rax=%p rbx=%p rcx=%p rdx=%p rsi=%p rdi=%p rbp=%p rsp=%p",
    (void *)c->Rax, (void *)c->Rbx, (void *)c->Rcx, (void *)c->Rdx, (void *)c->Rsi, (void *)c->Rdi, (void *)c->Rbp, (void *)c->Rsp);
  const UINT_PTR *pStack = (const UINT_PTR *)c->Rsp;
#endif
  int nFound = 0;
  for (int i = 0; i < 1024 && nFound < 24; i++)
  {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(&pStack[i], &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
      break;
    if (Trace_IsCodeAddress(pStack[i]))
    {
      Trace_ModuleAddress((const void *)pStack[i], szWhere, sizeof(szWhere));
      Trace_Write("!!!   [sp+0x%03x] %s", i * (int)sizeof(UINT_PTR), szWhere);
      nFound++;
    }
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

//////////////////////////////////////////////////////////////////////////
void Trace_Open(const char *szFileName)
{
  if (s_pTraceFile)
    return;
  s_pTraceFile = fopen(szFileName, "wt");
  if (!s_pTraceFile)
    return;
  s_dwMainThread = GetCurrentThreadId();
  SYSTEMTIME st;
  GetLocalTime(&st);
  Trace_Write("XRenderD3D9Traced trace, %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  s_pExceptionHandler = AddVectoredExceptionHandler(1, Trace_ExceptionHandler);
}

void Trace_Close()
{
  if (s_pExceptionHandler)
  {
    RemoveVectoredExceptionHandler(s_pExceptionHandler);
    s_pExceptionHandler = NULL;
  }
  if (s_pTraceFile)
  {
    Trace_Write("Trace closed");
    fclose(s_pTraceFile);
    s_pTraceFile = NULL;
  }
}

void Trace_RegisterCVars()
{
  static bool s_bRegistered = false;
  if (!iConsole || s_bRegistered)
    return;
  s_bRegistered = true;
  iConsole->Register("xrt_Trace", &CV_xrt_Trace, (float)CV_xrt_Trace, 0,
    "Renderer trace log (XRenderD3D9Traced.log): 0 - off, 1 - calls, 2 - calls and structures");
  iConsole->Register("xrt_TraceCalls", &CV_xrt_TraceCalls, (float)CV_xrt_TraceCalls, 0,
    "Number of calls of every renderer method written to the trace log");
  iConsole->Register("xrt_TraceFrames", &CV_xrt_TraceFrames, (float)CV_xrt_TraceFrames, 0,
    "Per-frame call statistics are logged for the first N frames and then every 500th frame");
}

void Trace_Write(const char *szFormat, ...)
{
  if (!s_pTraceFile)
    return;
  char szBuf[2048];
  va_list args;
  va_start(args, szFormat);
  _vsnprintf(szBuf, sizeof(szBuf) - 1, szFormat, args);
  va_end(args);
  szBuf[sizeof(szBuf) - 1] = 0;
  fputs(szBuf, s_pTraceFile);
  fputc('\n', s_pTraceFile);
  // The trace is mostly needed when the game crashes, so nothing may stay in the buffer.
  fflush(s_pTraceFile);
}

//////////////////////////////////////////////////////////////////////////
//! Whether every call of the method is written out (XRENDERTRACED_TRACE_METHODS, names as in the
//! log separated by ',', e.g. "DownLoadToVideoMemory,CLeafBuffer::CreateBufferFromSource").
static bool IsAlwaysTraced(const char *szDisplay)
{
  static char s_szMethods[1024];
  static bool s_bRead = false;
  if (!s_bRead)
  {
    s_bRead = true;
    DWORD nLen = GetEnvironmentVariableA("XRENDERTRACED_TRACE_METHODS", s_szMethods + 1, sizeof(s_szMethods) - 2);
    if (!nLen || nLen >= sizeof(s_szMethods) - 2)
      nLen = 0;
    // ",Name1,Name2," so that a name is found as ",Name,".
    s_szMethods[0] = ',';
    s_szMethods[nLen + 1] = ',';
    s_szMethods[nLen + 2] = 0;
  }
  char szKey[80];
  _snprintf(szKey, sizeof(szKey) - 1, ",%s,", szDisplay);
  szKey[sizeof(szKey) - 1] = 0;
  return strstr(s_szMethods, szKey) != NULL;
}

STraceSite::STraceSite(const char *szName, const char *szSignature)
{
  m_szName = szName;
  // The hook classes stand for the classes of the original renderer they hook.
  static const char *s_Classes[][2] =
  {
    { "CRendererProxy::", "" },
    { "CLeafBufferHooks::", "CLeafBuffer::" },
  };
  _snprintf(m_szDisplay, sizeof(m_szDisplay), "%s", szName);
  for (int i = 0; i < (int)(sizeof(s_Classes) / sizeof(s_Classes[0])); i++)
  {
    size_t nLen = strlen(s_Classes[i][0]);
    if (!strncmp(szName, s_Classes[i][0], nLen))
    {
      _snprintf(m_szDisplay, sizeof(m_szDisplay), "%s%s", s_Classes[i][1], szName + nLen);
      break;
    }
  }
  m_szDisplay[sizeof(m_szDisplay) - 1] = 0;
  // __FUNCSIG__ without the noise: "void CRendererProxy::SetCamera(const CCamera &)".
  static const char *s_Noise[] = { "__thiscall ", "__cdecl ", "__stdcall ", "class ", "struct ", "enum " };
  size_t n = 0;
  for (const char *s = szSignature; *s && n < sizeof(m_szSignature) - 1; )
  {
    bool bSkipped = false;
    for (int i = 0; i < (int)(sizeof(s_Noise) / sizeof(s_Noise[0])); i++)
    {
      size_t nLen = strlen(s_Noise[i]);
      if (!strncmp(s, s_Noise[i], nLen) && (s == szSignature || !isalnum((unsigned char)s[-1]) && s[-1] != '_'))
      {
        s += nLen;
        bSkipped = true;
        break;
      }
    }
    if (!bSkipped)
      m_szSignature[n++] = *s++;
  }
  m_szSignature[n] = 0;
  m_nCalls = 0;
  m_nFrameCalls = 0;
  m_nOtherThreadCalls = 0;
  m_bAlways = IsAlwaysTraced(m_szDisplay);
  m_pNext = s_pSites;
  s_pSites = this;
}

bool STraceSite::Hit()
{
  m_nCalls++;
  m_nFrameCalls++;
  return CV_xrt_Trace > 0 && s_pTraceFile && (m_nCalls <= CV_xrt_TraceCalls || s_bCapture || m_bAlways);
}

STraceScope::STraceScope(STraceSite &Site) : m_Site(Site)
{
  m_bMainThread = GetCurrentThreadId() == s_dwMainThread;
  if (m_bMainThread)
    Watch_EnterRenderer(Site.m_szDisplay);
  else if (Site.m_nOtherThreadCalls++ == 0)
    Trace_Write("[thread] %s called from thread %u (the renderer was loaded by thread %u)", Site.m_szDisplay, GetCurrentThreadId(),
      s_dwMainThread);
}

STraceScope::~STraceScope()
{
  if (m_bMainThread)
    Watch_LeaveRenderer(m_Site.m_szDisplay);
}

void Trace_Call(const STraceSite &Site, const char *szFormat, ...)
{
  char szArgs[1536];
  va_list args;
  va_start(args, szFormat);
  _vsnprintf(szArgs, sizeof(szArgs) - 1, szFormat, args);
  va_end(args);
  szArgs[sizeof(szArgs) - 1] = 0;
  const char *szName = Site.m_szDisplay;
  // The first call also writes the full signature, so the log documents the argument types.
  if (Site.m_nCalls == 1)
    Trace_Write("[%d] %s(%s)    // %s", Site.m_nCalls, szName, szArgs, Site.m_szSignature);
  else
    Trace_Write("[%d] %s(%s)%s", Site.m_nCalls, szName, szArgs, Site.m_nCalls == CV_xrt_TraceCalls && !s_bCapture && !Site.m_bAlways ? "  -- further calls are only counted" : "");
}

void Trace_FrameEnd(int nFrameID)
{
  s_nLastFrame = nFrameID;
  bool bWrite = CV_xrt_Trace > 0 && (nFrameID - s_nRestartFrame <= CV_xrt_TraceFrames || (nFrameID % 500) == 0 || s_bCapture);
  if (bWrite)
  {
    Trace_Write("==== frame %d: calls ====", nFrameID);
    for (STraceSite *pSite = s_pSites; pSite; pSite = pSite->m_pNext)
    {
      if (pSite->m_nFrameCalls)
        Trace_Write("  %6d  %s", pSite->m_nFrameCalls, pSite->m_szDisplay);
    }
    for (STraceSite *pSite = s_pSites; pSite; pSite = pSite->m_pNext)
    {
      if (pSite->m_nOtherThreadCalls)
        Trace_Write("  %s: %d calls from other threads so far", pSite->m_szDisplay, pSite->m_nOtherThreadCalls);
    }
  }
  for (STraceSite *pSite = s_pSites; pSite; pSite = pSite->m_pNext)
    pSite->m_nFrameCalls = 0;
  if (s_bCapture)
  {
    Trace_Write("==== end of captured frame %d ====", nFrameID);
    s_bCapture = false;
    Watch_WriteSummary("after the captured frame");
    VCallHooks_WriteSummary("after the captured frame");
  }
}

const char *Trace_Addr(const void *pAddr)
{
  static char s_szBuf[8][MAX_PATH + 32];
  static int s_nBuf = 0;
  char *szOut = s_szBuf[s_nBuf++ & 7];
  if (!pAddr)
    return "(null)";
  Trace_ModuleAddress(pAddr, szOut, sizeof(s_szBuf[0]));
  return szOut;
}

bool Trace_IsCode(const void *pAddr)
{
  return Trace_IsCodeAddress((UINT_PTR)pAddr);
}

bool Trace_IsReadable(const void *p, size_t nSize)
{
  MEMORY_BASIC_INFORMATION mbi;
  if (!p || !VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
    return false;
  return (const char *)p + nSize <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

const char *Trace_HeapPrefix(const void *pBlock)
{
  static char s_szBuf[4][64];
  static int s_nBuf = 0;
  char *szOut = s_szBuf[s_nBuf++ & 3];
  const DWORD *pPrefix = (const DWORD *)((const char *)pBlock - 16);
  if (!pBlock || !Trace_IsReadable(pPrefix, 16))
    return "(unreadable)";
  sprintf_s(szOut, sizeof(s_szBuf[0]), "%08x %08x %08x %08x", pPrefix[0], pPrefix[1], pPrefix[2], pPrefix[3]);
  return szOut;
}

const char *Trace_VTable(const void *pObject)
{
  if (!pObject)
    return "(null)";
  if (!Trace_IsReadable(pObject, sizeof(void *)))
    return "(unreadable)";
  return Trace_Addr(*(const void *const *)pObject);
}

void Trace_Restart(const char *szReason)
{
  Trace_Write("==== trace restarted after frame %d: %s ====", s_nLastFrame, szReason);
  s_nRestartFrame = s_nLastFrame;
  for (STraceSite *pSite = s_pSites; pSite; pSite = pSite->m_pNext)
    pSite->m_nCalls = 0;
}

void Trace_CaptureFrame()
{
  Trace_Restart("capture of the next frame");
  s_bCapture = true;
}

//////////////////////////////////////////////////////////////////////////
// Names

const char *Trace_RETypeName(int nType)
{
  static const char *s_Names[] =
  {
    "Unknown", "Dummy", "Sky", "Beam", "Poly", "Curve", "MotModel", "MeshModel", "PolyBlend", "AnimPolyBlend",
    "ClientPoly", "ClientPoly2D", "ParticleSpray", "TriMesh", "TriMeshShadow", "Prefab", "Flare", "FlareGeom",
    "FlareProp", "Tree", "Tree_Leaves", "Tree_Branches", "Terrain", "SkyZone", "OcLeaf", "TerrainSector", "2DQuad",
    "FarTreeSprites", "AnimModel", "MotionBlur", "ShadowMapGen", "TerrainDetailTextureLayers", "TerrainParticles",
    "Ocean", "Glare", "OcclusionQuery", "TempMesh", "ClearStencil", "FlashBang", "ScreenProcess", "HDRProcess",
  };
  if (nType >= 0 && nType < (int)(sizeof(s_Names) / sizeof(s_Names[0])))
    return s_Names[nType];
  return "?";
}

const char *Trace_VertexFormatName(int nFormat)
{
  static const char *s_Names[] =
  {
    "0", "P3F", "P3F_COL4UB", "P3F_TEX2F", "P3F_COL4UB_TEX2F", "TRP3F_COL4UB_TEX2F", "P3F_COL4UB_COL4UB", "P3F_N",
    "P3F_N_COL4UB", "P3F_N_TEX2F", "P3F_N_COL4UB_TEX2F", "P3F_N_COL4UB_COL4UB", "P3F_COL4UB_COL4UB_TEX2F",
    "P3F_N_COL4UB_COL4UB_TEX2F", "T3F_B3F_N3F", "TEX2F", "P3F_COL4UB_TEX2F_TEX2F",
  };
  if (nFormat >= 0 && nFormat < VERTEX_FORMAT_NUMS)
    return s_Names[nFormat];
  return "?";
}

static const char *SafeStr(const char *s)
{
  return s ? s : "(null)";
}

//////////////////////////////////////////////////////////////////////////
// Dumps

void Dump_Hex(const char *szName, const void *pData, int nSize)
{
  Trace_Write("    %s @%p, %d bytes:", szName, pData, nSize);
  if (!pData)
    return;
  const byte *p = (const byte *)pData;
  for (int i = 0; i < nSize; i += 16)
  {
    char szLine[128];
    int n = _snprintf(szLine, sizeof(szLine), "      %04x:", i);
    for (int j = i; j < i + 16 && j < nSize; j++)
      n += _snprintf(szLine + n, sizeof(szLine) - n, " %02x", p[j]);
    Trace_Write("%s", szLine);
  }
}

void Dump_HexLine(const char *szName, const void *pData, int nSize)
{
  char szLine[1024];
  int n = _snprintf(szLine, sizeof(szLine), "    %s %p:", szName, pData);
  const byte *p = (const byte *)pData;
  for (int i = 0; i < nSize && n < (int)sizeof(szLine) - 4; i++)
    n += _snprintf(szLine + n, sizeof(szLine) - n, i % 4 ? "%02x" : " %02x", p[i]);
  Trace_Write("%s", szLine);
}

int Trace_HeapBlockSize(const void *pBlock)
{
  const DWORD *pPrefix = (const DWORD *)((const char *)pBlock - 4);
  if (!Trace_IsReadable(pPrefix, 4))
    return -1;
  return (int)*pPrefix;
}

void Dump_Unknown(const char *szName, const void *pData, int nSize)
{
  Trace_Write("    %s @%p, %d bytes:", szName, pData, nSize);
  if (!pData || !Trace_IsReadable(pData, nSize))
    return;
  const byte *p = (const byte *)pData;
  for (int i = 0; i + 4 <= nSize; i += 4)
  {
    DWORD dw = *(const DWORD *)(p + i);
    float f = *(const float *)(p + i);
    char szLine[256];
    int n = _snprintf(szLine, sizeof(szLine), "      +0x%03x  %08x  %11d", i, dw, (int)dw);
    // A float with a plausible exponent (not a small integer, not a denormal).
    int nExp = (dw >> 23) & 0xFF;
    if (nExp > 100 && nExp < 160)
      n += _snprintf(szLine + n, sizeof(szLine) - n, "  %g", f);
    // A pointer to readable memory: the heap block size and the module of its first member.
    if (i + 8 <= nSize)
    {
      const void *pPtr = *(const void *const *)(p + i);
      if ((UINT_PTR)pPtr > 0x10000 && Trace_IsReadable(pPtr, sizeof(void *)))
      {
        const void *pFirst = *(const void *const *)pPtr;
        n += _snprintf(szLine + n, sizeof(szLine) - n, "  ptr %p block 0x%x first %s", pPtr, Trace_HeapBlockSize(pPtr),
          Trace_IsCode(pFirst) || Trace_IsCode(Trace_IsReadable(pFirst, sizeof(void *)) ? *(const void *const *)pFirst : NULL)
            ? Trace_Addr(pFirst) : "data");
      }
    }
    Trace_Write("%s", szLine);
  }
}

//! Dump_Unknown, then the same for the data blocks it points to (not objects with a vtable).
static void Dump_Tree(const std::string &sName, const void *p, int nSize, int nDepth)
{
  Dump_Unknown(sName.c_str(), p, nSize);
  if (nDepth <= 0 || !Trace_IsReadable(p, nSize))
    return;
  const byte *b = (const byte *)p;
  for (int i = 0; i + 8 <= nSize; i += 4)
  {
    const void *pPtr = *(const void *const *)(b + i);
    if ((UINT_PTR)pPtr <= 0x10000 || pPtr == p || !Trace_IsReadable(pPtr, sizeof(void *)))
      continue;
    const void *pFirst = *(const void *const *)pPtr;
    if (Trace_IsCode(pFirst) || (Trace_IsReadable(pFirst, sizeof(void *)) && Trace_IsCode(*(const void *const *)pFirst)))
      continue;
    int nBlock = Trace_HeapBlockSize(pPtr);
    char szSub[32];
    // A list2: the pointer is followed by the count and the allocated count.
    if (i + 16 <= nSize)
    {
      int nCount = *(const int *)(b + i + 8), nAllocated = *(const int *)(b + i + 12);
      if (nCount > 0 && nCount <= nAllocated && nAllocated < 0x10000 && nBlock > 0 && nBlock % nAllocated == 0)
      {
        int nElement = nBlock / nAllocated;
        Trace_Write("    %s +0x%02x: list2 of %d (%d allocated), 0x%x bytes per element", sName.c_str(), i, nCount, nAllocated,
          nElement);
        sprintf_s(szSub, sizeof(szSub), " +0x%02x[0]", i);
        if (nElement <= 0x400)
          Dump_Tree(sName + szSub, pPtr, nElement, nDepth - 1);
        continue;
      }
    }
    if (nBlock <= 0 || nBlock > 0x400)
      continue;
    sprintf_s(szSub, sizeof(szSub), " +0x%02x->", i);
    Dump_Tree(sName + szSub, pPtr, nBlock, nDepth - 1);
  }
}

const void *g_pTraceLastShadowCasters = NULL;

void Dump_Opaque(const char *szName, const void *p, int nMaxDumps, int nDepth)
{
  if (!p)
    return;
  // Counted separately for the captured frame.
  static std::map<std::string, int> s_Dumps[2];
  int &nDumps = s_Dumps[s_bCapture][szName];
  if (nDumps >= nMaxDumps)
    return;
  nDumps++;
  int nSize = Trace_HeapBlockSize(p);
  Trace_Write("    %s @%p: heap block 0x%x", szName, p, nSize);
  Dump_Tree(szName, p, nSize > 0 && nSize <= 0x400 ? nSize : 0x100, nDepth);
}

void Dump_OpaqueList(const char *szName, const void *pList, int nMaxDumps, int nDepth)
{
  if (!pList || !Trace_IsReadable(pList, sizeof(list2<char>)))
    return;
  // The element type is unknown; as a list of chars, GetMemoryUsage() is the allocated count.
  list2<char> &List = *(list2<char> *)pList;
  const void *pElements = List.Count() ? &List[0] : NULL;
  int nAllocated = List.GetMemoryUsage();
  int nBlock = Trace_HeapBlockSize(pElements);
  int nElement = nAllocated > 0 && nBlock > 0 ? nBlock / nAllocated : 0;
  // Counted separately for the captured frame.
  static std::map<std::string, int> s_Dumps[2];
  int &nDumps = s_Dumps[s_bCapture][szName];
  if (nDumps >= nMaxDumps)
    return;
  nDumps++;
  Trace_Write("    %s @%p: %d elements (%d allocated) @%p, heap block 0x%x -> 0x%x per element", szName, pList, List.Count(),
    nAllocated, pElements, nBlock, nElement);
  for (int i = 0; i < List.Count() && i < 2 && nElement > 0 && nElement <= 0x400; i++)
    Dump_Tree(szName, (const char *)pElements + i * nElement, nElement, nDepth);
}

void Dump_Matrix(const char *szName, const float *m)
{
  Trace_Write("    %s: [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]", szName,
    m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
}

void Dump_Camera(const CCamera &Cam)
{
  const Vec3 &p = Cam.GetPos();
  Ang3 a = Cam.GetAngles();
  Trace_Write("    CCamera: pos (%.2f %.2f %.2f) angles (%.2f %.2f %.2f) fov %.3f ratio %.3f zmin %.3f zmax %.1f surface %.0fx%.0f",
    p.x, p.y, p.z, a.x, a.y, a.z, Cam.GetFov(), Cam.GetProjRatio(), Cam.GetZMin(), Cam.GetZMax(), Cam.GetViewSurfaceX(), Cam.GetViewSurfaceZ());
  Matrix44 m = Cam.GetVCMatrixD3D9();
  Dump_Matrix("VCMatrixD3D9", (const float *)&m);
}

void Dump_Object(const CCObject *pObj)
{
  if (!pObj)
  {
    Trace_Write("    CCObject: (null)");
    return;
  }
  Trace_Write("    CCObject #%d @%p: flags 0x%x color (%.2f %.2f %.2f %.2f) amb (%.2f %.2f %.2f) templ %d sort %.2f dist %.2f",
    pObj->m_Id, pObj, pObj->m_ObjFlags, pObj->m_Color.r, pObj->m_Color.g, pObj->m_Color.b, pObj->m_Color.a,
    pObj->m_AmbColor.x, pObj->m_AmbColor.y, pObj->m_AmbColor.z, pObj->m_nTemplId, pObj->m_SortId, pObj->m_fDistanceToCam);
  Trace_Write("      RE %p EF %p custom %p LM %d/%d occl %d dynLM 0x%x heat %.2f shadowCasters %p charInst %p LMTC %p",
    pObj->m_RE, pObj->m_EF, pObj->m_CustomData, pObj->m_nLMId, pObj->m_nLMDirId, pObj->m_nOcclId, pObj->m_DynLMMask,
    pObj->m_fHeatFactor, pObj->m_pShadowCasters, pObj->m_pCharInstance, pObj->m_pLMTCBufferO);
  Dump_Matrix("matrix", (const float *)&pObj->m_Matrix);
  Dump_OpaqueList("CCObject::m_pShadowCasters", pObj->m_pShadowCasters);
  if (pObj->m_pShadowCasters)
    g_pTraceLastShadowCasters = pObj->m_pShadowCasters;
  Dump_Opaque("CCObject::m_pShadowFrustum", pObj->m_pShadowFrustum);
}

void Dump_Shader(IShader *pSH)
{
  if (!pSH)
  {
    Trace_Write("    IShader: (null)");
    return;
  }
  Trace_Write("    IShader @%p (vtbl %s) '%s' id %d", pSH, Trace_VTable(pSH), SafeStr(pSH->GetName()), pSH->GetID());
}

void Dump_ShaderItem(const SShaderItem &Item)
{
  Trace_Write("    SShaderItem: shader %p '%s' resources %p", Item.m_pShader, Item.m_pShader ? SafeStr(Item.m_pShader->GetName()) : "",
    Item.m_pShaderResources);
  if (Item.m_pShaderResources)
    Dump_RenderResources(Item.m_pShaderResources);
}

void Dump_TexPic(ITexPic *pTex)
{
  if (!pTex)
  {
    Trace_Write("    ITexPic: (null)");
    return;
  }
  Trace_Write("    ITexPic @%p (vtbl %s) '%s' id %d %dx%d (orig %dx%d) flags 0x%x flags2 0x%x loaded %d", pTex, Trace_VTable(pTex),
    SafeStr(pTex->GetName()), pTex->GetTextureID(), pTex->GetWidth(), pTex->GetHeight(), pTex->GetOriginalWidth(), pTex->GetOriginalHeight(),
    pTex->GetFlags(), pTex->GetFlags2(), pTex->IsTextureLoaded());
}

void Dump_MatInfo(const CMatInfo *pMI, int nIndex)
{
  IShader *pSH = pMI->shaderItem.m_pShader;
  Trace_Write("      mat[%d] '%s' script '%s' id %d cgfId %d flags 0x%x phys %d verts %d+%d inds %d+%d shader '%s' res %p RE %p alpha %.2f",
    nIndex, pMI->sMaterialName, pMI->sScriptMaterial, pMI->m_Id, pMI->m_nCGFMaterialID, pMI->m_Flags, pMI->nGamePhysMatId,
    pMI->nFirstVertId, pMI->nNumVerts, pMI->nFirstIndexId, pMI->nNumIndices, pSH ? SafeStr(pSH->GetName()) : "(null)",
    pMI->shaderItem.m_pShaderResources, pMI->pRE, pMI->fAlpha);
}

void Dump_RE(CRendElement *pRE)
{
  if (!pRE)
  {
    Trace_Write("    RE: (null)");
    return;
  }
  Trace_Write("    RE @%p %s(%d) flags 0x%x sort %d custom %p/%d fogScale %.2f color (%.2f %.2f %.2f %.2f)",
    pRE, Trace_RETypeName(pRE->m_Type), pRE->m_Type, pRE->m_Flags, pRE->m_SortId, pRE->m_CustomData, pRE->m_nCountCustomData,
    pRE->m_fFogScale, pRE->m_Color.r, pRE->m_Color.g, pRE->m_Color.b, pRE->m_Color.a);
  switch (pRE->m_Type)
  {
  case eDATA_OcLeaf:
    {
      CREOcLeaf *pOC = (CREOcLeaf *)pRE;
      Trace_Write("      OcLeaf: buffer %p chunk %p", pOC->m_pBuffer, pOC->m_pChunk);
      if (pOC->m_pChunk)
        Dump_MatInfo(pOC->m_pChunk, -1);
    }
    break;
  case eDATA_Sky:
    {
      CRESky *pSky = (CRESky *)pRE;
      Trace_Write("      Sky: waterLevel %.2f stretching %.2f alpha %.2f sphereList %d fogLayers %p %p",
        pSky->m_fTerrainWaterLevel, pSky->m_fSkyBoxStretching, pSky->m_fAlpha, pSky->m_nSphereListId, pSky->m_parrFogLayer, pSky->m_parrFogLayer2);
    }
    break;
  case eDATA_2DQuad:
    {
      CRE2DQuad *pQ = (CRE2DQuad *)pRE;
      for (int i = 0; i < 4; i++)
        Trace_Write("      2DQuad v%d (%.2f %.2f %.2f) tc (%.3f %.3f)", i, pQ->m_arrVerts[i].xyz.x, pQ->m_arrVerts[i].xyz.y, pQ->m_arrVerts[i].xyz.z,
          pQ->m_arrVerts[i].st[0], pQ->m_arrVerts[i].st[1]);
    }
    break;
  case eDATA_Glare:
    {
      CREGlare *pG = (CREGlare *)pRE;
      Trace_Write("      Glare: %dx%d amount %.2f", pG->m_GlareWidth, pG->m_GlareHeight, pG->m_fGlareAmount);
    }
    break;
  case eDATA_OcclusionQuery:
    {
      CREOcclusionQuery *pQ = (CREOcclusionQuery *)pRE;
      Trace_Write("      OcclusionQuery: box (%.2f %.2f %.2f)-(%.2f %.2f %.2f) checkFrame %d drawFrame %d visSamples %d",
        pQ->m_vBoxMin.x, pQ->m_vBoxMin.y, pQ->m_vBoxMin.z, pQ->m_vBoxMax.x, pQ->m_vBoxMax.y, pQ->m_vBoxMax.z,
        pQ->m_nCheckFrame, pQ->m_nDrawFrame, pQ->m_nVisSamples);
    }
    break;
  }
}

static void Dump_ShaderParams(const TArray<SShaderParam> &Params)
{
  for (int i = 0; i < Params.Num(); i++)
  {
    const SShaderParam &p = Params[i];
    switch (p.m_Type)
    {
    case eType_FLOAT:
      Trace_Write("      param '%s' float %.3f mat %d", p.m_Name, p.m_Value.m_Float, p.m_nMaterial);
      break;
    case eType_FCOLOR:
      Trace_Write("      param '%s' color (%.3f %.3f %.3f %.3f) mat %d", p.m_Name, p.m_Value.m_Color[0], p.m_Value.m_Color[1],
        p.m_Value.m_Color[2], p.m_Value.m_Color[3], p.m_nMaterial);
      break;
    case eType_VECTOR:
      Trace_Write("      param '%s' vector (%.3f %.3f %.3f) mat %d", p.m_Name, p.m_Value.m_Vector[0], p.m_Value.m_Vector[1],
        p.m_Value.m_Vector[2], p.m_nMaterial);
      break;
    case eType_STRING:
      Trace_Write("      param '%s' string '%s' mat %d", p.m_Name, SafeStr(p.m_Value.m_String), p.m_nMaterial);
      break;
    default:
      Trace_Write("      param '%s' type %d int %d mat %d", p.m_Name, p.m_Type, p.m_Value.m_Int, p.m_nMaterial);
      break;
    }
  }
}

static void Dump_ResTexture(int nSlot, const SEfResTexture &t)
{
  const SEfTexModificator &m = t.m_TexModificator;
  Trace_Write("      tex[%d] '%s' flags 0x%x amount %d tile %d/%d tiling (%.2f %.2f) texgen %d rot %d moveU %d moveV %d TU pic %p type %d",
    nSlot, t.m_Name.c_str(), t.m_TexFlags, t.m_Amount, t.m_bUTile, t.m_bVTile, m.m_Tiling[0], m.m_Tiling[1],
    m.m_eTGType, m.m_eRotType, m.m_eUMoveType, m.m_eVMoveType, t.m_TU.m_ITexPic, t.m_TU.m_eTexType);
}

void Dump_InputResources(const SInputShaderResources *pRes)
{
  if (!pRes)
  {
    Trace_Write("    SInputShaderResources: (null)");
    return;
  }
  Trace_Write("    SInputShaderResources: resFlags 0x%x opacity %.2f alphaRef %.2f lightMaterial %p texturePath '%s' params %d",
    pRes->m_ResFlags, pRes->m_Opacity, pRes->m_AlphaRef, pRes->m_LMaterial, pRes->m_TexturePath.c_str(), pRes->m_ShaderParams.Num());
  if (pRes->m_LMaterial)
    Trace_Write("      light material '%s'", pRes->m_LMaterial->name);
  Dump_ShaderParams(pRes->m_ShaderParams);
  for (int i = 0; i < EFTT_MAX; i++)
  {
    if (!pRes->m_Textures[i].m_Name.empty())
      Dump_ResTexture(i, pRes->m_Textures[i]);
  }
}

void Dump_RenderResources(const SRenderShaderResources *pRes)
{
  if (!pRes)
  {
    Trace_Write("    SRenderShaderResources: (null)");
    return;
  }
  Trace_Write("    SRenderShaderResources @%p id %d refs %d: resFlags 0x%x opacity %.2f alphaRef %.2f params %d",
    pRes, pRes->m_Id, pRes->m_nRefCounter, pRes->m_ResFlags, pRes->m_Opacity, pRes->m_AlphaRef, pRes->m_ShaderParams.Num());
  for (int i = 0; i < EFTT_MAX; i++)
  {
    if (pRes->m_Textures[i])
      Dump_ResTexture(i, *pRes->m_Textures[i]);
  }
}

void Dump_Light(const CDLight *pL)
{
  if (!pL)
  {
    Trace_Write("    CDLight: (null)");
    return;
  }
  char szFlags[256] = "";
  static const char *s_Flags[] =
  {
    "DETAIL", "DIRECTIONAL", "DYNAMIC", "ACTIVE", "CASTSHADOW_MAPS", "POINT", "PROJECT", "CASTSHADOW_VOLUME",
    "POSITIONCHANGED", "NOATTENUATION", "UPDATED", "INWORLDSPACE", "TEMP", "STATIC_ADDED", "HASAMBIENT", "HEATSOURCE",
    "LIGHTSOURCE", "FAKE", "SUN", "COPY", "LOCAL", "LM", "THIS_AREA_ONLY", "AMBIENT_LIGHT", "IGNORE_OWNER",
    "IGNORE_TERRAIN", "ONLY_FOR_HIGHSPEC", "SPECULAR_ONLY_FOR_HIGHSPEC", "LMDOT3", "FAKE_RADIOSITY", "LMOCCL", "0x80000000",
  };
  for (int i = 0; i < 32; i++)
  {
    if (pL->m_Flags & (1u << i))
    {
      strncat(szFlags, s_Flags[i], sizeof(szFlags) - strlen(szFlags) - 2);
      strncat(szFlags, " ", sizeof(szFlags) - strlen(szFlags) - 1);
    }
  }
  Trace_Write("    CDLight @%p id %d '%s' flags 0x%x [%s]", pL, pL->m_Id, pL->m_Name, pL->m_Flags, szFlags);
  Trace_Write("      origin (%.2f %.2f %.2f) radius %.2f color (%.2f %.2f %.2f %.2f) spec (%.2f %.2f %.2f %.2f) direct %.2f",
    pL->m_Origin.x, pL->m_Origin.y, pL->m_Origin.z, pL->m_fRadius, pL->m_Color.r, pL->m_Color.g, pL->m_Color.b, pL->m_Color.a,
    pL->m_SpecColor.r, pL->m_SpecColor.g, pL->m_SpecColor.b, pL->m_SpecColor.a, pL->m_fDirectFactor);
  Trace_Write("      projAngles (%.2f %.2f %.2f) frustum %.2f shader %p image %p owner %p style %d corona %.2f lifeTime %.2f creator '%.8s'",
    pL->m_ProjAngles.x, pL->m_ProjAngles.y, pL->m_ProjAngles.z, pL->m_fLightFrustumAngle, pL->m_pShader, pL->m_pLightImage,
    pL->m_pOwner, pL->m_nLightStyle, pL->m_fCoronaScale, pL->m_fLifeTime, pL->m_sDebugName);
  Dump_Opaque("CDLight::m_pShadowMapLightSource", pL->m_pShadowMapLightSource);
}

void Dump_Vertices(const char *szName, const void *pData, int nFormat, int nVerts, int nMax)
{
  if (!pData || nFormat <= 0 || nFormat >= VERTEX_FORMAT_NUMS)
  {
    Trace_Write("    %s: %d verts, format %d, data %p", szName, nVerts, nFormat, pData);
    return;
  }
  const int nStride = m_VertexSize[nFormat];
  const SBufInfoTable &Offs = gBufInfoTable[nFormat];
  Trace_Write("    %s: %d verts, format %s (%d bytes)", szName, nVerts, Trace_VertexFormatName(nFormat), nStride);
  for (int i = 0; i < nVerts && i < nMax; i++)
  {
    const byte *v = (const byte *)pData + i * nStride;
    char szLine[512];
    int n = 0;
    if (nFormat == VERTEX_FORMAT_TEX2F)
      n += _snprintf(szLine + n, sizeof(szLine) - n, "tc (%.3f %.3f)", ((const float *)v)[0], ((const float *)v)[1]);
    else
    {
      const float *pos = (const float *)v;
      n += _snprintf(szLine + n, sizeof(szLine) - n, "pos (%.2f %.2f %.2f)", pos[0], pos[1], pos[2]);
      if (Offs.OffsNormal)
      {
        const float *nm = (const float *)(v + Offs.OffsNormal);
        n += _snprintf(szLine + n, sizeof(szLine) - n, " n (%.2f %.2f %.2f)", nm[0], nm[1], nm[2]);
      }
      if (Offs.OffsColor)
        n += _snprintf(szLine + n, sizeof(szLine) - n, " col %08x", *(const DWORD *)(v + Offs.OffsColor));
      if (Offs.OffsSecColor)
        n += _snprintf(szLine + n, sizeof(szLine) - n, " col2 %08x", *(const DWORD *)(v + Offs.OffsSecColor));
      if (Offs.OffsTC)
      {
        const float *tc = (const float *)(v + Offs.OffsTC);
        n += _snprintf(szLine + n, sizeof(szLine) - n, " tc (%.3f %.3f)", tc[0], tc[1]);
      }
    }
    szLine[sizeof(szLine) - 1] = 0;
    Trace_Write("      v%d %s", i, szLine);
  }
}

void Dump_Indices(const char *szName, const ushort *pInds, int nInds, int nMax)
{
  char szLine[512];
  int n = _snprintf(szLine, sizeof(szLine), "    %s: %d indices @%p:", szName, nInds, pInds);
  for (int i = 0; pInds && i < nInds && i < nMax; i++)
    n += _snprintf(szLine + n, sizeof(szLine) - n, " %d", pInds[i]);
  szLine[sizeof(szLine) - 1] = 0;
  Trace_Write("%s", szLine);
}

void Dump_ColorVerts(const SColorVert *pVerts, int nVerts)
{
  for (int i = 0; pVerts && i < nVerts && i < 8; i++)
    Trace_Write("      v%d (%.2f %.2f %.2f) tc (%.3f %.3f) col %08x", i, pVerts[i].vert.x, pVerts[i].vert.y, pVerts[i].vert.z,
      pVerts[i].dTC[0], pVerts[i].dTC[1], pVerts[i].color.dcolor);
}

void Dump_ColorVerts2D(const SColorVert2D *pVerts, int nVerts)
{
  for (int i = 0; pVerts && i < nVerts && i < 8; i++)
    Trace_Write("      v%d (%.2f %.2f) tc (%.3f %.3f) col %08x", i, pVerts[i].vert[0], pVerts[i].vert[1],
      pVerts[i].dTC[0], pVerts[i].dTC[1], pVerts[i].color.dcolor);
}

void Dump_IndexedMesh(const CIndexedMesh *pMesh)
{
  if (!pMesh)
  {
    Trace_Write("    CIndexedMesh: (null)");
    return;
  }
  CIndexedMesh *m = (CIndexedMesh *)pMesh;
  Trace_Write("    CIndexedMesh @%p: faces %d verts %d coords %d norms %d colors %p/%p tangents %p vertMats %p box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)",
    m, m->m_nFaceCount, m->m_nVertCount, m->m_nCoorCount, m->m_nNormCount, m->m_pColor, m->m_pColorSec, m->m_pTangBasis, m->m_pVertMats,
    m->m_vBoxMin.x, m->m_vBoxMin.y, m->m_vBoxMin.z, m->m_vBoxMax.x, m->m_vBoxMax.y, m->m_vBoxMax.z);
  Trace_Write("      materials %d, geoms %d, helpers %d, light sources %d", m->m_lstMatTable.Count(), m->m_lstGeomNames.Count(),
    m->GetHelpers()->Count(), m->GetLightSourcesList()->Count());
  for (int i = 0; i < m->m_lstMatTable.Count() && i < 16; i++)
    Dump_MatInfo(&m->m_lstMatTable[i], i);
  for (int i = 0; i < m->m_lstGeomNames.Count() && i < 8; i++)
    Trace_Write("      geom[%d] '%s'", i, SafeStr(m->m_lstGeomNames[i]));
  for (int i = 0; i < m->m_nFaceCount && i < 3; i++)
  {
    const CObjFace &f = m->m_pFaces[i];
    Trace_Write("      face[%d] v (%d %d %d) t (%d %d %d) n (%d %d %d) shader %d flags 0x%x", i, f.v[0], f.v[1], f.v[2],
      f.t[0], f.t[1], f.t[2], f.n[0], f.n[1], f.n[2], f.shader_id, f.m_dwFlags);
  }
  for (int i = 0; i < m->m_nVertCount && i < 3; i++)
    Trace_Write("      vert[%d] (%.2f %.2f %.2f)", i, m->m_pVerts[i].x, m->m_pVerts[i].y, m->m_pVerts[i].z);
}

void Dump_LeafBuffer(CLeafBuffer *pLB)
{
  if (!pLB)
  {
    Trace_Write("    CLeafBuffer: (null)");
    return;
  }
  Trace_Write("    CLeafBuffer @%p '%s': format %s sysVerts %d sysBuf %p vidBuf %p container %p indices %d prim %d mats %d dynamic %d box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)",
    pLB, SafeStr(pLB->m_sSource), Trace_VertexFormatName(pLB->m_nVertexFormat), pLB->m_SecVertCount, pLB->m_pSecVertBuffer,
    pLB->m_pVertexBuffer, pLB->m_pVertexContainer, pLB->m_SecIndices.Num(), pLB->m_nPrimetiveType, pLB->m_pMats ? pLB->m_pMats->Count() : -1,
    pLB->m_bDynamic, pLB->m_vBoxMin.x, pLB->m_vBoxMin.y, pLB->m_vBoxMin.z, pLB->m_vBoxMax.x, pLB->m_vBoxMax.y, pLB->m_vBoxMax.z);
  if (pLB->m_pMats)
  {
    for (int i = 0; i < pLB->m_pMats->Count() && i < 16; i++)
      Dump_MatInfo(&(*pLB->m_pMats)[i], i);
  }
}

void Dump_VertexBuffer(const char *szName, const CVertexBuffer *pVB, int nMax)
{
  if (!pVB)
  {
    Trace_Write("    %s: (null)", szName);
    return;
  }
  Trace_Write("    %s @%p: format %s verts %d dynamic %d fence %u/%d", szName, pVB, Trace_VertexFormatName(pVB->m_vertexformat),
    pVB->m_NumVerts, pVB->m_bDynamic, pVB->m_fence, pVB->m_bFenceSet);
  for (int i = 0; i < VSF_NUM; i++)
  {
    const SVertexStream &vs = pVB->m_VS[i];
    if (vs.m_VData || vs.m_VertBuf.m_pPtr)
      Trace_Write("      stream %d: data %p hw %p items %d locked %d dynamic %d offs %d pool %p", i, vs.m_VData, vs.m_VertBuf.m_pPtr,
        vs.m_nItems, vs.m_bLocked, vs.m_bDynamic, vs.m_nBufOffset, vs.m_pPool);
  }
  // Only system memory copies can be read; a locked D3D buffer pointer is valid until unlock.
  if (pVB->m_VS[VSF_GENERAL].m_VData && Trace_IsReadable(pVB->m_VS[VSF_GENERAL].m_VData, 1))
    Dump_Vertices("verts", pVB->m_VS[VSF_GENERAL].m_VData, pVB->m_vertexformat, pVB->m_NumVerts, nMax);
}

void Dump_RendParams(const SRendParams &rp)
{
  Trace_Write("    SRendParams: pos " TRACE_V3 " scale %g angles " TRACE_V3 " matrix %p sortOffset %g templ %d lights 0x%x strongest 0x%x fog %d bending %g",
    TRACE_V3_ARGS(rp.vPos), rp.fScale, TRACE_V3_ARGS(rp.vAngles), rp.pMatrix, rp.fCustomSortOffset, rp.nShaderTemplate, rp.nDLightMask,
    rp.nStrongestDLightMask, rp.nFogVolumeID, rp.fBending);
  Trace_Write("      state %p shadowCasters %p color " TRACE_V3 " alpha %g sort %d ambient " TRACE_V3 " dist %g objFlags 0x%x sqDist %g",
    rp.pStateShader, rp.pShadowMapCasters, TRACE_V3_ARGS(rp.vColor), rp.fAlpha, rp.nSortValue, TRACE_V3_ARGS(rp.vAmbientColor),
    rp.fDistance, rp.dwFObjFlags, rp.fSQDistance);
  Trace_Write("      shadowVolumeLight %p caller %p heat %g shadowExtent %g LM %p LMTC %p occlLights %d %d %d %d material %p params %p custom %p",
    rp.pShadowVolumeLightSource, rp.pCaller, rp.fHeatAmount, rp.fShadowVolumeExtent, rp.pLightMapInfo, rp.pLMTCBuffer,
    rp.arrOcclusionLightIds[0], rp.arrOcclusionLightIds[1], rp.arrOcclusionLightIds[2], rp.arrOcclusionLightIds[3], rp.pMaterial,
    rp.pShaderParams, rp.pCCObjCustomData);
  if (rp.pMatrix)
    Dump_Matrix("matrix", (const float *)rp.pMatrix);
  if (rp.pLightMapInfo)
    Trace_Write("      RenderLMData @%p: refs %d colorLerp %d hdrColorLerp %d domDirection %d occl %d", rp.pLightMapInfo,
      rp.pLightMapInfo->NumRefs(), rp.pLightMapInfo->GetColorLerpTex(), rp.pLightMapInfo->GetHDRColorLerpTex(),
      rp.pLightMapInfo->GetDomDirectionTex(), rp.pLightMapInfo->GetOcclTex());
  if (rp.pLMTCBuffer)
  {
    Dump_LeafBuffer(rp.pLMTCBuffer);
    Dump_VertexBuffer("LMTC sysBuf", rp.pLMTCBuffer->m_pSecVertBuffer);
    Dump_VertexBuffer("LMTC vidBuf", rp.pLMTCBuffer->m_pVertexBuffer);
  }
  Dump_OpaqueList("SRendParams::pShadowMapCasters", rp.pShadowMapCasters);
}

void Dump_VertexBufferSource(const VertexBufferSource *p)
{
  if (!p)
    return;
  Trace_Write("    VertexBufferSource: materials %u (entities %p shaders %p mats %p) primGroups %u indices %u verts %u (pos %p uv %p) REflags 0x%x",
    p->numMaterials, p->pMaterials, p->pShaders, p->pMats, p->numPrimGroups, p->numIndices, p->numVertices, p->pVertices, p->pUVs, p->nREFlags);
}
