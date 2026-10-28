//////////////////////////////////////////////////////////////////////
//
//	File: Water.cpp
//	Description: The water: the ocean around the camera and the water
//	volumes (eDC_Water), the foam on the beach (eDC_WaterBeach), the
//	sea floor under the ocean (eDC_WaterBottom) and the caustics on the
//	terrain under it (eDC_Caustics).
//
//	The water surfaces come first in the blended list. Before the first
//	one, the back buffer and the depth buffer are copied; the water program
//	(Shaders/Water.hlsl) reads the copies for the refraction and the depth
//	of the water behind the surface, and the scene's sky box for the
//	reflection. The beach is the foam texture running up the shore in two
//	waves; the sea floor is opaque, lit by the world colour.
//
//	With r_WaterReflections the ocean also reflects the scene mirrored in
//	its plane: before the main view is drawn, the 3D engine renders the sky
//	and the terrain (DrawLowDetail) seen by the camera mirrored in the
//	water plane into a target of its own, as a scene nested in the main
//	one, redrawn as r_WaterUpdateFactor, r_WaterUpdateDistance and
//	r_WaterUpdateDeltaAngle say. Its near plane is the water plane, so
//	nothing under the water shows in it.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "Texture.h"
#include "Scene.h"

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

// The optics of water without a fog volume: absorption per metre (red goes first; weak, as the
// levels paint the sea floor in the colours of the water above it), the light scattered per metre,
// the colour of deep water in full light where the level gives none (in the ocean's fog volume);
// and how far the waves move the refracted picture (a fraction of the screen).
constexpr float WATER_ABSORPTION[3] = { 0.030f, 0.024f, 0.005f };
constexpr float WATER_TURBIDITY = 0.005f;
constexpr float WATER_SCATTER[3] = { 0.18f, 0.28f, 0.54f };
constexpr float WATER_DISPLACEMENT = 0.03f;

// A beach wave runs up the shore in this many seconds (BEACH_WAVES of them at a time); the front of
// the foam texture is at this v; the foam is white, this many times the world colour's mean (about
// as bright as the sand, so that it shows over the water).
constexpr float BEACH_WAVE_PERIOD = 7.0f;
constexpr float BEACH_WAVE_FRONT = 0.85f;
constexpr float BEACH_FOAM_BRIGHTNESS = 1.05f;

constexpr int WAVE_TEXTURE_SIZE = 256;

// The caustics: the pattern repeats every so many metres, the light of a texel of the texture is
// stored as 255 / CAUSTICS_RANGE per unit of the mean; the share of the light on the sea floor that
// they move around (the direct sun's), and the depth where they are gone.
constexpr int CAUSTICS_TEXTURE_SIZE = 256;
constexpr float CAUSTICS_RANGE = 6.0f;
constexpr float CAUSTICS_SIZE = 4.0f;
constexpr float CAUSTICS_STRENGTH = 0.2f;
constexpr float CAUSTICS_DEPTH = 12.0f;

//! The wave texture, B, G, R, A bytes: the slopes of a height field that tiles over the texture,
//! dH/du in red and dH/dv in green, the height in blue (128: 0; 32 units: the RMS of the slopes,
//! and of the height). The field is a few hundred waves with whole numbers of cycles per tile,
//! their heights falling with the wave number as in a wind sea, most of them along u.
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
    Pixels[i * 4 + 3] = 255;
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

}

int CD3D12Renderer::GetCausticsTexture()
{
  if (!m_nCausticsTexture && m_pTextures)
  {
    const std::vector<byte> Pixels = MakeCausticsTexture(CAUSTICS_TEXTURE_SIZE, m_fCausticsMinMean);
    m_nCausticsTexture = m_pTextures->CreateFromMemory(Pixels.data(), CAUSTICS_TEXTURE_SIZE, CAUSTICS_TEXTURE_SIZE, eTF_8888, 2, true,
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
  if (!CV_d3d12_Caustics || !pFV || !pFV->bCaustics || fSun <= 0 || m_bReflectionPass || m_bSpritePass)
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

int CD3D12Renderer::GetWaveTexture()
{
  if (!m_nWaveTexture && m_pTextures)
  {
    const std::vector<byte> Pixels = MakeWaveTexture(WAVE_TEXTURE_SIZE);
    m_nWaveTexture = m_pTextures->CreateFromMemory(Pixels.data(), WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, eTF_8888, 2, true, FILTER_TRILINEAR, 0,
                                                   "$WaterWaves", 0);
    if (!m_nWaveTexture)
      m_nWaveTexture = -1;
  }
  return m_nWaveTexture > 0 ? m_pTextures->GetSRV(m_nWaveTexture) : D3D12_SRV_WHITE;
}

bool CD3D12Renderer::CopySceneForWater()
{
  if (!m_pDevice || !m_pDescriptors)
    return false;
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  if (!m_pSceneColorCopy || w != m_nSceneCopyWidth || h != m_nSceneCopyHeight)
  {
    ReleaseWaterResources();
    if (!m_pDevice->CreateTargetCopies(m_pSceneColorCopy, m_pSceneDepthCopy))
      return false;
    m_nSceneColorSRV = m_pDescriptors->AllocSRV(m_pSceneColorCopy.Get(), nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Desc.Texture2D.MipLevels = 1;
    m_nSceneDepthSRV = m_pDescriptors->AllocSRV(m_pSceneDepthCopy.Get(), &Desc);
    m_nSceneCopyWidth = w;
    m_nSceneCopyHeight = h;
    if (m_nSceneColorSRV < 0 || m_nSceneDepthSRV < 0)
    {
      ReleaseWaterResources();
      return false;
    }
  }
  m_pDevice->CopyTargets(m_pSceneColorCopy.Get(), m_pSceneDepthCopy.Get());
  m_bSceneCopied = true;
  return true;
}

void CD3D12Renderer::ReleaseWaterResources()
{
  if (m_pDescriptors)
  {
    if (m_nSceneColorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nSceneColorSRV);
    if (m_nSceneDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_nSceneDepthSRV);
  }
  m_nSceneColorSRV = m_nSceneDepthSRV = -1;
  if (m_pDevice)
  {
    m_pDevice->DeferredRelease(m_pSceneColorCopy);
    m_pDevice->DeferredRelease(m_pSceneDepthCopy);
  }
  m_pSceneColorCopy.Reset();
  m_pSceneDepthCopy.Reset();
  m_nSceneCopyWidth = m_nSceneCopyHeight = 0;
  m_bSceneCopied = false;
}

void CD3D12Renderer::UpdateWaterReflection(std::span<const SRenderItem> Items)
{
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

  // The targets: a fraction of the screen.
  const float fScale = std::clamp(CV_d3d12_WaterReflectionScale, 0.1f, 1.0f);
  const int w = std::max((int)(m_pDevice->GetWidth() * fScale), 1), h = std::max((int)(m_pDevice->GetHeight() * fScale), 1);
  if (!m_pReflectionColor || w != m_nReflectionWidth || h != m_nReflectionHeight)
  {
    ReleaseWaterReflection();
    m_hReflectionRTV = m_pDescriptors->AllocRTV();
    m_hReflectionDSV = m_pDescriptors->AllocDSV();
    if (!m_pDevice->CreateOffscreenTargets(w, h, m_pReflectionColor, m_pReflectionDepth, m_hReflectionRTV, m_hReflectionDSV) ||
        (m_nReflectionSRV = m_pDescriptors->AllocSRV(m_pReflectionColor.Get(), nullptr)) < 0)
    {
      ReleaseWaterReflection();
      return;
    }
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
  m_pDevice->SetOffscreenTargets(m_pReflectionColor.Get(), m_hReflectionRTV, m_hReflectionDSV, w, h);
  const float Clear[4] = { m_vClearColor.x, m_vClearColor.y, m_vClearColor.z, 0 };
  m_pDevice->ClearColor(Clear);
  m_pDevice->ClearDepth(1.0f, 0, true, true);
  m_bReflectionPass = true;
  m_fReflectionWaterZ = fWaterZ;
  SetCamera(Mirror);
  p3D->SetCamera(Mirror, false);
  p3D->DrawLowDetail(CV_d3d12_WaterReflectionFlags);
  m_bReflectionPass = false;
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
  }
  m_nReflectionSRV = -1;
  m_hReflectionRTV = {};
  m_hReflectionDSV = {};
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
  if (!m_bSceneCopied && !CopySceneForWater())
    return false;
  Key.nProgram = eD3D12P_Water;
  Key.nState = 0;               // depth tested, no depth writes, replaces the pixel
  Key.nCull = R_CULL_NONE;      // seen from above and below
  dc.nFlags = 0;
  dc.nTex0 = (uint32)m_nSceneColorSRV;
  dc.nTex1 = (uint32)m_nSceneDepthSRV;

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

  // The ocean's planar reflection (UpdateWaterReflection).
  dc.nTex6 = D3D12_SRV_WHITE;
  if (!bVolume && m_bReflectionValid && m_nReflectionSRV >= 0)
  {
    dc.nTex6 = (uint32)m_nReflectionSRV;
    dc.vParams1[3] = 1;
    memcpy(dc.mParams0, m_ReflectionViewProj.data, sizeof(dc.mParams0));
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

bool CD3D12Renderer::SetupBeachDraw(SPipelineKey &Key, SDrawConstants &dc, int nWave)
{
  if (!m_pTextures)
    return false;
  if (!m_pBeachTexture)
    m_pBeachTexture = m_pTextures->LoadTexture("textures/wave1", FT_CLAMP, 0, eTT_Base, -1, -1, -1, 0);
  if (!m_pBeachTexture || !m_pBeachTexture->IsTextureLoaded() || m_pBeachTexture->IsCube())
    return false;

  // The mesh runs from the sea (v 0) to the land (v 1) and fades out at both edges (vertex alpha;
  // its rgb, darker in places, is not used: foam is white). The front of the foam moves from the
  // sea to the land, appears quickly and fades out slowly on its way; each wave is a fraction of a
  // period behind the one before, every other one mirrored along the shore.
  Key.nProgram = eD3D12P_General;
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  Key.nCull = R_CULL_NONE;
  const float fTime = iTimer ? iTimer->GetCurrTime() : 0.0f;
  const float fPhase = Frac(fTime / BEACH_WAVE_PERIOD + (float)nWave / BEACH_WAVES);
  dc.nFlags = DF_TEX0 | DF_VCOLOR | DF_VALPHA_ONLY | DF_TEXMATRIX;
  dc.nTex0 = m_pBeachTexture->GetSRV();
  dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
  if (nWave & 1)
    Set4(dc.vTexMat0, -1, 0, 1, 0);
  else
    Set4(dc.vTexMat0, 1, 0, 0, 0);
  Set4(dc.vTexMat1, 0, 1, BEACH_WAVE_FRONT - fPhase, 0);
  const auto SmoothStep = [](float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
  const float fFade = SmoothStep(0.0f, 0.12f, fPhase) * (1 - SmoothStep(0.4f, 1.0f, fPhase));
  const float b = BEACH_FOAM_BRIGHTNESS * (m_WorldColor[0] + m_WorldColor[1] + m_WorldColor[2]) / 3;
  Set4(dc.vColor, b, b, b, fFade);
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
