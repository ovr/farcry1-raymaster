//////////////////////////////////////////////////////////////////////
//
//	File: XRenderD3D9Traced.cpp
//	Description: DLL entry point of the tracing renderer proxy.
//
//	PackageRenderConstructor loads the original renderer (XRenderD3D9.dll
//	next to this DLL, or the DLL named by the XRENDERTRACED_DLL environment
//	variable), creates it with the same arguments and hands the engine a
//	CRendererProxy around it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RenderTrace.h"
#include "RendererProxy.h"
#include "VCallHooks.h"
#include <RendererAbiChecks.h>
#include <intrin.h>

// Routes new/malloc of this DLL to CrySystem's heap (CryMemoryManager.h), like every engine DLL.
// Initialized before the other globals of the DLL, which may allocate.
#pragma warning(disable: 4073) // initializers put in library initialization area
#pragma init_seg(lib)
_ACCESS_POOL;

ISystem *iSystem = NULL;
ILog *iLog = NULL;
IConsole *iConsole = NULL;
ITimer *iTimer = NULL;

static const char szDefaultRealDll[] = "XRenderD3D9.dll";
static const char szRealDllEnv[] = "XRENDERTRACED_DLL";
static const char szTraceFile[] = "XRenderD3D9Traced.log";

static HMODULE GetThisModule()
{
  HMODULE hMod = NULL;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&GetThisModule, &hMod);
  return hMod;
}

//! Full path of the original renderer: a bare file name is looked up in the directory of this DLL.
static void GetRealDllPath(char *szPath, size_t nSize)
{
  char szName[MAX_PATH];
  DWORD nLen = GetEnvironmentVariableA(szRealDllEnv, szName, sizeof(szName));
  if (!nLen || nLen >= sizeof(szName))
    strcpy(szName, szDefaultRealDll);
  if (strchr(szName, '\\') || strchr(szName, '/'))
  {
    _snprintf(szPath, nSize, "%s", szName);
  }
  else
  {
    char szDir[MAX_PATH];
    GetModuleFileNameA(GetThisModule(), szDir, sizeof(szDir));
    char *szSlash = strrchr(szDir, '\\');
    if (szSlash)
      szSlash[1] = 0;
    else
      szDir[0] = 0;
    _snprintf(szPath, nSize, "%s%s", szDir, szName);
  }
  szPath[nSize - 1] = 0;
}

//! R_*_RENDERER type of a retail renderer DLL, by its file name; -1 if it is not one of them.
static int GetRendererType(const char *szPath)
{
  const char *szName = strrchr(szPath, '\\');
  szName = szName ? szName + 1 : szPath;
  for (int i = 0; i < (int)(sizeof(RetailRenderer::Drivers) / sizeof(RetailRenderer::Drivers[0])); i++)
  {
    if (!stricmp(szName, RetailRenderer::Drivers[i].szDll))
      return RetailRenderer::Drivers[i].nType;
  }
  return -1;
}

extern "C" __declspec(dllexport) IRenderer *PackageRenderConstructor(int argc, char *argv[], SCryRenderInterface *sp)
{
  // igcpMalloc is not initialized by CrySystem (see RetailRenderer.h).
  iLog = sp->ipLog;
  iConsole = sp->ipConsole;
  iTimer = sp->ipTimer;
  iSystem = sp->ipSystem;

  Trace_Open(szTraceFile);
  Trace_Write("PackageRenderConstructor(argc %d, argv %p, sp %p): log %p console %p timer %p system %p test_int %p physics %p",
    argc, argv, sp, sp->ipLog, sp->ipConsole, sp->ipTimer, sp->ipSystem, sp->ipTest_int, sp->pIPhysicalWorld);
  Trace_Write("  called from %s", Trace_Addr(_ReturnAddress()));

  char szPath[MAX_PATH];
  GetRealDllPath(szPath, sizeof(szPath));
  // The DLL is never unloaded: the engine keeps objects of the original renderer (shaders,
  // textures, leaf buffers) until the process exits.
  HMODULE hReal = LoadLibraryA(szPath);
  if (!hReal || hReal == GetThisModule())
  {
    Trace_Write("Can't load the original renderer '%s' (error %u)", szPath, GetLastError());
    if (iLog)
      iLog->LogError("XRenderD3D9Traced: can't load the original renderer '%s'", szPath);
    return NULL;
  }
  RetailRenderer::PFN_PackageRenderConstructor pfnCreate =
    (RetailRenderer::PFN_PackageRenderConstructor)GetProcAddress(hReal, RetailRenderer::szPackageRenderConstructor);
  if (!pfnCreate)
  {
    Trace_Write("'%s' has no %s export", szPath, RetailRenderer::szPackageRenderConstructor);
    return NULL;
  }
  Trace_Write("Original renderer '%s' loaded at %p", szPath, hReal);
  VCallHooks_SetRenderer(hReal);
  // The console variables and commands the original renderer registers (it does so from here on),
  // and the variables each module looks up.
  VCallHooks_InstallEngine("IConsole", sp->ipConsole);
  // The engine's services the original renderer uses.
  VCallHooks_InstallEngine("ISystem", sp->ipSystem);
  VCallHooks_InstallEngine("ILog", sp->ipLog);
  VCallHooks_InstallEngine("ITimer", sp->ipTimer);
  VCallHooks_InstallEngine("ICryPak", sp->ipSystem ? sp->ipSystem->GetIPak() : NULL);
  VCallHooks_InstallEngine("IStreamEngine", sp->ipSystem ? sp->ipSystem->GetStreamEngine() : NULL);
  VCallHooks_InstallEngine("IPhysicalWorld", sp->pIPhysicalWorld);

  IRenderer *pReal = pfnCreate(argc, argv, sp);
  Trace_Write("  -> real IRenderer %p (vtbl %s)", pReal, Trace_VTable(pReal));
  if (!pReal)
    return NULL;
  if (iLog)
    iLog->Log("XRenderD3D9Traced: tracing '%s' to %s", szPath, szTraceFile);
  return new CRendererProxy(pReal, GetRendererType(szPath));
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD dwReason, LPVOID lpReserved)
{
  if (dwReason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls((HMODULE)hModule);
  else if (dwReason == DLL_PROCESS_DETACH)
    Trace_Close();
  return TRUE;
}
