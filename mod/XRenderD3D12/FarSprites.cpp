//////////////////////////////////////////////////////////////////////
//
//	File: FarSprites.cpp
//	Description: The far vegetation sprites: pictures of the vegetation
//	objects from 24 directions (MakeSprite), drawn as upright quads in
//	place of the instances far away (DrawObjSprites).
//
//	MakeSprite gives the 3D engine a texture ID at once and remembers the
//	object. Before the next main view is drawn (BakeSprites), the renderer
//	draws each new object from its 24 directions with an orthographic
//	camera into the tiles of an off screen target, twice the sprite size,
//	reads the target back and makes a texture array of the 24 pictures
//	(MakeSprite's IDs stay without a GPU texture): alpha is the part of a
//	texel the object covers, the colour of the empty texels comes from
//	their neighbours (so that filtering does not darken the edges), and the
//	alpha of each mip level is scaled so that as much of it passes the
//	alpha test as of level 0 (so that the far trees do not thin out).
//	The object is drawn as the scene draws it, with half its colour and a
//	white world colour; DrawObjSprites draws the texture times twice the
//	world colour and the instance's brightness, as the vegetation shaders
//	draw the object itself.
//
//	DrawObjSprites gets the instances to draw as sprites (their members
//	and the groups: RendererStructures.h, "Far vegetation sprites"): one
//	quad per instance, standing on the object's box, as wide as the
//	object turning about its axis, facing the camera, with the picture
//	of the direction it is seen from; one draw per object.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Texture.h"
#include "Scene.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace RendererStructures;

namespace
{

//! The pictures are drawn this many times larger than the textures, then filtered down.
constexpr int SPRITE_SUPERSAMPLE = 2;
//! The off screen target is at most this many pixels a side.
constexpr int SPRITE_TARGET_MAX = 4096;
//! The sizes d3d12_SpriteRes may give.
constexpr int SPRITE_MIN_RES = 32, SPRITE_MAX_RES = 256;
//! The alpha test of the sprites.
constexpr float SPRITE_ALPHA_REF = 128.0f / 255.0f;
//! The colour the objects are drawn with into the pictures (DrawObjSprites doubles it).
constexpr float SPRITE_COLOR = 0.5f;

//! The index (0 to 23) of MakeSprite's angle: 90 to 435 degrees in steps of 15.
int SpriteAngleIndex(float fAngle)
{
  int n = (int)lroundf((fAngle - 90) / 15) % CD3D12Renderer::SPRITE_ANGLES;
  return n < 0 ? n + CD3D12Renderer::SPRITE_ANGLES : n;
}

//! The index of the sprite of an instance seen in the horizontal direction fDegrees (from the
//! camera to the instance, 0 to 360): the one of fAngle = 165 - 15 * floor(fDegrees / 15),
//! brought into 90 to 435.
int SpriteIndexOfDirection(float fDegrees)
{
  int nAngle = 165 - 15 * (int)floorf(fDegrees / 15);
  while (nAngle < 90)
    nAngle += 360;
  while (nAngle >= 450)
    nAngle -= 360;
  return (nAngle - 90) / 15;
}

//! The direction the picture of index n shows the object in (radians, from the camera to the
//! object): the middle of the directions SpriteIndexOfDirection gives n for.
float SpriteViewAngle(int n)
{
  return DEG2RAD(172.5f - (90.0f + 15.0f * n));
}

//! A texel of a sprite being made: the colour (not multiplied by alpha) and the covered part.
struct SPixelF
{
  float r = 0, g = 0, b = 0, a = 0;
};

//! What MakeSpriteSlice works in, sized once for the slice size (so that it allocates nothing:
//! it runs on worker threads).
struct SSliceScratch
{
  std::vector<std::vector<SPixelF>> Levels;  //!< the levels as floats
  std::vector<float> Alphas;                 //!< a level's alphas, to sort

  void Resize(const SImage &Image)
  {
    Levels.resize(Image.nMips);
    for (int m = 0; m < Image.nMips; m++)
      Levels[m].resize((size_t)Image.MipWidth(m) * Image.MipHeight(m));
    Alphas.resize(Levels[0].size());
  }
};

uint8 ToByte(float v)
{
  return (uint8)(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f);
}

//! Level 0 (n x n): f x f pixels of the picture (B, G, R, A, alpha 255 where the object is) per texel.
void DownsamplePicture(const uint8 *pSrc, int nPitch, int n, int f, SPixelF *pOut)
{
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
    {
      int r = 0, g = 0, b = 0, a = 0;
      for (int j = 0; j < f; j++)
      {
        const uint8 *p = pSrc + (size_t)(y * f + j) * nPitch + (size_t)x * f * 4;
        for (int i = 0; i < f; i++, p += 4)
        {
          b += p[0] * p[3];
          g += p[1] * p[3];
          r += p[2] * p[3];
          a += p[3];
        }
      }
      SPixelF o;
      o.a = a / (255.0f * f * f);
      if (a > 0)
      {
        const float k = 1.0f / (255.0f * a);
        o.r = r * k;
        o.g = g * k;
        o.b = b * k;
      }
      pOut[(size_t)y * n + x] = o;
    }
}

//! The next level: 2 x 2 texels each, alpha averaged, the colour weighted by alpha.
void DownsampleLevel(const SPixelF *pIn, int w, int h, SPixelF *pOut)
{
  const int w2 = std::max(w / 2, 1), h2 = std::max(h / 2, 1);
  for (int y = 0; y < h2; y++)
    for (int x = 0; x < w2; x++)
    {
      SPixelF Sum;
      for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++)
        {
          const SPixelF &p = pIn[(size_t)std::min(2 * y + j, h - 1) * w + std::min(2 * x + i, w - 1)];
          Sum.r += p.r * p.a;
          Sum.g += p.g * p.a;
          Sum.b += p.b * p.a;
          Sum.a += p.a;
        }
      SPixelF o;
      o.a = Sum.a / 4;
      if (Sum.a > 0)
      {
        o.r = Sum.r / Sum.a;
        o.g = Sum.g / Sum.a;
        o.b = Sum.b / Sum.a;
      }
      pOut[(size_t)y * w2 + x] = o;
    }
}

//! The empty texels (alpha 0) of a level get the colour of the texel above them in the next
//! (smaller) level, whose empty texels have theirs already: the colour of the nearest covered
//! texels, without a hole anywhere.
void FillEmptyTexels(SPixelF *pImg, int w, int h, const SPixelF *pNext, int w2, int h2)
{
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
    {
      SPixelF &o = pImg[(size_t)y * w + x];
      if (o.a > 0)
        continue;
      const SPixelF &p = pNext[(size_t)std::min(y / 2, h2 - 1) * w2 + std::min(x / 2, w2 - 1)];
      o.r = p.r;
      o.g = p.g;
      o.b = p.b;
    }
}

//! The part of the texels whose alpha passes the alpha test.
float Coverage(const std::vector<SPixelF> &Img)
{
  size_t n = 0;
  for (const SPixelF &p : Img)
    n += p.a >= SPRITE_ALPHA_REF;
  return Img.empty() ? 0.0f : (float)n / Img.size();
}

//! The scale of alpha that lets the part fCoverage of the texels pass the alpha test: the alpha
//! test reference over the alpha at that place in the order of the texels' alphas (from 1/16 to 16).
float CoverageScale(const std::vector<SPixelF> &Img, float fCoverage, std::vector<float> &Alphas)
{
  const size_t n = (size_t)std::lround(fCoverage * Img.size());
  if (n == 0)
    return 1.0f;
  for (size_t i = 0; i < Img.size(); i++)
    Alphas[i] = Img[i].a;
  const auto End = Alphas.begin() + Img.size();
  std::nth_element(Alphas.begin(), Alphas.begin() + (n - 1), End, std::greater<float>());
  const float a = Alphas[n - 1];
  // A little more, so that this texel still passes after the rounding to bytes.
  return a > 0 ? std::clamp(SPRITE_ALPHA_REF / a * 1.01f, 1.0f / 16, 16.0f) : 16.0f;
}

//! Slice nSlice of a sprite array (B8G8R8A8, all levels) from its picture (SPRITE_SUPERSAMPLE
//! times the slice's size a side at pSrc, rows nPitch bytes apart). Allocates nothing.
void MakeSpriteSlice(const uint8 *pSrc, int nPitch, SImage &Out, int nSlice, SSliceScratch &S)
{
  const int nMips = Out.nMips;
  auto &Levels = S.Levels;
  DownsamplePicture(pSrc, nPitch, Out.nWidth, SPRITE_SUPERSAMPLE, Levels[0].data());
  for (int m = 1; m < nMips; m++)
    DownsampleLevel(Levels[m - 1].data(), Out.MipWidth(m - 1), Out.MipHeight(m - 1), Levels[m].data());
  // The colours of the empty texels, from the smallest level up (every level was made from the
  // covered texels only).
  for (int m = nMips - 2; m >= 0; m--)
    FillEmptyTexels(Levels[m].data(), Out.MipWidth(m), Out.MipHeight(m), Levels[m + 1].data(), Out.MipWidth(m + 1),
                    Out.MipHeight(m + 1));

  const float fCoverage = Coverage(Levels[0]);
  for (int m = 0; m < nMips; m++)
  {
    const float fScale = m > 0 && fCoverage > 0 ? CoverageScale(Levels[m], fCoverage, S.Alphas) : 1.0f;
    uint8 *pDst = Out.Bits(nSlice, m);
    for (const SPixelF &p : Levels[m])
    {
      pDst[0] = ToByte(p.b);
      pDst[1] = ToByte(p.g);
      pDst[2] = ToByte(p.r);
      pDst[3] = ToByte(p.a * fScale);
      pDst += 4;
    }
  }
}

//! The slices of a pass: MakeSpriteSlice for the 24 tiles of each object, on several threads
//! that take the next tile each (one scratch per thread).
struct SSliceJobs
{
  const uint8 *pPixels = nullptr;  //!< the target read back
  int nWidth = 0;                  //!< its width
  int nTile = 0, nTilesPerRow = 0;
  SImage *pImages = nullptr;       //!< one per object
  int nJobs = 0;                   //!< tiles
  std::atomic<int> nNext = 0;

  void Run(SSliceScratch &S)
  {
    for (int t; (t = nNext.fetch_add(1)) < nJobs;)
    {
      const uint8 *pTile = pPixels + ((size_t)(t / nTilesPerRow) * nTile * nWidth + (size_t)(t % nTilesPerRow) * nTile) * 4;
      MakeSpriteSlice(pTile, nWidth * 4, pImages[t / CD3D12Renderer::SPRITE_ANGLES], t % CD3D12Renderer::SPRITE_ANGLES, S);
    }
  }
};

struct SSliceWorker
{
  SSliceJobs *pJobs = nullptr;
  SSliceScratch *pScratch = nullptr;

  static DWORD WINAPI Main(void *pParam)
  {
    const SSliceWorker &W = *(const SSliceWorker *)pParam;
    W.pJobs->Run(*W.pScratch);
    return 0;
  }
};

//! The threads to make the slices on, with the calling one.
int SliceThreadCount()
{
  return (int)std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
}

}

unsigned int CD3D12Renderer::MakeSprite(float object_scale, int tex_size, float angle, IStatObj * pStatObj, uchar * pTmpBuffer, uint def_tid)
{
  // The pictures are made before the next main view (BakeSprites), into a texture array of the
  // object; the IDs the engine gets have no GPU texture.
  if (!pStatObj || !m_pTextures)
    return 0;
  SSpriteSet &Set = m_Sprites[pStatObj];
  const int n = SpriteAngleIndex(angle);
  if (Set.nID[n] && m_pTextures->GetByID(Set.nID[n]) == Set.pTex[n] && Set.pTex[n])
    Set.pTex[n]->AddRef();
  else
  {
    Set.nID[n] = m_pTextures->CreateFromImage(SImage(), "$FarSprite", true, 0);
    Set.pTex[n] = m_pTextures->GetByID(Set.nID[n]);
  }
  Set.bBaked = false;
  if (std::find(m_SpritesToBake.begin(), m_SpritesToBake.end(), pStatObj) == m_SpritesToBake.end())
    m_SpritesToBake.push_back(pStatObj);
  return Set.nID[n];
}

void CD3D12Renderer::BakeSprites()
{
  if (m_SpritesToBake.empty() || m_bSpritePass || m_bReflectionPass || !m_pDevice || !m_pDescriptors || !m_pTextures || !m_pScene ||
      !EnsureFrame())
    return;
  PurgeSprites();
  const auto tStart = std::chrono::steady_clock::now();
  std::vector<IStatObj *> Objects;
  Objects.swap(m_SpritesToBake);

  // Tiles of twice the sprite size, 24 per object, as many objects at a time as the target holds.
  int nSize = SPRITE_MIN_RES;
  while (nSize * 2 <= std::clamp(CV_d3d12_SpriteRes, SPRITE_MIN_RES, SPRITE_MAX_RES))
    nSize *= 2;
  const int nTile = nSize * SPRITE_SUPERSAMPLE;
  const int nTilesPerRow = std::min(SPRITE_TARGET_MAX / nTile, SPRITE_ANGLES);
  const int nMaxRows = SPRITE_TARGET_MAX / nTile;
  const int nObjectsPerPass = std::max(nTilesPerRow * nMaxRows / SPRITE_ANGLES, 1);
  const int nPassObjects = std::min((int)Objects.size(), nObjectsPerPass);
  const int w = nTilesPerRow * nTile;
  const int h = (nPassObjects * SPRITE_ANGLES + nTilesPerRow - 1) / nTilesPerRow * nTile;

  ComPtr<ID3D12Resource> pColor, pDepth;
  const D3D12_CPU_DESCRIPTOR_HANDLE hRTV = m_pDescriptors->AllocRTV(), hDSV = m_pDescriptors->AllocDSV();
  std::vector<uint8> Pixels;
  int nMade = 0;
  if (hRTV.ptr && hDSV.ptr && m_pDevice->CreateOffscreenTargets(w, h, pColor, pDepth, hRTV, hDSV))
  {
    // What the pictures' scenes change.
    const CCamera Camera = m_Camera;
    const Matrix44 CameraView = m_CameraView, ViewMatrix = m_ViewMatrix, ProjMatrix = m_ProjMatrix, SkyProjMatrix = m_SkyProjMatrix;
    const int nMatrixDepth = m_nMatrixDepth;
    const bool bFog = m_bFog;
    float WorldColor[4];
    memcpy(WorldColor, m_WorldColor, sizeof(WorldColor));

    Pixels.resize((size_t)w * h * 4);
    // An array per object of a pass and a scratch per thread, made here: the threads allocate
    // nothing.
    std::vector<SImage> Images(nPassObjects);
    for (SImage &Image : Images)
    {
      Image.Allocate(DXGI_FORMAT_B8G8R8A8_UNORM, nSize, nSize, Img::FullMipCount(nSize, nSize), SPRITE_ANGLES);
      Image.bArray = true;
      Image.nSrcFlags = FT_HASALPHA;
    }
    std::vector<SSliceScratch> Scratch(SliceThreadCount());
    for (SSliceScratch &S : Scratch)
      S.Resize(Images[0]);
    std::vector<SSliceWorker> Workers(Scratch.size() - 1);
    std::vector<HANDLE> Threads;
    Threads.reserve(Workers.size());

    for (size_t nFirst = 0; nFirst < Objects.size(); nFirst += nPassObjects)
    {
      const size_t nCount = std::min(Objects.size() - nFirst, (size_t)nPassObjects);
      m_pDevice->SetOffscreenTargets(pColor.Get(), hRTV, hDSV, w, h);
      const float Clear[4] = { 0, 0, 0, 0 };
      m_pDevice->ClearColor(Clear);
      m_pDevice->ClearDepth(1.0f, 0, true, true);
      m_bSpritePass = true;
      m_bFog = false;
      m_WorldColor[0] = m_WorldColor[1] = m_WorldColor[2] = m_WorldColor[3] = 1;
      for (size_t i = 0; i < nCount; i++)
        BakeSpriteObject(Objects[nFirst + i], (int)i * SPRITE_ANGLES, nTilesPerRow, nTile);
      m_bSpritePass = false;
      m_bTargetRect = false;
      m_pDevice->SetBackBufferTargets();
      if (!m_pDevice->ReadBackTexture(pColor.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, Pixels.data(), w, h))
        continue;

      SSliceJobs Jobs;
      Jobs.pPixels = Pixels.data();
      Jobs.nWidth = w;
      Jobs.nTile = nTile;
      Jobs.nTilesPerRow = nTilesPerRow;
      Jobs.pImages = Images.data();
      Jobs.nJobs = (int)nCount * SPRITE_ANGLES;
      Threads.clear();
      for (size_t k = 0; k < Workers.size(); k++)
      {
        Workers[k] = { &Jobs, &Scratch[k + 1] };
        if (HANDLE hThread = CreateThread(nullptr, 0, SSliceWorker::Main, &Workers[k], 0, nullptr))
          Threads.push_back(hThread);
      }
      Jobs.Run(Scratch[0]);
      for (HANDLE hThread : Threads)
      {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
      }

      for (size_t i = 0; i < nCount; i++)
      {
        auto It = m_Sprites.find(Objects[nFirst + i]);
        if (It == m_Sprites.end())
          continue;
        SSpriteSet &Set = It->second;
        // The object's array (again in the one it has, if the engine baked it before).
        const int nOldID = Set.pArray && m_pTextures->GetByID(Set.nArrayID) == Set.pArray ? Set.nArrayID : 0;
        Set.nArrayID = m_pTextures->CreateFromImage(Images[i], "$FarSprites", true, nOldID);
        Set.pArray = m_pTextures->GetByID(Set.nArrayID);
        Set.bBaked = Set.pArray && Set.pArray->m_pGPU;
        nMade += SPRITE_ANGLES;
      }
    }

    // The frame as it was.
    m_Camera = Camera;
    m_CameraView = CameraView;
    m_ViewMatrix = ViewMatrix;
    m_ProjMatrix = ProjMatrix;
    m_SkyProjMatrix = SkyProjMatrix;
    m_nMatrixDepth = nMatrixDepth;
    m_bFog = bFog;
    memcpy(m_WorldColor, WorldColor, sizeof(WorldColor));
    InvalidateFrameConstants();
  }
  if (hRTV.ptr)
    m_pDescriptors->FreeRTV(hRTV);
  if (hDSV.ptr)
    m_pDescriptors->FreeDSV(hDSV);
  m_pDevice->DeferredRelease(pColor);
  m_pDevice->DeferredRelease(pDepth);
  if (iLog)
    iLog->Log("XRenderD3D12: %d far vegetation sprites (%d x %d) of %d objects made in %.0f ms", nMade, nSize, nSize, (int)Objects.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count());
}

void CD3D12Renderer::BakeSpriteObject(IStatObj *pObj, int nFirstTile, int nTilesPerRow, int nTileSize)
{
  auto It = m_Sprites.find(pObj);
  CLeafBuffer *pLB = pObj->GetLeafBuffer();
  if (It == m_Sprites.end() || !pLB)
    return;
  SSpriteSet &Set = It->second;

  // The quad of DrawObjSprites: as wide as the object turning about its z axis (the largest
  // horizontal distance of a box corner from the axis), as high as the box, centred on the box.
  const Vec3 vMin = pObj->GetBoxMin(), vMax = pObj->GetBoxMax();
  float fRadius = 0;
  for (const float x : { vMin.x, vMax.x })
    for (const float y : { vMin.y, vMax.y })
      fRadius = std::max(fRadius, sqrtf(x * x + y * y));
  Set.vBoxMin = vMin;
  Set.vBoxMax = vMax;
  Set.fRadius = fRadius;
  const float fHeight = vMax.z - vMin.z;
  if (fRadius <= 0 || fHeight <= 0)
    return;
  const Vec3 vCenter = (vMin + vMax) * 0.5f;

  // An orthographic camera on the box's centre: x from -fRadius to fRadius across the view, y the
  // box's height; the box is within fRadius of the centre, so with the camera fRadius + 1 away all
  // of it lies between 0.5 and 2 fRadius + 2 in front of the camera.
  const float fNear = 0.5f, fFar = 2 * fRadius + 2;
  Matrix44 Proj;
  memset(Proj.data, 0, sizeof(Proj.data));
  Proj.data[0] = 1 / fRadius;
  Proj.data[5] = 2 / fHeight;
  Proj.data[10] = 1 / (fNear - fFar);
  Proj.data[14] = fNear / (fNear - fFar);
  Proj.data[15] = 1;

  for (int n = 0; n < SPRITE_ANGLES; n++)
  {
    // Looking along d (horizontal), x to the right (d x z), y up, z back.
    const float a = SpriteViewAngle(n);
    const Vec3 d(cosf(a), sinf(a), 0), e(sinf(a), -cosf(a), 0), up(0, 0, 1), back = -d;
    const Vec3 vEye = vCenter - d * (fRadius + 1);
    Matrix44 View;
    memset(View.data, 0, sizeof(View.data));
    View.data[0] = e.x;  View.data[1] = up.x;  View.data[2] = back.x;
    View.data[4] = e.y;  View.data[5] = up.y;  View.data[6] = back.y;
    View.data[8] = e.z;  View.data[9] = up.z;  View.data[10] = back.z;
    View.data[12] = -vEye.Dot(e);
    View.data[13] = -vEye.Dot(up);
    View.data[14] = -vEye.Dot(back);
    View.data[15] = 1;
    m_CameraView = m_ViewMatrix = View;
    m_nMatrixDepth = 0;
    m_ProjMatrix = m_SkyProjMatrix = Proj;
    InvalidateFrameConstants();

    const int t = nFirstTile + n;
    const int x = t % nTilesPerRow * nTileSize, y = t / nTilesPerRow * nTileSize;
    m_bTargetRect = true;
    m_TargetRect = { x, y, x + nTileSize, y + nTileSize };

    // The object as the scene draws it, in a scene of its own.
    EF_StartEf();
    CCObject *pOb = m_pScene->GetTempObject();
    pOb->m_Matrix.SetIdentity();
    pOb->m_Color = CFColor(SPRITE_COLOR, SPRITE_COLOR, SPRITE_COLOR, 1);
    pOb->m_AmbColor = Vec3(1, 1, 1);
    pLB->AddRenderElements(pOb, 0, -1, 0, 0, nullptr);
    EF_EndEf3D(SHDF_SORT);
  }
}

void CD3D12Renderer::PurgeSprites()
{
  // An object whose sprites the engine has all removed is gone: its array goes too.
  if (!m_pTextures)
    return;
  for (auto It = m_Sprites.begin(); It != m_Sprites.end();)
  {
    const SSpriteSet &Set = It->second;
    bool bUsed = false;
    for (int n = 0; n < SPRITE_ANGLES && !bUsed; n++)
      bUsed = Set.pTex[n] && m_pTextures->GetByID(Set.nID[n]) == Set.pTex[n];
    if (bUsed)
    {
      ++It;
      continue;
    }
    if (Set.pArray && m_pTextures->GetByID(Set.nArrayID) == Set.pArray)
      m_pTextures->ReleaseTexture(Set.pArray, true);
    m_SpritesToBake.erase(std::remove(m_SpritesToBake.begin(), m_SpritesToBake.end(), It->first), m_SpritesToBake.end());
    It = m_Sprites.erase(It);
  }
}

void CD3D12Renderer::ReleaseSprites()
{
  // The textures go with the others.
  m_Sprites.clear();
  m_SpritesToBake.clear();
  m_SpriteQuads.clear();
}

void CD3D12Renderer::DrawObjSprites(list2<CStatObjInst*> *pList, float fMaxViewDist, CObjManager *pObjMan)
{
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  if (!pList || pList->Count() <= 0 || !p3D || !m_pTextures || !EnsureFrame())
    return;

  // The groups of the instances, looked up once per call: the sprites of the group's object (its
  // LOD 1 when it has one), the brightness, and bFadeSize from the engine's array of groups.
  struct SGroup
  {
    bool bLooked = false;
    const SSpriteSet *pSet = nullptr;
    float fBrightness = 1;
    bool bFadeSize = true;
  };
  std::array<SGroup, 256> Groups;
  const uint8 *pGroupArray = nullptr;
  int nGroupCount = 0;
  if (pObjMan)
  {
    pGroupArray = *(const uint8 *const *)((const uint8 *)pObjMan + nBin64Offset_ObjManager_Groups);
    nGroupCount = *(const int *)((const uint8 *)pObjMan + nBin64Offset_ObjManager_GroupCount);
  }
  auto FindSet = [this](IStatObj *pObj) -> const SSpriteSet *
  {
    auto It = pObj ? m_Sprites.find(pObj) : m_Sprites.end();
    return It != m_Sprites.end() && It->second.bBaked ? &It->second : nullptr;
  };

  const Vec3 vCam = m_Camera.GetPos();
  m_SpriteQuads.clear();
  for (int i = 0; i < pList->Count(); i++)
  {
    const uint8 *p = (const uint8 *)(*pList)[i];
    if (!p)
      continue;
    const int nGroup = p[nBin64Offset_StatObjInst_Group];
    SGroup &g = Groups[nGroup];
    if (!g.bLooked)
    {
      g.bLooked = true;
      IStatInstGroup Info;
      if (p3D->GetStatInstGroup(nGroup, Info) && Info.pStatObj)
      {
        IStatObj *pLod = Info.pStatObj->GetLodObject(1);
        g.pSet = FindSet(pLod ? pLod : Info.pStatObj);
        if (!g.pSet && pLod)
          g.pSet = FindSet(Info.pStatObj);
        g.fBrightness = Info.fBrightness;
      }
      if (pGroupArray && nGroup < nGroupCount)
        g.bFadeSize = pGroupArray[(size_t)nGroup * nBin64Size_StatInstGroup + nBin64Offset_StatInstGroup_FadeSize] != 0;
    }
    if (!g.pSet)
      continue;

    const float fInstMaxViewDist = *(const float *)(p + nBin64Offset_StatObjInst_MaxViewDist);
    const float fDistance = *(const float *)(p + nBin64Offset_StatObjInst_Distance);
    const Vec3 vPos = *(const Vec3 *)(p + nBin64Offset_StatObjInst_Pos);
    const float fScale = *(const float *)(p + nBin64Offset_StatObjInst_Scale);
    const int nBrightness = p[nBin64Offset_StatObjInst_Brightness];

    // With bFadeSize the quad shrinks to nothing over the last eighth of the view distance.
    float f = 1;
    if (g.bFadeSize)
    {
      const float E = std::min(fInstMaxViewDist, 0.8f * fMaxViewDist);
      f = E > 0 ? std::min(1.0f, (E - fDistance) / (E / 8)) : 0.0f;
    }
    const float k = fScale * f;
    if (k <= 0)
      continue;

    // The picture of the direction from the camera to the instance.
    const float dx = vPos.x - vCam.x, dy = vPos.y - vCam.y;
    const float fLen = sqrtf(dx * dx + dy * dy);
    if (fLen < 1e-3f)
      continue;
    float fDegrees = RAD2DEG(atan2f(dy, dx));
    if (fDegrees < 0)
      fDegrees += 360;
    const int n = SpriteIndexOfDirection(fDegrees);
    const SSpriteSet &Set = *g.pSet;
    if (!Set.pTex[n] || m_pTextures->GetByID(Set.nID[n]) != Set.pTex[n] || m_pTextures->GetByID(Set.nArrayID) != Set.pArray ||
        !Set.pArray->m_pGPU)
      continue;

    // Standing on the box (scaled about the instance's position), facing the camera.
    const float ex = dy / fLen, ey = -dx / fLen;
    const float cx = vPos.x + k * (Set.vBoxMin.x + Set.vBoxMax.x) * 0.5f, cy = vPos.y + k * (Set.vBoxMin.y + Set.vBoxMax.y) * 0.5f;
    const float z0 = vPos.z + k * Set.vBoxMin.z, z1 = vPos.z + k * Set.vBoxMax.z;
    const float r = k * Set.fRadius;
    const uint8 b = (uint8)std::lround(std::clamp(g.fBrightness * nBrightness / 100.0f, 0.0f, 1.0f) * 255);
    UCol Color;
    Color.dcolor = 0xFF000000u | ((uint32)b << 16) | ((uint32)b << 8) | b;
    SSpriteQuad &q = m_SpriteQuads.emplace_back();
    // u from 2 n to 2 n + 1: slice n (DF_TEX0_ARRAY).
    const float u = 2.0f * n;
    q.nSRV = Set.pArray->GetSRV();
    q.v[0] = { Vec3(cx - r * ex, cy - r * ey, z1), Color, { u, 0 } };
    q.v[1] = { Vec3(cx + r * ex, cy + r * ey, z1), Color, { u + 1, 0 } };
    q.v[2] = { Vec3(cx - r * ex, cy - r * ey, z0), Color, { u, 1 } };
    q.v[3] = { Vec3(cx + r * ex, cy + r * ey, z0), Color, { u + 1, 1 } };
  }
  if (m_SpriteQuads.empty())
    return;

  // One draw per object (texture array): texture * vertex colour * 2 * the world colour, alpha
  // tested, depth written, fogged, both sides.
  std::sort(m_SpriteQuads.begin(), m_SpriteQuads.end(), [](const SSpriteQuad &a, const SSpriteQuad &b) { return a.nSRV < b.nSRV; });
  SPipelineKey Key = {};
  Key.nState = GS_DEPTHWRITE;
  Key.nProgram = eD3D12P_General;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  const Matrix44 ViewProj = m_CameraView * m_ProjMatrix;
  memcpy(dc.mWorldViewProj, ViewProj.data, sizeof(dc.mWorldViewProj));
  dc.nFlags = DF_TEX0_ARRAY | DF_VCOLOR | DF_VCOLOR_X2 | DF_ALPHATEST | (m_bFog ? DF_FOG : 0);
  dc.fAlphaRef = SPRITE_ALPHA_REF;
  dc.vColor[0] = m_WorldColor[0];
  dc.vColor[1] = m_WorldColor[1];
  dc.vColor[2] = m_WorldColor[2];
  dc.vColor[3] = 1;
  dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
  m_fDepthMin = 0;
  m_fDepthMax = 1;
  m_bItemScissor = false;
  constexpr size_t MAX_QUADS = 0x10000 / 4;
  std::vector<struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F> Verts;
  std::vector<ushort> Inds;
  for (size_t nFirst = 0; nFirst < m_SpriteQuads.size();)
  {
    size_t nEnd = nFirst;
    while (nEnd < m_SpriteQuads.size() && m_SpriteQuads[nEnd].nSRV == m_SpriteQuads[nFirst].nSRV && nEnd - nFirst < MAX_QUADS)
      nEnd++;
    Verts.clear();
    Inds.clear();
    for (size_t i = nFirst; i < nEnd; i++)
    {
      const ushort nBase = (ushort)Verts.size();
      Verts.insert(Verts.end(), std::begin(m_SpriteQuads[i].v), std::end(m_SpriteQuads[i].v));
      for (const ushort k : { 0, 1, 2, 2, 1, 3 })
        Inds.push_back((ushort)(nBase + k));
    }
    dc.nTex0 = m_SpriteQuads[nFirst].nSRV;
    if (m_pFrameDump)
      fprintf(m_pFrameDump.get(), "    far sprites: %d quads, texture table %d\n", (int)(nEnd - nFirst), dc.nTex0);
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F_COL4UB_TEX2F, Verts.data(), (int)Verts.size(), Inds.data(), (int)Inds.size());
    nFirst = nEnd;
  }
}
