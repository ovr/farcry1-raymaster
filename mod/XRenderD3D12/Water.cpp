//////////////////////////////////////////////////////////////////////
//
//	File: Water.cpp
//	Description: The water: the ocean around the camera and the water
//	volumes (eDC_Water), the foam on the beach (eDC_WaterBeach) and the
//	sea floor under the ocean (eDC_WaterBottom).
//
//	The water surfaces come first in the blended list. Before the first
//	one, the back buffer and the depth buffer are copied; the water program
//	(Shaders/Water.hlsl) reads the copies for the refraction and the depth
//	of the water behind the surface, and the scene's sky box for the
//	reflection. The beach is the foam texture running up the shore in two
//	waves; the sea floor is opaque, lit by the world colour.
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

// The water's optics: absorption per metre (red goes first), the colour it scatters back in full
// light, and how far the waves move the refracted picture (a fraction of the screen).
constexpr float WATER_ABSORPTION[3] = { 0.9f, 0.4f, 0.3f };
constexpr float WATER_SCATTER[3] = { 0.10f, 0.23f, 0.50f };
constexpr float WATER_DISPLACEMENT = 0.03f;

// A beach wave runs up the shore in this many seconds; the front of the foam texture is at this v.
constexpr float BEACH_WAVE_PERIOD = 7.0f;
constexpr float BEACH_WAVE_FRONT = 0.85f;

constexpr int WAVE_TEXTURE_SIZE = 256;

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
  Set4(dc.vParams0, (fBumpX + fBumpY) * 1.2f, fReflect, (float)CV_d3d12_WaterDebug, 0);
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

  // The water's own colour, lit by the world colour and the sun's height; a water volume (not one of
  // the terrain's water shaders) by what is behind it, as it may be indoors.
  const float fLight = 0.4f + 0.6f * std::clamp(m_SunDir[2], 0.0f, 1.0f);
  const bool bVolume = _strnicmp(it.pShader->GetName(), "terrain", 7) != 0;
  Set4(dc.vParams2, WATER_ABSORPTION[0], WATER_ABSORPTION[1], WATER_ABSORPTION[2], WATER_DISPLACEMENT);
  Set4(dc.vParams3, WATER_SCATTER[0] * m_WorldColor[0] * fLight, WATER_SCATTER[1] * m_WorldColor[1] * fLight,
       WATER_SCATTER[2] * m_WorldColor[2] * fLight, bVolume ? 1.0f : 0.0f);
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

  // The mesh runs from the sea (v 0) to the land (v 1) and fades out at both edges (vertex alpha).
  // The front of the foam moves from the sea to the land and fades in and out on its way; the
  // second wave is half a period behind, mirrored along the shore.
  Key.nProgram = eD3D12P_General;
  Key.nState = GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA;
  Key.nCull = R_CULL_NONE;
  const float fTime = iTimer ? iTimer->GetCurrTime() : 0.0f;
  const float fPhase = Frac(fTime / BEACH_WAVE_PERIOD + nWave * 0.5f);
  dc.nFlags = DF_TEX0 | DF_VCOLOR | DF_TEXMATRIX;
  dc.nTex0 = m_pBeachTexture->GetSRV();
  dc.nSamplers = XR_SAMPLER_LINEAR_CLAMP;
  if (nWave & 1)
    Set4(dc.vTexMat0, -1, 0, 1, 0);
  else
    Set4(dc.vTexMat0, 1, 0, 0, 0);
  Set4(dc.vTexMat1, 0, 1, BEACH_WAVE_FRONT - fPhase, 0);
  const float fFade = sinf(fPhase * 3.14159265f);
  Set4(dc.vColor, m_WorldColor[0] * 0.9f, m_WorldColor[1] * 0.9f, m_WorldColor[2] * 0.9f, fFade);
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
