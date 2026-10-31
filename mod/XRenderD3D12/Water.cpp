// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Water.cpp
//	Description: The water: the ocean around the camera and the water
//	volumes (eDC_Water), the foam on the beach (eDC_WaterBeach), the
//	sea floor under the ocean (eDC_WaterBottom) and the caustics on the
//	terrain under it (eDC_Caustics); also the glass of the scopes
//	(eDC_ScopeLens), which shows the same copy of the scene, or a copy of
//	the part of it around the middle of the screen that the glass shows.
//
//	The water surfaces come first in the blended list. Before the first
//	one, the back buffer and the depth buffer are copied; the water program
//	(Shaders/Water.hlsl) reads the copies for the refraction and the depth
//	of the water behind the surface, and the scene's sky box for the
//	reflection. The beach is the foam texture running up the shore in three
//	waves, wobbled by a noise, over the wet sand; the sea floor is opaque,
//	lit by the world colour.
//
//	With r_WaterReflections the ocean also reflects the scene mirrored in
//	its plane: before the main view is drawn, the 3D engine renders the sky,
//	the terrain, the static objects and the far sprites (DrawLowDetail with
//	d3d12_WaterReflectionFlags; not the entities: drawn for another camera,
//	the fish then swim differently in the main view) seen by the camera
//	mirrored in the water plane into a target of its own, as a scene nested
//	in the main one, redrawn as r_WaterUpdateFactor, r_WaterUpdateDistance and
//	r_WaterUpdateDeltaAngle say. Its near plane is the water plane, so
//	nothing under the water shows in it. With r_HDRRendering the target is
//	in D3D12_HDR_FORMAT, as the main scene. Its mip levels (each the mean of
//	2 x 2 texels of the one above) let the water blur it where the ripple
//	gets finer than a pixel (Water.hlsl; d3d12_WaterReflectionBlur): the
//	wave texture's alpha holds the squared slope, whose mean over a mip
//	texel, less the square of the mean slope, is the variance it lost.
//
//	With d3d12_RTReflections the water also reflects the scene for rays
//	(RayTracing.h): the main view's opaque draws and the static ones of the
//	planar reflection's scene, which sees what is out of the main view;
//	the water program's variants with rays (Water.hlsl, XR_RAYTRACE) trace
//	a ray along the reflected direction instead of the screen space
//	reflection, or where it finds nothing.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "D3D12Pipeline.h"
#include "Texture.h"
#include "Scene.h"
#include "RayTracing.h"
#include <atomic>

namespace
{

void Set4(float v[4], float x, float y, float z, float w)
{
  v[0] = x; v[1] = y; v[2] = z; v[3] = w;
}

//! A float shader parameter of the object, fDefault without one.
float GetObjectParam(const CCObject *pObj, const char *szName, float fDefault)
{
  const TArray<SShaderParam> *pParams = pObj->m_ShaderParams;
  for (int i = 0; pParams && i < pParams->Num(); i++)
  {
    const SShaderParam &p = (*pParams)[i];
    if (p.m_Type == eType_FLOAT && !_stricmp(p.m_Name, szName))
      return p.m_Value.m_Float;
  }
  return fDefault;
}

float Frac(float f)
{
  return f - floorf(f);
}

//! The ocean (one of the terrain's water shaders), not a water volume.
bool IsOcean(const SRenderItem &it)
{
  return it.eDrawClass == eDC_Water && _strnicmp(it.pShader->GetName(), "terrain", 7) == 0;
}

float Sign(float f)
{
  return f > 0 ? 1.0f : f < 0 ? -1.0f : 0.0f;
}

// The water reflection is redrawn after at most this many seconds (with r_WaterUpdateFactor), and
// its camera's near plane is at most this far.
constexpr float REFLECTION_MAX_INTERVAL = 0.3f;
constexpr float REFLECTION_MAX_NEAR = 16.0f;

//! A triangle over the whole target (clip space).
const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };

// The optics of water without a fog volume: absorption per metre (red goes first; weak, as the
// levels paint the sea floor in the colours of the water above it), the light scattered per metre,
// the colour of deep water in full light where the level gives none (in the ocean's fog volume);
// and how far the waves move the refracted picture (a fraction of the screen).
constexpr float WATER_ABSORPTION[3] = { 0.030f, 0.024f, 0.005f };
constexpr float WATER_TURBIDITY = 0.005f;
constexpr float WATER_SCATTER[3] = { 0.18f, 0.28f, 0.54f };
constexpr float WATER_DISPLACEMENT = 0.03f;

// A beach wave runs up the shore and back in this many seconds (BEACH_WAVES of them at a time), the
// second this many seconds behind the first; the foam is white, this many times the world colour's
// mean (about as bright as the sand, so that it shows over the water).
// The foam wobbles: its texture moved by the slopes of a noise normal map this many metres across
// times this (a few hundredths of the texture, the front's teeth made uneven), the noise swaying back
// and forth by a quarter of its size with the wave.
constexpr float BEACH_WAVE_PERIOD = 10.0f;
constexpr float BEACH_WAVE_LAG = 4.0f;
constexpr float BEACH_FOAM_BRIGHTNESS = 1.05f;
constexpr float BEACH_BUMP_SIZE = 16.0f;
constexpr float BEACH_BUMP_OFFSET = 0.5f;

constexpr int WAVE_TEXTURE_SIZE = 256;
constexpr float WAVE_SLOPE_SQ_RANGE = 16.0f;

// The caustics: the pattern repeats every so many metres, the light of a texel of the texture is
// stored as 255 / CAUSTICS_RANGE per unit of the mean; the share of the light on the sea floor that
// they move around (the direct sun's), and the depth where they are gone.
constexpr int CAUSTICS_TEXTURE_SIZE = 256;
constexpr float CAUSTICS_RANGE = 6.0f;
constexpr float CAUSTICS_SIZE = 4.0f;
constexpr float CAUSTICS_STRENGTH = 0.2f;
constexpr float CAUSTICS_DEPTH = 12.0f;

// The glass of a scope: the picture through it is the scene around the middle of the screen (where
// the scope aims), this many times as wide as the glass (it shows a little more than it covers);
// the unevenness of the glass moves it by this RMS in the glass's texture coordinates; its tint
// (blue: the red mostly goes), how opaque its middle is, and the distance from the middle (in its
// texture coordinates) where that halves, falling off steeply beyond.
constexpr float SCOPE_LENS_FIELD = 1.2f;
constexpr float SCOPE_LENS_WOBBLE = 0.004f;
constexpr float SCOPE_LENS_TINT[3] = { 0.37f, 0.58f, 0.82f };
constexpr float SCOPE_LENS_OPACITY = 0.9f;
constexpr float SCOPE_LENS_RADIUS = 0.3f;
// The part of the scene copied for the glass: around the middle of the screen, SCOPE_LENS_FIELD times
// the glass's size on the screen, this much more (its unevenness, the derivatives of a quad) and
// this many pixels.
constexpr float SCOPE_LENS_REACH = 1.25f;
constexpr float SCOPE_LENS_MARGIN = 4.0f;

//! The wave texture, B, G, R, A bytes: the slopes of a height field that tiles over the texture,
//! dH/du in red and dH/dv in green, the height in blue (128: 0; 32 units: the RMS of the slopes,
//! and of the height), in alpha the square of the slopes as red and green give them, (dH/du)^2 +
//! (dH/dv)^2 in those units, WAVE_SLOPE_SQ_RANGE at 255 (the mean of the mip levels' texels, less
//! the square of their mean slope, is the variance of the slopes they smoothed out). The field is
//! a few hundred waves with whole numbers of cycles per tile, their heights falling with the wave
//! number as in a wind sea, most of them along u.
std::vector<byte> MakeWaveTexture(int N)
{
  constexpr int TABLE = 4096;               // a whole cycle; N divides it
  constexpr float PI = 3.14159265f;
  std::vector<float> Sin(TABLE);
  for (int i = 0; i < TABLE; i++)
    Sin[i] = sinf(i * 2 * PI / TABLE);

  struct SWave
  {
    int kx, ky, nPhase;
    float a;
  };
  std::vector<SWave> Waves;
  uint32 nRand = 0x2545F491u;
  auto Rand = [&nRand]()
  {
    nRand ^= nRand << 13;
    nRand ^= nRand >> 17;
    nRand ^= nRand << 5;
    return (nRand & 0xFFFFFF) / 16777216.0f;
  };
  constexpr int K_MIN = 2, K_MAX = 40;
  for (int ky = 0; ky <= K_MAX; ky++)
    for (int kx = -K_MAX; kx <= K_MAX; kx++)
    {
      // One of k and -k.
      if (ky == 0 && kx <= 0)
        continue;
      const float k = sqrtf((float)(kx * kx + ky * ky));
      const float fChance = Rand();
      if (k < K_MIN || k > K_MAX || fChance > 0.12f)
        continue;
      const float c = kx / k;
      const float fSpread = 0.15f + 0.85f * c * c;
      Waves.push_back({ kx, ky, (int)(Rand() * TABLE), powf(k, -1.9f) * fSpread * (0.5f + Rand()) });
    }

  // H = sum a cos(2 pi (k . p) / N + phase); the slopes up to the factor 2 pi / N.
  const int nStep = TABLE / N;
  std::vector<float> Field((size_t)N * N * 3);
  double fSlopeSq = 0, fHeightSq = 0;
  for (int y = 0; y < N; y++)
    for (int x = 0; x < N; x++)
    {
      float h = 0, sx = 0, sy = 0;
      for (const SWave &w : Waves)
      {
        const int i = ((w.kx * x + w.ky * y) * nStep + w.nPhase) & (TABLE - 1);
        const float fSin = Sin[i], fCos = Sin[(i + TABLE / 4) & (TABLE - 1)];
        h += w.a * fCos;
        sx -= w.a * w.kx * fSin;
        sy -= w.a * w.ky * fSin;
      }
      float *pOut = &Field[((size_t)y * N + x) * 3];
      pOut[0] = sx;
      pOut[1] = sy;
      pOut[2] = h;
      fSlopeSq += 0.5 * (sx * sx + sy * sy);
      fHeightSq += h * h;
    }
  const float fSlopeScale = 32.0f / (float)sqrt(fSlopeSq / ((double)N * N));
  const float fHeightScale = 32.0f / (float)sqrt(fHeightSq / ((double)N * N));
  auto Encode = [](float f) { return (byte)std::clamp((int)lrintf(128 + f), 1, 255); };
  std::vector<byte> Pixels((size_t)N * N * 4);
  for (size_t i = 0; i < (size_t)N * N; i++)
  {
    Pixels[i * 4 + 0] = Encode(Field[i * 3 + 2] * fHeightScale);
    Pixels[i * 4 + 1] = Encode(Field[i * 3 + 1] * fSlopeScale);
    Pixels[i * 4 + 2] = Encode(Field[i * 3 + 0] * fSlopeScale);
    const float dx = (Pixels[i * 4 + 2] - 128) / 32.0f, dy = (Pixels[i * 4 + 1] - 128) / 32.0f;
    Pixels[i * 4 + 3] = (byte)std::clamp((int)lrintf((dx * dx + dy * dy) * (255 / WAVE_SLOPE_SQ_RANGE)), 0, 255);
  }
  return Pixels;
}

//! The caustics texture, B, G, R, A bytes (the same in each): the light a uniform beam gives a
//! plane under a tiling wave field, as the density of rays refracted by its slopes (a few rays
//! per texel, each moved by the slope where it crosses the surface and spread over the four
//! nearest texels), scaled to a mean of 1 and stored as 255 / CAUSTICS_RANGE per unit.
//! fMinMean: the mean of the smaller of two texels taken at random (of the stored values).
std::vector<byte> MakeCausticsTexture(int N, float &fMinMean)
{
  constexpr int TABLE = 4096;               // a whole cycle; N divides it
  constexpr float PI = 3.14159265f;
  std::vector<float> Sin(TABLE);
  for (int i = 0; i < TABLE; i++)
    Sin[i] = sinf(i * 2 * PI / TABLE);
  uint32 nRand = 0x1F3D5B79u;
  auto Rand = [&nRand]()
  {
    nRand ^= nRand << 13;
    nRand ^= nRand >> 17;
    nRand ^= nRand << 5;
    return (nRand & 0xFFFFFF) / 16777216.0f;
  };

  // The slopes of a height field of waves in every direction, whole numbers of cycles per tile,
  // their heights falling with the wave number; RMS 1.
  constexpr int K_MIN = 2, K_MAX = 24;
  const int nStep = TABLE / N;
  std::vector<float> SX((size_t)N * N), SY((size_t)N * N);
  for (int ky = 0; ky <= K_MAX; ky++)
    for (int kx = -K_MAX; kx <= K_MAX; kx++)
    {
      if (ky == 0 && kx <= 0)
        continue;
      const float k = sqrtf((float)(kx * kx + ky * ky));
      const float fChance = Rand();
      if (k < K_MIN || k > K_MAX || fChance > 0.25f)
        continue;
      const float a = powf(k, -1.9f) * (0.5f + Rand());
      const int nPhase = (int)(Rand() * TABLE);
      for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
        {
          const float s = a * Sin[((kx * x + ky * y) * nStep + nPhase) & (TABLE - 1)];
          SX[(size_t)y * N + x] -= s * kx;
          SY[(size_t)y * N + x] -= s * ky;
        }
    }
  double fSq = 0;
  for (size_t i = 0; i < SX.size(); i++)
    fSq += 0.5 * (SX[i] * SX[i] + SY[i] * SY[i]);
  const float fNorm = 1 / std::max((float)sqrt(fSq / SX.size()), 1e-6f);

  // The rays: RAYS x RAYS per texel, moved by DISPLACEMENT texels per unit of slope.
  constexpr int RAYS = 4;
  constexpr float DISPLACEMENT = 4.0f;
  const auto Wrap = [N](int i) { return ((i % N) + N) % N; };
  const auto Bilinear = [&](const std::vector<float> &F, float x, float y)
  {
    const int x0 = (int)floorf(x), y0 = (int)floorf(y);
    const float tx = x - x0, ty = y - y0;
    const int xa = Wrap(x0), xb = Wrap(x0 + 1), ya = Wrap(y0), yb = Wrap(y0 + 1);
    return (F[(size_t)ya * N + xa] * (1 - tx) + F[(size_t)ya * N + xb] * tx) * (1 - ty) +
           (F[(size_t)yb * N + xa] * (1 - tx) + F[(size_t)yb * N + xb] * tx) * ty;
  };
  std::vector<float> Light((size_t)N * N);
  for (int ry = 0; ry < N * RAYS; ry++)
    for (int rx = 0; rx < N * RAYS; rx++)
    {
      const float x = (float)rx / RAYS, y = (float)ry / RAYS;
      const float px = x - DISPLACEMENT * fNorm * Bilinear(SX, x, y), py = y - DISPLACEMENT * fNorm * Bilinear(SY, x, y);
      const int x0 = (int)floorf(px), y0 = (int)floorf(py);
      const float tx = px - x0, ty = py - y0;
      const int xa = Wrap(x0), xb = Wrap(x0 + 1), ya = Wrap(y0), yb = Wrap(y0 + 1);
      Light[(size_t)ya * N + xa] += (1 - tx) * (1 - ty);
      Light[(size_t)ya * N + xb] += tx * (1 - ty);
      Light[(size_t)yb * N + xa] += (1 - tx) * ty;
      Light[(size_t)yb * N + xb] += tx * ty;
    }

  // Each texel got RAYS * RAYS rays on average.
  std::vector<byte> Pixels((size_t)N * N * 4);
  std::vector<float> Stored(Light.size());
  for (size_t i = 0; i < Light.size(); i++)
  {
    const int v = std::clamp((int)lrintf(Light[i] / (RAYS * RAYS) * 255 / CAUSTICS_RANGE), 0, 255);
    Pixels[i * 4 + 0] = Pixels[i * 4 + 1] = Pixels[i * 4 + 2] = Pixels[i * 4 + 3] = (byte)v;
    Stored[i] = v * CAUSTICS_RANGE / 255;
  }
  // The smaller of two: the i-th of n sorted values (from 0) is it with chance (2 (n - i) - 1) / n^2.
  std::sort(Stored.begin(), Stored.end());
  const double n = (double)Stored.size();
  double fSum = 0;
  for (size_t i = 0; i < Stored.size(); i++)
    fSum += Stored[i] * (2 * (n - i) - 1);
  fMinMean = (float)(fSum / (n * n));
  return Pixels;
}

//! The pixels of the wave and caustics textures: the same every time, made once for the process on
//! a thread of its own from the renderer's start (StartWaterTextures). Made on first use they took
//! some 50 ms of the frame where the water first shows. Kept for the process (a renderer made again
//! takes them as they are).
struct SWaterPixels
{
  std::vector<byte> Waves, Caustics;
  float fCausticsMinMean = 1;
};
std::atomic<SWaterPixels *> g_pWaterPixels{ nullptr };
HANDLE g_hWaterThread = nullptr;

DWORD WINAPI MakeWaterPixels(void *)
{
  SWaterPixels *p = new SWaterPixels;
  p->Waves = MakeWaveTexture(WAVE_TEXTURE_SIZE);
  p->Caustics = MakeCausticsTexture(CAUSTICS_TEXTURE_SIZE, p->fCausticsMinMean);
  g_pWaterPixels = p;
  return 0;
}

//! Waits for the thread (made here, on this thread, when it could not start).
const SWaterPixels &WaterPixels()
{
  if (!g_pWaterPixels && g_hWaterThread)
    WaitForSingleObject(g_hWaterThread, INFINITE);
  if (!g_pWaterPixels)
    MakeWaterPixels(nullptr);
  return *g_pWaterPixels;
}

}

void CD3D12Renderer::StartWaterTextures()
{
  if (!g_pWaterPixels && !g_hWaterThread)
    g_hWaterThread = CreateThread(nullptr, 0, MakeWaterPixels, nullptr, 0, nullptr);
}

void CD3D12Renderer::EndWaterTextures()
{
  if (g_hWaterThread)
  {
    WaitForSingleObject(g_hWaterThread, INFINITE);
    CloseHandle(g_hWaterThread);
    g_hWaterThread = nullptr;
  }
}

int CD3D12Renderer::GetCausticsTexture()
{
  if (!m_nCausticsTexture && m_pTextures)
  {
    const SWaterPixels &Pixels = WaterPixels();
    m_fCausticsMinMean = Pixels.fCausticsMinMean;
    m_nCausticsTexture = m_pTextures->CreateFromMemory(Pixels.Caustics.data(), CAUSTICS_TEXTURE_SIZE, CAUSTICS_TEXTURE_SIZE, eTF_8888, 2, true,
                                                       FILTER_TRILINEAR, 0, "$Caustics", 0);
    if (!m_nCausticsTexture || !(m_fCausticsMinMean > 0))
      m_nCausticsTexture = -1;
  }
  return m_nCausticsTexture > 0 ? m_pTextures->GetSRV(m_nCausticsTexture) : -1;
}

bool CD3D12Renderer::SetupCausticsDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it)
{
  // The fog pass of a sector under the ocean (Scene.cpp), in daylight.
  const SFogVolume *pFV = m_pScene->GetFogVolume(it.nFog);
  const float fSun = std::clamp(m_SunDir[2] * 4, 0.0f, 1.0f);
  if (!CV_d3d12_Caustics || !pFV || !pFV->bCaustics || fSun <= 0 || m_bReflectionPass || m_bEnvCubePass || m_bSpritePass)
    return false;
  const int nTex = GetCausticsTexture();
  if (nTex < 0)
    return false;

  // Multiplied in twice over the terrain drawn (before its shadows), without depth writes.
  Key.nProgram = eD3D12P_General;
  Key.nState = GS_BLSRC_DSTCOL | GS_BLDST_SRCCOL;
  dc.nFlags = DF_CAUSTICS;
  dc.nTex0 = (uint32)nTex;
  // The light under a point of the floor crossed the surface towards the sun, refracted (sine over
  // 1.33): its horizontal shift per metre of depth.
  const float fH = sqrtf(m_SunDir[0] * m_SunDir[0] + m_SunDir[1] * m_SunDir[1]);
  const float fSinW = std::min(fH / 1.33f, 0.99f), fShift = fSinW / sqrtf(1 - fSinW * fSinW) / std::max(fH, 1e-4f);
  const float fTime = iTimer ? iTimer->GetCurrTime() : 0.0f;
  Set4(dc.vParams0, pFV->fFogLayerZ, 1 / CAUSTICS_SIZE, fTime, CAUSTICS_STRENGTH * fSun);
  Set4(dc.vParams1, m_SunDir[0] * fShift, m_SunDir[1] * fShift, CAUSTICS_DEPTH, CAUSTICS_RANGE / m_fCausticsMinMean);
  return true;
}

bool CD3D12Renderer::SetupScopeLensDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it, const Matrix44 &WorldViewProj)
{
  // With r_ScopeLens_fx, in the main view: over the copy of the scene the list has (the water's in
  // the blended list), else over a copy of the part of it that the glass shows (the colour alone),
  // blended, without depth writes. Its samples stay in that part.
  if (!IsScopeLensOn() || m_bReflectionPass || m_bShadowMapPass || m_bSpritePass || m_nRecurseLevel > 1)
    return false;
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  int Rect[4] = { 0, 0, w, h };
  if (!m_bSceneCopied && !(ScopeLensRect(it, WorldViewProj, Rect) ? CopySceneForWater(Rect) : CopySceneForWater()))
    return false;
  dc.nFlags = 0;
  dc.nFlags2 = DF2_SCOPE_LENS;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  dc.nTex1 = (uint32)GetWaveTexture();
  Set4(dc.vColor, SCOPE_LENS_TINT[0], SCOPE_LENS_TINT[1], SCOPE_LENS_TINT[2], SCOPE_LENS_OPACITY);
  Set4(dc.vParams0, SCOPE_LENS_FIELD, SCOPE_LENS_WOBBLE, SCOPE_LENS_RADIUS, 0);
  Set4(dc.vParams1, (Rect[0] + 0.5f) / w, (Rect[1] + 0.5f) / h, (Rect[2] - 0.5f) / w, (Rect[3] - 0.5f) / h);
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  return true;
}

bool CD3D12Renderer::ScopeLensRect(const SRenderItem &it, const Matrix44 &WorldViewProj, int Rect[4]) const
{
  // The glass's vertices on the screen.
  const CREOcLeaf *pLeaf = it.pRE && it.pRE->m_Type == eDATA_OcLeaf ? static_cast<const CREOcLeaf *>(it.pRE) : nullptr;
  const CMatInfo *pChunk = pLeaf ? pLeaf->m_pChunk : nullptr;
  const CLeafBuffer *pVC = pLeaf && pLeaf->m_pBuffer ? pLeaf->m_pBuffer->GetVertexContainer() : nullptr;
  if (!pChunk || !pVC)
    return false;
  const CVertexBuffer *pVB = pVC->m_pSecVertBuffer && pVC->m_pSecVertBuffer->m_VS[VSF_GENERAL].m_VData ? pVC->m_pSecVertBuffer : pVC->m_pVertexBuffer;
  if (!pVB || !pVB->m_VS[VSF_GENERAL].m_VData)
    return false;
  const int nStride = CD3D12Pipeline::GetVertexSize(pVB->m_vertexformat);
  const int nFirst = std::max(pChunk->nFirstVertId, 0);
  const int nEnd = std::min(nFirst + pChunk->nNumVerts, pVB->m_NumVerts);
  if (nStride < (int)sizeof(Vec3) || nFirst >= nEnd)
    return false;
  const byte *pData = (const byte *)pVB->m_VS[VSF_GENERAL].m_VData;
  const float *m = WorldViewProj.data;
  float Min[2] = { 1e30f, 1e30f }, Max[2] = { -1e30f, -1e30f };
  for (int i = nFirst; i < nEnd; i++)
  {
    const Vec3 &p = *(const Vec3 *)(pData + (size_t)i * nStride);
    const float fW = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
    if (fW <= 1e-4f)
      return false;
    for (int c = 0; c < 2; c++)
    {
      const float v = (p.x * m[c] + p.y * m[4 + c] + p.z * m[8 + c] + m[12 + c]) / fW;
      Min[c] = std::min(Min[c], v);
      Max[c] = std::max(Max[c], v);
    }
  }
  // A pixel shows the scene at SCOPE_LENS_FIELD times its offset from the middle of the glass, which
  // is on the glass, from the middle of the screen (in the frame's pixels, as the shader's); that in
  // the target's pixels, with the texels next to it that a bilinear sample reads.
  float sx = 1, sy = 1;
  if (m_pDevice->IsInScene())
    GetSceneScale(sx, sy);
  const float Screen[2] = { std::max(m_Viewport[2], 1) * sx, std::max(m_Viewport[3], 1) * sy };
  const float fSize = std::max((Max[0] - Min[0]) * 0.5f * Screen[0], (Max[1] - Min[1]) * 0.5f * Screen[1]);
  const float fReach = SCOPE_LENS_FIELD * SCOPE_LENS_REACH * fSize + SCOPE_LENS_MARGIN;
  const int Size[2] = { m_pDevice->GetMainWidth(), m_pDevice->GetMainHeight() };
  for (int c = 0; c < 2; c++)
  {
    const float fScale = Size[c] / Screen[c];
    Rect[c] = std::clamp((int)floorf((0.5f * Screen[c] - fReach) * fScale - 0.5f), 0, Size[c]);
    Rect[2 + c] = std::clamp((int)floorf((0.5f * Screen[c] + fReach) * fScale + 0.5f) + 1, Rect[c], Size[c]);
  }
  return Rect[2] > Rect[0] && Rect[3] > Rect[1];
}

int CD3D12Renderer::GetWaveTexture()
{
  if (!m_nWaveTexture && m_pTextures)
  {
    m_nWaveTexture = m_pTextures->CreateFromMemory(WaterPixels().Waves.data(), WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, eTF_8888, 2, true,
                                                   FILTER_TRILINEAR, 0, "$WaterWaves", 0);
    if (!m_nWaveTexture)
      m_nWaveTexture = -1;
  }
  return m_nWaveTexture > 0 ? m_pTextures->GetSRV(m_nWaveTexture) : D3D12_SRV_WHITE;
}

bool CD3D12Renderer::CopySceneForWater(const int *pRect)
{
  if (!m_pDevice || !m_pDescriptors)
    return false;
  // The copies of the main view's target: the back buffer, or the HDR target or an upscaler's scene.
  SSceneCopy &Copy = m_SceneCopies[m_pDevice->IsInScene() || m_pDevice->IsInHdr() ? 1 : 0];
  const int w = m_pDevice->GetMainWidth(), h = m_pDevice->GetMainHeight();
  const DXGI_FORMAT ColorFormat = m_pDevice->GetMainColorFormat(), DepthFormat = m_pDevice->GetMainDepthSRVFormat();
  if (!Copy.pColor || w != Copy.nWidth || h != Copy.nHeight || ColorFormat != Copy.ColorFormat || DepthFormat != Copy.DepthFormat)
  {
    FreeSceneCopy(Copy);
    if (!m_pDevice->CreateTargetCopies(Copy.pColor, &Copy.pDepth))
      return false;
    Copy.nColorSRV = m_pDescriptors->AllocSRV(Copy.pColor.Get(), nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Format = DepthFormat;
    Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Texture2D.MipLevels = 1;
    Copy.nDepthSRV = m_pDescriptors->AllocSRV(Copy.pDepth.Get(), &Desc);
    Copy.nWidth = w;
    Copy.nHeight = h;
    Copy.ColorFormat = ColorFormat;
    Copy.DepthFormat = DepthFormat;
    if (Copy.nColorSRV < 0 || Copy.nDepthSRV < 0)
    {
      FreeSceneCopy(Copy);
      return false;
    }
  }
  if (pRect)
  {
    const D3D12_BOX Box = { (UINT)pRect[0], (UINT)pRect[1], 0, (UINT)pRect[2], (UINT)pRect[3], 1 };
    m_pDevice->CopyTargets(Copy.pColor.Get(), nullptr, &Box);
  }
  else
    m_pDevice->CopyTargets(Copy.pColor.Get(), Copy.pDepth.Get());
  m_nSceneColorSRV = Copy.nColorSRV;
  m_nSceneDepthSRV = Copy.nDepthSRV;
  m_bSceneCopied = !pRect;
  m_pSceneCopy = pRect ? nullptr : &Copy;
  m_nSceneTileSRV = -1;
  return true;
}

bool CD3D12Renderer::BuildDepthTiles()
{
  SSceneCopy *pCopy = m_pSceneCopy;
  if (!pCopy || !pCopy->pDepth || !m_pDevice || !m_pDescriptors || !m_pUpload || !m_pCompute)
    return false;
  // Level 0 a multiple of 2^(DEPTH_TILE_LEVELS - 1) texels a side, so that each next level is half the
  // one before (its texels past the copy's those of its edge).
  constexpr int ALIGN = 1 << (DEPTH_TILE_LEVELS - 1);
  constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
  const int w = ((pCopy->nWidth + XR_DEPTH_TILE - 1) / XR_DEPTH_TILE + ALIGN - 1) & ~(ALIGN - 1);
  const int h = ((pCopy->nHeight + XR_DEPTH_TILE - 1) / XR_DEPTH_TILE + ALIGN - 1) & ~(ALIGN - 1);
  if (!pCopy->pTiles)
  {
    pCopy->pTiles = m_pUpload->CreateComputeTarget(w, h, DEPTH_TILE_LEVELS, DXGI_FORMAT_R32_FLOAT, L"water depth tiles");
    if (pCopy->pTiles)
    {
      pCopy->pTiles->m_nSRV = m_pDescriptors->AllocSRV(pCopy->pTiles->m_pResource, nullptr);
      pCopy->nTileTables = m_pDescriptors->AllocRange(2 * TABLE);
    }
    if (!pCopy->pTiles || pCopy->pTiles->m_nSRV < 0 || pCopy->nTileTables < 0)
    {
      FreeDepthTiles(*pCopy);
      return false;
    }
    // The first kernel's table: t0 the depth copy; u0..u4 levels 0..4. The second's: u0 level 4, u1
    // and u2 levels 5 and 6.
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = pCopy->DepthFormat;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
    UAV.Format = DXGI_FORMAT_R32_FLOAT;
    UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Resource *pTiles = pCopy->pTiles->m_pResource;
    for (int t = 0; t < 2; t++)
    {
      const int nTable = pCopy->nTileTables + t * TABLE;
      for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
        m_pDescriptors->WriteSRV(nTable + i, t == 0 && i == 0 ? pCopy->pDepth.Get() : nullptr, &SRV);
      for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
      {
        const int nLevel = t == 0 ? i : 4 + i;
        UAV.Texture2D.MipSlice = (UINT)std::min(nLevel, DEPTH_TILE_LEVELS - 1);
        m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, t == 0 || i < 3 ? pTiles : nullptr, &UAV);
      }
    }
    // Made in UNORDERED_ACCESS (called in a frame).
    m_pDevice->Transition(pTiles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    pCopy->nTileWidth = w;
    pCopy->nTileHeight = h;
  }

  // A group 16 x 16 texels of level 0 and those under them up to level 4, then a group 16 x 16 of
  // level 5 and level 6 from them.
  static_assert(DEPTH_TILE_LEVELS == 7, "the kernels make levels 0..6");
  SDepthTileConstants tc = {};
  Set4(tc.vSize, 1.0f / pCopy->nWidth, 1.0f / pCopy->nHeight, (float)(w >> 5), (float)(h >> 5));
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(tc), 256);
  memcpy(CB.pCPU, &tc, sizeof(tc));
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  ID3D12Resource *pTiles = pCopy->pTiles->m_pResource;
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  m_pDevice->Transition(pCopy->pDepth.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, READ);
  m_pDevice->Transition(pTiles, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(pList, eD3D12K_DepthTiles, CB.nGPU, pCopy->nTileTables, pCopy->nTileTables + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)w / 16, (UINT)h / 16);
  m_pDevice->UavBarrier(pTiles);
  m_pCompute->Dispatch(pList, eD3D12K_DepthTilesUp, CB.nGPU, pCopy->nTileTables + TABLE, pCopy->nTileTables + TABLE + CD3D12Compute::COMPUTE_SRVS,
                       (UINT)((w >> 5) + 15) / 16, (UINT)((h >> 5) + 15) / 16);
  m_pDevice->Transition(pTiles, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pCopy->pDepth.Get(), READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_nSceneTileSRV = pCopy->pTiles->m_nSRV;
  return true;
}

void CD3D12Renderer::FreeDepthTiles(SSceneCopy &Copy)
{
  if (m_pSceneCopy == &Copy)
    m_nSceneTileSRV = -1;
  if (Copy.nTileTables >= 0 && m_pDescriptors)
    m_pDescriptors->FreeRange(Copy.nTileTables, 2 * (CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS));
  Copy.nTileTables = -1;
  if (Copy.pTiles && m_pUpload)
    m_pUpload->ReleaseTexture(Copy.pTiles);
  Copy.pTiles = nullptr;
  Copy.nTileWidth = Copy.nTileHeight = 0;
}

void CD3D12Renderer::FreeSceneCopy(SSceneCopy &Copy)
{
  if (m_nSceneColorSRV == Copy.nColorSRV)
    m_nSceneColorSRV = m_nSceneDepthSRV = -1;
  FreeDepthTiles(Copy);
  if (m_pSceneCopy == &Copy)
    m_pSceneCopy = nullptr;
  if (m_pDescriptors)
  {
    if (Copy.nColorSRV >= 0)
      m_pDescriptors->FreeSRV(Copy.nColorSRV);
    if (Copy.nDepthSRV >= 0)
      m_pDescriptors->FreeSRV(Copy.nDepthSRV);
  }
  if (m_pDevice)
  {
    m_pDevice->DeferredRelease(Copy.pColor);
    m_pDevice->DeferredRelease(Copy.pDepth);
  }
  Copy = SSceneCopy();
}

void CD3D12Renderer::ReleaseWaterResources()
{
  for (SSceneCopy &Copy : m_SceneCopies)
    FreeSceneCopy(Copy);
  m_bSceneCopied = false;
}

void CD3D12Renderer::UpdateWaterReflection(std::span<const SRenderItem> Items)
{
  FUNCTION_PROFILER(iSystem, PROFILE_RENDERER);
  // The main view of the game with the ocean in it (the water volumes have levels of their own),
  // the camera above the water.
  I3DEngine *p3D = iSystem ? iSystem->GetI3DEngine() : nullptr;
  const bool bOcean = CV_r_WaterReflections && (m_nSceneFlags & SHDF_ALLOWHDR) && m_nRecurseLevel <= 1 &&
                      std::any_of(Items.begin(), Items.end(), IsOcean);
  if (!bOcean || !p3D || !m_pDevice || !m_pDescriptors || m_bReflectionPass || !EnsureFrame())
  {
    m_bReflectionValid = false;
    return;
  }
  const CCamera Camera = m_Camera;
  const Vec3 vPos = Camera.GetPos();
  const float fWaterZ = p3D->GetWaterLevel();
  if (vPos.z <= fWaterZ)
  {
    m_bReflectionValid = false;
    return;
  }

  // The targets: a fraction of the screen, in the main scene's format (HDR with r_HDRRendering;
  // drawn before BeginMainScene).
  const float fScale = std::clamp(CV_d3d12_WaterReflectionScale, 0.1f, 1.0f);
  const int w = std::max((int)(m_pDevice->GetWidth() * fScale), 1), h = std::max((int)(m_pDevice->GetHeight() * fScale), 1);
  const DXGI_FORMAT Format = IsHdrOn() ? D3D12_HDR_FORMAT : D3D12_BACKBUFFER_FORMAT;
  if (!m_pReflectionColor || w != m_nReflectionWidth || h != m_nReflectionHeight || m_pReflectionColor->GetDesc().Format != Format)
  {
    // The colour target with its mip levels (down to 1 x 1 at most), each with an RTV, and an SRV
    // of each level alone but the last (the next is made from it).
    ReleaseWaterReflection();
    int nMips = 1;
    while (nMips < REFLECTION_MIPS && std::max(w, h) >> nMips)
      nMips++;
    m_hReflectionRTV = m_pDescriptors->AllocRTV();
    m_hReflectionDSV = m_pDescriptors->AllocDSV();
    bool bViews = true;
    for (int i = 1; i < nMips; i++)
    {
      m_hReflectionMipRTV[i - 1] = m_pDescriptors->AllocRTV();
      bViews = bViews && m_hReflectionMipRTV[i - 1].ptr;
    }
    if (!bViews ||
        !m_pDevice->CreateOffscreenTargets(w, h, m_pReflectionColor, m_pReflectionDepth, m_hReflectionRTV, m_hReflectionDSV, Format, nMips,
                                           m_hReflectionMipRTV) ||
        (m_nReflectionSRV = m_pDescriptors->AllocSRV(m_pReflectionColor.Get(), nullptr)) < 0)
    {
      ReleaseWaterReflection();
      return;
    }
    for (int i = 0; i + 1 < nMips; i++)
    {
      D3D12_SHADER_RESOURCE_VIEW_DESC View = {};
      View.Format = Format;
      View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      View.Texture2D.MostDetailedMip = (UINT)i;
      View.Texture2D.MipLevels = 1;
      if ((m_nReflectionMipSRV[i] = m_pDescriptors->AllocSRV(m_pReflectionColor.Get(), &View)) < 0)
      {
        ReleaseWaterReflection();
        return;
      }
    }
    m_nReflectionMips = nMips;
    m_nReflectionWidth = w;
    m_nReflectionHeight = h;
  }

  // Due when the last one is older than r_WaterUpdateFactor seconds per metre to the nearest water
  // (at most 0.3 s), or the camera has moved more than r_WaterUpdateDistance or turned more than
  // r_WaterUpdateDeltaAngle degrees since.
  const float fToWater = p3D->GetDistanceToSectorWithWater();
  const float fTime = iTimer ? iTimer->GetCurrTime() : 0.0f;
  const Vec3 vDir = ConvertToRadAngles(Camera.GetAngles());
  const bool bDue = !m_bReflectionValid || fWaterZ != m_fReflectionWaterZ || fTime < m_fReflectionTime ||
                    fTime - m_fReflectionTime > std::min(CV_r_WaterUpdateFactor * fToWater, REFLECTION_MAX_INTERVAL) ||
                    (vPos - m_vReflectionCamPos).GetLength() > CV_r_WaterUpdateDistance ||
                    vDir.Dot(m_vReflectionCamDir) < cosf(DEG2RAD(CV_r_WaterUpdateDeltaAngle));
  if (!bDue)
    return;

  // The frame's camera mirrored in the water plane: below the water by as much as the camera is
  // above it, the pitch negated, no roll; the near plane at the distance to the water, at most 16 m.
  CCamera Mirror = Camera;
  Mirror.SetPos(Vec3(vPos.x, vPos.y, 2 * fWaterZ - vPos.z));
  const Ang3 vAngles = Camera.GetAngles();
  Mirror.SetAngle(Vec3(-vAngles.x, 0, vAngles.z));
  Mirror.SetZMin(std::clamp(fToWater, std::max(Camera.GetZMin(), 0.1f), REFLECTION_MAX_NEAR));
  Mirror.Update();

  // What the reflection's scene changes.
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bFog = m_bFog;
  const float fFogStart = m_fFogStart, fFogEnd = m_fFogEnd;
  float FogColor[4], WorldColor[4];
  memcpy(FogColor, m_FogColor, sizeof(FogColor));
  memcpy(WorldColor, m_WorldColor, sizeof(WorldColor));
  const Vec3 vClearColor = m_vClearColor;
  const int nState = m_nState, nCullMode = m_nCullMode;

  // The 3D engine renders the scene (a frame of its own, nested in this one) with the mirrored
  // camera into the targets.
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "water reflection: %d x %d%s, %d mip levels, water level %.2f\n", w, h, Format == D3D12_HDR_FORMAT ? " (HDR)" : "",
            m_nReflectionMips, fWaterZ);
  m_pDevice->SetOffscreenTargets(m_pReflectionColor.Get(), m_hReflectionRTV, m_hReflectionDSV, w, h);
  const float Clear[4] = { m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 0 };
  m_pDevice->ClearColor(Clear);
  m_pDevice->ClearDepth(1.0f, 0, true, true);
  m_bReflectionPass = true;
  m_fReflectionWaterZ = fWaterZ;
  SetCamera(Mirror);
  p3D->SetCamera(Mirror, false);
  // Its static draws join the scene for rays until the next one (what the main view misses).
  const bool bRayCapture = WantsRays();
  if (bRayCapture)
    m_pRayScene->BeginReflectionCapture(m_CameraView * m_ProjMatrix);
  {
    SCpuTime Time(m_fFrameLowDetailMs[LD_WATER]);
    p3D->DrawLowDetail(CV_d3d12_WaterReflectionFlags);
  }
  if (bRayCapture)
    m_pRayScene->EndReflectionCapture();
  m_bReflectionPass = false;

  // Its mip levels: each texel the mean of the 2 x 2 above it (one bilinear tap halfway between).
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_Post;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  Key.nTarget = 1;
  Key.nColorFormat = Format == D3D12_HDR_FORMAT ? PIPELINE_COLOR_HDR : PIPELINE_COLOR_BGRA8;
  for (int i = 1; i < m_nReflectionMips; i++)
  {
    const int nMipW = std::max(w >> i, 1), nMipH = std::max(h >> i, 1);
    m_pDevice->SetOffscreenTargets(m_pReflectionColor.Get(), m_hReflectionMipRTV[i - 1], {}, nMipW, nMipH, (UINT)i);
    SDrawConstants dc = DefaultDrawConstants();
    dc.nFlags = POST_DOWNSAMPLE;
    dc.nTex0 = (uint32)m_nReflectionMipSRV[i - 1];
    Set4(dc.vParams0, 1.0f / nMipW, 1.0f / nMipH, 1, 0);
    DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  }
  m_pDevice->SetBackBufferTargets();

  // The frame as it was.
  p3D->SetCamera(Camera, false);
  SetCamera(Camera);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bFog = bFog;
  m_fFogStart = fFogStart;
  m_fFogEnd = fFogEnd;
  memcpy(m_FogColor, FogColor, sizeof(FogColor));
  memcpy(m_WorldColor, WorldColor, sizeof(WorldColor));
  m_vClearColor = vClearColor;
  m_nState = nState;
  m_nCullMode = nCullMode;
  InvalidateFrameConstants();

  m_fReflectionTime = fTime;
  m_vReflectionCamPos = vPos;
  m_vReflectionCamDir = vDir;
  m_bReflectionValid = true;
}

void CD3D12Renderer::ClipProjectionToWater(Matrix44 &Proj, const Matrix44 &View) const
{
  // The water plane in view space, the side above the water positive: the world's z axis turned by
  // the view (row 2 of the view matrix, which multiplies row vectors), through the point (0, 0, W).
  const float *v = View.data;
  const Vec3 n(v[8], v[9], v[10]);
  const Vec3 p(m_fReflectionWaterZ * v[8] + v[12], m_fReflectionWaterZ * v[9] + v[13], m_fReflectionWaterZ * v[10] + v[14]);
  const float d = -n.Dot(p);
  // The camera must be below the water.
  if (d >= 0)
    return;
  // The near plane becomes the water plane (an oblique near plane): the clip space z row is the
  // plane scaled so that the far plane goes through the corner Q of the far plane farthest on the
  // plane's positive side (clip (sign x, sign y, 1, 1)), and still encloses the view frustum.
  float *m = Proj.data;
  const float q[4] = { Sign(n.x) / m[0], Sign(n.y) / m[5], -1, (1 + m[10]) / m[14] };
  const float fDot = n.x * q[0] + n.y * q[1] + n.z * q[2] + d * q[3];
  if (fabsf(fDot) < 1e-8f)
    return;
  const float a = 1 / fDot;
  m[2] = n.x * a;
  m[6] = n.y * a;
  m[10] = n.z * a;
  m[14] = d * a;
}

void CD3D12Renderer::ReleaseWaterReflection()
{
  if (m_pDescriptors)
  {
    if (m_nReflectionSRV >= 0)
      m_pDescriptors->FreeSRV(m_nReflectionSRV);
    if (m_hReflectionRTV.ptr)
      m_pDescriptors->FreeRTV(m_hReflectionRTV);
    if (m_hReflectionDSV.ptr)
      m_pDescriptors->FreeDSV(m_hReflectionDSV);
    for (int i = 0; i < REFLECTION_MIPS - 1; i++)
    {
      if (m_hReflectionMipRTV[i].ptr)
        m_pDescriptors->FreeRTV(m_hReflectionMipRTV[i]);
      if (m_nReflectionMipSRV[i] >= 0)
        m_pDescriptors->FreeSRV(m_nReflectionMipSRV[i]);
    }
  }
  m_nReflectionSRV = -1;
  m_hReflectionRTV = {};
  m_hReflectionDSV = {};
  for (int i = 0; i < REFLECTION_MIPS - 1; i++)
  {
    m_hReflectionMipRTV[i] = {};
    m_nReflectionMipSRV[i] = -1;
  }
  m_nReflectionMips = 0;
  if (m_pDevice)
  {
    m_pDevice->DeferredRelease(m_pReflectionColor);
    m_pDevice->DeferredRelease(m_pReflectionDepth);
  }
  m_pReflectionColor.Reset();
  m_pReflectionDepth.Reset();
  m_nReflectionWidth = m_nReflectionHeight = 0;
  m_bReflectionValid = false;
}

bool CD3D12Renderer::SetupWaterDraw(SPipelineKey &Key, SDrawConstants &dc, const SRenderItem &it)
{
  // In an environment cube only the ocean, with no copy of the face: the scene behind it at the far
  // plane, so that it is deep water (its fog volume's colour) reflecting the sky.
  if (m_bEnvCubePass ? !IsOcean(it) : !m_bSceneCopied && !CopySceneForWater())
    return false;
  Key.nProgram = eD3D12P_Water;
  Key.nState = 0;               // depth tested, no depth writes, replaces the pixel
  Key.nCull = R_CULL_NONE;      // seen from above and below
  dc.nFlags = 0;
  dc.nTex0 = m_bEnvCubePass ? D3D12_SRV_WHITE : (uint32)m_nSceneColorSRV;
  dc.nTex1 = m_bEnvCubePass ? D3D12_SRV_WHITE : (uint32)m_nSceneDepthSRV;
  // The depth tiles of the copy for the march of the ocean's screen space reflection (a lake's or a
  // river's meets its banks near the water: the tiles would cost more than they save).
  dc.nShadowTex[0] = dc.nShadowTex[1] = 0;
  if (!m_bEnvCubePass && IsOcean(it) && (m_nSceneTileSRV >= 0 || BuildDepthTiles()))
  {
    dc.nShadowTex[0] = (uint32)m_nSceneTileSRV;
    dc.nShadowTex[1] = DEPTH_TILE_LEVELS;
  }

  // Waves as steep as the bump amounts of the object; how much of the sky it reflects.
  const CCObject *pObj = it.pObj;
  const float fBumpX = GetObjectParam(pObj, "bumpamountx", 0.03f), fBumpY = GetObjectParam(pObj, "bumpamounty", 0.02f);
  const float fReflect = std::clamp(GetObjectParam(pObj, "reflectamount", 1.0f), 0.0f, 1.0f);
  const bool bVolume = !IsOcean(it);
  const SFogVolume *pFV = bVolume ? nullptr : m_pScene->GetOceanFogVolume();
  Set4(dc.vParams0, (fBumpX + fBumpY) * 1.2f, fReflect, (float)CV_d3d12_WaterDebug, std::max(CV_d3d12_WaterSunGlint, 0.0f));
  dc.nTex5 = GetWaveTexture();

  // The sky box the scene's sky drew (as it drew it).
  dc.nTex2 = dc.nTex3 = dc.nTex4 = D3D12_SRV_WHITE;
  Set4(dc.vParams1, 0, 0, 1, 0);
  if (m_nSceneSkyBox >= 0 && m_nSceneSkyBox < (int)m_SkyBoxes.size())
  {
    CTexture *const *pTex = m_SkyBoxes[m_nSceneSkyBox].pTex;
    const auto Usable = [](CTexture *p) { return p && p->IsTextureLoaded() && !p->IsCube(); };
    if (Usable(pTex[0]) && Usable(pTex[1]) && Usable(pTex[2]))
    {
      dc.nTex2 = pTex[0]->GetSRV();
      dc.nTex3 = pTex[1]->GetSRV();
      dc.nTex4 = pTex[2]->GetSRV();
      Set4(dc.vParams1, m_fSceneSkyZ1, 1, m_fSceneSkyAlpha, 0);
    }
  }

  // The ocean's planar reflection (UpdateWaterReflection), seen by the frame's camera.
  dc.nTex6 = D3D12_SRV_WHITE;
  if (!bVolume && !m_bEnvCubePass && m_bReflectionValid && m_nReflectionSRV >= 0)
  {
    dc.nTex6 = (uint32)m_nReflectionSRV;
    dc.vParams1[3] = 1;
    memcpy(dc.mParams0, m_ReflectionViewProj.data, sizeof(dc.mParams0));
  }
  // How much the ripple finer than a pixel blurs the reflection (and turns the fresnel term).
  Set4(dc.vTexGenU, std::max(CV_d3d12_WaterReflectionBlur, 0.0f), 0, 0, 0);
  // The reflection by rays, with this frame's scene for them.
  if (CV_d3d12_RTReflections > 0 && !m_bEnvCubePass && m_pRayScene && m_pRayScene->GetTLAS())
  {
    Key.bRayTrace = 1;
    const uint32 nMask = RayMask();
    Set4(dc.vTexGenV, (float)std::min(CV_d3d12_RTReflections, 2), std::max(CV_d3d12_RTDistance, 1.0f), 0, 0);
    memcpy(&dc.vTexGenV[2], &nMask, sizeof(nMask));
  }

  Set4(dc.vParams2, WATER_ABSORPTION[0] + WATER_TURBIDITY, WATER_ABSORPTION[1] + WATER_TURBIDITY, WATER_ABSORPTION[2] + WATER_TURBIDITY,
       WATER_DISPLACEMENT);

  // The colour of deep water: the ocean's that of its fog volume (the one with caustics) as the level
  // gives it, laid over the scene behind by its layer and distance (1 % of the scene left at twice
  // the distance); else lit by the world colour, the sun's height and the sky; a water volume (not
  // one of the terrain's water shaders) lit by what is behind it, as it may be indoors.
  if (pFV)
  {
    Set4(dc.vParams3, pFV->Color.r, pFV->Color.g, pFV->Color.b, 0);
    dc.vParams2[0] = pFV->fFogLayerZ;
    dc.vParams2[1] = logf(100.0f) / (2 * std::max(pFV->fMaxFogDist, 1.0f));
  }
  else
  {
    const float fLight = 0.4f + 0.6f * std::clamp(m_SunDir[2], 0.0f, 1.0f);
    Set4(dc.vParams3, WATER_SCATTER[0] * m_WorldColor[0] * fLight, WATER_SCATTER[1] * m_WorldColor[1] * fLight,
         WATER_SCATTER[2] * m_WorldColor[2] * fLight, bVolume ? 1.0f : 2.0f);
  }
  return true;
}

bool CD3D12Renderer::SetupBeachDraw(SPipelineKey &Key, SDrawConstants &dc, int nPass)
{
  if (!m_pTextures)
    return false;
  if (!m_pBeachTexture)
    m_pBeachTexture = m_pTextures->LoadTexture("textures/wave1", FT_CLAMP, 0, eTT_Base, -1, -1, -1, 0);
  if (!m_pBeachWetTexture)
    m_pBeachWetTexture = m_pTextures->LoadTexture("textures/water2", 0, 0, eTT_Base, -1, -1, -1, 0);
  if (!m_pBeachBumpTexture)
    m_pBeachBumpTexture = m_pTextures->LoadTexture("textures/fire_noise", 0, 0, eTT_Bumpmap, -1, -1, -1, 0);
  CTexture *pTex = nPass < BEACH_WAVES ? m_pBeachTexture : m_pBeachWetTexture;
  if (!pTex || !pTex->IsTextureLoaded() || pTex->IsCube())
    return false;

  // The mesh runs from the sea (v 0) to the land (v 1) and fades out at both edges (vertex alpha).
  Key.nProgram = eD3D12P_General;
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  Key.nCull = R_CULL_NONE;
  dc.nTex0 = pTex->GetSRV();
  const float fTime = iTimer ? iTimer->GetCurrTime() : 0.0f;
  if (nPass == BEACH_WAVES)
  {
    // The wet sand: a faint grey noise in the vertex colour, three times across the strip, drifting
    // up the beach; it darkens the strip by about 2 levels.
    dc.nFlags = DF_TEX0 | DF_VCOLOR | DF_TEXMATRIX;
    dc.nFlags2 &= ~DF2_BUMP_OFFSET;
    dc.nSamplers = XR_SAMPLER_LINEAR_WRAP;
    Set4(dc.vTexMat0, 1.25f, 0, 0, 0);
    Set4(dc.vTexMat1, 0, 3, Frac(0.2f * fTime), 0);
    Set4(dc.vColor, 1, 1, 1, 0.6f);
    return true;
  }

  // The foam (white: the mesh's rgb, darker in places, is not used) runs up the beach and back: the
  // texture squeezed towards the sea, least when the front is highest. u stays as it is: it runs 0 to
  // 1 and back between the cross sections, and the texture is clamped. The noise that wobbles it
  // sways with the wave. The second wave is behind the first, the third the first's opposite.
  const float fAngle = 2 * gf_PI * Frac((fTime + (nPass == 1 ? BEACH_WAVE_LAG : 0.0f)) / BEACH_WAVE_PERIOD);
  float fSway = 0.25f * sinf(fAngle), fSqueeze = 2 + 0.9f * cosf(fAngle - 0.2f * gf_PI);
  if (nPass == 2)
  {
    fSway = -fSway;
    fSqueeze = 4 - fSqueeze;
  }
  dc.nFlags = DF_TEX0 | DF_VCOLOR | DF_VALPHA_ONLY | DF_TEXMATRIX;
  dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
  Set4(dc.vTexMat0, 1, 0, 0, 0);
  Set4(dc.vTexMat1, 0, fSqueeze, 0, 0);
  if (m_pBeachBumpTexture && m_pBeachBumpTexture->IsTextureLoaded() && !m_pBeachBumpTexture->IsCube())
  {
    dc.nFlags |= DF_TEXGEN1_WORLD;
    dc.nFlags2 |= DF2_BUMP_OFFSET;
    dc.nTex3 = m_pBeachBumpTexture->GetSRV();
    Set4(dc.vTexGen1U, 1 / BEACH_BUMP_SIZE, 0, 0, fSway);
    Set4(dc.vTexGen1V, 0, 1 / BEACH_BUMP_SIZE, 0, 0);
    dc.vParams0[0] = BEACH_BUMP_OFFSET;
  }
  const float b = BEACH_FOAM_BRIGHTNESS * (m_WorldColor[0] + m_WorldColor[1] + m_WorldColor[2]) / 3;
  Set4(dc.vColor, b, b, b, 1);
  return true;
}

void CD3D12Renderer::SetupWaterBottomDraw(SPipelineKey &Key, SDrawConstants &dc, CTexture *pTex)
{
  // The texture the element was given (or none), repeated every 16 metres, in the world colour.
  Key.nProgram = eD3D12P_General;
  Key.nState = GS_DEPTHWRITE;
  Key.nCull = R_CULL_NONE;
  dc.nFlags &= ~(DF_TEXMATRIX | DF_LIGHTING | DF_VCOLOR);
  if (pTex)
  {
    dc.nFlags |= DF_TEXGEN_WORLD;
    Set4(dc.vTexGenU, 0, 1.0f / 16, 0, 0);
    Set4(dc.vTexGenV, 1.0f / 16, 0, 0, 0);
    dc.nSamplers = XR_SAMPLER_LINEAR_WRAP;
    Set4(dc.vColor, m_WorldColor[0], m_WorldColor[1], m_WorldColor[2], 1);
  }
  else
    Set4(dc.vColor, m_WorldColor[0] * 0.45f, m_WorldColor[1] * 0.4f, m_WorldColor[2] * 0.3f, 1);
}
