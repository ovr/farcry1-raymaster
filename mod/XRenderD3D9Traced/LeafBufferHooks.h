// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: LeafBufferHooks.h
//	Description: Tracing of the CLeafBuffer methods of the original renderer.
//
//	The 3D engine submits its geometry through CLeafBuffer methods
//	(AddRenderElements, Render, ...) that run inside the original renderer,
//	not through IRenderer. The hooks replace the entries of the CLeafBuffer
//	vtable of the original renderer with tracing functions that forward to
//	the original ones, so every leaf buffer is traced, including the ones the
//	renderer uses internally. XRENDERTRACED_NO_LB_HOOKS=1 turns them off.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! Hooks the vtable of pLB's class (once; later calls only check it is the same class).
void LeafBufferHooks_Install(CLeafBuffer *pLB);
//! Restores the original vtable entries.
void LeafBufferHooks_Uninstall();
