// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: StdAfx.h
//	Description: Precompiled header of the Direct3D 12 renderer.
//
//////////////////////////////////////////////////////////////////////

#pragma once

// Same STL configuration as the engine modules (see CryGame/StdAfx.h).
#define _NOTHREADS
#define _STLP_NO_THREADS

// platform.h includes CryMemoryManager.h: like the engine modules, the renderer allocates
// everything (new, malloc, TArray, list2) from CrySystem's heap. That heap only aligns to 4 bytes
// on x64: no __declspec(align) or SSE types in objects on it.
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
#include <LeafBuffer.h>
#include <RendElement.h>
#include <CREOcLeaf.h>

#include <XRenderCommon.h>

// windows.h (through platform.h) defines min and max as macros; our code uses std::min/std::max.
#undef min
#undef max

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <memory>
#include <vector>
#include <string>
#include <span>
#include <algorithm>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

// Engine interfaces handed to PackageRenderConstructor (XRenderD3D12.cpp).
extern ISystem *iSystem;
extern ILog *iLog;
extern IConsole *iConsole;
extern ITimer *iTimer;
