// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: TestTexture.cpp
//	Description: A test picture made by the demos themselves.
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include "TestTexture.h"

std::vector<unsigned char> TestTexture_Pixels(int nSize)
{
  // B, G, R of the quadrants: top left, top right, bottom left, bottom right.
  static const unsigned char Quadrants[4][3] = { { 0, 0, 255 }, { 0, 255, 0 }, { 255, 0, 0 }, { 0, 255, 255 } };
  const int nCell = nSize / 8;
  const int nLine = nSize >= 128 ? 2 : 1;
  std::vector<unsigned char> Pixels(nSize * nSize * 4);
  for (int y = 0; y < nSize; y++)
  {
    for (int x = 0; x < nSize; x++)
    {
      unsigned char *p = &Pixels[(y * nSize + x) * 4];
      const unsigned char *pColor = Quadrants[(y >= nSize / 2) * 2 + (x >= nSize / 2)];
      bool bBlack = x % nCell < nLine || y % nCell < nLine || x >= nSize - nLine || y >= nSize - nLine || (x < nCell && y < nCell);
      p[0] = bBlack ? 0 : pColor[0];
      p[1] = bBlack ? 0 : pColor[1];
      p[2] = bBlack ? 0 : pColor[2];
      p[3] = 255;
    }
  }
  return Pixels;
}

int TestTexture_Create(IRenderer *pRenderer, int nSize)
{
  std::vector<unsigned char> Pixels = TestTexture_Pixels(nSize);
  char szName[] = "$DemoTestTexture";
  unsigned int nId = pRenderer->DownLoadToVideoMemory(Pixels.data(), nSize, nSize, eTF_8888, eTF_8888, 0, true, FILTER_BILINEAR, 0,
    szName, FT_NOMIPS);
  return nId ? (int)nId : -1;
}
