// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: OceanFFT.cpp
//	Description: The FFT ocean (d3d12_WaterFFT): the waves of a wind sea
//	(Tessendorf, "Simulating Ocean Water") in place of the static wave
//	texture of the water's program.
//
//	The sea of the wind d3d12_WaterWind (OceanSpectrum.h: JONSWAP's spectrum,
//	three cascades of 256 x 256 texels 250, 40.5 and 6.9 metres a side, h0 of
//	each) is made on the CPU when the wind changes and uploaded as h0
//	(RGBA32F, a slice a cascade). Once a frame before the main view's
//	scenes, when its list has water (UpdateOcean), four kernels
//	(Shaders/OceanFFT.hlsl) make the waves at the frame's time:
//	  - the rows: the four spectra of the sea at that time from h0, their
//	    inverse FFTs along x (into m_Ocean.pRows, two slices a cascade);
//	  - the columns: their inverse FFTs along y, the fields (pFields);
//	  - the waves: their texels (the slopes, the height in the weight of
//	    the cascade's slopes, the squared slope; moved by the horizontal
//	    displacement of d3d12_WaterChop, the sharp crests, as a trochoid's
//	    of Gerstner's) into level 0 of pWaves (RGBA16F, a
//	    slice a cascade), the means of 2 x 2 into levels 1 to 4 (level 4 also
//	    into pLevel4);
//	  - the mip levels: 4 to 8 from pLevel4.
//	With d3d12_WaterFoam three more follow, at most 120 times a second:
//	  - the foam: its state (the whitecaps breaking and the foam fading,
//	    RGBA32F) into one of pFoam from the other;
//	  - its view: the state into pFoamView (RGBA16F, the water's), the
//	    means of 2 x 2 into levels 1 to 4 (level 4 also into pFoamLevel4);
//	  - the view's mip levels 4 to 8, as the waves'.
//	The water's program (Water.hlsl's WaveNormal) samples the three slices
//	at the world position over their tiles, each in metres per metre, their
//	slopes summed, times the bump amounts' factor and d3d12_WaterSlope over
//	the sea's own RMS (SetOceanConstants: by default the old texture's
//	steepness, the wind setting only the waves' lengths, directions and
//	speeds). The displacement is the wind sea's own times d3d12_WaterChop,
//	so that how much narrower the crests get does not change with that
//	scale; the glint's ripple finer than the short cascade is Cox and
//	Munk's of the wind less the cascades' (times the bump amounts' factor,
//	not that scale: the glint and the sun's path widen with the wind).
//	The waves stay as they were in a frame without water (the environment
//	cubes may still draw the ocean).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Compute.h"
#include "Scene.h"
#include "OceanSpectrum.h"
#include <chrono>

namespace
{
  void Set4(float v[4], float x, float y, float z, float w)
  {
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
  }

  static_assert(OceanSpectrum::SIZE == XR_OCEAN_SIZE && OceanSpectrum::CASCADES == XR_OCEAN_CASCADES, "OceanSpectrum.h and Constants.h");
  static_assert(XR_OCEAN_SIZE >> (XR_OCEAN_MIPS - 1) == 1 && XR_OCEAN_SIZE / 16 == XR_OCEAN_LEVEL4, "the kernels' levels");
  static_assert(XR_CAUSTICS_TILE == 32 && XR_CAUSTICS_MIPS == 5 && XR_OCEAN_SIZE % XR_CAUSTICS_TILE == 0, "the caustics kernel's levels");

  constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
  //! Rows, columns, waves, their mip levels; the foam's state into each of its two targets, the view of each, the view's
  //! mip levels; the caustics (their mip levels in the kernel): a table each.
  constexpr int KERNELS = 10;
  constexpr int FOAM_TABLE = 4, FOAM_VIEW_TABLE = 6, FOAM_MIPS_TABLE = 8, CAUSTICS_TABLE = 9;
  static_assert(OceanSpectrum::CAUSTICS_CASCADES == XR_CAUSTICS_CASCADES && OceanSpectrum::CAUSTICS_FIRST == XR_CAUSTICS_FIRST,
                "OceanSpectrum.h and Constants.h");
  //! The caustics on the sea floor (d3d12_WaterCaustics; General.hlsl's DF_CAUSTICS): the share of the floor's light the
  //! sun's (what they move around; the sky's and the water's light stay), less at a low sun, none in its shadow; the
  //! depth (m) by which they are gone (the light scattered in the water evens them out), the most they brighten a pixel.
  constexpr float CAUSTICS_SUN_SHARE = 0.6f;
  constexpr float CAUSTICS_FADE_DEPTH = 16.0f;
  constexpr float CAUSTICS_MOST = 4.0f;
  constexpr DXGI_FORMAT FIELDS_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
  constexpr DXGI_FORMAT WAVES_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
  //! The foam's state: in 32 bits, since a GPU may round a half float towards 0 as it stores it, and the share kept each
  //! frame (0.996 at 60 frames a second) then faded the foam 4 % faster over 2 s at 60 frames a second (Ocean_FoamDecay).
  //! The water reads its view in half floats, as the waves (two of the 32-bit texels a pixel cost the water at 4K
  //! 0.11 ms more on Carrier).
  constexpr DXGI_FORMAT FOAM_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT, FOAM_VIEW_FORMAT = WAVES_FORMAT;
  //! The least determinant of the moved surface's Jacobian (OceanWaves): where the crest would fold over.
  constexpr float LEAST_JACOBIAN = 0.1f;
  //! The bump amounts the slopes' factor is 1 at (Water.cpp's defaults, 0.03 and 0.02, times 1.2).
  constexpr float DEFAULT_BUMP = 0.06f;
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

  //! The factor of the sea's slopes and heights that gives them the RMS fSlope along an axis (d3d12_WaterSlope; 0: the
  //! sea's own, fSeaRMS).
  float SeaScale(float fSlope, float fSeaRMS)
  {
    return fSlope > 0 && fSeaRMS > 0 ? fSlope / fSeaRMS : 1.0f;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC ArraySRV(DXGI_FORMAT Format, int nSlices, int nMips)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Format = Format;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.Texture2DArray.MipLevels = (UINT)nMips;
    SRV.Texture2DArray.ArraySize = (UINT)nSlices;
    return SRV;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC ArrayUAV(DXGI_FORMAT Format, int nSlices, int nMip)
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
    UAV.Format = Format;
    UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    UAV.Texture2DArray.MipSlice = (UINT)nMip;
    UAV.Texture2DArray.ArraySize = (UINT)nSlices;
    return UAV;
  }
}

bool CD3D12Renderer::EnsureOceanTargets()
{
  SOceanState &o = m_Ocean;
  if (o.pWaves)
    return true;
  if (o.bFailed || !m_pDevice || !m_pUpload || !m_pDescriptors || !m_pCompute)
    return false;
  constexpr int N = XR_OCEAN_SIZE, C = XR_OCEAN_CASCADES;
  o.pRows = m_pUpload->CreateComputeTarget(N, N, 1, FIELDS_FORMAT, L"XRenderD3D12 ocean rows", 2 * C);
  o.pFields = m_pUpload->CreateComputeTarget(N, N, 1, FIELDS_FORMAT, L"XRenderD3D12 ocean fields", 2 * C);
  o.pWaves = m_pUpload->CreateComputeTarget(N, N, XR_OCEAN_MIPS, WAVES_FORMAT, L"XRenderD3D12 ocean waves", C);
  o.pLevel4 = m_pUpload->CreateComputeTarget(XR_OCEAN_LEVEL4, XR_OCEAN_LEVEL4, 1, WAVES_FORMAT, L"XRenderD3D12 ocean waves' level 4", C);
  o.pFoam[0] = m_pUpload->CreateComputeTarget(N, N, 1, FOAM_FORMAT, L"XRenderD3D12 ocean foam 0", C);
  o.pFoam[1] = m_pUpload->CreateComputeTarget(N, N, 1, FOAM_FORMAT, L"XRenderD3D12 ocean foam 1", C);
  o.pFoamView = m_pUpload->CreateComputeTarget(N, N, XR_OCEAN_MIPS, FOAM_VIEW_FORMAT, L"XRenderD3D12 ocean foam's view", C);
  o.pFoamLevel4 = m_pUpload->CreateComputeTarget(XR_OCEAN_LEVEL4, XR_OCEAN_LEVEL4, 1, FOAM_VIEW_FORMAT, L"XRenderD3D12 ocean foam's level 4", C);
  constexpr int CC = XR_CAUSTICS_CASCADES;
  o.pCaustics = m_pUpload->CreateComputeTarget(N, N, XR_CAUSTICS_MIPS, WAVES_FORMAT, L"XRenderD3D12 ocean caustics", CC);
  o.nTables = m_pDescriptors->AllocRange(KERNELS * TABLE);
  static_assert(FOAM_VIEW_FORMAT == WAVES_FORMAT, "the views' SRVs and tables");
  const D3D12_SHADER_RESOURCE_VIEW_DESC MipsSRV = ArraySRV(WAVES_FORMAT, C, XR_OCEAN_MIPS);
  for (CD3D12Texture *pTex : { o.pWaves, o.pFoamView })
    if (pTex)
      pTex->m_nSRV = m_pDescriptors->AllocSRV(pTex->m_pResource, &MipsSRV);
  const D3D12_SHADER_RESOURCE_VIEW_DESC CausticsSRV = ArraySRV(WAVES_FORMAT, CC, XR_CAUSTICS_MIPS);
  if (o.pCaustics)
    o.pCaustics->m_nSRV = m_pDescriptors->AllocSRV(o.pCaustics->m_pResource, &CausticsSRV);
  if (!o.pRows || !o.pFields || !o.pWaves || !o.pLevel4 || !o.pFoam[0] || !o.pFoam[1] || !o.pFoamView || !o.pFoamLevel4 || !o.pCaustics ||
      o.nTables < 0 || o.pWaves->m_nSRV < 0 || o.pFoamView->m_nSRV < 0 || o.pCaustics->m_nSRV < 0)
  {
    iLog->LogError("XRenderD3D12: no FFT ocean (its targets)");
    ReleaseOcean();
    o.bFailed = true;
    return false;
  }

  // The tables (Shaders/OceanFFT.hlsl's bindings); t0 of the rows' is h0 (MakeOcean), the views the
  // kernels do not use null.
  // The foam's tables FOAM_TABLE + t make state t from the other (t1, the frame before's), FOAM_VIEW_TABLE + t the view's
  // levels 0 to 4 of state t, FOAM_MIPS_TABLE the view's levels 4 to 8.
  const D3D12_SHADER_RESOURCE_VIEW_DESC FieldsSRV = ArraySRV(FIELDS_FORMAT, 2 * C, 1), Level4SRV = ArraySRV(WAVES_FORMAT, C, 1);
  const D3D12_SHADER_RESOURCE_VIEW_DESC StateSRV = ArraySRV(FOAM_FORMAT, C, 1);
  for (int k = 0; k < KERNELS; k++)
  {
    const int nTable = o.nTables + k * TABLE;
    const bool bFoam = k >= FOAM_TABLE && k < FOAM_VIEW_TABLE, bView = k >= FOAM_VIEW_TABLE && k < FOAM_MIPS_TABLE;
    const bool bViewMips = k == FOAM_MIPS_TABLE;
    const int t = bFoam ? k - FOAM_TABLE : bView ? k - FOAM_VIEW_TABLE : 0;
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
    {
      ID3D12Resource *pIn = nullptr;
      const D3D12_SHADER_RESOURCE_VIEW_DESC *pSRV = &FieldsSRV;
      if (k == 1 && i == 0)
        pIn = o.pRows->m_pResource;
      else if ((k == 2 || bFoam) && i == 0)
        pIn = o.pFields->m_pResource;
      else if (bFoam && i == 1)
        pIn = o.pFoam[1 - t]->m_pResource, pSRV = &StateSRV;
      else if (bView && i == 0)
        pIn = o.pFoam[t]->m_pResource, pSRV = &StateSRV;
      else if (k == 3 && i == 0)
        pIn = o.pLevel4->m_pResource, pSRV = &Level4SRV;
      else if (bViewMips && i == 0)
        pIn = o.pFoamLevel4->m_pResource, pSRV = &Level4SRV;
      else if (k == CAUSTICS_TABLE && i == 0)
        pIn = o.pWaves->m_pResource, pSRV = &Level4SRV;   // their level 0
      m_pDescriptors->WriteSRV(nTable + i, pIn, pSRV);
    }
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = ArrayUAV(FIELDS_FORMAT, 2 * C, 0);
      ID3D12Resource *pOut = nullptr;
      if (k < 2)
        pOut = i == 0 ? (k == 0 ? o.pRows : o.pFields)->m_pResource : nullptr;
      else if (k == CAUSTICS_TABLE)
      {
        UAV = ArrayUAV(WAVES_FORMAT, CC, i);
        pOut = o.pCaustics->m_pResource;
      }
      else if (bFoam)
      {
        UAV = ArrayUAV(FOAM_FORMAT, C, 0);
        pOut = i == 0 ? o.pFoam[t]->m_pResource : nullptr;
      }
      else if (k == 2 || bView)
      {
        UAV = ArrayUAV(WAVES_FORMAT, C, i < 4 ? i : 0);
        pOut = i < 4 ? (bView ? o.pFoamView : o.pWaves)->m_pResource : (bView ? o.pFoamLevel4 : o.pLevel4)->m_pResource;
      }
      else
      {
        UAV = ArrayUAV(WAVES_FORMAT, C, 4 + i);
        pOut = (bViewMips ? o.pFoamView : o.pWaves)->m_pResource;
      }
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, pOut, &UAV);
    }
  }
  // Made in UNORDERED_ACCESS; the waves and the foam's view at rest where the water's program reads them, the foam's
  // states where the kernels read them, the kernels' inputs where the next kernel reads them.
  for (CD3D12Texture *pTex : { o.pWaves, o.pFoamView, o.pCaustics })
    m_pDevice->Transition(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  for (CD3D12Texture *pTex : { o.pFoam[0], o.pFoam[1] })
    m_pDevice->Transition(pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  o.nFrame = -1;
  o.nFoamFrame = -1;
  o.nCausticsFrame = -1;
  if (iLog)
    iLog->Log("XRenderD3D12: the FFT ocean: %d cascades of %d x %d texels (%g, %g, %g m)", C, N, N, XR_OCEAN_TILE0, XR_OCEAN_TILE1, XR_OCEAN_TILE2);
  return true;
}

bool CD3D12Renderer::MakeOcean(float fWind)
{
  SOceanState &o = m_Ocean;
  const auto t0 = std::chrono::steady_clock::now();
  OceanSpectrum::SSea Sea;
  OceanSpectrum::MakeSea(fWind, Sea);
  SD3D12SubData Sub[XR_OCEAN_CASCADES];
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
    Sub[c] = { Sea.Cascades[c].H0.data(), (uint32)(XR_OCEAN_SIZE * 4 * sizeof(float)) };
  if (o.pH0)
    m_pUpload->ReleaseTexture(o.pH0);
  o.pH0 = m_pUpload->CreateTexture(XR_OCEAN_SIZE, XR_OCEAN_SIZE, 1, FIELDS_FORMAT, false, Sub, XR_OCEAN_CASCADES);
  if (!o.pH0)
    return false;
  m_pDescriptors->WriteSRV(o.nTables, o.pH0->m_pResource, nullptr);
  o.fWind = fWind;
  o.fSlopeRMS = (float)Sea.SlopeRMS();
  o.fSlopeRMS0 = (float)std::sqrt((Sea.Cascades[0].fSlopeVarX + Sea.Cascades[0].fSlopeVarY) / 2);
  o.fFineVar = (float)Sea.FineVar();
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
  {
    o.fHeightWeight[c] = (float)Sea.HeightWeight(c);
    o.fDivRMS[c] = (float)Sea.JacobianSigma(c, 1.0);
    // The whitecaps' threshold at a chop of 1 / RMS of the divergence (WhitecapJacobian: 1 + chop RMS z).
    const double fShare = OceanSpectrum::WhitecapCoverage(fWind) * OceanSpectrum::WHITECAP_SHARE[c] * OceanSpectrum::WHITECAP_ACTIVE;
    o.fWhitecapZ[c] = fShare > 0 ? (float)OceanSpectrum::NormalQuantile(fShare) : 0.0f;
  }
  o.fHeightRMS0 = (float)std::sqrt(Sea.Cascades[0].fHeightVar);
  o.fHs = (float)Sea.SignificantHeight();
  o.fPeakSpeed = (float)Sea.PeakSpeed();
  if (iLog)
  {
    const double fHeightVar = Sea.Cascades[0].fHeightVar + Sea.Cascades[1].fHeightVar + Sea.Cascades[2].fHeightVar;
    iLog->Log("XRenderD3D12: the FFT ocean's sea of a wind of %g m/s: significant height %.2f m, slopes' RMS %.3f an axis, finer ripple's "
              "variance %.4f (%.0f ms)", fWind, 4 * sqrt(fHeightVar), o.fSlopeRMS, o.fFineVar,
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
  return true;
}

void CD3D12Renderer::UpdateOcean(std::span<const SRenderItem> Items)
{
  if (!CV_d3d12_WaterFFT)
  {
    if (m_Ocean.pWaves || m_Ocean.bFailed)
      ReleaseOcean();
    return;
  }
  SOceanState &o = m_Ocean;
  if (o.nFrame == m_nFrames || !std::any_of(Items.begin(), Items.end(), [](const SRenderItem &it) { return it.eDrawClass == eDC_Water; }))
    return;
  if (!EnsureFrame() || !EnsureOceanTargets())
    return;
  const float fWind = std::max(CV_d3d12_WaterWind, 0.5f);
  if ((!o.pH0 || fWind != o.fWind) && !MakeOcean(fWind))
    return;

  SOceanConstants oc = {};
  // The time's fraction of the sea's period, in double (the timer's seconds grow without end).
  const double fTime = iTimer ? (double)iTimer->GetCurrTime() : 0.0;
  const double fTau = fmod(fTime, OceanSpectrum::PERIOD) / OceanSpectrum::PERIOD;
  // Tessendorf's lambda of the wind sea's own displacement: how much narrower its crests get does not change with
  // the slopes' scale (d3d12_WaterSlope).
  const float fChop = std::max(CV_d3d12_WaterChop, 0.0f);
  Set4(oc.vParams, (float)(fTau < 0 ? fTau + 1 : fTau), fChop, LEAST_JACOBIAN, 0);
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
    Set4(oc.vCascades[c], (float)OceanSpectrum::TILE[c], o.fHeightWeight[c], (float)(OceanSpectrum::TILE[c] / XR_OCEAN_SIZE), 0);
  // The whitecaps (d3d12_WaterFoam): the foam of the frame before fades by the time since (none left in the first frame
  // or after the foam was off), each cascade's crests break below its determinant (SSea::WhitecapJacobian), white over a
  // ramp of FOAM_RAMP of its spread.
  const bool bFoam = CV_d3d12_WaterFoam > 0;
  if (!bFoam)
    o.nFoamFrame = -1;
  const double fSince = o.nFoamFrame >= 0 ? fTime - o.fFoamTime : -1;
  Set4(oc.vFoam, fSince >= 0 ? (float)std::exp(-std::min(fSince, 60.0) / OceanSpectrum::FOAM_LIFE) : 0.0f, 0, 0, 0);
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
  {
    const float fSigma = fChop * o.fDivRMS[c];
    const bool bBreaks = o.fWhitecapZ[c] != 0 && fSigma > 0;
    Set4(oc.vFoamCascades[c], bBreaks ? 1 + fSigma * o.fWhitecapZ[c] : -1.0f, bBreaks ? 1 / ((float)OceanSpectrum::FOAM_RAMP * fSigma) : 0.0f,
         fSince > 0 ? (float)OceanSpectrum::FoamSpreadShare(c, std::min(fSince, 60.0)) : 0.0f, 0);
  }
  // The caustics: towards the sun, the slopes as drawn (d3d12_WaterSlope's scale; the bump amounts at their default),
  // each caustics cascade's four depths.
  Set4(oc.vSun, m_SunDir[0], m_SunDir[1], m_SunDir[2], SeaScale(CV_d3d12_WaterSlope, o.fSlopeRMS));
  for (int c = 0; c < XR_CAUSTICS_CASCADES; c++)
  {
    const float d = (float)OceanSpectrum::CAUSTICS_DEPTH[c];
    Set4(oc.vCausticsDepths[c], d, 2 * d, 4 * d, 8 * d);
  }
  const SD3D12FrameAlloc CB = m_pUpload->AllocFrame(sizeof(oc), 256);
  memcpy(CB.pCPU, &oc, sizeof(oc));

  GpuSection("ocean");
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  ID3D12Resource *pH0 = o.pH0->m_pResource, *pRows = o.pRows->m_pResource, *pFields = o.pFields->m_pResource;
  ID3D12Resource *pWaves = o.pWaves->m_pResource, *pLevel4 = o.pLevel4->m_pResource;
  const auto Table = [&](int k, bool bUAVs) { return o.nTables + k * TABLE + (bUAVs ? CD3D12Compute::COMPUTE_SRVS : 0); };
  constexpr UINT N = XR_OCEAN_SIZE, C = XR_OCEAN_CASCADES;
  m_pDevice->EndOffscreen();
  m_pDevice->Transition(pH0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, READ);
  m_pCompute->Dispatch(pList, eD3D12K_OceanRows, CB.nGPU, Table(0, false), Table(0, true), 1, N, C);
  m_pDevice->Transition(pH0, READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pRows, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_pCompute->Dispatch(pList, eD3D12K_OceanColumns, CB.nGPU, Table(1, false), Table(1, true), 1, N, C);
  m_pDevice->Transition(pRows, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pDevice->Transition(pFields, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_pDevice->Transition(pWaves, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pCompute->Dispatch(pList, eD3D12K_OceanWaves, CB.nGPU, Table(2, false), Table(2, true), N / 16, N / 16, C);
  m_pDevice->Transition(pFields, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pDevice->Transition(pLevel4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_pCompute->Dispatch(pList, eD3D12K_OceanMips, CB.nGPU, Table(3, false), Table(3, true), 1, 1, C);
  m_pDevice->Transition(pLevel4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  // The caustics (d3d12_WaterCaustics) when the frame draws them, in daylight: from the waves' level 0 at the frame's
  // time, their mip levels as the waves'.
  const bool bCaustics = CV_d3d12_WaterCaustics > 0 && CV_d3d12_Caustics && m_SunDir[2] > 0 &&
                         std::any_of(Items.begin(), Items.end(), [](const SRenderItem &it) { return it.eDrawClass == eDC_Caustics; });
  if (bCaustics)
  {
    ID3D12Resource *pCaustics = o.pCaustics->m_pResource;
    constexpr UINT CC = XR_CAUSTICS_CASCADES;
    m_pDevice->Transition(pWaves, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, READ);
    m_pDevice->Transition(pCaustics, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pCompute->Dispatch(pList, eD3D12K_OceanCaustics, CB.nGPU, Table(CAUSTICS_TABLE, false), Table(CAUSTICS_TABLE, true), N / XR_CAUSTICS_TILE,
                         N / XR_CAUSTICS_TILE, CC);
    m_pDevice->Transition(pWaves, READ, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_pDevice->Transition(pCaustics, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    o.nCausticsFrame = m_nFrames;
  }
  else
    m_pDevice->Transition(pWaves, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  if (bFoam && (fSince < 0 || fSince >= OceanSpectrum::FOAM_STEP))
  {
    // The foam's state into the other target than the frame before's, from it; then its view and the view's mip levels
    // (at most once a FOAM_STEP: between, the water reads the last).
    const int t = o.nFoamFrame >= 0 ? 1 - o.nFoam : 0;
    ID3D12Resource *pState = o.pFoam[t]->m_pResource, *pView = o.pFoamView->m_pResource, *pFoamLevel4 = o.pFoamLevel4->m_pResource;
    m_pDevice->Transition(pFields, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_pDevice->Transition(pState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pCompute->Dispatch(pList, eD3D12K_OceanFoam, CB.nGPU, Table(FOAM_TABLE + t, false), Table(FOAM_TABLE + t, true), N / 16, N / 16, C);
    m_pDevice->Transition(pFields, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pDevice->Transition(pState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_pDevice->Transition(pView, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pCompute->Dispatch(pList, eD3D12K_OceanFoamView, CB.nGPU, Table(FOAM_VIEW_TABLE + t, false), Table(FOAM_VIEW_TABLE + t, true), N / 16,
                         N / 16, C);
    m_pDevice->Transition(pFoamLevel4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_pCompute->Dispatch(pList, eD3D12K_OceanMips, CB.nGPU, Table(FOAM_MIPS_TABLE, false), Table(FOAM_MIPS_TABLE, true), 1, 1, C);
    m_pDevice->Transition(pFoamLevel4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_pDevice->Transition(pView, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    o.nFoam = t;
    o.nFoamFrame = m_nFrames;
    o.fFoamTime = fTime;
  }
  o.nFrame = m_nFrames;
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "ocean: wind %g m/s, tau %.6f, chop %.3f, slopes' RMS %.3f, finer ripple %.4f, foam %g (left %.4f, breaks below %.3f %.3f)\n",
            o.fWind, oc.vParams[0], fChop, o.fSlopeRMS, o.fFineVar, CV_d3d12_WaterFoam, oc.vFoam[0], oc.vFoamCascades[0][0], oc.vFoamCascades[1][0]);
}

bool CD3D12Renderer::SetOceanConstants(SDrawConstants &dc, float fBump, int nFoamPattern) const
{
  const SOceanState &o = m_Ocean;
  if (!CV_d3d12_WaterFFT || !o.pWaves || o.nFrame < 0)
    return false;
  // The bump amounts a factor of the wind's sea (1 at their default), d3d12_WaterSlope its slopes' RMS there; the
  // finer ripple the wind's, times the bump amounts' factor alone (scaled as the cascades, the least at 3.5 m/s, 0.0011
  // at 5, Carrier's glint would shrink to a small core of the game's patch).
  const float fLevel = fBump / DEFAULT_BUMP;
  dc.vParams0[0] = fLevel * SeaScale(CV_d3d12_WaterSlope, o.fSlopeRMS);
  dc.nShadowTex[2] = (uint32)o.pWaves->m_nSRV;
  Set4(dc.vAddColor, o.fSlopeRMS, std::max(o.fFineVar * fLevel * fLevel, (float)OceanSpectrum::LEAST_FINE_VARIANCE), 0, 0);

  // The foam (d3d12_WaterFoam; Water.hlsl's WaterFoam): its texels of the frame, the long waves' height's RMS, the sea's
  // significant height as drawn (scaled as the slopes), the pattern; the speed of the waves at the spectrum's peak along
  // the wind; the pattern's drift with the wind (3 % of its speed, the wind's drift of the surface) as each sample's tile
  // wraps it.
  Set4(dc.vTexGen1U, 0, 0, 0, 0);
  if (nFoamPattern >= 0 && CV_d3d12_WaterFoam > 0 && o.nFoamFrame >= 0 && o.fHeightRMS0 > 0)
  {
    dc.nShadowTex[3] = (uint32)o.pFoamView->m_nSRV;
    const uint32 nPattern = (uint32)nFoamPattern;
    Set4(dc.vTexGen1U, CV_d3d12_WaterFoam, 1 / o.fHeightRMS0, o.fHs * dc.vParams0[0], 0);
    memcpy(&dc.vTexGen1U[3], &nPattern, sizeof(nPattern));
    const double W[2] = { OceanSpectrum::WIND[0], OceanSpectrum::WIND[1] };
    // The swell's surf (d3d12_WaterSurf: its height at the shore; Water.hlsl's SurfFoam): the periods gone, wrapped at
    // a multiple of each bore's strength's hash (it jumps once in 10 hours).
    const double fCycles = std::fmod((iTimer ? (double)iTimer->GetCurrTime() : 0.0) / XR_FOAM_SURF_PERIOD, 4096.0);
    Set4(dc.vTexGen1V, (float)(o.fPeakSpeed * W[0]), (float)(o.fPeakSpeed * W[1]), std::max(CV_d3d12_WaterSurf, 0.0f), (float)fCycles);
    const double fDrift = 0.03 * o.fWind * (iTimer ? (double)iTimer->GetCurrTime() : 0.0);
    const double d[2] = { fDrift * W[0], fDrift * W[1] };
    const double r[2] = { XR_FOAM_TURN_COS * d[0] - XR_FOAM_TURN_SIN * d[1], XR_FOAM_TURN_SIN * d[0] + XR_FOAM_TURN_COS * d[1] };
    Set4(dc.vTexMat0, (float)std::fmod(d[0], XR_FOAM_TILE0), (float)std::fmod(d[1], XR_FOAM_TILE0), (float)std::fmod(r[0], XR_FOAM_TILE1),
         (float)std::fmod(r[1], XR_FOAM_TILE1));
    // The breaker index's noise drifts at 0.25 m/s (its octaves apart: the one turned): where the waves break further out
    // changes over some seconds (it jumps once in 6 hours, as the drift wraps).
    const double fNoiseDrift = std::fmod(0.25 * (iTimer ? (double)iTimer->GetCurrTime() : 0.0), 6000.0);
    Set4(dc.vTexMat1, (float)(fNoiseDrift * W[0]), (float)(fNoiseDrift * W[1]), 0, 0);
  }
  return true;
}

bool CD3D12Renderer::SetOceanCaustics(SDrawConstants &dc, float fSun) const
{
  const SOceanState &o = m_Ocean;
  if (!CV_d3d12_WaterFFT || CV_d3d12_WaterCaustics <= 0 || !o.pCaustics || o.nCausticsFrame < 0)
    return false;
  // General.hlsl's DF_CAUSTICS of the FFT ocean (vParams2.x > 0): the caustics' texture (nTex1, an array), each caustics
  // cascade's 1 / tile, its first depth; the share of the light they move (fSun: less at a low sun), the depth they are
  // gone by, the most they brighten.
  constexpr int F = XR_CAUSTICS_FIRST;
  dc.nTex1 = (uint32)o.pCaustics->m_nSRV;
  Set4(dc.vParams2, (float)(1 / OceanSpectrum::TILE[F]), (float)(1 / OceanSpectrum::TILE[F + 1]), 0, 0);
  Set4(dc.vParams3, (float)OceanSpectrum::CAUSTICS_DEPTH[0], (float)OceanSpectrum::CAUSTICS_DEPTH[1], 0, 0);
  dc.vParams0[3] = CAUSTICS_SUN_SHARE * fSun;
  dc.vParams1[2] = CAUSTICS_FADE_DEPTH;
  dc.vParams1[3] = CAUSTICS_MOST;
  return true;
}

void CD3D12Renderer::ReleaseOcean()
{
  SOceanState &o = m_Ocean;
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : { o.pH0, o.pRows, o.pFields, o.pWaves, o.pLevel4, o.pFoam[0], o.pFoam[1], o.pFoamView, o.pFoamLevel4, o.pCaustics })
      m_pUpload->ReleaseTexture(pTex);
  }
  if (m_pDescriptors && o.nTables >= 0)
    m_pDescriptors->FreeRange(o.nTables, KERNELS * TABLE);
  o = SOceanState();
}
