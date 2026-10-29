// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RendererTextures.cpp
//	Description: The IRenderer texture methods.
//
//	Pixel data the engine passes (eTF_8888, eTF_0888) is 4 bytes per
//	pixel in the order B, G, R, A, rows from the top. Texture IDs, names
//	and references: CTextureManager (Texture.h).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Texture.h"

namespace
{
  //! Writes an uncompressed TGA file with stdio: nBits 8 (grey), 24 (B, G, R) or 32 (B, G, R, A)
  //! per pixel, rows from the top in pData; bFlip stores the picture the other way up.
  bool WriteTGAFile(const char *szName, const byte *pData, int nWidth, int nHeight, int nBits, bool bFlip)
  {
    if (!szName || !szName[0] || !pData || nWidth <= 0 || nHeight <= 0 || nWidth > 0xffff || nHeight > 0xffff)
      return false;
    if (nBits != 8 && nBits != 24 && nBits != 32)
      return false;
    FILE *f = fopen(szName, "wb");
    if (!f)
    {
      if (iLog)
        iLog->LogWarning("Cannot write '%s'", szName);
      return false;
    }
    byte Header[18] = {};
    Header[2] = (nBits == 8) ? 3 : 2;                   // grey or true colour, uncompressed
    Header[12] = (byte)(nWidth & 0xff);
    Header[13] = (byte)(nWidth >> 8);
    Header[14] = (byte)(nHeight & 0xff);
    Header[15] = (byte)(nHeight >> 8);
    Header[16] = (byte)nBits;
    // Bits 0-3: alpha bits; bit 5: the first row is the top one.
    Header[17] = (byte)((nBits == 32 ? 8 : 0) | (bFlip ? 0 : 0x20));
    const size_t nRow = (size_t)nWidth * (nBits / 8);
    const bool bOk = fwrite(Header, sizeof(Header), 1, f) == 1 && fwrite(pData, nRow, (size_t)nHeight, f) == (size_t)nHeight;
    fclose(f);
    return bOk;
  }

  DXGI_FORMAT BCFormat(ETEX_Format eTF)
  {
    switch (eTF)
    {
    case eTF_DXT1: return DXGI_FORMAT_BC1_UNORM;
    case eTF_DXT3: return DXGI_FORMAT_BC2_UNORM;
    case eTF_DXT5: return DXGI_FORMAT_BC3_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
  }
}

bool CD3D12Renderer::SaveTga(unsigned char *sourcedata,int sourceformat,int w,int h,const char *filename,bool flip)
{
  // sourceformat: FORMAT_8_BIT, FORMAT_24_BIT or FORMAT_32_BIT (bits per pixel).
  return WriteTGAFile(filename, sourcedata, w, h, sourceformat, flip);
}

void CD3D12Renderer::WriteDDS(byte *dat, int wdt, int hgt, int Size, const char *name, EImFormat eF, int NumMips)
{
  if (iLog)
    iLog->LogWarning("WriteDDS is not supported ('%s')", name ? name : "");
}

void CD3D12Renderer::WriteTGA(byte *dat, int wdt, int hgt, const char *name, int bits)
{
  WriteTGAFile(name, dat, wdt, hgt, bits, false);
}

void CD3D12Renderer::WriteJPG(byte *dat, int wdt, int hgt, char *name)
{
  if (iLog)
    iLog->LogWarning("WriteJPG is not supported ('%s')", name ? name : "");
}

int CD3D12Renderer::FontCreateTexture(int Width, int Height, byte *pData, ETEX_Format eTF)
{
  // The font bitmap: B, G, R, A with the glyph coverage in A; clamped, bilinear, no mip maps.
  if (!m_pTextures)
    return 0;
  return m_pTextures->CreateFromMemory(pData, Width, Height, eTF, 0, false, FILTER_BILINEAR, 0, "$Font", FT_NOMIPS | FT_FONT);
}

bool CD3D12Renderer::FontUpdateTexture(int nTexId, int X, int Y, int USize, int VSize, byte *pData)
{
  // pData holds the USize x VSize rectangle at (X, Y), in the format the texture was made with
  // (the engine passes the whole bitmap at (0, 0)).
  if (!m_pTextures || !m_pTextures->GetByID(nTexId))
    return false;
  m_pTextures->UpdateRect(nTexId, pData, X, Y, USize, VSize, eTF_Unknown);
  return true;
}

bool CD3D12Renderer::EF_PrecacheResource(ITexPic *pTP, float fDist, float fTimeToReady, int Flags)
{
  // Textures are on the GPU as soon as they are loaded.
  return true;
}

void CD3D12Renderer::EF_ReloadTextures()
{
}

ITexPic *CD3D12Renderer::EF_GetTextureByID(int Id)
{
  return m_pTextures ? m_pTextures->GetByID(Id) : nullptr;
}

ITexPic *CD3D12Renderer::EF_LoadTexture(const char* nameTex, uint flags, uint flags2, byte eTT, float fAmount1, float fAmount2, int Id, int BindId)
{
  return m_pTextures ? m_pTextures->LoadTexture(nameTex, flags, flags2, eTT, fAmount1, fAmount2, Id, BindId) : nullptr;
}

int CD3D12Renderer::EF_LoadLightmap(const char *name)
{
  return m_pTextures ? m_pTextures->LoadTextureID(name, 0, FT_LM, FT2_NOANISO) : 0;
}

bool CD3D12Renderer::EF_ScanEnvironmentCM(const char *name, int size, Vec3& Pos)
{
  return false;
}

bool CD3D12Renderer::EF_SetLightHole(Vec3 vPos, Vec3 vNormal, int idTex, float fScale, bool bAdditive)
{
  return false;
}

unsigned int CD3D12Renderer::DownLoadToVideoMemory(unsigned char *data,int w, int h, ETEX_Format eTFSrc, ETEX_Format eTFDst, int nummipmap, bool repeat, int filter, int Id, char *szCacheName, int flags)
{
  // The texture keeps the source format (DXT formats as blocks, the others as B8G8R8A8); mip
  // maps are made from level 0 when nummipmap asks for them.
  if (!m_pTextures)
    return 0;
  return (unsigned int)m_pTextures->CreateFromMemory(data, w, h, eTFSrc, nummipmap, repeat, filter, Id, szCacheName, flags);
}

void CD3D12Renderer::UpdateTextureInVideoMemory(uint tnum, unsigned char *newdata,int posx,int posy,int w,int h,ETEX_Format eTFSrc)
{
  if (m_pTextures)
    m_pTextures->UpdateRect((int)tnum, newdata, posx, posy, w, h, eTFSrc);
}

unsigned int CD3D12Renderer::LoadTexture(const char * filename,int *tex_type,unsigned int def_tid,bool compresstodisk,bool bWarn)
{
  if (!m_pTextures)
    return def_tid;
  return (unsigned int)m_pTextures->LoadTextureID(filename, (int)def_tid);
}

bool CD3D12Renderer::DXTCompress(byte *raw_data,int nWidth,int nHeight,ETEX_Format eTF, bool bUseHW, bool bGenMips, int nSrcBytesPerPix, MIPDXTcallback callback)
{
  return false;
}

bool CD3D12Renderer::DXTDecompress(byte *srcData,byte *dstData, int nWidth,int nHeight,ETEX_Format eSrcTF, bool bUseHW, int nDstBytesPerPix)
{
  // Level 0 to B, G, R (3 bytes per pixel) or B, G, R, A (4), rows from the top.
  const DXGI_FORMAT Format = BCFormat(eSrcTF);
  if (Format == DXGI_FORMAT_UNKNOWN)
    return false;
  return Img::DecodeBC(srcData, Format, nWidth, nHeight, dstData, nWidth * nDstBytesPerPix, nDstBytesPerPix);
}

void CD3D12Renderer::RemoveTexture(unsigned int TextureId)
{
  // One reference; 0, 0xFFFFFFFF and IDs removed before do nothing.
  if (m_pTextures)
    m_pTextures->RemoveByID((int)TextureId);
}

void CD3D12Renderer::RemoveTexture(ITexPic * pTexPic)
{
  if (m_pTextures && pTexPic)
    m_pTextures->ReleaseTexture(static_cast<CTexture *>(pTexPic), false);
}

int CD3D12Renderer::GenerateAlphaGlowTexture(float k)
{
  return m_pTextures ? m_pTextures->CreateAlphaGlow(k) : 0;
}

int CD3D12Renderer::LoadAnimatedTexture(const char * format,const int nCount)
{
  return m_pTextures ? m_pTextures->LoadAnimated(format, nCount) : 0;
}

void CD3D12Renderer::RemoveAnimatedTexture(AnimTexInfo * pInfo)
{
  if (m_pTextures)
    m_pTextures->RemoveAnimated(pInfo);
}

AnimTexInfo *CD3D12Renderer::GetAnimTexInfoFromId(int nId)
{
  return m_pTextures ? m_pTextures->GetAnimInfo(nId) : nullptr;
}

int CD3D12Renderer::ScreenToTexture()
{
  return 0;
}
