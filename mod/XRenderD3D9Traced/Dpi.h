// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: Dpi.h
//	Description: DPI awareness of the game's process.
//
//	Windows stretches the window of a program that does not declare
//	itself DPI aware when the display scale is above 100 %: the picture
//	gets blurred and the window's sizes are scaled. The renderer makes
//	the game's window in IRenderer::Init, so the proxy declares the
//	process per monitor DPI aware (v2) before that, and the window is made
//	in physical pixels, one to one with the back buffer.
//	XRENDERTRACED_DPI=0 leaves the process as the game starts it.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! Declares the process per monitor DPI aware, unless XRENDERTRACED_DPI=0; writes what it did to
//! the log. Before the original renderer is created.
void Dpi_MakeAware();
//! "DPI 144 (150 %), per monitor aware" for the window, as the log writes it.
void Dpi_Describe(HWND hWnd, char *szText, size_t nSize);
