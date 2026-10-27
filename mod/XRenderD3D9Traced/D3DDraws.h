//////////////////////////////////////////////////////////////////////
//
//	File: D3DDraws.h
//	Description: The Direct3D 9 draw calls of the original renderer in the
//	captured frame.
//
//	The renderer draws everything it collected inside EF_EndEf3D (and the
//	2D calls), out of sight of the IRenderer calls. With
//	XRENDERTRACED_D3D_DRAWS=1 the device of the original renderer
//	(EF_Query(EFQ_D3DDevice)) gets a copy of its vtable for the captured
//	frame in which Clear writes its arguments and DrawPrimitive,
//	DrawIndexedPrimitive and their UP variants write a line ("[d3d] ...")
//	before they draw: the primitives, the
//	vertex and index buffers, the vertex declaration or FVF, the shaders,
//	the textures and the render element whose mfDraw is running (x64, from
//	VCallHooks), for a leaf buffer element also its source and material.
//	The first vertices of a non-indexed draw (8, or XRENDERTRACED_D3D_VERTS)
//	follow the line: from memory
//	for the *UP calls, and from the dynamic vertex buffers, whose Lock and
//	Unlock are hooked the same way from the first draw that uses them (what
//	is written into them is kept until the end of the frame).
//	XRENDERTRACED_D3D_DRAWS=2 adds the full device state (D3DState.h) after
//	every line, XRENDERTRACED_D3D_DRAWS=3 also the constants of the bound
//	vertex and pixel shaders. The vtable of d3d9.dll itself is not changed,
//	and the device gets its own vtable back at the end of the frame.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! At the start of the captured frame.
void D3DDraws_Begin(IRenderer *pRenderer);
//! XRENDERTRACED_D3D_RT=1: from the first call on, the first SetRenderTarget of every frame to a
//! surface other than the back buffer writes a line "[d3d rt] frame N: ..." (64 at most, counted
//! again after each level load), to find the frames that render into textures (shadow maps).
//! Called at every BeginFrame.
void D3DDraws_WatchRenderTargets(IRenderer *pRenderer, int nFrame);
//! After a level load: the 64 lines of D3DDraws_WatchRenderTargets start again.
void D3DDraws_RestartRenderTargets();
//! At the end of the frame (after Update).
void D3DDraws_End();
//! When the renderer calls the mfDraw of a render element (VCallHooks): a line "[d3d] mfDraw"
//! in the captured frame, also for elements that end up drawing nothing.
void D3DDraws_OnDrawRE();
