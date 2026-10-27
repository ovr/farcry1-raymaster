//////////////////////////////////////////////////////////////////////
//
//	File: VCallHooks.h
//	Description: Tracing of the virtual functions of the renderer objects the engine holds.
//
//	Besides IRenderer and CLeafBuffer, the engine calls virtual functions of
//	the other objects the renderer gives it (IShader, ITexPic, the CRE*
//	render elements) directly. The hooks replace the entries of their vtables
//	in the original renderer with stubs that count every call by the module it
//	comes from, write out the first calls from the engine with their
//	arguments and results, and call the original function
//	(VCallThunk_x64.asm). The slots and their names come from
//	mod/XRenderCommon/VTables.inl. The calls of CREScreenProcess::mfActivate,
//	mfSetParameter and mfGetParameter (the screen effects) are written out (the
//	first ones of each parameter; all with XRENDERTRACED_SCREENFX_ALL=1).
//	x64 only; XRENDERTRACED_NO_VCALL_HOOKS=1 turns them off.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! The module of the original renderer: calls from it are the renderer's own.
void VCallHooks_SetRenderer(HMODULE hRenderer);
//! Hooks the vtable of pObject, an object of the original renderer of the SDK class szClass (one
//! of VTables.inl), once per vtable. szTag tells apart vtables hooked as the same class.
void VCallHooks_Install(const char *szClass, void *pObject, const char *szTag = NULL);
//! Hooks the vtable of a render element by its type.
void VCallHooks_InstallRE(CRendElement *pRE);
//! The render element whose CRendElement::mfDraw is running, NULL outside of one (x64).
CRendElement *VCallHooks_DrawingRE();
//! Writes which virtual functions were called, and from which modules.
void VCallHooks_WriteSummary(const char *szReason);
//! Restores the original vtable entries.
void VCallHooks_Uninstall();
