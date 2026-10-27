//////////////////////////////////////////////////////////////////////
//
//	File: XRenderCommon.h
//	Description: Renderer interface knowledge beyond the SDK headers.
//
//	Interface facts a compatible renderer needs beyond the SDK headers:
//	the retail Far Cry 1.4 contract (RetailRenderer.h), the structures
//	shared by the engine and the renderer (RendererStructures.h,
//	StructLayouts.txt) and the vtable slots of the renderer classes the
//	engine calls and of the engine interfaces the renderer calls (VTables.inl).
//	Include after IRenderer.h.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "RetailRenderer.h"
#include "RendererStructures.h"
