// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: Capture.h
//	Description: Reads and saves the back buffer of the current frame.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! Reads the frame drawn so far into B, G, R, A pixels (alpha 255), rows from the top; call before
//! ISystem::RenderEnd.
bool Capture_ReadFrame(IRenderer *pRenderer, int &nWidth, int &nHeight, std::vector<unsigned char> &Pixels);
//! Saves the frame drawn so far as a 32-bit TGA file (alpha 255); call before ISystem::RenderEnd.
bool Capture_SaveFrame(IRenderer *pRenderer, const char *szFile);
