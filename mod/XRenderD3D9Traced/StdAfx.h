// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: StdAfx.h
//	Description: Precompiled header of the tracing renderer proxy.
//
//////////////////////////////////////////////////////////////////////

#pragma once

// Same STL configuration as the engine modules (see CryGame/StdAfx.h).
#define _NOTHREADS
#define _STLP_NO_THREADS

// platform.h includes CryMemoryManager.h: like the retail engine modules, the proxy allocates
// everything (new, malloc, TArray, list2) from CrySystem's heap.
#include <platform.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#ifndef SAFE_DELETE
#define SAFE_DELETE(p)			{ if(p) { delete (p);		(p)=NULL; } }
#endif
#ifndef SAFE_DELETE_ARRAY
#define SAFE_DELETE_ARRAY(p)	{ if(p) { delete[] (p);		(p)=NULL; } }
#endif
#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)			{ if(p) { (p)->Release();	(p)=NULL; } }
#endif

#include <Cry_Math.h>
#include <Cry_Camera.h>
#include <ISystem.h>
#include <IConsole.h>
#include <ILog.h>
#include <ITimer.h>
#include <ICryPak.h>
#include <IStreamEngine.h>
#include <IRenderer.h>
#include <I3DEngine.h>
#include <MeshIdx.h>
#include <VertexBufferSource.h>

#include <XRenderCommon.h>

// Engine interfaces handed to PackageRenderConstructor (XRenderD3D9Traced.cpp).
extern ISystem *iSystem;
extern ILog *iLog;
extern IConsole *iConsole;
extern ITimer *iTimer;
