//////////////////////////////////////////////////////////////////////
//
//	File: D3DState.h
//	Description: Direct3D 9 state of the original renderer after a call.
//
//	The original renderer hands out its device with
//	EF_Query(EFQ_D3DDevice). Reading the render, texture stage and sampler
//	states from it after a call shows what the call set up for drawing
//	(e.g. the blend factors behind FontSetBlending). Only Get* methods of
//	the device are called, so the renderer's state is not changed.
//	The methods are chosen with XRENDERTRACED_D3D_STATE (names as in the
//	log, separated by ','); only methods that use TRACE_D3D_STATE can
//	write it.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! Writes the device state after the traced call (lines starting with "    d3d").
void D3DState_Write(IRenderer *pRenderer);

//! After the call of the original renderer, in a method that started with TRACE_CALL.
#define TRACE_D3D_STATE(pRenderer) \
  do { if (bTraced && s_TraceSite.m_bD3DState) D3DState_Write(pRenderer); } while (0)
