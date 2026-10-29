// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Tga.h
//	Description: Uncompressed TGA files of the saved frames.
//
//	Header only: each program compiles it with its own allocator (the
//	demos with CrySystem's heap, DemoCompare with the CRT's).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <stdio.h>
#include <vector>

//! Writes B, G, R, A pixels (nPitch bytes per row, rows from the top) as a 32-bit TGA file.
inline bool Tga_Write(const char *szFile, int nWidth, int nHeight, const unsigned char *pPixels, int nPitch)
{
  FILE *pFile = fopen(szFile, "wb");
  if (!pFile)
    return false;
  // Uncompressed true colour, 32 bits, 8 alpha bits, rows from the top.
  const unsigned char Header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (unsigned char)nWidth, (unsigned char)(nWidth >> 8),
    (unsigned char)nHeight, (unsigned char)(nHeight >> 8), 32, 0x28 };
  bool bOk = fwrite(Header, 1, sizeof(Header), pFile) == sizeof(Header);
  for (int y = 0; y < nHeight && bOk; y++)
    bOk = fwrite(pPixels + y * nPitch, 4, nWidth, pFile) == (size_t)nWidth;
  fclose(pFile);
  return bOk;
}

//! Reads an uncompressed 24- or 32-bit TGA file into B, G, R, A pixels, rows from the top
//! (alpha 255 for 24-bit files).
inline bool Tga_Read(const char *szFile, int &nWidth, int &nHeight, std::vector<unsigned char> &Pixels)
{
  FILE *pFile = fopen(szFile, "rb");
  if (!pFile)
    return false;
  unsigned char Header[18];
  bool bOk = fread(Header, 1, sizeof(Header), pFile) == sizeof(Header) && Header[1] == 0 && Header[2] == 2 &&
    (Header[16] == 24 || Header[16] == 32);
  if (bOk)
  {
    nWidth = Header[12] | Header[13] << 8;
    nHeight = Header[14] | Header[15] << 8;
    int nBytes = Header[16] / 8;
    bool bTopDown = (Header[17] & 0x20) != 0;
    fseek(pFile, Header[0], SEEK_CUR);
    std::vector<unsigned char> Row(nWidth * nBytes);
    Pixels.resize(nWidth * nHeight * 4);
    for (int y = 0; y < nHeight && bOk; y++)
    {
      bOk = fread(Row.data(), nBytes, nWidth, pFile) == (size_t)nWidth;
      unsigned char *pDst = &Pixels[(bTopDown ? y : nHeight - 1 - y) * nWidth * 4];
      for (int x = 0; x < nWidth; x++)
      {
        pDst[x * 4 + 0] = Row[x * nBytes + 0];
        pDst[x * 4 + 1] = Row[x * nBytes + 1];
        pDst[x * 4 + 2] = Row[x * nBytes + 2];
        pDst[x * 4 + 3] = nBytes == 4 ? Row[x * nBytes + 3] : 255;
      }
    }
  }
  fclose(pFile);
  return bOk;
}
