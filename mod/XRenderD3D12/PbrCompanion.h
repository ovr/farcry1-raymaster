// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: PbrCompanion.h
//	Description: The RayMaster maps beside a texture of the game
//	(Texture.cpp's FindPbrCompanions; the unit tests), without the
//	renderer's types. The game's folder RayMaster holds, by the paks'
//	paths, maps made from the game's own textures (AssetViewer's
//	--pbr-batch --pbr-orig): <texture>_pbr_m.dds, BC3, R roughness, G
//	metal, B ambient occlusion, A height (0 a hollow, 1 a bump, 0.5 flat:
//	the reference plane, no height), and <texture>_pbr_n.dds, BC1, the
//	normals (green along growing v, as the game's _ddn maps), only where
//	the material has no normal map of its own; both of the texture's
//	size, with their mips. r_Parallax takes the height (parallax
//	occlusion mapping), r_PbrNormals the normals.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PBRCOMPANION_H
#define XRENDERD3D12_PBRCOMPANION_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace PbrCompanion
{
  constexpr std::string_view FOLDER = "raymaster/";  //!< the game's folder RayMaster (a loose folder: CryPak's FOpen finds it)
  constexpr int FLAT_HEIGHT = 128;                   //!< the reference plane's height (of 255): no offset
  constexpr int FLAT_TOLERANCE = 1;                  //!< a height map all within this of FLAT_HEIGHT is flat: none to draw

  //! The file of a texture's map: szSuffix "_pbr_m" (the material map) or "_pbr_n" (the normals) after the base of the file
  //! the texture was read from (lower case, '/' separated, without the extension: Texture.cpp's SSearch::Base).
  inline std::string Path(std::string_view Base, std::string_view Suffix)
  {
    std::string s;
    s.reserve(FOLDER.size() + Base.size() + Suffix.size() + 4);
    s.append(FOLDER).append(Base).append(Suffix).append(".dds");
    return s;
  }

  //! The 16 alphas of a BC3 block's alpha half (8 bytes: two end points, 16 indices of 3 bits): a0 > a1 the 8 value mode
  //! (6 between), else the 6 value one (4 between, indices 6 and 7 0 and 255).
  inline void DecodeAlphaBlock(const uint8_t *pBlock, uint8_t vAlpha[16])
  {
    const int a0 = pBlock[0], a1 = pBlock[1];
    int vPalette[8] = { a0, a1 };
    if (a0 > a1)
      for (int i = 1; i <= 6; i++)
        vPalette[1 + i] = ((7 - i) * a0 + i * a1) / 7;
    else
    {
      for (int i = 1; i <= 4; i++)
        vPalette[1 + i] = ((5 - i) * a0 + i * a1) / 5;
      vPalette[6] = 0;
      vPalette[7] = 255;
    }
    uint64_t nBits = 0;
    for (int i = 0; i < 6; i++)
      nBits |= (uint64_t)pBlock[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++)
      vAlpha[i] = (uint8_t)vPalette[(nBits >> (3 * i)) & 7];
  }

  //! A BC3 level of nBlocks blocks (16 bytes each) whose alpha (the height) is all within FLAT_TOLERANCE of FLAT_HEIGHT: a
  //! flat height map, nothing for parallax to offset. False for no blocks.
  inline bool IsFlatHeight(const uint8_t *pData, size_t nBlocks)
  {
    if (!pData || !nBlocks)
      return false;
    uint8_t vAlpha[16];
    for (size_t i = 0; i < nBlocks; i++)
    {
      DecodeAlphaBlock(pData + 16 * i, vAlpha);
      for (uint8_t a : vAlpha)
        if (std::abs((int)a - FLAT_HEIGHT) > FLAT_TOLERANCE)
          return false;
    }
    return true;
  }

  //! The levels a full mip chain of a w x h picture has (to 1 x 1).
  inline int FullMips(int nWidth, int nHeight)
  {
    int n = 1;
    for (int s = std::max(nWidth, nHeight); s > 1; s >>= 1)
      n++;
    return n;
  }

  //! A map fit to draw: a picture of one face, its levels down to at most 4 x 4 (a compressed map gets no levels made: one
  //! level alone would shimmer under parallax), of the texture's aspect (it is made from it, of its size or a power of two
  //! less).
  inline bool IsUsable(int nWidth, int nHeight, int nMips, int nFaces, int nTexWidth, int nTexHeight)
  {
    if (nWidth < 1 || nHeight < 1 || nFaces != 1 || nMips < 1)
      return false;
    if (nMips > FullMips(nWidth, nHeight) || (std::max(nWidth, nHeight) >> (nMips - 1)) > 4)
      return false;
    if (nTexWidth > 0 && nTexHeight > 0 && (int64_t)nWidth * nTexHeight != (int64_t)nHeight * nTexWidth)
      return false;
    return true;
  }
}

#endif
