//////////////////////////////////////////////////////////////////////
//
//	File: XRenderCommon.h
//	Description: Renderer interface knowledge beyond the SDK headers.
//
//	Interface facts a compatible renderer needs beyond the SDK headers:
//	the retail Far Cry 1.4 contract (RetailRenderer.h), the runtime
//	behaviour recorded by XRenderD3D9Traced (Bin64Behaviour.h) and the
//	structures shared by the engine and the renderer (RendererStructures.h,
//	StructLayouts.txt) and the vtable slots of the renderer classes the
//	engine calls (VTables.inl).
//	Include after IRenderer.h.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "RetailRenderer.h"
#include "Bin64Behaviour.h"
#include "RendererStructures.h"
