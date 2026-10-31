// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Texture.cpp
//	Description: Textures (ITexPic) and the texture manager.
//
//	How a texture name becomes a file: back slashes become slashes, the
//	name becomes lower case and its extension (from the last dot of the
//	file name on) is replaced by ".dds", ".tga" and ".jpg" in turn; the
//	first file that opens is the texture's (a file that opens but cannot
//	be read gives the placeholder). A name without a folder is tried as
//	"textures/<name>", then as "<name>"; a name in another folder as
//	"<name>", then as "textures/<name>"; a name in "textures/" only as it
//	is. Every file is read through ICryPak within the call that names it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "Texture.h"
#include "Shaders/Constants.h"

namespace
{
  //! The faces of a cube map made of six files, in the order of the cube's faces.
  constexpr const char *g_CubeFaces[6] = { "_posx", "_negx", "_posy", "_negy", "_posz", "_negz" };
  //! The extensions a texture name is tried with, in this order.
  constexpr const char *g_Extensions[3] = { ".dds", ".tga", ".jpg" };
  //! The key suffix of cube maps: the same file can be a 2D texture and a cube.
  constexpr const char *g_CubeKey = "#cube";

  //! Lower case, '/' for '\'.
  std::string NormalizePath(const char *szPath)
  {
    std::string s = szPath ? szPath : "";
    for (char &c : s)
      c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    return s;
  }

  //! Without the extension: from the last dot of the file name on.
  std::string StripExtension(const std::string &s)
  {
    const size_t nSlash = s.find_last_of('/');
    const size_t nDot = s.find_last_of('.');
    if (nDot != std::string::npos && (nSlash == std::string::npos || nDot > nSlash))
      return s.substr(0, nDot);
    return s;
  }

  std::string FileNamePart(const std::string &s)
  {
    const size_t nSlash = s.find_last_of('/');
    return nSlash == std::string::npos ? s : s.substr(nSlash + 1);
  }

  bool StartsWith(const std::string &s, std::string_view Prefix)
  {
    return s.size() >= Prefix.size() && s.compare(0, Prefix.size(), Prefix) == 0;
  }

  bool EndsWith(const std::string &s, std::string_view Suffix)
  {
    return s.size() >= Suffix.size() && s.compare(s.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
  }

  void AddUnique(std::vector<std::string> &List, std::string s)
  {
    if (std::find(List.begin(), List.end(), s) == List.end())
      List.push_back(std::move(s));
  }

  //! The base names (without extension) a texture name is searched as, in order: the ones of
  //! EF_LoadTexture first (their count is returned), then those in the material's folder Dir
  //! ("" or a folder ending in '/').
  size_t Candidates(const std::string &Base, const std::string &Dir, std::vector<std::string> &Out)
  {
    Out.clear();
    if (Base.empty())
      return 0;
    if (StartsWith(Base, "textures/"))
      AddUnique(Out, Base);
    else if (Base.find('/') == std::string::npos)
    {
      AddUnique(Out, "textures/" + Base);
      AddUnique(Out, Base);
    }
    else
    {
      AddUnique(Out, Base);
      AddUnique(Out, "textures/" + Base);
    }
    const size_t nPlain = Out.size();
    if (!Dir.empty())
    {
      AddUnique(Out, Dir + Base);
      AddUnique(Out, Dir + FileNamePart(Base));
    }
    return nPlain;
  }

  //! Fills one face with transparent black (BC1: 3-colour blocks of index 3).
  void ClearFace(SImage &Image, int nFace)
  {
    for (int nMip = 0; nMip < Image.nMips; nMip++)
    {
      byte *p = Image.Bits(nFace, nMip);
      const size_t nSize = Image.MipSize(nMip);
      memset(p, 0, nSize);
      if (Image.Format == DXGI_FORMAT_BC1_UNORM)
        for (size_t i = 0; i + 8 <= nSize; i += 8)
          memset(p + i + 4, 0xff, 4);
    }
  }

  //! Keeps the first nMips levels of every face.
  void KeepLevels(SImage &Image, int nMips)
  {
    if (nMips >= Image.nMips)
      return;
    SImage Out;
    Out.Allocate(Image.Format, Image.nWidth, Image.nHeight, nMips, Image.nFaces);
    Out.nSrcFlags = Image.nSrcFlags;
    for (int nFace = 0; nFace < Image.nFaces; nFace++)
      for (int nMip = 0; nMip < nMips; nMip++)
        memcpy(Out.Bits(nFace, nMip), Image.Bits(nFace, nMip), Image.MipSize(nMip));
    Image = std::move(Out);
  }

  //! A height map loaded as a bump map, B8G8R8A8: level 0 becomes a normal map with the height
  //! (the mean of r, g and b) in alpha; the slopes are 4 times the height's forward differences
  //! (along x the mean of rows y and y + 1), wrapping at the edges, n = normalize(-sx, -sy, 1).
  //! The levels below are made again from it.
  void HeightToNormalMap(SImage &Image)
  {
    const int w = Image.nWidth, h = Image.nHeight;
    const byte *pSrc = Image.Bits(0, 0);
    std::vector<int> Heights((size_t)w * h);
    for (size_t i = 0; i < Heights.size(); i++)
      Heights[i] = (pSrc[i * 4] + pSrc[i * 4 + 1] + pSrc[i * 4 + 2]) / 3;
    auto At = [&](int x, int y) { return Heights[(size_t)(y % h) * w + x % w] / 255.0f; };
    auto Encode = [](float v) { return (byte)std::clamp(std::lround(v * 127.5f + 127.5f), 0L, 255L); };

    SImage Out;
    Out.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, w, h, 1, 1);
    Out.nSrcFlags = FT_HASALPHA;
    byte *pDst = Out.Bits(0, 0);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        const float sx = 4 * 0.5f * ((At(x + 1, y) - At(x, y)) + (At(x + 1, y + 1) - At(x, y + 1)));
        const float sy = 4 * (At(x, y + 1) - At(x, y));
        const float fInvLen = 1 / sqrtf(sx * sx + sy * sy + 1);
        byte *p = pDst + ((size_t)y * w + x) * 4;
        p[0] = Encode(fInvLen);
        p[1] = Encode(-sy * fInvLen);
        p[2] = Encode(-sx * fInvLen);
        p[3] = (byte)Heights[(size_t)y * w + x];
      }
    Img::GenerateMips(Out);
    Image = std::move(Out);
  }

  //! Drops the nDrop largest levels (r_TexResolution and friends): the smallest level kept is at
  //! least 4 x 4 for block compressed formats, and one level always stays.
  void DropLevels(SImage &Image, int nDrop)
  {
    const int nMinSize = Img::IsBC(Image.Format) ? 4 : 1;
    while (nDrop > 0 && Image.nMips > 1 && Image.MipWidth(1) >= nMinSize && Image.MipHeight(1) >= nMinSize)
    {
      SImage Out;
      Out.Allocate(Image.Format, Image.MipWidth(1), Image.MipHeight(1), Image.nMips - 1, Image.nFaces);
      Out.nSrcFlags = Image.nSrcFlags;
      for (int nFace = 0; nFace < Image.nFaces; nFace++)
        for (int nMip = 0; nMip < Out.nMips; nMip++)
          memcpy(Out.Bits(nFace, nMip), Image.Bits(nFace, nMip + 1), Out.MipSize(nMip));
      Image = std::move(Out);
      nDrop--;
    }
  }

  int GetCVarInt(const char *szName, int nDefault)
  {
    ICVar *pVar = iConsole ? iConsole->GetCVar(szName) : nullptr;
    return pVar ? pVar->GetIVal() : nDefault;
  }

  //! A cube from six pictures (an empty one for a missing face: transparent black). The faces
  //! must be square and of one size; differing formats are decompressed to B8G8R8A8.
  bool AssembleCube(SImage (&Faces)[6], SImage &Out)
  {
    if (Faces[0].nFaces == 6)
    {
      Out = std::move(Faces[0]);
      return true;
    }
    const int nSize = Faces[0].nWidth;
    if (Faces[0].nFaces != 1 || nSize != Faces[0].nHeight)
      return false;
    bool bSameFormat = true;
    for (const SImage &Face : Faces)
      if (Face.nFaces && Face.Format != Faces[0].Format)
        bSameFormat = false;
    if (!bSameFormat)
      for (SImage &Face : Faces)
        if (Face.nFaces && !Img::Decompress(Face))
          Face = SImage();
    int nMips = Faces[0].nMips;
    for (SImage &Face : Faces)
    {
      if (Face.nFaces != 1 || Face.nWidth != nSize || Face.nHeight != nSize || Face.Format != Faces[0].Format)
        Face = SImage();
      else
        nMips = std::min(nMips, Face.nMips);
    }
    if (!Faces[0].nFaces)
      return false;
    Out.Allocate(Faces[0].Format, nSize, nSize, nMips, 6);
    Out.nSrcFlags = Faces[0].nSrcFlags;
    for (int nFace = 0; nFace < 6; nFace++)
    {
      if (!Faces[nFace].nFaces)
      {
        ClearFace(Out, nFace);
        continue;
      }
      for (int nMip = 0; nMip < nMips; nMip++)
        memcpy(Out.Bits(nFace, nMip), Faces[nFace].Bits(0, nMip), Out.MipSize(nMip));
    }
    return true;
  }

  //! A cube with the picture on +X and the other five faces transparent black.
  bool SingleFaceCube(const SImage &Picture, SImage &Out)
  {
    if (Picture.nFaces != 1 || Picture.nWidth != Picture.nHeight)
      return false;
    Out.Allocate(Picture.Format, Picture.nWidth, Picture.nHeight, Picture.nMips, 6);
    Out.nSrcFlags = Picture.nSrcFlags;
    memcpy(Out.Bits(0, 0), Picture.Data.data(), Picture.FaceSize());
    for (int nFace = 1; nFace < 6; nFace++)
      ClearFace(Out, nFace);
    return true;
  }

  //! The picture of DownLoadToVideoMemory: BC blocks for the DXT formats (decompressed when the
  //! size is not a whole number of blocks), else B8G8R8A8. pData NULL: zeros. False for a source
  //! format that is not known (Out is then black).
  bool BuildImage(const byte *pData, int nWidth, int nHeight, ETEX_Format eSrc, SImage &Out)
  {
    DXGI_FORMAT BC = DXGI_FORMAT_UNKNOWN;
    int nDXT = 0;
    switch (eSrc)
    {
    case eTF_DXT1: BC = DXGI_FORMAT_BC1_UNORM; nDXT = FT_DXT1; break;
    case eTF_DXT3: BC = DXGI_FORMAT_BC2_UNORM; nDXT = FT_DXT3; break;
    case eTF_DXT5: BC = DXGI_FORMAT_BC3_UNORM; nDXT = FT_DXT5; break;
    default: break;
    }
    if (BC != DXGI_FORMAT_UNKNOWN)
    {
      Out.Allocate(BC, nWidth, nHeight, 1, 1);
      Out.nSrcFlags = nDXT;
      if (pData)
        memcpy(Out.Data.data(), pData, Out.MipSize(0));
      if ((nWidth & 3) || (nHeight & 3))
        Img::Decompress(Out);
      return true;
    }
    Out.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, nWidth, nHeight, 1, 1);
    const int nBytes = Img::BytesPerPixel(eSrc);
    if (!nBytes)
      return false;
    if (eSrc != eTF_0888 && eSrc != eTF_0565 && eSrc != eTF_0555 && eSrc != eTF_RGB8)
      Out.nSrcFlags = FT_HASALPHA;
    if (pData)
      return Img::ConvertToBGRA(pData, eSrc, nWidth, nHeight, Out.Data.data());
    std::vector<byte> Zeros((size_t)nWidth * nHeight * nBytes, 0);
    return Img::ConvertToBGRA(Zeros.data(), eSrc, nWidth, nHeight, Out.Data.data());
  }

  //! The file name of frame nFrame of an animated texture: sprintf(szFormat, nFrame) for a format
  //! with one integer conversion, else the digits in front of the extension count up. False when
  //! there is no such frame (a format without digits has frame 0 only) or the format has other
  //! conversions.
  bool AnimFrameName(const char *szFormat, int nFrame, std::string &Out)
  {
    Out.clear();
    if (strchr(szFormat, '%'))
    {
      bool bConverted = false;
      for (const char *p = szFormat; *p; )
      {
        if (*p != '%')
        {
          Out += *p++;
          continue;
        }
        if (p[1] == '%')
        {
          Out += '%';
          p += 2;
          continue;
        }
        if (bConverted)
          return false;
        std::string Spec = "%";
        p++;
        while (*p && strchr("-+ #0", *p))
          Spec += *p++;
        int nWidth = 0;
        while (isdigit((unsigned char)*p))
        {
          nWidth = nWidth * 10 + (*p - '0');
          Spec += *p++;
        }
        if (*p == '.')
        {
          Spec += *p++;
          while (isdigit((unsigned char)*p))
            Spec += *p++;
        }
        while (*p == 'h' || *p == 'l')
          p++;
        if (!*p || !strchr("diuxX", *p) || nWidth > 32)
          return false;
        Spec += *p++;
        char szNumber[64];
        snprintf(szNumber, sizeof(szNumber), Spec.c_str(), nFrame);
        Out += szNumber;
        bConverted = true;
      }
      return true;
    }
    Out = szFormat;
    const size_t nSlash = Out.find_last_of("/\\");
    const size_t nFirst = (nSlash == std::string::npos) ? 0 : nSlash + 1;
    size_t nDot = Out.find_last_of('.');
    if (nDot == std::string::npos || nDot < nFirst)
      nDot = Out.size();
    size_t nStart = nDot;
    while (nStart > nFirst && isdigit((unsigned char)Out[nStart - 1]))
      nStart--;
    if (nStart == nDot)
      return nFrame == 0;
    char szNumber[32];
    snprintf(szNumber, sizeof(szNumber), "%0*d", (int)(nDot - nStart), nFrame);
    Out.replace(nStart, nDot - nStart, szNumber);
    return true;
  }
}

//////////////////////////////////////////////////////////////////////////
// CTexture

CTexture::CTexture(int nID, const char *szName)
  : m_Name(szName ? szName : "")
  , m_nID(nID)
{
}

void CTexture::AddRef()
{
  if (!m_bRemoved)
    m_nRefs++;
}

void CTexture::Release(int bForce)
{
  if (g_pRenderer && g_pRenderer->m_pTextures)
    g_pRenderer->m_pTextures->ReleaseTexture(this, bForce != 0);
}

const char *CTexture::GetName()
{
  return m_Name.c_str();
}

int CTexture::GetWidth()
{
  return m_nWidth;
}

int CTexture::GetHeight()
{
  return m_nHeight;
}

int CTexture::GetOriginalWidth()
{
  return m_nOrigWidth;
}

int CTexture::GetOriginalHeight()
{
  return m_nOrigHeight;
}

int CTexture::GetTextureID()
{
  return m_nID;
}

int CTexture::GetFlags()
{
  return m_nFlags;
}

int CTexture::GetFlags2()
{
  return m_nFlags2;
}

void CTexture::SetClamp(bool bEnable)
{
  m_bClamp = bEnable;
}

bool CTexture::IsTextureLoaded()
{
  return m_bLoaded && !m_bRemoved;
}

void CTexture::PrecacheAsynchronously(float fDist, int Flags)
{
}

void CTexture::Preload(int Flags)
{
}

byte *CTexture::GetData32()
{
  // The pixels are not kept in system memory.
  return nullptr;
}

bool CTexture::SetFilter(int nFilter)
{
  m_bPoint = (nFilter == FILTER_NONE);
  return true;
}

int CTexture::GetSRV() const
{
  return (m_pGPU && m_pGPU->m_nSRV >= 0) ? m_pGPU->m_nSRV : D3D12_SRV_WHITE;
}

int CTexture::GetSampler() const
{
  if (m_bPoint)
    return m_bClamp ? XR_SAMPLER_POINT_CLAMP : XR_SAMPLER_POINT_WRAP;
  return m_bClamp ? XR_SAMPLER_LINEAR_CLAMP : XR_SAMPLER_LINEAR_WRAP;
}

bool CTexture::IsCube() const
{
  return m_bCube && m_pGPU;
}

//////////////////////////////////////////////////////////////////////////
// CTextureManager

CTextureManager::CTextureManager() = default;

CTextureManager::~CTextureManager()
{
  Shutdown();
}

bool CTextureManager::Init()
{
  // The placeholder first: it gets ID 4096.
  if (!m_pPlaceholder)
  {
    m_pPlaceholder = NewTexture("$Placeholder");
    m_pPlaceholder->m_bPermanent = true;
    m_pPlaceholder->m_nFlags = FT_NOTFOUND | FT_DXT1 | FT_NOSTREAM | FT_HASMIPS | FT_NORESIZE;
    m_pPlaceholder->m_nFlags2 = FT2_DIFFUSETEXTURE;
  }
  if (!m_pWhite)
  {
    m_pWhite = NewTexture("$White");
    m_pWhite->m_bPermanent = true;
    m_pWhite->m_nFlags = FT_NOMIPS | FT_NOREMOVE;
    m_pWhite->m_nFlags2 = FT2_WASLOADED;
  }

  // A red and white checker (IsTextureLoaded stays false).
  if (!m_pPlaceholder->m_pGPU)
  {
    SImage Image;
    Image.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, 128, 128, 1, 1);
    byte *p = Image.Data.data();
    for (int y = 0; y < 128; y++)
      for (int x = 0; x < 128; x++, p += 4)
      {
        const bool bRed = ((x >> 4) ^ (y >> 4)) & 1;
        p[0] = bRed ? 0 : 255;
        p[1] = bRed ? 0 : 255;
        p[2] = 255;
        p[3] = 255;
      }
    Img::GenerateMips(Image);
    Upload(m_pPlaceholder, Image);
  }

  if (!m_pWhite->m_pGPU)
  {
    SImage Image;
    Image.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, 1, 1, 1, 1);
    memset(Image.Data.data(), 255, Image.Data.size());
    if (Upload(m_pWhite, Image) && g_pRenderer && g_pRenderer->m_pDescriptors)
      g_pRenderer->m_pDescriptors->WriteSRV(D3D12_SRV_WHITE, m_pWhite->m_pGPU->m_pResource, nullptr);
    m_pWhite->m_bLoaded = (m_pWhite->m_pGPU != nullptr);
  }

  m_bInit = m_pPlaceholder->m_pGPU && m_pWhite->m_pGPU;
  return m_bInit;
}

void CTextureManager::Shutdown()
{
  m_AnimTex.clear();
  CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
  for (std::unique_ptr<CTexture> &pTex : m_ByID)
    if (pTex && pTex->m_pGPU)
    {
      if (pUpload)
        pUpload->ReleaseTexture(pTex->m_pGPU);
      pTex->m_pGPU = nullptr;
    }
  m_ByID.clear();
  m_FreeIDs.clear();
  m_Removed.clear();
  m_ByName.clear();
  m_Missing.clear();
  m_Warned.clear();
  m_WarnedFormats.clear();
  m_pWhite = nullptr;
  m_pPlaceholder = nullptr;
  m_bInit = false;
}

CTexture *CTextureManager::GetByID(int nID)
{
  const int nIndex = nID - TEXTURE_FIRST_ID;
  if (nIndex < 0 || nIndex >= (int)m_ByID.size())
    return nullptr;
  return m_ByID[nIndex].get();
}

int CTextureManager::GetSRV(int nID)
{
  CTexture *pTex = GetByID(nID);
  return pTex ? pTex->GetSRV() : D3D12_SRV_WHITE;
}

CTexture *CTextureManager::GetWhite()
{
  if (!m_pWhite)
    Init();
  return m_pWhite;
}

CTexture *CTextureManager::GetPlaceholder()
{
  if (!m_pPlaceholder)
    Init();
  return m_pPlaceholder;
}

CTexture *CTextureManager::NewTexture(const char *szName, int nID)
{
  int nIndex = -1;
  if (nID >= TEXTURE_FIRST_ID && nID <= TEXTURE_MAX_ID)
  {
    const int nWanted = nID - TEXTURE_FIRST_ID;
    if (nWanted >= (int)m_ByID.size())
    {
      // The IDs in between become free ones.
      for (int i = (int)m_ByID.size(); i < nWanted; i++)
        m_FreeIDs.push_back(TEXTURE_FIRST_ID + i);
      m_ByID.resize(nWanted + 1);
      nIndex = nWanted;
    }
    else if (!m_ByID[nWanted])
    {
      m_FreeIDs.erase(std::remove(m_FreeIDs.begin(), m_FreeIDs.end(), nID), m_FreeIDs.end());
      nIndex = nWanted;
    }
  }
  if (nIndex < 0)
  {
    if (!m_FreeIDs.empty())
    {
      nIndex = m_FreeIDs.back() - TEXTURE_FIRST_ID;
      m_FreeIDs.pop_back();
    }
    else
    {
      if ((int)m_ByID.size() > TEXTURE_MAX_ID - TEXTURE_FIRST_ID)
      {
        if (iLog)
          iLog->LogWarning("Texture '%s': no texture ID left", szName ? szName : "");
        return nullptr;
      }
      nIndex = (int)m_ByID.size();
      m_ByID.emplace_back();
    }
  }
  m_ByID[nIndex] = std::make_unique<CTexture>(TEXTURE_FIRST_ID + nIndex, szName);
  return m_ByID[nIndex].get();
}

void CTextureManager::Remove(CTexture *pTex)
{
  if (!pTex || pTex->m_bRemoved || pTex->m_bPermanent)
    return;
  for (const std::string &Key : pTex->m_Keys)
  {
    auto It = m_ByName.find(Key);
    if (It != m_ByName.end() && It->second == pTex)
      m_ByName.erase(It);
  }
  pTex->m_Keys.clear();
  if (pTex->m_pGPU)
  {
    if (g_pRenderer && g_pRenderer->m_pUpload)
      g_pRenderer->m_pUpload->ReleaseTexture(pTex->m_pGPU);
    pTex->m_pGPU = nullptr;
  }
  pTex->m_bRemoved = true;
  pTex->m_bLoaded = false;
  pTex->m_nRefs = 0;
  const int nIndex = pTex->m_nID - TEXTURE_FIRST_ID;
  if (nIndex >= 0 && nIndex < (int)m_ByID.size() && m_ByID[nIndex].get() == pTex)
  {
    m_Removed.push_back(std::move(m_ByID[nIndex]));
    m_FreeIDs.push_back(pTex->m_nID);
  }
}

bool CTextureManager::Upload(CTexture *pTex, const SImage &Image)
{
  pTex->m_nWidth = pTex->m_nOrigWidth = Image.nWidth;
  pTex->m_nHeight = pTex->m_nOrigHeight = Image.nHeight;
  pTex->m_bCube = (Image.nFaces == 6 && !Image.bArray);
  pTex->m_bArray = Image.bArray;
  CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
  if (!pUpload || Image.Data.empty() || Image.nMips < 1)
    return false;
  std::vector<SD3D12SubData> Sub((size_t)Image.nFaces * Image.nMips);
  for (int nFace = 0; nFace < Image.nFaces; nFace++)
    for (int nMip = 0; nMip < Image.nMips; nMip++)
    {
      SD3D12SubData &S = Sub[(size_t)nFace * Image.nMips + nMip];
      S.pData = Image.Bits(nFace, nMip);
      S.nPitch = Img::RowPitch(Image.Format, Image.MipWidth(nMip));
    }
  CD3D12Texture *pGPU = pUpload->CreateTexture(Image.nWidth, Image.nHeight, Image.nMips, Image.Format, pTex->m_bCube, Sub.data(),
                                               Image.bArray ? Image.nFaces : 1);
  if (!pGPU)
    return false;
  // The engine's name, for DRED's page faults and the debug layer.
  if (g_pRenderer->m_pDevice->NamesObjects() && !pTex->m_Name.empty())
  {
    wchar_t szName[MAX_PATH];
    if (MultiByteToWideChar(CP_ACP, 0, pTex->m_Name.c_str(), -1, szName, MAX_PATH) > 0)
      pGPU->m_pResource->SetName(szName);
  }
  if (pTex->m_pGPU)
    pUpload->ReleaseTexture(pTex->m_pGPU);
  pTex->m_pGPU = pGPU;
  return true;
}

void CTextureManager::AddKey(CTexture *pTex, const std::string &Key)
{
  m_ByName[Key] = pTex;
  if (std::find(pTex->m_Keys.begin(), pTex->m_Keys.end(), Key) == pTex->m_Keys.end())
    pTex->m_Keys.push_back(Key);
}

CTexture *CTextureManager::FindKey(const std::string &Key)
{
  auto It = m_ByName.find(Key);
  return It == m_ByName.end() ? nullptr : It->second;
}

bool CTextureManager::ReadFile(const char *szPath, std::vector<byte> &Data)
{
  ICryPak *pPak = iSystem ? iSystem->GetIPak() : nullptr;
  if (!pPak)
    return false;
  FILE *f = pPak->FOpen(szPath, "rb");
  if (!f)
    return false;
  Data.clear();
  if (pPak->FSeek(f, 0, SEEK_END) == 0)
  {
    const long nSize = pPak->FTell(f);
    if (nSize > 0 && pPak->FSeek(f, 0, SEEK_SET) == 0)
    {
      Data.resize((size_t)nSize);
      const size_t nRead = pPak->FRead(Data.data(), 1, (size_t)nSize, f);
      Data.resize(nRead);
    }
  }
  pPak->FClose(f);
  return true;
}

bool CTextureManager::Search(const std::vector<std::string> &Bases, size_t nPlain, SImage &Out, SSearch &Res)
{
  std::vector<byte> File;
  for (size_t i = 0; i < Bases.size(); i++)
    for (const char *szExt : g_Extensions)
    {
      const std::string Path = Bases[i] + szExt;
      if (!ReadFile(Path.c_str(), File))
        continue;
      Res.bOpened = true;
      Res.bViaDir = (i >= nPlain);
      Res.Base = Bases[i];
      Res.bDecoded = Img::LoadFile(File, szExt, Out);
      return Res.bDecoded;
    }
  return false;
}

bool CTextureManager::SearchCube(const std::string &Base, const std::string &Dir, SImage &Out, SSearch &Res)
{
  // A name of any face stands for the cube.
  std::string FaceBase = Base;
  for (const char *szFace : g_CubeFaces)
    if (EndsWith(Base, szFace))
      FaceBase = Base.substr(0, Base.size() - strlen(szFace));
  std::vector<std::string> Bases;

  // Six files <base>_posx ... <base>_negz, the others in the folder +X was found in.
  size_t nPlain = Candidates(FaceBase + g_CubeFaces[0], Dir, Bases);
  SImage Faces[6];
  if (Search(Bases, nPlain, Faces[0], Res))
  {
    const std::string Stem = Res.Base.substr(0, Res.Base.size() - strlen(g_CubeFaces[0]));
    for (int nFace = 1; nFace < 6; nFace++)
    {
      SSearch FaceRes;
      if (!Search({ Stem + g_CubeFaces[nFace] }, 1, Faces[nFace], FaceRes))
        Faces[nFace] = SImage();
    }
    Res.bDecoded = AssembleCube(Faces, Out);
    return Res.bDecoded;
  }
  if (Res.bOpened)
    return false;

  // One picture: a cube file as it is, any other picture on the +X face.
  nPlain = Candidates(FaceBase, Dir, Bases);
  SImage Picture;
  if (!Search(Bases, nPlain, Picture, Res))
    return false;
  if (Picture.nFaces == 6)
  {
    Out = std::move(Picture);
    return true;
  }
  Res.bSingle = true;
  Res.bDecoded = SingleFaceCube(Picture, Out);
  return Res.bDecoded;
}

CTexture *CTextureManager::Load(const char *szName, const char *szTexturePath, uint nFlags, uint nFlags2, byte eTT, bool bWarn, bool bRememberMiss)
{
  if (!m_pPlaceholder)
    Init();
  if (!szName || !szName[0])
    return nullptr;
  const std::string Name = NormalizePath(szName);
  if (Name == "$white" && m_pWhite)
  {
    m_pWhite->AddRef();
    return m_pWhite;
  }
  const std::string Base = StripExtension(Name);
  std::string Dir = NormalizePath(szTexturePath);
  if (!Dir.empty() && Dir.back() != '/')
    Dir += '/';
  const bool bCube = (eTT == eTT_Cubemap) || (nFlags2 & FT2_FORCECUBEMAP);
  // A bump map whose name is no normal map's (*_ddn*, *.ddn*) is a height map, made a normal map;
  // a compressed one is not loaded.
  const bool bHeightBump = eTT == eTT_Bumpmap && !bCube && Base.find("_ddn") == std::string::npos &&
    Base.find(".ddn") == std::string::npos;
  const std::string Suffix = bCube ? g_CubeKey : bHeightBump ? "#height" : "";
  const std::string Key = Base + Suffix;
  // A name searched in a material's folder as well: its answer depends on the folder.
  const std::string DirKey = Dir.empty() ? std::string() : Dir + "|" + Key;

  CTexture *pTex = FindKey(Key);
  if (!pTex && !DirKey.empty())
    pTex = FindKey(DirKey);
  if (pTex)
  {
    pTex->m_nRefs++;
    pTex->m_nFlags2 |= FT2_WASFOUND;
    return pTex;
  }
  const std::string &MissKey = DirKey.empty() ? Key : DirKey;
  if (bRememberMiss && m_Missing.count(MissKey))
    return nullptr;

  SImage Image;
  SSearch Res;
  if (bCube)
    SearchCube(Base, Dir, Image, Res);
  else
  {
    std::vector<std::string> Bases;
    const size_t nPlain = Candidates(Base, Dir, Bases);
    Search(Bases, nPlain, Image, Res);
  }
  if (!Res.bDecoded)
  {
    if (bRememberMiss)
      m_Missing.insert(MissKey);
    if (iLog && (bWarn || Res.bOpened) && m_Warned.insert(MissKey).second)
      iLog->LogWarning(Res.bOpened ? "Texture '%s': the file cannot be read (%s)" : "Texture '%s' not found%s", szName,
        Res.bOpened ? Res.Base.c_str() : "");
    return nullptr;
  }
  if (bHeightBump && (Img::IsBC(Image.Format) || Image.nFaces != 1))
  {
    if (bRememberMiss)
      m_Missing.insert(MissKey);
    if (iLog && m_Warned.insert(MissKey).second)
      iLog->LogWarning("Texture '%s': a compressed height map is no bump map (%s)", szName, Res.Base.c_str());
    return nullptr;
  }

  // The file may be loaded already under another name.
  const std::string FoundKey = Res.Base + (Res.bSingle ? "#single" : "") + Suffix;
  pTex = FindKey(FoundKey);
  if (pTex)
  {
    pTex->m_nRefs++;
    pTex->m_nFlags2 |= FT2_WASFOUND;
  }
  else
  {
    if (bHeightBump)
      HeightToNormalMap(Image);
    if (nFlags & FT_NOMIPS)
      KeepLevels(Image, 1);
    else if (!Img::IsBC(Image.Format) && Image.nMips == 1)
      Img::GenerateMips(Image);
    // The texture resolution of the video options: each step halves the picture.
    if (!(nFlags & (FT_NOMIPS | FT_NORESIZE)) && Image.nFaces == 1)
      DropLevels(Image, GetCVarInt(eTT == eTT_Bumpmap ? "r_TexBumpResolution" : "r_TexResolution", 0));

    pTex = NewTexture(szName);
    if (!pTex)
      return nullptr;
    Upload(pTex, Image);
    pTex->m_nFlags = (int)(nFlags & ~FT_NOTFOUND) | Image.nSrcFlags | (Image.nMips > 1 ? FT_HASMIPS : 0) |
      (eTT == eTT_Bumpmap ? FT_HASNORMALMAP : 0);
    pTex->m_nFlags2 = (int)nFlags2 | FT2_WASLOADED | (Res.bSingle ? FT2_CUBEASSINGLETEXTURE : 0);
    pTex->m_bClamp = (nFlags & FT_CLAMP) || (nFlags2 & (FT2_UCLAMP | FT2_VCLAMP));
    pTex->m_bPoint = (nFlags2 & FT2_FILTER_NEAREST) != 0;
    pTex->m_bLoaded = (pTex->m_pGPU != nullptr);
    AddKey(pTex, FoundKey);
  }
  if (!Res.bViaDir)
    AddKey(pTex, Key);
  if (!DirKey.empty())
    AddKey(pTex, DirKey);
  return pTex;
}

CTexture *CTextureManager::LoadTexture(const char *szName, uint nFlags, uint nFlags2, byte eTT, float fAmount1, float fAmount2, int nID, int nBindID)
{
  CTexture *pTex = Load(szName, nullptr, nFlags, nFlags2, eTT, true, false);
  if (!pTex && (pTex = GetPlaceholder()) != nullptr)
    pTex->AddRef();
  return pTex;
}

CTexture *CTextureManager::LoadMaterialTexture(const char *szName, const char *szTexturePath, uint nFlags, uint nFlags2, byte eTT)
{
  CTexture *pTex = Load(szName, szTexturePath, nFlags, nFlags2, eTT, true, false);
  if (!pTex && (pTex = GetPlaceholder()) != nullptr)
    pTex->AddRef();
  return pTex;
}

int CTextureManager::LoadTextureID(const char *szName, int nDefault, uint nFlags, uint nFlags2)
{
  CTexture *pTex = Load(szName, nullptr, nFlags, nFlags2, eTT_Base, true, true);
  return pTex ? pTex->m_nID : nDefault;
}

void CTextureManager::ReleaseTexture(CTexture *pTex, bool bForce)
{
  if (!pTex || pTex->m_bRemoved)
    return;
  // A texture loaded with FT_NOREMOVE stays until it is released by force, as in the original
  // renderer: the engine removes the menu's mouse pointer right after the menu has loaded it, and
  // its ID, made free, became the next texture's (the loading screen's).
  if (pTex->m_bPermanent || (!bForce && (pTex->m_nFlags & FT_NOREMOVE)))
  {
    if (pTex->m_nRefs > 1)
      pTex->m_nRefs--;
    return;
  }
  if (--pTex->m_nRefs <= 0 || bForce)
    Remove(pTex);
}

void CTextureManager::RemoveByID(int nID)
{
  if (CTexture *pTex = GetByID(nID))
    ReleaseTexture(pTex, false);
}

int CTextureManager::CreateFromMemory(const byte *pData, int nWidth, int nHeight, ETEX_Format eSrc, int nMips, bool bRepeat, int nFilter, int nID, const char *szName, int nFlags)
{
  if (nWidth <= 0 || nHeight <= 0 || nWidth > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || nHeight > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
    return 0;
  if (!m_pPlaceholder)
    Init();
  SImage Image;
  if (!BuildImage(pData, nWidth, nHeight, eSrc, Image) && m_WarnedFormats.insert((int)eSrc).second && iLog)
    iLog->LogWarning("DownLoadToVideoMemory: texture format %d is not supported (the texture is black)", (int)eSrc);
  if (!Img::IsBC(Image.Format) && !(nFlags & FT_NOMIPS) && (nMips > 1 || nFilter == FILTER_TRILINEAR))
    Img::GenerateMips(Image);

  CTexture *pTex = nID ? GetByID(nID) : nullptr;
  if (pTex && pTex->m_bPermanent)
    pTex = nullptr;
  if (pTex)
  {
    if (szName)
      pTex->m_Name = szName;
  }
  else if (!(pTex = NewTexture(szName ? szName : "$Memory", nID)))
    return 0;
  Upload(pTex, Image);
  pTex->m_nFlags = nFlags | Image.nSrcFlags | (Image.nMips > 1 ? FT_HASMIPS : 0);
  pTex->m_eSrcFormat = eSrc;
  pTex->m_bClamp = !bRepeat;
  pTex->m_bPoint = (nFilter == FILTER_NONE);
  pTex->m_bLoaded = (pTex->m_pGPU != nullptr);
  return pTex->m_nID;
}

int CTextureManager::CreateFromImage(const SImage &Image, const char *szName, bool bClamp, int nID)
{
  if (!m_pPlaceholder)
    Init();
  CTexture *pTex = nID ? GetByID(nID) : nullptr;
  if (pTex && pTex->m_bPermanent)
    pTex = nullptr;
  if (pTex)
  {
    if (szName)
      pTex->m_Name = szName;
  }
  else if (!(pTex = NewTexture(szName ? szName : "$Memory", nID)))
    return 0;
  if (!Image.Data.empty())
    Upload(pTex, Image);
  pTex->m_nFlags = Image.nSrcFlags | (Image.nMips > 1 ? FT_HASMIPS : 0);
  pTex->m_bClamp = bClamp;
  pTex->m_bPoint = false;
  pTex->m_bLoaded = (pTex->m_pGPU != nullptr);
  return pTex->m_nID;
}

void CTextureManager::UpdateRect(int nID, const byte *pData, int x, int y, int nWidth, int nHeight, ETEX_Format eSrc)
{
  CTexture *pTex = GetByID(nID);
  CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
  if (!pTex || !pTex->m_pGPU || !pData || !pUpload || nWidth <= 0 || nHeight <= 0)
    return;
  if (eSrc == eTF_Unknown)
    eSrc = pTex->m_eSrcFormat;
  const DXGI_FORMAT Format = pTex->m_pGPU->m_Format;
  const int nTexWidth = pTex->m_nWidth, nTexHeight = pTex->m_nHeight;
  DXGI_FORMAT SrcBC = DXGI_FORMAT_UNKNOWN;
  if (eSrc == eTF_DXT1)
    SrcBC = DXGI_FORMAT_BC1_UNORM;
  else if (eSrc == eTF_DXT3)
    SrcBC = DXGI_FORMAT_BC2_UNORM;
  else if (eSrc == eTF_DXT5)
    SrcBC = DXGI_FORMAT_BC3_UNORM;

  if (Img::IsBC(Format))
  {
    // Whole blocks only (the texture's size is a whole number of blocks).
    if (SrcBC != Format || (x & 3) || (y & 3) || x < 0 || y < 0 || x >= nTexWidth || y >= nTexHeight)
      return;
    const int w = std::min((nWidth + 3) & ~3, nTexWidth - x);
    const int h = std::min((nHeight + 3) & ~3, nTexHeight - y);
    SD3D12SubData Sub;
    Sub.pData = pData;
    Sub.nPitch = Img::RowPitch(Format, nWidth);
    pUpload->UpdateTexture(pTex->m_pGPU, 0, 0, x, y, w, h, Sub);
    return;
  }

  // B8G8R8A8: the source rectangle as B, G, R, A.
  std::vector<byte> Converted;
  const byte *pBGRA = pData;
  if (SrcBC != DXGI_FORMAT_UNKNOWN)
  {
    Converted.resize((size_t)nWidth * nHeight * 4);
    if (!Img::DecodeBC(pData, SrcBC, nWidth, nHeight, Converted.data(), nWidth * 4, 4))
      return;
    pBGRA = Converted.data();
  }
  else if (eSrc != eTF_8888)
  {
    Converted.resize((size_t)nWidth * nHeight * 4);
    if (!Img::ConvertToBGRA(pData, eSrc, nWidth, nHeight, Converted.data()))
    {
      if (m_WarnedFormats.insert((int)eSrc).second && iLog)
        iLog->LogWarning("UpdateTextureInVideoMemory: texture format %d is not supported", (int)eSrc);
      return;
    }
    pBGRA = Converted.data();
  }
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(nTexWidth, x + nWidth), y1 = std::min(nTexHeight, y + nHeight);
  if (x1 <= x0 || y1 <= y0)
    return;
  SD3D12SubData Sub;
  Sub.pData = pBGRA + ((size_t)(y0 - y) * nWidth + (x0 - x)) * 4;
  Sub.nPitch = (uint32)nWidth * 4;
  pUpload->UpdateTexture(pTex->m_pGPU, 0, 0, x0, y0, x1 - x0, y1 - y0, Sub);
}

int CTextureManager::CreateAlphaGlow(float fK)
{
  if (!m_pPlaceholder)
    Init();
  // Black with alpha clamp(256 * (1 - r / 128), 0, 255) for r the distance from (128, 128).
  constexpr int nSize = 256;
  SImage Image;
  Image.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, nSize, nSize, 1, 1);
  byte *p = Image.Data.data();
  for (int y = 0; y < nSize; y++)
    for (int x = 0; x < nSize; x++, p += 4)
    {
      const float fDX = (float)(x - nSize / 2), fDY = (float)(y - nSize / 2);
      float fT = 1.0f - sqrtf(fDX * fDX + fDY * fDY) / (nSize / 2);
      fT = std::max(fT, 0.0f);
      if (fK > 0.0f && fK != 1.0f)
        fT = powf(fT, fK);
      p[3] = (byte)std::clamp((int)(256.0f * fT), 0, 255);
    }
  Img::GenerateMips(Image);
  CTexture *pTex = NewTexture("$AlphaGlow");
  if (!pTex)
    return 0;
  Upload(pTex, Image);
  pTex->m_nFlags = FT_HASALPHA | FT_HASMIPS;
  pTex->m_eSrcFormat = eTF_8000;
  pTex->m_bLoaded = (pTex->m_pGPU != nullptr);
  return pTex->m_nID;
}

int CTextureManager::LoadAnimated(const char *szFormat, int nCount)
{
  if (!szFormat || !szFormat[0] || nCount <= 0)
    return 0;
  for (size_t i = 0; i < m_AnimTex.size(); i++)
  {
    AnimTexInfo *pInfo = m_AnimTex[i].pInfo.get();
    if (pInfo && pInfo->nFramesCount == nCount && !strcmp(pInfo->sName, szFormat))
    {
      pInfo->nRefCounter++;
      return (int)i + 1;
    }
  }

  SAnimTex Anim;
  Anim.pInfo = std::make_unique<AnimTexInfo>();
  AnimTexInfo *pInfo = Anim.pInfo.get();
  snprintf(pInfo->sName, sizeof(pInfo->sName), "%s", szFormat);
  // Frames until the first one that is not found (a missing last frame is the normal end).
  std::string FrameName;
  for (int nFrame = 0; nFrame < nCount; nFrame++)
  {
    if (!AnimFrameName(szFormat, nFrame, FrameName))
      break;
    CTexture *pTex = Load(FrameName.c_str(), nullptr, 0, 0, eTT_Base, nFrame == 0, false);
    if (!pTex)
      break;
    Anim.Frames.push_back(pTex);
    Anim.BindIds.push_back(pTex->m_nID);
  }
  // No frame at all: the placeholder, so that the engine always has a frame to show.
  if (Anim.Frames.empty())
    if (CTexture *pTex = GetPlaceholder())
    {
      pTex->AddRef();
      Anim.Frames.push_back(pTex);
      Anim.BindIds.push_back(pTex->m_nID);
    }
  pInfo->pBindIds = Anim.BindIds.data();
  pInfo->nFramesCount = (int)Anim.BindIds.size();
  pInfo->nRefCounter = 1;

  // The lowest free ID (the BindIds buffer and the AnimTexInfo keep their addresses on a move).
  size_t nSlot = 0;
  while (nSlot < m_AnimTex.size() && m_AnimTex[nSlot].pInfo)
    nSlot++;
  if (nSlot == m_AnimTex.size())
    m_AnimTex.push_back(std::move(Anim));
  else
    m_AnimTex[nSlot] = std::move(Anim);
  return (int)nSlot + 1;
}

AnimTexInfo *CTextureManager::GetAnimInfo(int nID)
{
  if (nID < 1 || nID > (int)m_AnimTex.size())
    return nullptr;
  return m_AnimTex[nID - 1].pInfo.get();
}

void CTextureManager::RemoveAnimated(AnimTexInfo *pInfo)
{
  if (!pInfo)
    return;
  for (SAnimTex &Anim : m_AnimTex)
  {
    if (Anim.pInfo.get() != pInfo)
      continue;
    if (--pInfo->nRefCounter > 0)
      return;
    for (CTexture *pTex : Anim.Frames)
      ReleaseTexture(pTex, false);
    Anim = SAnimTex();
    while (!m_AnimTex.empty() && !m_AnimTex.back().pInfo)
      m_AnimTex.pop_back();
    return;
  }
}
