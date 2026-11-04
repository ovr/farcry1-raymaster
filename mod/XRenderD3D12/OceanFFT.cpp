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
//	    the cascade's slopes, the squared slope; with the displacement of
//	    d3d12_WaterChop the sharp crests) into level 0 of pWaves (RGBA16F, a
//	    slice a cascade), the means of 2 x 2 into levels 1 to 4 (level 4 also
//	    into pLevel4);
//	  - the mip levels: 4 to 8 from pLevel4.
//	The water's program (Water.hlsl's WaveNormal) samples the three slices
//	at the world position over their tiles, each in metres per metre, their
//	slopes summed, times the bump amounts' factor and d3d12_WaterSlope over
//	the sea's own RMS (SetOceanConstants: by default the old texture's
//	steepness, the wind setting only the waves' lengths, directions and
//	speeds). The waves stay as they were in a frame without water (the
//	environment cubes may still draw the ocean).
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

  constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;
  constexpr int KERNELS = 4;                       //!< rows, columns, waves, mip levels: a table each
  constexpr DXGI_FORMAT FIELDS_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
  constexpr DXGI_FORMAT WAVES_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
  //! The least determinant of the moved surface's Jacobian (OceanWaves): where the crest would fold over.
  constexpr float LEAST_JACOBIAN = 0.1f;
  //! The bump amounts the slopes' factor is 1 at (Water.cpp's defaults, 0.03 and 0.02, times 1.2).
  constexpr float DEFAULT_BUMP = 0.06f;
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

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
  o.nTables = m_pDescriptors->AllocRange(KERNELS * TABLE);
  if (o.pWaves)
  {
    const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = ArraySRV(WAVES_FORMAT, C, XR_OCEAN_MIPS);
    o.pWaves->m_nSRV = m_pDescriptors->AllocSRV(o.pWaves->m_pResource, &SRV);
  }
  if (!o.pRows || !o.pFields || !o.pWaves || !o.pLevel4 || o.nTables < 0 || o.pWaves->m_nSRV < 0)
  {
    iLog->LogError("XRenderD3D12: no FFT ocean (its targets)");
    ReleaseOcean();
    o.bFailed = true;
    return false;
  }

  // The tables (Shaders/OceanFFT.hlsl's bindings); t0 of the rows' is h0 (MakeOcean), the views the
  // kernels do not use null.
  const D3D12_SHADER_RESOURCE_VIEW_DESC FieldsSRV = ArraySRV(FIELDS_FORMAT, 2 * C, 1), Level4SRV = ArraySRV(WAVES_FORMAT, C, 1);
  for (int k = 0; k < KERNELS; k++)
  {
    const int nTable = o.nTables + k * TABLE;
    for (int i = 0; i < CD3D12Compute::COMPUTE_SRVS; i++)
    {
      ID3D12Resource *pIn = i > 0 || k == 0 ? nullptr : k == 1 ? o.pRows->m_pResource : k == 2 ? o.pFields->m_pResource : o.pLevel4->m_pResource;
      m_pDescriptors->WriteSRV(nTable + i, pIn, k == 3 ? &Level4SRV : &FieldsSRV);
    }
    for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = ArrayUAV(FIELDS_FORMAT, 2 * C, 0);
      ID3D12Resource *pOut = nullptr;
      if (k < 2)
        pOut = i == 0 ? (k == 0 ? o.pRows : o.pFields)->m_pResource : nullptr;
      else if (k == 2)
      {
        UAV = ArrayUAV(WAVES_FORMAT, C, i < 4 ? i : 0);
        pOut = (i < 4 ? o.pWaves : o.pLevel4)->m_pResource;
      }
      else
      {
        UAV = ArrayUAV(WAVES_FORMAT, C, 4 + i);
        pOut = o.pWaves->m_pResource;
      }
      m_pDescriptors->WriteUAV(nTable + CD3D12Compute::COMPUTE_SRVS + i, pOut, &UAV);
    }
  }
  // Made in UNORDERED_ACCESS; the waves at rest where the water's program reads them, the
  // kernels' inputs where the next kernel reads them.
  m_pDevice->Transition(o.pWaves->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  o.nFrame = -1;
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
  o.fFineVar = (float)Sea.FineVar();
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
    o.fHeightWeight[c] = (float)Sea.HeightWeight(c);
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
  Set4(oc.vParams, (float)(fTau < 0 ? fTau + 1 : fTau), 0.0f, LEAST_JACOBIAN, 0);
  for (int c = 0; c < XR_OCEAN_CASCADES; c++)
    Set4(oc.vCascades[c], (float)OceanSpectrum::TILE[c], o.fHeightWeight[c], (float)(OceanSpectrum::TILE[c] / XR_OCEAN_SIZE), 0);
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
  m_pDevice->Transition(pWaves, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  o.nFrame = m_nFrames;
  if (m_pFrameDump)
    fprintf(m_pFrameDump.get(), "ocean: wind %g m/s, tau %.6f, slopes' RMS %.3f, finer ripple %.4f\n", o.fWind, oc.vParams[0], o.fSlopeRMS, o.fFineVar);
}

bool CD3D12Renderer::SetOceanConstants(SDrawConstants &dc, float fBump) const
{
  const SOceanState &o = m_Ocean;
  if (!CV_d3d12_WaterFFT || !o.pWaves || o.nFrame < 0)
    return false;
  // The bump amounts a factor of the wind's sea (1 at their default), d3d12_WaterSlope its slopes' RMS there.
  dc.vParams0[0] = fBump / DEFAULT_BUMP * (CV_d3d12_WaterSlope > 0 && o.fSlopeRMS > 0 ? CV_d3d12_WaterSlope / o.fSlopeRMS : 1.0f);
  dc.nShadowTex[2] = (uint32)o.pWaves->m_nSRV;
  Set4(dc.vAddColor, o.fSlopeRMS, o.fFineVar, 0, 0);
  return true;
}

void CD3D12Renderer::ReleaseOcean()
{
  SOceanState &o = m_Ocean;
  if (m_pUpload)
  {
    for (CD3D12Texture *pTex : { o.pH0, o.pRows, o.pFields, o.pWaves, o.pLevel4 })
      m_pUpload->ReleaseTexture(pTex);
  }
  if (m_pDescriptors && o.nTables >= 0)
    m_pDescriptors->FreeRange(o.nTables, KERNELS * TABLE);
  o = SOceanState();
}
