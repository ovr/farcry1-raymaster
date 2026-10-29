// SPDX-License-Identifier: Unlicense
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RendererAbiChecks.h
//	Description: Compile-time checks that the SDK renderer classes have
//	the layout of the retail Far Cry 1.4 renderer (sizes from
//	RetailRenderer.h). Include in one source file of a renderer.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "RetailRenderer.h"
// The only render element header RendElement.h does not include.
#include <CREPolyMesh.h>

#if defined(WIN32) && !defined(WIN64)

static_assert(sizeof(CLeafBuffer) == RetailRenderer::SIZEOF_CLeafBuffer, "CLeafBuffer does not match the retail layout");
static_assert(sizeof(CRendElement) == RetailRenderer::SIZEOF_CRendElement, "CRendElement does not match the retail layout");

// Same values as RetailRenderer::RESize() (not constexpr in C++14).
static_assert(sizeof(CREDummy) == 0x58, "CREDummy does not match the retail layout");
static_assert(sizeof(CRESky) == 0x270, "CRESky does not match the retail layout");
static_assert(sizeof(CREPolyMesh) == 0x88, "CREPolyMesh does not match the retail layout");
static_assert(sizeof(CRETriMeshShadow) == 0x3E8, "CRETriMeshShadow does not match the retail layout");
static_assert(sizeof(CREOcLeaf) == 0xB0, "CREOcLeaf does not match the retail layout");
static_assert(sizeof(CRECommon) == 0x58, "CRECommon does not match the retail layout");
static_assert(sizeof(CRE2DQuad) == 0xA8, "CRE2DQuad does not match the retail layout");
static_assert(sizeof(CREGlare) == 0x64, "CREGlare does not match the retail layout");
static_assert(sizeof(CREOcclusionQuery) == 0x80, "CREOcclusionQuery does not match the retail layout");
static_assert(sizeof(CREFlashBang) == 0x74, "CREFlashBang does not match the retail layout");
static_assert(sizeof(CREScreenProcess) == 0x5C, "CREScreenProcess does not match the retail layout");

#endif // WIN32 && !WIN64
