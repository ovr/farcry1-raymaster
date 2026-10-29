// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: ImageLoaders.cpp
//	Description: DDS, TGA and JPG files, pixel conversions, mip maps.
//
//	Every picture ends up as B8G8R8A8 or as BC1 / BC2 / BC3 blocks
//	(SImage, Texture.h). Uncompressed DDS layouts (A8R8G8B8, X8R8G8B8,
//	R8G8B8, A8, L8, A8L8, ...) are converted to B8G8R8A8; alpha-only
//	pictures become black with that alpha, luminance grey. 16-bit colour
//	DDS files (R5G6B5, X1R5G5B5, ...) are not read: such a texture gets the
//	placeholder. JPG goes through WIC.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Texture.h"

#include <bit>
#include <wincodec.h>

//////////////////////////////////////////////////////////////////////////
// SImage

void SImage::Allocate(DXGI_FORMAT Fmt, int nW, int nH, int nLevels, int nFaceCount)
{
  Format = Fmt;
  nWidth = nW;
  nHeight = nH;
  nMips = nLevels;
  nFaces = nFaceCount;
  Data.assign(FaceSize() * nFaces, 0);
}

size_t SImage::MipSize(int nMip) const
{
  return (size_t)Img::RowPitch(Format, MipWidth(nMip)) * Img::RowCount(Format, MipHeight(nMip));
}

size_t SImage::FaceSize() const
{
  size_t nSize = 0;
  for (int nMip = 0; nMip < nMips; nMip++)
    nSize += MipSize(nMip);
  return nSize;
}

size_t SImage::Offset(int nFace, int nMip) const
{
  size_t nOffset = FaceSize() * nFace;
  for (int i = 0; i < nMip; i++)
    nOffset += MipSize(i);
  return nOffset;
}

//////////////////////////////////////////////////////////////////////////
// Helpers

namespace
{
  constexpr DXGI_FORMAT FMT_BGRA = DXGI_FORMAT_B8G8R8A8_UNORM;

  uint32 BlockBytes(DXGI_FORMAT Format)
  {
    return Format == DXGI_FORMAT_BC1_UNORM ? 8 : 16;
  }

  inline uint16 Read16(const byte *p)
  {
    return (uint16)(p[0] | (p[1] << 8));
  }

  inline uint32 Read32(const byte *p)
  {
    return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
  }

  inline byte Expand5(uint32 v)
  {
    return (byte)((v << 3) | (v >> 2));
  }

  inline byte Expand6(uint32 v)
  {
    return (byte)((v << 2) | (v >> 4));
  }

  inline byte Expand4(uint32 v)
  {
    return (byte)(v * 17);
  }

  //! One channel of a masked pixel layout, scaled to 0..255 (no mask: 0).
  struct SChannel
  {
    uint32 nMask = 0, nShift = 0, nMax = 0;

    explicit SChannel(uint32 nChannelMask) : nMask(nChannelMask)
    {
      if (nMask)
      {
        nShift = (uint32)std::countr_zero(nMask);
        nMax = nMask >> nShift;
      }
    }

    byte Get(uint32 v) const
    {
      if (!nMask)
        return 0;
      const uint64 x = (v & nMask) >> nShift;
      return nMax == 255 ? (byte)x : (byte)((x * 255 + nMax / 2) / nMax);
    }
  };

  //! An uncompressed pixel layout: bits per pixel and channel masks (0: not there).
  struct SMaskFormat
  {
    uint32 nBits = 0;
    uint32 R = 0, G = 0, B = 0, A = 0;
    bool bLuminance = false;          //!< R is the luminance
  };

  void ConvertMasked(const byte *pSrc, const SMaskFormat &M, int nWidth, int nHeight, byte *pDst)
  {
    const SChannel R(M.R), G(M.G), B(M.B), A(M.A);
    const uint32 nBytes = M.nBits / 8;
    const size_t nCount = (size_t)nWidth * nHeight;
    for (size_t i = 0; i < nCount; i++, pSrc += nBytes, pDst += 4)
    {
      uint32 v = 0;
      memcpy(&v, pSrc, nBytes);
      const byte r = R.Get(v);
      pDst[0] = M.bLuminance ? r : B.Get(v);
      pDst[1] = M.bLuminance ? r : G.Get(v);
      pDst[2] = r;
      pDst[3] = M.A ? A.Get(v) : 255;
    }
  }

  //! The 16 texels of a BC1 colour block as B, G, R, A. bThreeColour: BC1's mode with transparent
  //! black when colour 0 <= colour 1 (BC2 and BC3 always use four colours).
  void DecodeColourBlock(const byte *p, bool bThreeColour, byte (&Texels)[16][4])
  {
    const uint16 c0 = Read16(p), c1 = Read16(p + 2);
    byte Colours[4][4];
    const uint16 c[2] = { c0, c1 };
    for (int i = 0; i < 2; i++)
    {
      Colours[i][0] = Expand5(c[i] & 31);
      Colours[i][1] = Expand6((c[i] >> 5) & 63);
      Colours[i][2] = Expand5((c[i] >> 11) & 31);
      Colours[i][3] = 255;
    }
    if (c0 > c1 || !bThreeColour)
    {
      for (int k = 0; k < 3; k++)
      {
        Colours[2][k] = (byte)((2 * Colours[0][k] + Colours[1][k]) / 3);
        Colours[3][k] = (byte)((Colours[0][k] + 2 * Colours[1][k]) / 3);
      }
      Colours[2][3] = Colours[3][3] = 255;
    }
    else
    {
      for (int k = 0; k < 3; k++)
        Colours[2][k] = (byte)((Colours[0][k] + Colours[1][k]) / 2);
      Colours[2][3] = 255;
      memset(Colours[3], 0, 4);
    }
    const uint32 nIndices = Read32(p + 4);
    for (int i = 0; i < 16; i++)
      memcpy(Texels[i], Colours[(nIndices >> (2 * i)) & 3], 4);
  }

  // DDS file layout (the public DirectDraw Surface format).
  struct SDDSPixelFormat
  {
    uint32 dwSize, dwFlags, dwFourCC, dwRGBBitCount, dwRBitMask, dwGBitMask, dwBBitMask, dwABitMask;
  };

  struct SDDSHeader
  {
    uint32 dwSize, dwFlags, dwHeight, dwWidth, dwPitchOrLinearSize, dwDepth, dwMipMapCount;
    uint32 dwReserved1[11];
    SDDSPixelFormat PF;
    uint32 dwCaps, dwCaps2, dwCaps3, dwCaps4, dwReserved2;
  };
  static_assert(sizeof(SDDSHeader) == 124, "DDS header size");

  constexpr uint32 DDPF_ALPHAPIXELS = 0x1;
  constexpr uint32 DDPF_ALPHA = 0x2;
  constexpr uint32 DDPF_FOURCC = 0x4;
  constexpr uint32 DDPF_RGB = 0x40;
  constexpr uint32 DDPF_LUMINANCE = 0x20000;
  constexpr uint32 DDSCAPS2_CUBEMAP = 0x200;
  constexpr uint32 DDSCAPS2_CUBEMAP_ALLFACES = 0xfc00;
  constexpr uint32 DDSCAPS2_VOLUME = 0x200000;
  //! The face bits of a cube map, in the order of the faces in the file.
  constexpr uint32 g_DDSFaceBits[6] = { 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000 };

  constexpr uint32 MakeFourCC(char a, char b, char c, char d)
  {
    return (uint32)(byte)a | ((uint32)(byte)b << 8) | ((uint32)(byte)c << 16) | ((uint32)(byte)d << 24);
  }

  //! The masks of the Direct3D format codes some files put in the FourCC field.
  bool MaskFromFormatCode(uint32 nCode, SMaskFormat &M)
  {
    switch (nCode)
    {
    case 20: M = { 24, 0xff0000, 0xff00, 0xff, 0 }; return true;                     // R8G8B8
    case 21: M = { 32, 0xff0000, 0xff00, 0xff, 0xff000000 }; return true;            // A8R8G8B8
    case 22: M = { 32, 0xff0000, 0xff00, 0xff, 0 }; return true;                     // X8R8G8B8
    case 23: M = { 16, 0xf800, 0x7e0, 0x1f, 0 }; return true;                        // R5G6B5
    case 24: M = { 16, 0x7c00, 0x3e0, 0x1f, 0 }; return true;                        // X1R5G5B5
    case 25: M = { 16, 0x7c00, 0x3e0, 0x1f, 0x8000 }; return true;                   // A1R5G5B5
    case 26: M = { 16, 0xf00, 0xf0, 0xf, 0xf000 }; return true;                      // A4R4G4B4
    case 28: M = { 8, 0, 0, 0, 0xff }; return true;                                  // A8
    case 32: M = { 32, 0xff, 0xff00, 0xff0000, 0xff000000 }; return true;            // A8B8G8R8
    case 33: M = { 32, 0xff, 0xff00, 0xff0000, 0 }; return true;                     // X8B8G8R8
    case 50: M = { 8, 0xff, 0, 0, 0, true }; return true;                            // L8
    case 51: M = { 16, 0xff, 0, 0, 0xff00, true }; return true;                      // A8L8
    default: return false;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// Img

bool Img::IsBC(DXGI_FORMAT Format)
{
  return Format == DXGI_FORMAT_BC1_UNORM || Format == DXGI_FORMAT_BC2_UNORM || Format == DXGI_FORMAT_BC3_UNORM;
}

uint32 Img::RowPitch(DXGI_FORMAT Format, int nWidth)
{
  if (IsBC(Format))
    return (uint32)std::max(1, (nWidth + 3) / 4) * BlockBytes(Format);
  return (uint32)nWidth * 4;
}

uint32 Img::RowCount(DXGI_FORMAT Format, int nHeight)
{
  return IsBC(Format) ? (uint32)std::max(1, (nHeight + 3) / 4) : (uint32)nHeight;
}

int Img::FullMipCount(int nWidth, int nHeight)
{
  int nMips = 1;
  while (nWidth > 1 || nHeight > 1)
  {
    nWidth = std::max(1, nWidth / 2);
    nHeight = std::max(1, nHeight / 2);
    nMips++;
  }
  return nMips;
}

bool Img::LoadFile(std::span<const byte> File, const char *szExt, SImage &Out)
{
  Out = SImage();
  if (File.size() >= 4 && !memcmp(File.data(), "DDS ", 4))
    return LoadDDS(File, Out);
  if (szExt && !_stricmp(szExt, ".tga"))
    return LoadTGA(File, Out);
  return LoadWIC(File, Out);
}

bool Img::LoadDDS(std::span<const byte> File, SImage &Out)
{
  constexpr size_t nDataStart = 4 + sizeof(SDDSHeader);
  if (File.size() < nDataStart || memcmp(File.data(), "DDS ", 4))
    return false;
  SDDSHeader H;
  memcpy(&H, File.data() + 4, sizeof(H));
  const int nWidth = (int)H.dwWidth, nHeight = (int)H.dwHeight;
  if (nWidth <= 0 || nHeight <= 0 || nWidth > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || nHeight > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    return false;
  if (H.dwCaps2 & DDSCAPS2_VOLUME)
    return false;
  const bool bCube = (H.dwCaps2 & DDSCAPS2_CUBEMAP) != 0;
  if (bCube && nWidth != nHeight)
    return false;

  // The format: BC blocks, or a masked layout converted to B8G8R8A8.
  DXGI_FORMAT BC = DXGI_FORMAT_UNKNOWN;
  int nSrcFlags = 0;
  SMaskFormat M;
  const SDDSPixelFormat &PF = H.PF;
  if (PF.dwFlags & DDPF_FOURCC)
  {
    switch (PF.dwFourCC)
    {
    case MakeFourCC('D', 'X', 'T', '1'): BC = DXGI_FORMAT_BC1_UNORM; nSrcFlags = FT_DXT1; break;
    case MakeFourCC('D', 'X', 'T', '2'):
    case MakeFourCC('D', 'X', 'T', '3'): BC = DXGI_FORMAT_BC2_UNORM; nSrcFlags = FT_DXT3; break;
    case MakeFourCC('D', 'X', 'T', '4'):
    case MakeFourCC('D', 'X', 'T', '5'): BC = DXGI_FORMAT_BC3_UNORM; nSrcFlags = FT_DXT5; break;
    default:
      if (!MaskFromFormatCode(PF.dwFourCC, M))
        return false;
      break;
    }
  }
  else if (PF.dwFlags & (DDPF_RGB | DDPF_LUMINANCE | DDPF_ALPHA))
  {
    M.nBits = PF.dwRGBBitCount;
    M.bLuminance = (PF.dwFlags & DDPF_LUMINANCE) != 0;
    if (PF.dwFlags & (DDPF_RGB | DDPF_LUMINANCE))
    {
      M.R = PF.dwRBitMask;
      M.G = M.bLuminance ? 0 : PF.dwGBitMask;
      M.B = M.bLuminance ? 0 : PF.dwBBitMask;
    }
    if (PF.dwFlags & (DDPF_ALPHAPIXELS | DDPF_ALPHA))
      M.A = PF.dwABitMask;
  }
  else
    return false;
  if (BC == DXGI_FORMAT_UNKNOWN)
  {
    if (M.nBits != 8 && M.nBits != 16 && M.nBits != 24 && M.nBits != 32)
      return false;
    // 16-bit colour layouts are not read.
    if (M.nBits == 16 && !M.bLuminance && (M.R | M.G | M.B))
      return false;
    if (M.A)
      nSrcFlags = FT_HASALPHA;
  }

  // Levels and faces as stored: every face with all its levels.
  const int nFileMips = std::clamp((int)H.dwMipMapCount, 1, FullMipCount(nWidth, nHeight));
  const uint32 nPixelBytes = M.nBits / 8;
  auto SrcLevelSize = [&](int nMip) -> size_t
  {
    const int w = std::max(1, nWidth >> nMip), h = std::max(1, nHeight >> nMip);
    if (BC != DXGI_FORMAT_UNKNOWN)
      return (size_t)RowPitch(BC, w) * RowCount(BC, h);
    return (size_t)w * h * nPixelBytes;
  };
  size_t nSrcFace = 0;
  for (int nMip = 0; nMip < nFileMips; nMip++)
    nSrcFace += SrcLevelSize(nMip);
  uint32 nFaceBits = bCube ? (H.dwCaps2 & DDSCAPS2_CUBEMAP_ALLFACES) : 0;
  if (bCube && !nFaceBits)
    nFaceBits = DDSCAPS2_CUBEMAP_ALLFACES;
  const int nStoredFaces = bCube ? std::popcount(nFaceBits) : 1;
  const size_t nAvail = File.size() - nDataStart;

  // A short file keeps the levels it has in full (2D textures only).
  int nMips = nFileMips;
  if (nSrcFace * nStoredFaces > nAvail)
  {
    if (bCube)
      return false;
    size_t nSize = 0;
    nMips = 0;
    while (nMips < nFileMips && nSize + SrcLevelSize(nMips) <= nAvail)
      nSize += SrcLevelSize(nMips++);
    if (!nMips)
      return false;
  }

  Out.Allocate(BC != DXGI_FORMAT_UNKNOWN ? BC : FMT_BGRA, nWidth, nHeight, nMips, bCube ? 6 : 1);
  Out.nSrcFlags = nSrcFlags;
  const byte *pFace = File.data() + nDataStart;
  for (int nFace = 0; nFace < Out.nFaces; nFace++)
  {
    if (bCube && !(nFaceBits & g_DDSFaceBits[nFace]))
      continue;                       // not in the file: stays zero (black)
    const byte *p = pFace;
    for (int nMip = 0; nMip < nMips; nMip++)
    {
      if (BC != DXGI_FORMAT_UNKNOWN)
        memcpy(Out.Bits(nFace, nMip), p, Out.MipSize(nMip));
      else
        ConvertMasked(p, M, Out.MipWidth(nMip), Out.MipHeight(nMip), Out.Bits(nFace, nMip));
      p += SrcLevelSize(nMip);
    }
    pFace += nSrcFace;
  }
  // BC textures need a size of whole blocks.
  if (BC != DXGI_FORMAT_UNKNOWN && ((nWidth & 3) || (nHeight & 3)))
    return Decompress(Out);
  return true;
}

bool Img::LoadTGA(std::span<const byte> File, SImage &Out)
{
  const size_t nFileSize = File.size();
  if (nFileSize < 18)
    return false;
  const byte *d = File.data();
  const int nIDLength = d[0], nColourMapType = d[1], nType = d[2];
  const int nMapFirst = Read16(d + 3), nMapLength = Read16(d + 5), nMapBits = d[7];
  const int nWidth = Read16(d + 12), nHeight = Read16(d + 14), nBits = d[16], nDesc = d[17];
  if (!nWidth || !nHeight || nWidth > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || nHeight > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    return false;
  // 1 colour mapped, 2 true colour, 3 grey; 9, 10, 11 the same run length encoded.
  const bool bRLE = nType >= 9;
  const int nKind = bRLE ? nType - 8 : nType;
  if (nKind < 1 || nKind > 3)
    return false;
  const int nAlphaBits = nDesc & 0x0f;

  // Decodes one pixel of nPixelBits from p to B, G, R, A.
  auto DecodeTrue = [nAlphaBits](const byte *p, int nPixelBits, byte *q)
  {
    switch (nPixelBits)
    {
    case 32: q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = p[3]; break;
    case 24: q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255; break;
    default:
      {
        const uint32 v = Read16(p);
        q[0] = Expand5(v & 31);
        q[1] = Expand5((v >> 5) & 31);
        q[2] = Expand5((v >> 10) & 31);
        q[3] = (nPixelBits == 16 && nAlphaBits) ? ((v & 0x8000) ? 255 : 0) : 255;
      }
      break;
    }
  };

  size_t nPos = 18 + (size_t)nIDLength;
  std::vector<byte> Palette;          // B, G, R, A per index
  bool bAlpha = false;
  if (nColourMapType == 1)
  {
    if (nMapBits != 15 && nMapBits != 16 && nMapBits != 24 && nMapBits != 32)
      return false;
    const size_t nEntryBytes = (size_t)(nMapBits + 7) / 8;
    if (nPos + nEntryBytes * nMapLength > nFileSize)
      return false;
    if (nKind == 1)
    {
      Palette.assign((size_t)(nMapFirst + nMapLength) * 4, 0);
      for (int i = 0; i < nMapLength; i++)
        DecodeTrue(d + nPos + i * nEntryBytes, nMapBits, &Palette[(size_t)(nMapFirst + i) * 4]);
      bAlpha = (nMapBits == 32) || (nMapBits == 16 && nAlphaBits);
    }
    nPos += nEntryBytes * nMapLength;
  }
  else if (nKind == 1)
    return false;

  switch (nKind)
  {
  case 1: if (nBits != 8 && nBits != 16) return false; break;
  case 2: if (nBits != 15 && nBits != 16 && nBits != 24 && nBits != 32) return false;
    bAlpha = (nBits == 32) || (nBits == 16 && nAlphaBits);
    break;
  case 3: if (nBits != 8 && nBits != 16) return false;
    bAlpha = (nBits == 16);
    break;
  }
  const size_t nPixelBytes = (size_t)(nBits + 7) / 8;

  auto Decode = [&](const byte *p, byte *q)
  {
    if (nKind == 2)
      DecodeTrue(p, nBits, q);
    else if (nKind == 3)
    {
      q[0] = q[1] = q[2] = p[0];
      q[3] = (nBits == 16) ? p[1] : 255;
    }
    else
    {
      const size_t nIndex = (nBits == 16) ? Read16(p) : p[0];
      if (nIndex * 4 + 4 <= Palette.size())
        memcpy(q, &Palette[nIndex * 4], 4);
      else
        q[0] = q[1] = q[2] = 0, q[3] = 255;
    }
  };

  // The pixels in the order of the file.
  const size_t nCount = (size_t)nWidth * nHeight;
  std::vector<byte> Pixels(nCount * 4, 0);
  if (!bRLE)
  {
    if (nPos + nCount * nPixelBytes > nFileSize)
      return false;
    for (size_t i = 0; i < nCount; i++)
      Decode(d + nPos + i * nPixelBytes, &Pixels[i * 4]);
  }
  else
  {
    size_t i = 0;
    while (i < nCount && nPos < nFileSize)
    {
      const byte nHead = d[nPos++];
      const size_t nRun = (size_t)(nHead & 0x7f) + 1;
      if (nHead & 0x80)
      {
        if (nPos + nPixelBytes > nFileSize)
          break;
        byte Pixel[4];
        Decode(d + nPos, Pixel);
        nPos += nPixelBytes;
        for (size_t k = 0; k < nRun && i < nCount; k++, i++)
          memcpy(&Pixels[i * 4], Pixel, 4);
      }
      else
      {
        for (size_t k = 0; k < nRun && i < nCount && nPos + nPixelBytes <= nFileSize; k++, i++, nPos += nPixelBytes)
          Decode(d + nPos, &Pixels[i * 4]);
      }
    }
  }

  // Rows from the top: bit 5 of the descriptor says the file starts with the top row, bit 4
  // that rows run from right to left.
  const bool bTopFirst = (nDesc & 0x20) != 0, bRightFirst = (nDesc & 0x10) != 0;
  Out.Allocate(FMT_BGRA, nWidth, nHeight, 1, 1);
  Out.nSrcFlags = bAlpha ? FT_HASALPHA : 0;
  for (int y = 0; y < nHeight; y++)
  {
    const byte *pRow = &Pixels[(size_t)(bTopFirst ? y : nHeight - 1 - y) * nWidth * 4];
    byte *pDst = Out.Bits(0, 0) + (size_t)y * nWidth * 4;
    if (!bRightFirst)
      memcpy(pDst, pRow, (size_t)nWidth * 4);
    else
      for (int x = 0; x < nWidth; x++)
        memcpy(pDst + x * 4, pRow + (size_t)(nWidth - 1 - x) * 4, 4);
  }
  return true;
}

bool Img::LoadWIC(std::span<const byte> File, SImage &Out)
{
  if (File.empty() || File.size() > 0xffffffffu)
    return false;
  // COM may be up already on this thread in another mode (RPC_E_CHANGED_MODE): then it is used
  // as it is; else it is started for this call only.
  const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool bOk = false;
  {
    ComPtr<IWICImagingFactory> pFactory;
    ComPtr<IWICStream> pStream;
    ComPtr<IWICBitmapDecoder> pDecoder;
    ComPtr<IWICBitmapFrameDecode> pFrame;
    ComPtr<IWICFormatConverter> pConverter;
    UINT nWidth = 0, nHeight = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))) &&
      SUCCEEDED(pFactory->CreateStream(&pStream)) &&
      SUCCEEDED(pStream->InitializeFromMemory(const_cast<BYTE *>(File.data()), (DWORD)File.size())) &&
      SUCCEEDED(pFactory->CreateDecoderFromStream(pStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder)) &&
      SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) &&
      SUCCEEDED(pFrame->GetSize(&nWidth, &nHeight)) &&
      nWidth > 0 && nHeight > 0 && nWidth <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION && nHeight <= D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION &&
      SUCCEEDED(pFactory->CreateFormatConverter(&pConverter)) &&
      SUCCEEDED(pConverter->Initialize(pFrame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
      Out.Allocate(FMT_BGRA, (int)nWidth, (int)nHeight, 1, 1);
      bOk = SUCCEEDED(pConverter->CopyPixels(nullptr, nWidth * 4, (UINT)Out.Data.size(), Out.Data.data()));
    }
  }
  if (SUCCEEDED(hrInit))
    CoUninitialize();
  if (!bOk)
    Out = SImage();
  return bOk;
}

void Img::GenerateMips(SImage &Image)
{
  if (Image.Format != FMT_BGRA || Image.nFaces < 1 || Image.nMips < 1)
    return;
  SImage Out;
  Out.Allocate(FMT_BGRA, Image.nWidth, Image.nHeight, FullMipCount(Image.nWidth, Image.nHeight), Image.nFaces);
  Out.nSrcFlags = Image.nSrcFlags;
  for (int nFace = 0; nFace < Out.nFaces; nFace++)
  {
    memcpy(Out.Bits(nFace, 0), Image.Bits(nFace, 0), Out.MipSize(0));
    for (int nMip = 1; nMip < Out.nMips; nMip++)
    {
      const byte *pSrc = Out.Bits(nFace, nMip - 1);
      byte *pDst = Out.Bits(nFace, nMip);
      const int nSrcW = Out.MipWidth(nMip - 1), nSrcH = Out.MipHeight(nMip - 1);
      const int nDstW = Out.MipWidth(nMip), nDstH = Out.MipHeight(nMip);
      for (int y = 0; y < nDstH; y++)
      {
        const byte *pRow0 = pSrc + (size_t)std::min(2 * y, nSrcH - 1) * nSrcW * 4;
        const byte *pRow1 = pSrc + (size_t)std::min(2 * y + 1, nSrcH - 1) * nSrcW * 4;
        for (int x = 0; x < nDstW; x++, pDst += 4)
        {
          const int x0 = std::min(2 * x, nSrcW - 1) * 4, x1 = std::min(2 * x + 1, nSrcW - 1) * 4;
          for (int c = 0; c < 4; c++)
            pDst[c] = (byte)((pRow0[x0 + c] + pRow0[x1 + c] + pRow1[x0 + c] + pRow1[x1 + c] + 2) >> 2);
        }
      }
    }
  }
  Image = std::move(Out);
}

bool Img::Decompress(SImage &Image)
{
  if (Image.Format == FMT_BGRA)
    return true;
  if (!IsBC(Image.Format))
    return false;
  SImage Out;
  Out.Allocate(FMT_BGRA, Image.nWidth, Image.nHeight, Image.nMips, Image.nFaces);
  Out.nSrcFlags = Image.nSrcFlags;
  for (int nFace = 0; nFace < Out.nFaces; nFace++)
    for (int nMip = 0; nMip < Out.nMips; nMip++)
      DecodeBC(Image.Bits(nFace, nMip), Image.Format, Out.MipWidth(nMip), Out.MipHeight(nMip), Out.Bits(nFace, nMip), Out.MipWidth(nMip) * 4, 4);
  Image = std::move(Out);
  return true;
}

bool Img::DecodeBC(const byte *pSrc, DXGI_FORMAT Format, int nWidth, int nHeight, byte *pDst, int nDstPitch, int nDstBytes)
{
  if (!IsBC(Format) || !pSrc || !pDst || nWidth <= 0 || nHeight <= 0 || (nDstBytes != 3 && nDstBytes != 4))
    return false;
  const int nBlocksX = std::max(1, (nWidth + 3) / 4), nBlocksY = std::max(1, (nHeight + 3) / 4);
  const uint32 nBlockBytes = BlockBytes(Format);
  for (int by = 0; by < nBlocksY; by++)
    for (int bx = 0; bx < nBlocksX; bx++)
    {
      const byte *p = pSrc + ((size_t)by * nBlocksX + bx) * nBlockBytes;
      byte Texels[16][4];
      if (Format == DXGI_FORMAT_BC1_UNORM)
        DecodeColourBlock(p, true, Texels);
      else if (Format == DXGI_FORMAT_BC2_UNORM)
      {
        // 4 bits of alpha per texel, texel 0 in the low bits.
        DecodeColourBlock(p + 8, false, Texels);
        for (int i = 0; i < 16; i++)
          Texels[i][3] = Expand4((p[i / 2] >> ((i & 1) * 4)) & 15);
      }
      else
      {
        // Two alpha end points and 3-bit indices.
        DecodeColourBlock(p + 8, false, Texels);
        const int a0 = p[0], a1 = p[1];
        byte Alpha[8];
        Alpha[0] = (byte)a0;
        Alpha[1] = (byte)a1;
        if (a0 > a1)
          for (int i = 1; i <= 6; i++)
            Alpha[i + 1] = (byte)(((7 - i) * a0 + i * a1) / 7);
        else
        {
          for (int i = 1; i <= 4; i++)
            Alpha[i + 1] = (byte)(((5 - i) * a0 + i * a1) / 5);
          Alpha[6] = 0;
          Alpha[7] = 255;
        }
        uint64 nBits = 0;
        for (int i = 0; i < 6; i++)
          nBits |= (uint64)p[2 + i] << (8 * i);
        for (int i = 0; i < 16; i++)
          Texels[i][3] = Alpha[(nBits >> (3 * i)) & 7];
      }
      for (int ty = 0; ty < 4; ty++)
      {
        const int y = by * 4 + ty;
        if (y >= nHeight)
          break;
        for (int tx = 0; tx < 4; tx++)
        {
          const int x = bx * 4 + tx;
          if (x >= nWidth)
            break;
          memcpy(pDst + (size_t)y * nDstPitch + (size_t)x * nDstBytes, Texels[ty * 4 + tx], nDstBytes);
        }
      }
    }
  return true;
}

int Img::BytesPerPixel(ETEX_Format eTF)
{
  switch (eTF)
  {
  case eTF_8888:
  case eTF_0888:
  case eTF_RGBA:
    return 4;
  case eTF_RGB8:
    return 3;
  case eTF_0565:
  case eTF_0555:
  case eTF_4444:
  case eTF_1555:
  case eTF_0088:
    return 2;
  case eTF_8000:
    return 1;
  default:
    return 0;
  }
}

bool Img::ConvertToBGRA(const byte *pSrc, ETEX_Format eTF, int nWidth, int nHeight, byte *pDst)
{
  const int nBytes = BytesPerPixel(eTF);
  if (!nBytes || !pSrc || !pDst)
    return false;
  const size_t nCount = (size_t)nWidth * nHeight;
  if (eTF == eTF_8888)
  {
    memcpy(pDst, pSrc, nCount * 4);
    return true;
  }
  for (size_t i = 0; i < nCount; i++, pSrc += nBytes, pDst += 4)
  {
    switch (eTF)
    {
    case eTF_0888: pDst[0] = pSrc[0]; pDst[1] = pSrc[1]; pDst[2] = pSrc[2]; pDst[3] = 255; break;
    case eTF_RGBA: pDst[0] = pSrc[2]; pDst[1] = pSrc[1]; pDst[2] = pSrc[0]; pDst[3] = pSrc[3]; break;
    case eTF_RGB8: pDst[0] = pSrc[0]; pDst[1] = pSrc[1]; pDst[2] = pSrc[2]; pDst[3] = 255; break;
    case eTF_8000: pDst[0] = pDst[1] = pDst[2] = 0; pDst[3] = pSrc[0]; break;
    case eTF_0088: pDst[0] = pDst[1] = pDst[2] = pSrc[0]; pDst[3] = pSrc[1]; break;
    default:
      {
        const uint32 v = Read16(pSrc);
        switch (eTF)
        {
        case eTF_0565:
          pDst[0] = Expand5(v & 31); pDst[1] = Expand6((v >> 5) & 63); pDst[2] = Expand5((v >> 11) & 31); pDst[3] = 255;
          break;
        case eTF_4444:
          pDst[0] = Expand4(v & 15); pDst[1] = Expand4((v >> 4) & 15); pDst[2] = Expand4((v >> 8) & 15); pDst[3] = Expand4((v >> 12) & 15);
          break;
        default:                      // eTF_0555, eTF_1555
          pDst[0] = Expand5(v & 31); pDst[1] = Expand5((v >> 5) & 31); pDst[2] = Expand5((v >> 10) & 31);
          pDst[3] = (eTF == eTF_1555 && !(v & 0x8000)) ? 0 : 255;
          break;
        }
      }
      break;
    }
  }
  return true;
}
