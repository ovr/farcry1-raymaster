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
//	occlusion mapping), r_PbrNormals the normals; RayMaster/pbr_depth.tsv
//	(AssetViewer's --pbr-depth) each texture's share of r_ParallaxDepth by
//	its physical material (r_ParallaxMaterials); <texture>.dds there the
//	texture itself made twice as large (r_TexUpscaled).
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_PBRCOMPANION_H
#define XRENDERD3D12_PBRCOMPANION_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cctype>
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

  //! The table of each texture's parallax depth (r_ParallaxMaterials): beside the maps, AssetViewer's --pbr-depth.
  constexpr std::string_view DEPTH_TABLE = "raymaster/pbr_depth.tsv";
  constexpr float DEPTH_SHARE_MOST = 4.0f;           //!< the largest share of r_ParallaxDepth a row may give

  //! The rows of the depth table's text: "<texture>\t<share>[\t...]", the texture the base of its file (lower case, '/'
  //! separated, no extension: Path's Base; the text's own taken so, '\\' as '/'), the share of r_ParallaxDepth its parallax
  //! gets (0 none, at most DEPTH_SHARE_MOST); '#' starts a comment line; a row without a share or with one not a number
  //! left out; the last of the same texture's rows counts. Calls Add(Base, fShare) for each; returns the rows taken.
  template <class FAdd>
  int ParseDepthTable(std::string_view Text, FAdd &&Add)
  {
    int nRows = 0;
    while (!Text.empty())
    {
      const size_t nEnd = Text.find('\n');
      std::string_view Line = Text.substr(0, nEnd);
      Text = nEnd == std::string_view::npos ? std::string_view() : Text.substr(nEnd + 1);
      if (!Line.empty() && Line.back() == '\r')
        Line.remove_suffix(1);
      const size_t nTab = Line.find('\t');
      if (Line.empty() || Line[0] == '#' || nTab == 0 || nTab == std::string_view::npos)
        continue;
      std::string Base(Line.substr(0, nTab));
      for (char &c : Base)
        c = c == '\\' ? '/' : (char)std::tolower((unsigned char)c);
      std::string_view Rest = Line.substr(nTab + 1);
      const std::string Share(Rest.substr(0, Rest.find('\t')));
      char *pEnd = nullptr;
      const float f = std::strtof(Share.c_str(), &pEnd);
      if (Share.empty() || pEnd != Share.c_str() + Share.size() || !(f == f))
        continue;
      Add(Base, std::clamp(f, 0.0f, DEPTH_SHARE_MOST));
      nRows++;
    }
    return nRows;
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

  //! The upscaled textures (r_TexUpscaled): the game's textures made larger by a network (AssetViewer's --upscale-batch: a
  //! cleanup network for the compression's blocks, then an upscaling one, twice the side, compressed as the texture was,
  //! with its levels), in the game's folder RayMaster beside the maps, at the paks' paths, as .dds (a .tga's and a
  //! .jpg's too); their manifest beside the depth table says the set is there.
  constexpr std::string_view UPSCALED_MANIFEST = "raymaster/upscaled_manifest.tsv";

  //! The upscaled file of a texture read from Base (as Path's: lower case, '/' separated, without the extension):
  //! raymaster/<Base>.dds, the maps' names without their suffix.
  inline std::string UpscaledPath(std::string_view Base)
  {
    return Path(Base, "");
  }

  //! An upscaled picture fit to take the place of a texture's: one face, as the texture, wider than it, of its aspect, no
  //! more levels than its chain has; where the texture has levels (nTexLevels: its own, or those Texture.cpp makes for an
  //! uncompressed picture without them) the upscaled one's go down as small as the texture's do (or to 4 x 4, BC's last
  //! full blocks): a compressed picture gets no levels made, and one level alone would shimmer far off. nLevels: the
  //! upscaled picture's levels, counted as nTexLevels are.
  inline bool IsUpscaledUsable(int nWidth, int nHeight, int nLevels, int nFaces, int nTexWidth, int nTexHeight, int nTexLevels, int nTexFaces)
  {
    if (nWidth < 1 || nHeight < 1 || nLevels < 1 || nFaces != 1 || nTexWidth < 1 || nTexHeight < 1 || nTexLevels < 1 || nTexFaces != 1)
      return false;
    if (nWidth <= nTexWidth || nHeight < nTexHeight || (int64_t)nWidth * nTexHeight != (int64_t)nHeight * nTexWidth)
      return false;
    if (nLevels > FullMips(nWidth, nHeight))
      return false;
    if (nTexLevels > 1)
    {
      const int nTexLast = std::max(nTexWidth, nTexHeight) >> (std::min(nTexLevels, FullMips(nTexWidth, nTexHeight)) - 1);
      const int nLast = std::max(nWidth, nHeight) >> (nLevels - 1);
      if (nLast > std::max(4, 2 * nTexLast))
        return false;
    }
    return true;
  }

  //! The side the engine is told is an upscaled texture's original one (ITexPic::GetOriginalWidth: the interface maps its
  //! texel rectangles by it): the texture's own side nTexSide, as much smaller as the upscaled side nUpSide became nSide
  //! (r_TexResolution's levels dropped); at least 1.
  inline int UpscaledOriginalSide(int nTexSide, int nUpSide, int nSide)
  {
    if (nUpSide < 1)
      return nTexSide;
    return std::max(1, (int)((int64_t)nTexSide * nSide / nUpSide));
  }

  //! r_Skin: the characters' skin is the texture of their faces and heads, read from Base (as Path's: lower case, '/'
  //! separated, without the extension): a file under objects/characters/ whose name says face or head (jc_head_dark2,
  //! valeriface, merc_rear_face_blood), not a normal or height map of one (_ddn, _ddp, _bump). The physical materials
  //! do not tell it (most faces are mat_default, Jack's hair mat_head).
  inline bool IsSkin(std::string_view Base)
  {
    constexpr std::string_view CHARACTERS = "objects/characters/";
    if (Base.substr(0, CHARACTERS.size()) != CHARACTERS)
      return false;
    const size_t nSlash = Base.rfind('/');
    const std::string_view Name = Base.substr(nSlash + 1);
    if (Name.find("face") == std::string_view::npos && Name.find("head") == std::string_view::npos)
      return false;
    for (std::string_view Map : { "_ddn", "_ddp", "_bump" })
      if (Name.size() >= Map.size() && Name.substr(Name.size() - Map.size()) == Map)
        return false;
    return true;
  }

  //! A texture of the sky boxes, read from Base (as Path's): a file under textures/skys/. A surface of one stands for
  //! the sky behind an opening (the windows of Training's bunker: planes of its sky box's side, lit by themselves),
  //! which the volumetric light's rays towards the sun pass (XR_RAY_SKY, d3d12_VolumetricLightRays).
  inline bool IsSky(std::string_view Base)
  {
    constexpr std::string_view SKYS = "textures/skys/";
    return Base.substr(0, SKYS.size()) == SKYS;
  }
}

#endif
