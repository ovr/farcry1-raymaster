// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Texture.h
//	Description: Textures (ITexPic) and the texture manager.
//
//	CTexture is the renderer's ITexPic; CTextureManager owns all of them,
//	hands out the IDs (from 4096), finds files through ICryPak and makes
//	the GPU textures (D3D12Upload.h). The IRenderer texture methods are in
//	RendererTextures.cpp, the file formats in ImageLoaders.cpp.
//
//	IDs: the placeholder for missing files is 4096, the white texture
//	4097, the others follow. A removed texture's ID is handed out again
//	(the most recently freed first), so the IDs stay small: the engine
//	keeps some of them in shorts (CCObject::m_nLMId, m_NumCM).
//	References: every load that returns a texture adds one, Release and
//	RemoveTexture (by ID or pointer) drop one, and the last one removes the
//	texture. A removed CTexture keeps its object (no GPU texture, no ID)
//	until Shutdown, so a late call on a pointer the engine still holds is
//	harmless.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <unordered_set>

class CD3D12Texture;

//! The first texture ID (the placeholder); the white texture has the next one.
constexpr int TEXTURE_FIRST_ID = 4096;
//! IDs above this are never handed out (the engine keeps texture IDs in shorts).
constexpr int TEXTURE_MAX_ID = 32767;

//////////////////////////////////////////////////////////////////////////
// Pictures in system memory (ImageLoaders.cpp)

//! A picture as the GPU takes it: nFaces faces (1, or 6 for a cube in the order +X, -X, +Y, -Y,
//! +Z, -Z, or the slices of a texture array with bArray), each with nMips levels, tightly packed
//! (face 0 level 0, face 0 level 1, ..., face 1 level 0, ...), rows from the top. Format is
//! DXGI_FORMAT_B8G8R8A8_UNORM or BC1..BC3_UNORM.
struct SImage
{
  DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
  int nWidth = 0, nHeight = 0;
  int nMips = 0;
  int nFaces = 0;
  bool bArray = false;                //!< the faces are the slices of a texture array (not a cube)
  int nSrcFlags = 0;                  //!< FT_DXT1 / FT_DXT3 / FT_DXT5 / FT_HASALPHA from the source format
  std::vector<byte> Data;

  //! Sizes Data for this format, size, levels and faces, filled with zeros.
  void Allocate(DXGI_FORMAT Fmt, int nW, int nH, int nLevels, int nFaceCount);
  int MipWidth(int nMip) const { return std::max(1, nWidth >> nMip); }
  int MipHeight(int nMip) const { return std::max(1, nHeight >> nMip); }
  size_t MipSize(int nMip) const;
  size_t FaceSize() const;
  size_t Offset(int nFace, int nMip) const;
  byte *Bits(int nFace, int nMip) { return Data.data() + Offset(nFace, nMip); }
  const byte *Bits(int nFace, int nMip) const { return Data.data() + Offset(nFace, nMip); }
};

namespace Img
{
  bool IsBC(DXGI_FORMAT Format);
  //! Bytes per row of a level (per row of 4x4 blocks for BC formats).
  uint32 RowPitch(DXGI_FORMAT Format, int nWidth);
  //! Rows of a level (rows of blocks for BC formats).
  uint32 RowCount(DXGI_FORMAT Format, int nHeight);
  //! Levels down to 1x1.
  int FullMipCount(int nWidth, int nHeight);

  //! A file's picture by its contents: DDS by its magic, else TGA for szExt ".tga", else WIC (JPG).
  bool LoadFile(std::span<const byte> File, const char *szExt, SImage &Out);
  bool LoadDDS(std::span<const byte> File, SImage &Out);
  bool LoadTGA(std::span<const byte> File, SImage &Out);
  //! JPG (and whatever else WIC decodes), from memory, as B8G8R8A8.
  bool LoadWIC(std::span<const byte> File, SImage &Out);

  //! B8G8R8A8 only: keeps level 0 of every face and makes the levels below it with a box filter
  //! (all levels down to 1x1).
  void GenerateMips(SImage &Image);
  //! BC1..BC3 to B8G8R8A8, every face and level; true when the picture is B8G8R8A8 afterwards.
  bool Decompress(SImage &Image);
  //! One level of BC1 / BC2 / BC3 blocks to pixels of nDstBytes (3: B, G, R; 4: B, G, R, A),
  //! rows nDstPitch bytes apart.
  bool DecodeBC(const byte *pSrc, DXGI_FORMAT Format, int nWidth, int nHeight, byte *pDst, int nDstPitch, int nDstBytes);
  //! Bytes per pixel of an uncompressed ETEX_Format this renderer converts, 0 for any other.
  int BytesPerPixel(ETEX_Format eTF);
  //! nWidth x nHeight pixels of eTF (tightly packed, rows from the top) to B, G, R, A. eTF_8888
  //! and eTF_0888 are B, G, R, A bytes (eTF_0888 gets alpha 255); alpha-only formats give black
  //! with that alpha. False for a format BytesPerPixel does not know.
  bool ConvertToBGRA(const byte *pSrc, ETEX_Format eTF, int nWidth, int nHeight, byte *pDst);
}

//////////////////////////////////////////////////////////////////////////
// Textures

class CTexture : public ITexPic
{
public:
  CTexture(int nID, const char *szName);
  ~CTexture() = default;
  CTexture(const CTexture &) = delete;
  CTexture &operator=(const CTexture &) = delete;

  // ITexPic
  virtual void AddRef() override;
  virtual void Release(int bForce = false) override;
  virtual const char *GetName() override;
  virtual int GetWidth() override;
  virtual int GetHeight() override;
  virtual int GetOriginalWidth() override;
  virtual int GetOriginalHeight() override;
  virtual int GetTextureID() override;
  virtual int GetFlags() override;
  virtual int GetFlags2() override;
  virtual void SetClamp(bool bEnable) override;
  virtual bool IsTextureLoaded() override;
  virtual void PrecacheAsynchronously(float fDist, int Flags) override;
  virtual void Preload(int Flags) override;
  virtual byte *GetData32() override;
  virtual bool SetFilter(int nFilter) override;

  //! Index in the texture table (D3D12Descriptors.h); D3D12_SRV_WHITE while there is no GPU texture.
  int GetSRV() const;
  //! XR_SAMPLER_* (Shaders/Constants.h) for the texture's clamp and filter settings.
  int GetSampler() const;
  bool IsCube() const;

  CD3D12Texture *m_pGPU = nullptr;

  // The manager's bookkeeping (Texture.cpp).
  std::string m_Name;                 //!< as the engine named it
  std::vector<std::string> m_Keys;    //!< the manager's name keys that lead to this texture
  int m_nID;
  int m_nRefs = 1;
  int m_nWidth = 0, m_nHeight = 0;
  int m_nOrigWidth = 0, m_nOrigHeight = 0;
  int m_nFlags = 0;                   //!< FT_*
  int m_nFlags2 = 0;                  //!< FT2_*
  ETEX_Format m_eSrcFormat = eTF_8888;  //!< pixel format of updates (DownLoadToVideoMemory, fonts)
  bool m_bClamp = false;              //!< clamp addressing (else wrap)
  bool m_bPoint = false;              //!< point filtering (else linear)
  bool m_bCube = false;
  bool m_bArray = false;              //!< a texture array (g_TextureArrays)
  bool m_bLoaded = false;             //!< IsTextureLoaded
  bool m_bPermanent = false;          //!< the white texture and the placeholder: never removed
  bool m_bRemoved = false;            //!< removed: no GPU texture, no ID, kept until Shutdown
};

class CTextureManager
{
public:
  CTextureManager();
  ~CTextureManager();
  CTextureManager(const CTextureManager &) = delete;
  CTextureManager &operator=(const CTextureManager &) = delete;

  //! After the device: makes the white texture (table slot D3D12_SRV_WHITE) and the placeholder.
  bool Init();
  //! Releases every texture (IRenderer::Release).
  void Shutdown();

  //! The texture with this ID; NULL for none (0, -1, removed, unknown).
  CTexture *GetByID(int nID);
  //! Texture table index for a texture ID; the white texture for none (-1, 0, unknown).
  int GetSRV(int nID);
  CTexture *GetWhite();
  //! The texture missing files get (ID 4096, IsTextureLoaded false).
  CTexture *GetPlaceholder();
  //! EF_LoadTexture semantics (flags FT_*, FT2_*, eTT ETexType); never NULL (a placeholder for a
  //! missing file). The texture gets one reference for the caller.
  CTexture *LoadTexture(const char *szName, uint nFlags, uint nFlags2, byte eTT, float fAmount1, float fAmount2, int nID, int nBindID);
  //! A material texture (SInputShaderResources::m_Textures): searched as LoadTexture does, then
  //! as szTexturePath (the folder of the object, may be NULL or "") + the whole name and as
  //! szTexturePath + the file name. Never NULL; one reference for the caller.
  CTexture *LoadMaterialTexture(const char *szName, const char *szTexturePath, uint nFlags, uint nFlags2, byte eTT);

  //! IRenderer::LoadTexture: the ID, or nDefault when no file is found (no placeholder); a name
  //! seen before gets the same answer without reading a file. One reference for the caller.
  int LoadTextureID(const char *szName, int nDefault, uint nFlags = 0, uint nFlags2 = 0);
  //! Drops one reference (bForce: removes it at once); the last one removes the texture.
  void ReleaseTexture(CTexture *pTex, bool bForce);
  //! RemoveTexture(ID): drops one reference; 0, -1, unknown and removed IDs do nothing.
  void RemoveByID(int nID);

  //! DownLoadToVideoMemory: a texture from pixels of eSrc (NULL: zeros). nID non-zero: that ID
  //! (the texture with it is replaced). Returns the ID, 0 on failure.
  int CreateFromMemory(const byte *pData, int nWidth, int nHeight, ETEX_Format eSrc, int nMips, bool bRepeat, int nFilter, int nID, const char *szName, int nFlags);
  //! A texture of the renderer's own from a picture with its levels (an empty Image: an ID without
  //! a GPU texture yet), linear filtering. nID non-zero: that texture gets the picture. Returns
  //! the ID, 0 on failure.
  int CreateFromImage(const SImage &Image, const char *szName, bool bClamp, int nID);
  //! UpdateTextureInVideoMemory: a rectangle of level 0, pixels of eSrc (eTF_Unknown: the
  //! texture's own source format), clipped to the texture.
  void UpdateRect(int nID, const byte *pData, int x, int y, int nWidth, int nHeight, ETEX_Format eSrc);
  //! GenerateAlphaGlowTexture: 256 x 256 with all levels, alpha falling from the centre.
  int CreateAlphaGlow(float fK);

  //! LoadAnimatedTexture / GetAnimTexInfoFromId / RemoveAnimatedTexture (IDs from 1).
  int LoadAnimated(const char *szFormat, int nCount);
  AnimTexInfo *GetAnimInfo(int nID);
  void RemoveAnimated(AnimTexInfo *pInfo);

private:
  //! An AnimTexInfo stays at its heap address until it is removed (the engine keeps the pointer).
  struct SAnimTex
  {
    std::unique_ptr<AnimTexInfo> pInfo;
    std::vector<int> BindIds;         //!< pInfo->pBindIds points here
    std::vector<CTexture *> Frames;   //!< one reference each
  };

  //! A new texture with a new ID (nID > 0: that ID when it is free), registered by ID.
  CTexture *NewTexture(const char *szName, int nID = 0);
  //! Gives up the texture: GPU texture, ID and keys; the object moves to m_Removed.
  void Remove(CTexture *pTex);
  //! Makes the GPU texture of pTex from Image (replacing one it has) and sets its sizes.
  bool Upload(CTexture *pTex, const SImage &Image);
  void AddKey(CTexture *pTex, const std::string &Key);
  CTexture *FindKey(const std::string &Key);

  //! What a search found.
  struct SSearch
  {
    bool bOpened = false;             //!< a file opened
    bool bDecoded = false;            //!< and its picture could be read
    bool bViaDir = false;             //!< found only in the material's folder
    bool bSingle = false;             //!< a cube made of one picture on +X
    std::string Base;                 //!< the file that opened, without extension
  };

  //! Search and load (the core of the loads above): the texture with a reference added, or NULL
  //! when no file is found. bWarn: log a missing file (once per name). bRememberMiss: a name
  //! not found once is not searched again (IRenderer::LoadTexture); the other loads search again,
  //! as a pak opened later (a level's) may have the file.
  CTexture *Load(const char *szName, const char *szTexturePath, uint nFlags, uint nFlags2, byte eTT, bool bWarn, bool bRememberMiss);
  //! Reads the first file of the base names (each with .dds, .tga, .jpg) that opens and decodes
  //! it; true when it could be decoded. Bases from nPlain on are in a material's folder.
  bool Search(const std::vector<std::string> &Bases, size_t nPlain, SImage &Out, SSearch &Res);
  //! A cube map: six files <base>_posx ... _negz, else the file <base> (a cube file, or one
  //! picture on +X). Base may be the name of any face.
  bool SearchCube(const std::string &Base, const std::string &Dir, SImage &Out, SSearch &Res);
  //! Reads a whole file through ICryPak; false when it does not open.
  bool ReadFile(const char *szPath, std::vector<byte> &Data);

  std::vector<std::unique_ptr<CTexture>> m_ByID;      //!< [ID - TEXTURE_FIRST_ID], NULL: free
  std::vector<int> m_FreeIDs;                         //!< removed IDs, handed out again from the back
  std::vector<std::unique_ptr<CTexture>> m_Removed;   //!< removed textures (see the head comment)
  std::unordered_map<std::string, CTexture *> m_ByName;
  std::unordered_set<std::string> m_Missing;          //!< IRenderer::LoadTexture names not found
  std::unordered_set<std::string> m_Warned;           //!< missing names already logged
  std::vector<SAnimTex> m_AnimTex;                    //!< [ID - 1], pInfo NULL: free
  std::unordered_set<int> m_WarnedFormats;            //!< unsupported ETEX_Formats already logged
  CTexture *m_pWhite = nullptr;
  CTexture *m_pPlaceholder = nullptr;
  bool m_bInit = false;
};
