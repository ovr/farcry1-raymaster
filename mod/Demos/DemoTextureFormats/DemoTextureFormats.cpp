// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: DemoTextureFormats.cpp
//	Description: Textures of the game in different formats, and mip
//	levels and filtering on a floor plane.
//
//	Frame 1: a labelled 3 x 3 grid of textures drawn in 2D, one per
//	format: DXT1, DXT3, DXT5, RGB with alpha (32 bit), RGB without alpha
//	(24 or 32 bit), RGB of 16 bit, luminance or alpha only (L8, A8, A8L8;
//	when the paks have none, a 64 x 64 alpha ramp made by the demo with
//	DownLoadToVideoMemory and eTF_8000), a normal map (3Dc / ATI2 if there
//	is one, else a *_ddn texture) and a cube map (its six faces, *_posx
//	to *_negz, as a cross: +y above, -x +z +x -z in a row, -y below). The
//	textures are found through ICryPak (FindFirst, then the DDS header
//	with FOpen and FRead) and loaded with EF_LoadTexture (the normal map
//	as eTT_Bumpmap). Each cell shows the texture as it is (left) and
//	blended by its alpha over a grey checker (right), with the format,
//	size and mip levels of the header ("as w x h" when the texture
//	has another size) and the file name. A texture that does not load is
//	drawn with what EF_LoadTexture gave back, marked "not loaded".
//	Frames 2 and 3: a floor plane receding to the horizon, from a camera
//	2 m above it looking 8 degrees down. The demo makes a black and white
//	checker of 256 x 256 with all its 9 levels, levels 1 to 8 tinted red,
//	green, blue, yellow, magenta, cyan, orange and white so that the level
//	drawn shows. It writes them into the DDS file
//	Demos\DemoTextureFormatsMips.dds, loaded with EF_LoadTexture, and gives
//	them to DownLoadToVideoMemory (repeated, trilinear). Frame 2: the DDS
//	checker (left half) and a texture of the game (right half). Frame 3:
//	the DownLoadToVideoMemory checker (left) and the DDS checker (right).
//	All repeat every 4 m. Frames 4 and later (a longer run, --until): the
//	three views in turn, 100 frames each, the floor's camera moving
//	forward by 1 m per second.
//
//	Scene file keys (Demos\DemoTextureFormats.ini):
//	  dxt1, dxt3, dxt5, rgba, rgb, rgb16, lum, normal = <file>
//	                     the texture of a cell (default: the first one found)
//	  cube = <file>      the cube map, its +x face *_posx.dds (default: the
//	                     first one found)
//	  dirs = <dir>, ...  the folders searched, with their sub folders
//	                     (default: textures/, objects/)
//	  scan = <n>         the most DDS headers read in the search (default 3000)
//	  minsize = <n>      the least width and height of a texture found (default 128)
//	  floor = <file>     the texture of the right half of the floor (default: the
//	                     DXT1 texture of the grid)
//	  aniso = <n>        runs "r_Texture_Anisotropic_Level n" at the start
//
//////////////////////////////////////////////////////////////////////

#include "DemoHost.h"
#include <IFont.h>
#include <VertexFormats.h>

// The DDS header (DDS_HEADER and DDS_PIXELFORMAT of the DirectX documentation): byte offsets from
// the start of the file, which begins with "DDS ".
static const int nDdsHeaderSize = 4, nDdsFlags = 8, nDdsHeight = 12, nDdsWidth = 16, nDdsPitch = 20, nDdsMips = 28,
  nDdsPfSize = 76, nDdsPfFlags = 80, nDdsFourCC = 84, nDdsBits = 88, nDdsMasks = 92, nDdsCaps = 108, nDdsCaps2 = 112,
  nDdsSize = 128;
static const unsigned DDPF_ALPHAPIXELS = 0x1, DDPF_ALPHA = 0x2, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40,
  DDPF_LUMINANCE = 0x20000, DDSCAPS2_CUBEMAP = 0x200;
// DDSD_CAPS, HEIGHT, WIDTH, PITCH, PIXELFORMAT and MIPMAPCOUNT; DDSCAPS_COMPLEX, TEXTURE and MIPMAP.
static const unsigned DDSD_MIPMAPPED = 0x2100F, DDSCAPS_MIPMAPPED = 0x401008;
//! The DDS file of the floor's checker, written by the demo (into the folder of the scene files).
static const char s_szMipsFile[] = "Demos/DemoTextureFormatsMips.dds";

enum ECell { eDXT1, eDXT3, eDXT5, eRGBA, eRGB, eRGB16, eLum, eNormal, eCube, eCells };
static const char *s_szCellKeys[eCells] = { "dxt1", "dxt3", "dxt5", "rgba", "rgb", "rgb16", "lum", "normal", "cube" };
//! The faces of a cube map, from the +x face's name: *_posx, *_negx, *_posy, *_negy, *_posz, *_negz.
static const char *s_szFaces[6] = { "_posx", "_negx", "_posy", "_negy", "_posz", "_negz" };
//! The places of the faces in the cross, in face sizes (column, row).
static const int s_nFaceAt[6][2] = { { 2, 1 }, { 0, 1 }, { 1, 0 }, { 1, 2 }, { 1, 1 }, { 3, 1 } };
//! Tints of the checker's mip levels 1 to 8 (B, G, R).
static const unsigned char s_Tints[8][3] = { { 64, 64, 255 }, { 64, 255, 64 }, { 255, 64, 64 }, { 64, 255, 255 },
  { 255, 64, 255 }, { 255, 255, 64 }, { 32, 160, 255 }, { 255, 255, 255 } };
static const int nCheckerSize = 256, nCheckerLevels = 9, nAlphaSize = 64;
//! Frames of each view from frame 4 on.
static const int nViewFrames = 100;

//! The items of a list separated by cDelim, without the spaces around them; empty ones are left out.
static std::vector<std::string> SplitList(const std::string &List, char cDelim)
{
  std::vector<std::string> Items;
  for (size_t nBegin = 0; nBegin <= List.size();)
  {
    size_t nEnd = List.find(cDelim, nBegin);
    if (nEnd == std::string::npos)
      nEnd = List.size();
    std::string Item = List.substr(nBegin, nEnd - nBegin);
    Item.erase(0, Item.find_first_not_of(" \t"));
    Item.erase(Item.find_last_not_of(" \t") + 1);
    if (!Item.empty())
      Items.push_back(Item);
    nBegin = nEnd + 1;
  }
  return Items;
}

//! A lower case copy.
static std::string Lower(const std::string &Text)
{
  std::string Res = Text;
  for (size_t i = 0; i < Res.size(); i++)
    Res[i] = (char)tolower((unsigned char)Res[i]);
  return Res;
}

static unsigned ReadU32(const unsigned char *p)
{
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static int CountBits(unsigned nMask)
{
  int n = 0;
  for (; nMask; nMask >>= 1)
    n += nMask & 1;
  return n;
}

//! A DDS texture: its name and what its header says.
struct SDdsInfo
{
  std::string Name;
  bool bRead = false;
  int nWidth = 0, nHeight = 0, nMips = 0;
  unsigned nPfFlags = 0, nFourCC = 0, nBits = 0, nCaps2 = 0;
  unsigned nMasks[4] = { 0, 0, 0, 0 };  // R, G, B, A

  //! The format as Direct3D names it: DXT1, A8R8G8B8, R8G8B8, L8, ...
  std::string Format() const
  {
    char sz[32] = "?";
    if (nPfFlags & DDPF_FOURCC)
    {
      for (int i = 0; i < 4; i++)
      {
        const char c = (char)(nFourCC >> (i * 8));
        sz[i] = c >= 32 && c < 127 ? c : '?';
      }
      sz[4] = 0;
    }
    else if (nPfFlags & DDPF_RGB)
    {
      const int nA = (nPfFlags & DDPF_ALPHAPIXELS) ? CountBits(nMasks[3]) : 0;
      const int nR = CountBits(nMasks[0]), nG = CountBits(nMasks[1]), nB = CountBits(nMasks[2]);
      const int nX = (int)nBits - nA - nR - nG - nB;
      char szA[8] = "";
      if (nA > 0)
        _snprintf(szA, sizeof(szA) - 1, "A%d", nA);
      else if (nX > 0)
        _snprintf(szA, sizeof(szA) - 1, "X%d", nX);
      _snprintf(sz, sizeof(sz) - 1, "%sR%dG%dB%d", szA, nR, nG, nB);
    }
    else if (nPfFlags & DDPF_LUMINANCE)
    {
      if (nPfFlags & DDPF_ALPHAPIXELS)
        _snprintf(sz, sizeof(sz) - 1, "A%dL%d", CountBits(nMasks[3]), CountBits(nMasks[0]));
      else
        _snprintf(sz, sizeof(sz) - 1, "L%d", nBits);
    }
    else if (nPfFlags & DDPF_ALPHA)
      _snprintf(sz, sizeof(sz) - 1, "A%d", nBits);
    sz[sizeof(sz) - 1] = 0;
    return sz;
  }

  //! The cell the texture belongs in, or -1.
  int Cell() const
  {
    const std::string Low = Lower(Name);
    if (nCaps2 & DDSCAPS2_CUBEMAP)
      return -1;
    const std::string Fmt = Format();
    if (Fmt == "ATI2" || Low.find("_ddn") != std::string::npos)
      return eNormal;
    if (Low.find("bump") != std::string::npos || Low.find("_ddp") != std::string::npos)
      return -1;
    if (nPfFlags & DDPF_FOURCC)
      return Fmt == "DXT1" ? eDXT1 : Fmt == "DXT3" ? eDXT3 : Fmt == "DXT5" ? eDXT5 : -1;
    if (nPfFlags & DDPF_RGB)
      return nBits == 16 ? eRGB16 : (nPfFlags & DDPF_ALPHAPIXELS) ? eRGBA : eRGB;
    if (nPfFlags & (DDPF_LUMINANCE | DDPF_ALPHA))
      return eLum;
    return -1;
  }
};

//! Whether Name is a face of a cube map (*_posx.dds, ...); its face index in nFace.
static bool IsCubeFace(const std::string &Name, int &nFace)
{
  const std::string Low = Lower(Name);
  for (nFace = 0; nFace < 6; nFace++)
  {
    const std::string Ending = std::string(s_szFaces[nFace]) + ".dds";
    if (Low.size() > Ending.size() && !Low.compare(Low.size() - Ending.size(), Ending.size(), Ending))
      return true;
  }
  return false;
}

//! The name of face nFace of the cube map whose +x face is PosX.
static std::string FaceName(const std::string &PosX, int nFace)
{
  const size_t nAt = Lower(PosX).rfind("_posx");
  if (nAt == std::string::npos)
    return PosX;
  return PosX.substr(0, nAt) + s_szFaces[nFace] + PosX.substr(nAt + 5);
}

class CDemoTextureFormats : public CDemo
{
public:
  virtual const char *GetName() const { return "DemoTextureFormats"; }
  virtual const char *GetDefaultFrames() const { return "1,2,3"; }

  virtual bool Init(CDemoHost &Host)
  {
    const CSceneFile &Scene = Host.GetScene();
    if (Scene.Has("aniso"))
      Host.Exec((std::string("r_Texture_Anisotropic_Level ") + Scene.Get("aniso")).c_str());
    ICryFont *pCryFont = Host.GetSystem()->GetICryFont();
    m_pFont = pCryFont ? pCryFont->GetFont("Default") : NULL;

    // The textures the scene names, then a search for the others.
    for (int i = 0; i < eCells; i++)
    {
      if (Scene.Has(s_szCellKeys[i]))
      {
        m_Cells[i].Name = Scene.Get(s_szCellKeys[i]);
        m_bFixed[i] = true;
        ReadHeader(Host, m_Cells[i]);
      }
    }
    m_nMinSize = Scene.GetInt("minsize", 128);
    int nLeft = Scene.GetInt("scan", 3000);
    std::vector<std::string> Dirs = SplitList(Scene.Get("dirs", "textures/, objects/"), ',');
    for (size_t i = 0; i < Dirs.size(); i++)
      Search(Host, Dirs[i][Dirs[i].size() - 1] == '/' ? Dirs[i] : Dirs[i] + "/", nLeft);
    Host.Log("DemoTextureFormats: %d DDS headers read", m_nRead);

    for (int i = 0; i < eCells; i++)
    {
      SDdsInfo &Info = m_Cells[i];
      Host.Log("DemoTextureFormats: %s: '%s' %s %d x %d, %d mips", s_szCellKeys[i], Info.Name.c_str(), Info.bRead ? Info.Format().c_str() : "-",
        Info.nWidth, Info.nHeight, Info.nMips);
      if (Info.Name.empty())
        continue;
      for (int f = 0; f < (i == eCube ? 6 : 1); f++)
      {
        m_pTextures[i][f] = LoadTexture(Host, i == eCube ? FaceName(Info.Name, f) : Info.Name, FT_NORESIZE,
          i == eNormal ? eTT_Bumpmap : eTT_Base, m_bLoaded[i][f]);
      }
    }
    if (!m_bLoaded[eLum][0])
      MakeAlphaTexture(Host);
    MakeBackground(Host);
    MakeChecker(Host);

    m_Floor.Name = Scene.Get("floor", m_Cells[eDXT1].Name.c_str());
    if (!m_Floor.Name.empty())
    {
      ReadHeader(Host, m_Floor);
      m_pFloor = LoadTexture(Host, m_Floor.Name, 0, eTT_Base, m_bFloorLoaded);
    }
    return m_nChecker > 0 || m_bMipsLoaded;
  }

  virtual void Draw(CDemoHost &Host, int nFrame)
  {
    char szMips[96];
    _snprintf(szMips, sizeof(szMips) - 1, "own checker DDS  %d x %d  %d levels", nCheckerSize, nCheckerSize, nCheckerLevels);
    szMips[sizeof(szMips) - 1] = 0;
    const int nMips = m_bMipsLoaded ? m_pMips->GetTextureID() : 0;
    // Views 0 to 2 in frames 1 to 3, then in turn.
    const int nView = nFrame <= 3 ? nFrame - 1 : ((nFrame - 4) / nViewFrames) % 3;
    const float fForward = nFrame <= 3 ? 0 : ((nFrame - 4) % nViewFrames) * Host.GetFrameTime();
    if (nView == 0)
      DrawGrid(Host);
    else if (nView == 1)
    {
      std::string Floor = m_Floor.bRead ? Describe(m_Floor) + LoadedSize(m_pFloor, m_bFloorLoaded, m_Floor) : "no game texture";
      DrawFloor(Host, fForward, nMips, m_pFloor ? m_pFloor->GetTextureID() : 0, szMips, Floor.c_str(), m_Floor.Name.c_str());
    }
    else
      DrawFloor(Host, fForward, m_nChecker, nMips, "own checker given to DownLoadToVideoMemory", szMips, s_szMipsFile);
  }

  virtual void Shutdown(CDemoHost &Host)
  {
    for (int i = 0; i < eCells; i++)
    {
      for (int f = 0; f < 6; f++)
      {
        if (m_pTextures[i][f])
          m_pTextures[i][f]->Release(false);
        m_pTextures[i][f] = NULL;
      }
    }
    if (m_pFloor)
      m_pFloor->Release(false);
    if (m_pMips)
      m_pMips->Release(false);
    m_pFloor = m_pMips = NULL;
    IRenderer *pRenderer = Host.GetRenderer();
    if (m_nAlpha > 0)
      pRenderer->RemoveTexture(m_nAlpha);
    if (m_nBackground > 0)
      pRenderer->RemoveTexture(m_nBackground);
    if (m_nChecker > 0)
      pRenderer->RemoveTexture(m_nChecker);
    m_nAlpha = m_nBackground = m_nChecker = 0;
  }

private:
  //! Reads the DDS header of Info.Name.
  bool ReadHeader(CDemoHost &Host, SDdsInfo &Info)
  {
    ICryPak *pPak = Host.GetPak();
    FILE *pFile = pPak->FOpen(Info.Name.c_str(), "rb");
    if (!pFile)
      return false;
    unsigned char Header[nDdsSize];
    const size_t nRead = pPak->FRead(Header, 1, nDdsSize, pFile);
    pPak->FClose(pFile);
    m_nRead++;
    if (nRead != nDdsSize || memcmp(Header, "DDS ", 4))
      return false;
    Info.nHeight = (int)ReadU32(Header + nDdsHeight);
    Info.nWidth = (int)ReadU32(Header + nDdsWidth);
    Info.nMips = (int)ReadU32(Header + nDdsMips);
    Info.nPfFlags = ReadU32(Header + nDdsPfFlags);
    Info.nFourCC = ReadU32(Header + nDdsFourCC);
    Info.nBits = ReadU32(Header + nDdsBits);
    for (int i = 0; i < 4; i++)
      Info.nMasks[i] = ReadU32(Header + nDdsMasks + i * 4);
    Info.nCaps2 = ReadU32(Header + nDdsCaps2);
    Info.bRead = true;
    return true;
  }

  //! Whether the search is done: every cell has a texture, the normal map one of ATI2 (unless the
  //! scene names it).
  bool AllFound() const
  {
    for (int i = 0; i < eCells; i++)
    {
      if (!m_bFixed[i] && (m_Cells[i].Name.empty() || (i == eNormal && m_Cells[i].Format() != "ATI2")))
        return false;
    }
    return true;
  }

  //! Walks Dir and its sub folders (sorted, the files first) until every cell has a texture or
  //! nLeft headers have been read.
  void Search(CDemoHost &Host, const std::string &Dir, int &nLeft)
  {
    if (nLeft <= 0 || AllFound())
      return;
    std::vector<std::string> Files = Host.FindFiles(Dir.c_str(), "*.dds");
    for (size_t i = 0; i < Files.size() && nLeft > 0 && !AllFound(); i++)
    {
      int nFace = 0;
      if (IsCubeFace(Files[i], nFace))
      {
        if (nFace == 0 && !m_bFixed[eCube] && m_Cells[eCube].Name.empty() && HasAllFaces(Files, Files[i]))
        {
          SDdsInfo Info;
          Info.Name = Files[i];
          nLeft--;
          if (ReadHeader(Host, Info) && Info.nWidth >= m_nMinSize && Info.nHeight >= m_nMinSize)
            m_Cells[eCube] = Info;
        }
        continue;
      }
      SDdsInfo Info;
      Info.Name = Files[i];
      nLeft--;
      if (!ReadHeader(Host, Info) || Info.nWidth < m_nMinSize || Info.nHeight < m_nMinSize)
        continue;
      const int nCell = Info.Cell();
      if (nCell < 0 || m_bFixed[nCell])
        continue;
      if (m_Cells[nCell].Name.empty() || (nCell == eNormal && Info.Format() == "ATI2" && m_Cells[nCell].Format() != "ATI2"))
        m_Cells[nCell] = Info;
    }
    std::vector<std::string> Dirs = Host.FindDirs(Dir.c_str());
    for (size_t i = 0; i < Dirs.size() && nLeft > 0 && !AllFound(); i++)
      Search(Host, Dirs[i], nLeft);
  }

  //! Whether Files has all six faces of the cube map whose +x face is PosX.
  static bool HasAllFaces(const std::vector<std::string> &Files, const std::string &PosX)
  {
    for (int f = 1; f < 6; f++)
    {
      const std::string Face = Lower(FaceName(PosX, f));
      bool bFound = false;
      for (size_t i = 0; i < Files.size() && !bFound; i++)
        bFound = Lower(Files[i]) == Face;
      if (!bFound)
        return false;
    }
    return true;
  }

  //! EF_LoadTexture; what it gives back is kept also when the texture is not loaded (bLoaded false).
  ITexPic *LoadTexture(CDemoHost &Host, const std::string &Name, uint nFlags, byte eTT, bool &bLoaded)
  {
    ITexPic *pTex = Host.GetRenderer()->EF_LoadTexture(Name.c_str(), nFlags, 0, eTT);
    bLoaded = pTex && pTex->IsTextureLoaded();
    if (pTex)
    {
      Host.Log("DemoTextureFormats: %s %s: id %d, %d x %d, flags 0x%x, flags2 0x%x", Name.c_str(), bLoaded ? "loaded" : "NOT loaded",
        pTex->GetTextureID(), pTex->GetWidth(), pTex->GetHeight(), pTex->GetFlags(), pTex->GetFlags2());
    }
    else
      Host.Log("DemoTextureFormats: %s NOT loaded: no texture", Name.c_str());
    return pTex;
  }

  //! "  as w x h" when the loaded texture's size is not the header's, "  not loaded" when it did not load.
  static std::string LoadedSize(ITexPic *pTex, bool bLoaded, const SDdsInfo &Info)
  {
    if (!pTex)
      return "";
    if (!bLoaded)
      return "  not loaded";
    if (pTex->GetWidth() == Info.nWidth && pTex->GetHeight() == Info.nHeight)
      return "";
    char sz[48];
    _snprintf(sz, sizeof(sz) - 1, "  as %d x %d", pTex->GetWidth(), pTex->GetHeight());
    sz[sizeof(sz) - 1] = 0;
    return sz;
  }

  //! The demo's own alpha texture: a ramp from left to right, the other way round inside a disc.
  void MakeAlphaTexture(CDemoHost &Host)
  {
    // Room for 4 bytes per pixel; the alpha values are the first nAlphaSize x nAlphaSize bytes.
    std::vector<unsigned char> Data(nAlphaSize * nAlphaSize * 4, 0);
    for (int y = 0; y < nAlphaSize; y++)
    {
      for (int x = 0; x < nAlphaSize; x++)
      {
        const int dx = x - nAlphaSize / 2, dy = y - nAlphaSize / 2;
        const int nRamp = x * 255 / (nAlphaSize - 1);
        Data[y * nAlphaSize + x] = (unsigned char)(dx * dx + dy * dy < nAlphaSize * nAlphaSize / 9 ? 255 - nRamp : nRamp);
      }
    }
    char szName[] = "$DemoTextureFormatsA8";
    m_nAlpha = (int)Host.GetRenderer()->DownLoadToVideoMemory(&Data[0], nAlphaSize, nAlphaSize, eTF_8000, eTF_8000, 0, true,
      FILTER_BILINEAR, 0, szName, FT_NOMIPS);
    Host.Log("DemoTextureFormats: own A8 texture: id %d", m_nAlpha);
  }

  //! The grey checker under the alpha views: 128 x 128, cells of 16.
  void MakeBackground(CDemoHost &Host)
  {
    const int nSize = 128;
    std::vector<unsigned char> Data(nSize * nSize * 4);
    for (int y = 0; y < nSize; y++)
    {
      for (int x = 0; x < nSize; x++)
      {
        unsigned char *p = &Data[(y * nSize + x) * 4];
        p[0] = p[1] = p[2] = ((x / 16 + y / 16) & 1) ? 0x60 : 0xA0;
        p[3] = 255;
      }
    }
    char szName[] = "$DemoTextureFormatsBack";
    m_nBackground = (int)Host.GetRenderer()->DownLoadToVideoMemory(&Data[0], nSize, nSize, eTF_8888, eTF_8888, 0, true,
      FILTER_BILINEAR, 0, szName, FT_NOMIPS);
  }

  //! The floor's checker with all its mip levels one after the other, 4 bytes per pixel (B, G, R, A),
  //! rows from the top: level 0 cells of 4 texels, black and white; each smaller level the mean of
  //! 2 x 2 texels of the one before, tinted. Given to DownLoadToVideoMemory and written into the DDS
  //! file s_szMipsFile (A8R8G8B8 with all levels), which EF_LoadTexture loads.
  void MakeChecker(CDemoHost &Host)
  {
    std::vector<unsigned char> Grey(nCheckerSize * nCheckerSize);
    for (int y = 0; y < nCheckerSize; y++)
      for (int x = 0; x < nCheckerSize; x++)
        Grey[y * nCheckerSize + x] = ((x / 4 + y / 4) & 1) ? 255 : 0;
    std::vector<unsigned char> Data;
    Data.reserve(nCheckerSize * nCheckerSize * 4 * 4 / 3 + 4);
    for (int nLevel = 0, nSize = nCheckerSize; nLevel < nCheckerLevels; nLevel++, nSize /= 2)
    {
      if (nLevel > 0)
      {
        std::vector<unsigned char> Half(nSize * nSize);
        for (int y = 0; y < nSize; y++)
        {
          for (int x = 0; x < nSize; x++)
          {
            const unsigned char *p = &Grey[(y * 2) * nSize * 2 + x * 2];
            Half[y * nSize + x] = (unsigned char)((p[0] + p[1] + p[nSize * 2] + p[nSize * 2 + 1] + 2) / 4);
          }
        }
        Grey.swap(Half);
      }
      for (int i = 0; i < nSize * nSize; i++)
      {
        for (int c = 0; c < 3; c++)
          Data.push_back(nLevel > 0 ? (unsigned char)(Grey[i] * s_Tints[nLevel - 1][c] / 255) : Grey[i]);
        Data.push_back(255);
      }
    }
    char szName[] = "$DemoTextureFormatsChecker";
    m_nChecker = (int)Host.GetRenderer()->DownLoadToVideoMemory(&Data[0], nCheckerSize, nCheckerSize, eTF_8888, eTF_8888,
      nCheckerLevels, true, FILTER_TRILINEAR, 0, szName, 0);
    Host.Log("DemoTextureFormats: checker: id %d", m_nChecker);

    unsigned char Header[nDdsSize] = { 'D', 'D', 'S', ' ' };
    const unsigned Fields[][2] = { { nDdsHeaderSize, nDdsSize - 4 }, { nDdsFlags, DDSD_MIPMAPPED }, { nDdsHeight, nCheckerSize },
      { nDdsWidth, nCheckerSize }, { nDdsPitch, nCheckerSize * 4 }, { nDdsMips, nCheckerLevels }, { nDdsPfSize, 32 },
      { nDdsPfFlags, DDPF_RGB | DDPF_ALPHAPIXELS }, { nDdsBits, 32 }, { nDdsMasks, 0xff0000 }, { nDdsMasks + 4, 0xff00 },
      { nDdsMasks + 8, 0xff }, { nDdsMasks + 12, 0xff000000 }, { nDdsCaps, DDSCAPS_MIPMAPPED } };
    for (size_t i = 0; i < sizeof(Fields) / sizeof(Fields[0]); i++)
      for (int b = 0; b < 4; b++)
        Header[Fields[i][0] + b] = (unsigned char)(Fields[i][1] >> (b * 8));
    FILE *pFile = fopen(s_szMipsFile, "wb");
    const bool bWritten = pFile && fwrite(Header, 1, nDdsSize, pFile) == nDdsSize && fwrite(&Data[0], 1, Data.size(), pFile) == Data.size();
    if (pFile)
      fclose(pFile);
    if (bWritten)
      m_pMips = LoadTexture(Host, s_szMipsFile, FT_NORESIZE, eTT_Base, m_bMipsLoaded);
    else
      Host.Log("DemoTextureFormats: %s could not be written", s_szMipsFile);
  }

  void SetFont(float fSize)
  {
    m_pFont->Reset();
    m_pFont->SetEffect("default");
    m_pFont->SetSameSize(false);
    m_pFont->SetCharWidthScale(1.0f);
    m_pFont->SetSize(vector2f(fSize, fSize));
    m_pFont->SetColor(color4f(1, 1, 1, 1));
  }

  //! The format line of a texture: format, size and mip levels.
  static std::string Describe(const SDdsInfo &Info)
  {
    char sz[96];
    _snprintf(sz, sizeof(sz) - 1, "%s  %d x %d  %d mips", Info.Format().c_str(), Info.nWidth, Info.nHeight, max(Info.nMips, 1));
    sz[sizeof(sz) - 1] = 0;
    return sz;
  }

  void DrawGrid(CDemoHost &Host)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    // The 2D calls use a virtual 800 x 600 screen: cells of 266 x 200.
    const float fCellW = 800.0f / 3, fCellH = 200.0f, fImage = 124.0f;
    for (int i = 0; i < eCells; i++)
    {
      const float x = (i % 3) * fCellW, y = (i / 3) * fCellH;
      const SDdsInfo &Info = m_Cells[i];
      std::string Title, File = Info.Name;
      const size_t nSlash = File.find_last_of("\\/");
      if (nSlash != std::string::npos)
        File.erase(0, nSlash + 1);
      const bool bOwnAlpha = i == eLum && !m_bLoaded[i][0];
      if (bOwnAlpha)
      {
        char sz[64];
        _snprintf(sz, sizeof(sz) - 1, "A8 (own, eTF_8000)  %d x %d", nAlphaSize, nAlphaSize);
        sz[sizeof(sz) - 1] = 0;
        Title = sz;
        File = Info.Name.empty() ? "no L8 / A8 texture found" : File + " not loaded";
        if (m_nAlpha <= 0)
          File += ", own one failed";
      }
      else if (!Info.bRead)
        Title = std::string(s_szCellKeys[i]) + (Info.Name.empty() ? ": none found" : ": header not read");
      else
      {
        Title = (i == eNormal ? "normal  " : i == eCube ? "cube  " : "") + Describe(Info) +
          LoadedSize(m_pTextures[i][0], m_bLoaded[i][0], Info);
      }
      if (m_pFont)
      {
        SetFont(14);
        m_pFont->DrawString(x + 6, y + 4, Title.c_str());
        SetFont(11);
        m_pFont->DrawString(x + 6, y + 22, File.c_str());
      }

      pRenderer->SetState(GS_NODEPTHTEST);
      if (i == eCube)
      {
        // The faces as a cross, 4 x 3 faces.
        const float fFace = 48.0f;
        for (int f = 0; f < 6; f++)
        {
          if (m_pTextures[i][f])
            pRenderer->Draw2dImage(x + 5 + s_nFaceAt[f][0] * fFace, y + 42 + s_nFaceAt[f][1] * fFace, fFace, fFace,
              m_pTextures[i][f]->GetTextureID(), 0, 1, 1, 0);
        }
        continue;
      }
      const int nTexId = bOwnAlpha ? m_nAlpha : m_pTextures[i][0] ? m_pTextures[i][0]->GetTextureID() : 0;
      if (nTexId <= 0)
        continue;
      // The image keeps the texture's aspect ratio.
      const bool bAspect = Info.bRead && !bOwnAlpha;
      const float fW = bAspect && Info.nHeight > Info.nWidth ? fImage * Info.nWidth / Info.nHeight : fImage;
      const float fH = bAspect && Info.nWidth > Info.nHeight ? fImage * Info.nHeight / Info.nWidth : fImage;
      pRenderer->Draw2dImage(x + 5, y + 42, fW, fH, nTexId, 0, 1, 1, 0);
      pRenderer->Draw2dImage(x + 137, y + 42, fW, fH, m_nBackground, 0, fH / fImage, fW / fImage, 0);
      pRenderer->SetState(GS_NODEPTHTEST | GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA);
      pRenderer->Draw2dImage(x + 137, y + 42, fW, fH, nTexId, 0, 1, 1, 0);
      pRenderer->SetState(GS_NODEPTHTEST);
    }
  }

  //! A rectangle of the floor (z = 0) from x0 to x1 and from y = 1 to 1000 m, the texture repeated every 4 m.
  static void DrawFloorPart(IRenderer *pRenderer, float x0, float x1, int nTexId)
  {
    static ushort Inds[6] = { 0, 1, 2, 0, 2, 3 };
    const float y0 = 1, y1 = 1000, fRepeat = 4;
    struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F Verts[4];
    const float Corners[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    for (int i = 0; i < 4; i++)
    {
      Verts[i].xyz = Vec3(Corners[i][0], Corners[i][1], 0);
      Verts[i].color.dcolor = 0xffffffff;
      Verts[i].st[0] = Corners[i][0] / fRepeat;
      Verts[i].st[1] = Corners[i][1] / fRepeat;
    }
    pRenderer->SetTexture(nTexId);
    pRenderer->DrawDynVB(Verts, Inds, 4, 6, R_PRIMV_TRIANGLES);
  }

  //! The floor with nLeft in its left half and nRight in its right half (none for 0), seen from
  //! fForward m along y, and the lines of text above each half.
  void DrawFloor(CDemoHost &Host, float fForward, int nLeft, int nRight, const char *szLeft, const char *szRight1, const char *szRight2)
  {
    IRenderer *pRenderer = Host.GetRenderer();
    const Vec3 vEye(0, fForward, 2);
    pRenderer->SetCamera(Host.LookAt(vEye, vEye + Vec3(0, 100, -100 * tanf(DEG2RAD(8.0f))), 70));
    pRenderer->SetCullMode(R_CULL_NONE);
    pRenderer->SetState(GS_DEPTHWRITE);
    if (nLeft > 0)
      DrawFloorPart(pRenderer, -800, 0, nLeft);
    if (nRight > 0)
      DrawFloorPart(pRenderer, 0, 800, nRight);
    pRenderer->SetCullMode(R_CULL_BACK);

    if (!m_pFont)
      return;
    char sz[160];
    SetFont(14);
    m_pFont->DrawString(10, 10, szLeft);
    m_pFont->DrawString(10, 28, "levels 1-8: red green blue yellow magenta cyan orange white");
    m_pFont->DrawString(410, 10, szRight1);
    m_pFont->DrawString(410, 28, szRight2);
    _snprintf(sz, sizeof(sz) - 1, "r_Texture_Anisotropic_Level %s", Host.GetCVarString("r_Texture_Anisotropic_Level").c_str());
    sz[sizeof(sz) - 1] = 0;
    m_pFont->DrawString(10, 50, sz);
  }

  IFFont *m_pFont = NULL;
  SDdsInfo m_Cells[eCells];
  ITexPic *m_pTextures[eCells][6] = {};
  SDdsInfo m_Floor;
  //! Whether the textures loaded (else they are what EF_LoadTexture gave back instead).
  bool m_bLoaded[eCells][6] = {};
  ITexPic *m_pFloor = NULL;
  bool m_bFloorLoaded = false;
  //! The checker of the DDS file.
  ITexPic *m_pMips = NULL;
  bool m_bMipsLoaded = false;
  int m_nAlpha = 0, m_nBackground = 0, m_nChecker = 0;
  int m_nMinSize = 128;
  int m_nRead = 0;
  //! The cells whose texture the scene names.
  bool m_bFixed[eCells] = {};
};

DEMO_PLUGIN(CDemoTextureFormats)
