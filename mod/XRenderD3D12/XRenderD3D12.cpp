// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: XRenderD3D12.cpp
//	Description: DLL entry point of the Direct3D 12 renderer.
//
//	CrySystem (or the tracing proxy in its place) calls PackageRenderConstructor
//	as (0, NULL, &sp) and then IRenderer::SetType; see RetailRenderer.h, "Loading".
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"

// Routes new/malloc of this DLL to CrySystem's heap (CryMemoryManager.h), like every engine DLL.
// Initialized before the other globals of the DLL, which may allocate.
#pragma warning(disable: 4073) // initializers put in library initialization area
#pragma init_seg(lib)
_ACCESS_POOL;

ISystem *iSystem = NULL;
ILog *iLog = NULL;
IConsole *iConsole = NULL;
ITimer *iTimer = NULL;

extern "C" __declspec(dllexport) IRenderer *PackageRenderConstructor(int argc, char *argv[], SCryRenderInterface *sp)
{
  // igcpMalloc is not initialized by CrySystem (see RetailRenderer.h).
  iLog = sp->ipLog;
  iConsole = sp->ipConsole;
  iTimer = sp->ipTimer;
  iSystem = sp->ipSystem;

  if (g_pRenderer)
    return g_pRenderer;
  CD3D12Renderer *pRenderer = new CD3D12Renderer;
  pRenderer->RegisterVariables();
  return pRenderer;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD dwReason, LPVOID lpReserved)
{
  if (dwReason == DLL_PROCESS_ATTACH)
    DisableThreadLibraryCalls((HMODULE)hModule);
  return TRUE;
}
