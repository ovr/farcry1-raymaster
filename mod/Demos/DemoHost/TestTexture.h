//////////////////////////////////////////////////////////////////////
//
//	File: TestTexture.h
//	Description: A test picture made by the demos themselves.
//
//	Square, with four quadrants of different colours (top left red, top
//	right green, bottom left blue, bottom right yellow), a black grid of
//	8 x 8 cells and the top left cell black: every turn and mirror of the
//	picture, and a swap of the red and blue channels, look different, so
//	a frame shows how the renderer placed it.
//
//////////////////////////////////////////////////////////////////////

#pragma once

//! The pixels of the picture, nSize x nSize, 4 bytes per pixel in the order B, G, R, A (alpha 255),
//! rows from the top.
std::vector<unsigned char> TestTexture_Pixels(int nSize);
//! Makes the picture a texture of the renderer (DownLoadToVideoMemory with eTF_8888, repeated, no mip maps,
//! bilinear filtering); the texture ID, or -1.
int TestTexture_Create(IRenderer *pRenderer, int nSize);
