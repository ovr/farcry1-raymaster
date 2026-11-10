// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: UpscalerTaa.cpp
//	Description: TAA (d3d12_AA taa without an upscaler), our own temporal
//	anti-aliasing (Shaders/Taa.hlsl; the method is Shaders/Taa.hlsli's) as
//	an upscaler of the display's size (Upscaler.h), so that it gets what
//	FSR gets (Upscale.cpp): the scene jittered, 8 phases of Halton(2, 3),
//	its motion vectors and depth, the textures sampled sharper, the
//	history reset, the HDR scene's light with its exposure
//	(d3d12_UpscaleHdr). Two kernels a frame:
//	  1. the frame blended into the history at each pixel's motion (the
//	     nearest of its 3 x 3's): two R16G16B16A16_FLOAT targets, one read
//	     and the other written in turn; into the picture too without
//	     sharpening, or with the debug view (d3d12_AADebug);
//	  2. with d3d12_UpscaleSharpness, the new history sharpened into the
//	     picture by AMD FidelityFX CAS (third_party/fidelityfx-cas,
//	     Shaders/TaaCas.hlsli), its sharpness d3d12_UpscaleSharpness.
//	d3d12_AAQuality is the frame's least share in the picture: low 1/5
//	to ultra 1/14, how many frames it gathers.
//	With d3d12_AASceneLight the first kernel makes the HDR scene's light
//	itself (Hdr.hlsl's pass, the volumetric light through the froxels)
//	into a tile of its group, in place of reading that pass's target.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "Upscaler.h"
#include "D3D12Compute.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"

// AMD FidelityFX CAS's constants on the CPU (CasSetup).
#define A_CPU 1
#include "ffx_a.h"
#include "ffx_cas.h"

namespace
{

constexpr int SRVS = CD3D12Compute::COMPUTE_SRVS;
constexpr int TABLE = CD3D12Compute::COMPUTE_SRVS + CD3D12Compute::COMPUTE_UAVS;  //!< slots of a table: its SRVs, then its UAVs
constexpr int JITTER_PHASES = 8;
//! The frame's least share in the picture by d3d12_AAQuality, low to ultra: at rest, and from a pixel's motion a frame on
//! (1.5 times it: the render tests' aa in motion kept 0.75-0.79 of the reference's texture detail and 0.79 of none's
//! flicker with it; twice it 0.81-0.84 and 0.82, the least share alone 0.68-0.74 and 0.75).
const float s_LeastShare[4] = { 1 / 5.0f, 1 / 7.0f, 1 / 10.0f, 1 / 14.0f };
const float s_MovingShare[4] = { 1.5f / 5, 1.5f / 7, 1.5f / 10, 1.5f / 14 };

//! The descriptor tables: the resolve's and the sharpening's reading history n.
enum ETable
{
  eT_Resolve0,        //!< t0 the colours, t1 the depth, t2 the motion vectors, t3 history 0; u0 history 1, u1 the picture, u2 the exposure
  eT_Resolve1,        //!< the same from history 1 into history 0
  eT_Sharpen0,        //!< t0 history 0, t1 the exposure; u0 the picture
  eT_Sharpen1,        //!< the same from history 1
  eT_Num
};

//! The inputs the tables hold views of.
enum EInput
{
  eI_Color,
  eI_Depth,
  eI_Velocity,
  eI_Output,
  eI_Exposure,
  eI_Num
};

constexpr D3D12_RESOURCE_STATES UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc(DXGI_FORMAT Format)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
  Desc.Format = Format;
  Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  Desc.Texture2D.MipLevels = 1;
  return Desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc(DXGI_FORMAT Format)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC Desc = {};
  Desc.Format = Format;
  Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  return Desc;
}

//! The format a view of the resource reads it in (its own; UNORM for a typeless one of 8 bits a channel).
DXGI_FORMAT ViewFormat(ID3D12Resource *pResource)
{
  const DXGI_FORMAT Format = pResource->GetDesc().Format;
  switch (Format)
  {
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  default:
    return Format;
  }
}

class CUpscalerTaa final : public IUpscaler
{
public:
  ~CUpscalerTaa() override { Release(); }

  bool Create(ID3D12Device *, int nWidth, int nHeight, bool, int, bool bHdr) override
  {
    Release();
    CD3D12Compute *pCompute = g_pRenderer ? g_pRenderer->m_pCompute.get() : nullptr;
    CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
    if (!pCompute || !pUpload || !g_pRenderer->m_pDescriptors || nWidth <= 0 || nHeight <= 0)
      return false;
    for (ED3D12Kernel e : { eD3D12K_TaaResolve, eD3D12K_TaaSharpen })
    {
      if (!pCompute->HasKernel(e))
      {
        iLog->LogWarning("XRenderD3D12: TAA: its kernels are not available");
        return false;
      }
    }
    m_pHistory[0] = pUpload->CreateComputeTarget(nWidth, nHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, L"TAA history 0");
    m_pHistory[1] = pUpload->CreateComputeTarget(nWidth, nHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, L"TAA history 1");
    if (!m_pHistory[0] || !m_pHistory[1])
    {
      Release();
      return false;
    }
    // The compute targets start in UNORDERED_ACCESS.
    m_eHistoryState[0] = m_eHistoryState[1] = UAV;
    m_nWidth = nWidth;
    m_nHeight = nHeight;
    m_bHdr = bHdr;
    m_bSceneLight = bHdr && pCompute->HasKernel(eD3D12K_TaaResolveLight);
    return true;
  }

  void GetRenderSize(int, int &nWidth, int &nHeight) override
  {
    nWidth = m_nWidth;
    nHeight = m_nHeight;
  }

  void GetJitter(int nFrame, int, float &x, float &y) override
  {
    const int i = nFrame % JITTER_PHASES + 1;
    x = Halton(i, 2) - 0.5f;
    y = Halton(i, 3) - 0.5f;
  }

  bool Dispatch(const SUpscaleInputs &In) override
  {
    CD3D12Compute *pCompute = g_pRenderer->m_pCompute.get();
    ID3D12GraphicsCommandList *pList = In.pCmdList;
    if (!pCompute || !pList || In.nRenderWidth != m_nWidth || In.nRenderHeight != m_nHeight || !UpdateTables(In))
    {
      if (!m_bFailed)
        iLog->LogError("XRenderD3D12: TAA: the frame of %d x %d is not its own (%d x %d), or no descriptors", In.nRenderWidth,
                       In.nRenderHeight, m_nWidth, m_nHeight);
      m_bFailed = true;
      return false;
    }
    m_bFailed = false;

    const int nQuality = std::clamp(In.nQuality, 0, 3);
    const bool bSharpen = In.fSharpness > 0 && !In.bDebugView;
    STaaConstants Consts = {};
    Consts.vSize[0] = (float)m_nWidth;
    Consts.vSize[1] = (float)m_nHeight;
    Consts.vSize[2] = 1.0f / m_nWidth;
    Consts.vSize[3] = 1.0f / m_nHeight;
    Consts.vParams[0] = s_LeastShare[nQuality];
    Consts.vParams[1] = std::clamp(In.fSharpness, 0.0f, 1.0f);
    Consts.vParams[2] = 1;
    Consts.vParams[3] = s_MovingShare[nQuality];
    Consts.vFlags[0] = (In.bReset || !m_bHistory ? TAA_RESET : 0) | (m_bHdr ? TAA_HDR : 0) | (m_bHdr && In.pExposure ? TAA_EXPOSURE : 0) |
                       (bSharpen ? 0 : TAA_OUTPUT) | (In.bDebugView ? TAA_DEBUG : 0);
    // The scene's light made by the resolve (d3d12_AASceneLight): its inputs by their slots of the texture table, the
    // resolve's table's views of the depth, the motion vectors and the history read.
    const bool bSceneLight = In.bSceneLight && m_bSceneLight && In.nSceneSRV >= 0;
    if (In.bSceneLight && !bSceneLight)
      return false;
    const int nResolve = eT_Resolve0 + m_nRead;
    if (bSceneLight)
    {
      Consts.vInputs[0] = (uint32)In.nSceneSRV;
      Consts.vInputs[1] = (uint32)(Table(nResolve) + 1);
      Consts.vInputs[2] = (uint32)(Table(nResolve) + 2);
      Consts.vInputs[3] = (uint32)(Table(nResolve) + 3);
      if (In.bVolumetric && In.nVolumeDepthSRV >= 0 && In.nFroxelsSRV >= 0)
      {
        Consts.vFlags[0] |= TAA_VOLUMETRIC;
        Consts.vVolumeInputs[0] = (uint32)In.nVolumeDepthSRV;
        Consts.vVolumeInputs[1] = (uint32)In.nFroxelsSRV;
        memcpy(Consts.vVolume0, In.vVolume0, sizeof(Consts.vVolume0));
        memcpy(Consts.vVolume1, In.vVolume1, sizeof(Consts.vVolume1));
      }
    }
    // CAS's sharpness is d3d12_UpscaleSharpness (0 its least, which 0 turns off here, 1 its most), at the picture's size.
    AU1 Cas0[4], Cas1[4];
    CasSetup(Cas0, Cas1, Consts.vParams[1], (AF1)m_nWidth, (AF1)m_nHeight, (AF1)m_nWidth, (AF1)m_nHeight);
    static_assert(sizeof(Cas0) == sizeof(Consts.vCas0) && sizeof(Cas1) == sizeof(Consts.vCas1), "CAS's constants");
    memcpy(Consts.vCas0, Cas0, sizeof(Cas0));
    memcpy(Consts.vCas1, Cas1, sizeof(Cas1));
    const SD3D12FrameAlloc CB = g_pRenderer->m_pUpload->AllocFrame(sizeof(Consts), 256);
    if (!CB.pCPU)
      return false;
    memcpy(CB.pCPU, &Consts, sizeof(Consts));

    const int nRead = m_nRead, nWrite = nRead ^ 1;
    ID3D12Resource *pRead = m_pHistory[nRead]->m_pResource, *pWrite = m_pHistory[nWrite]->m_pResource;
    const UINT gx = (UINT)(m_nWidth + XR_TAA_GROUP - 1) / XR_TAA_GROUP, gy = (UINT)(m_nHeight + XR_TAA_GROUP - 1) / XR_TAA_GROUP;
    // The device's queue (D3D12Barriers.h): given in one call before the next dispatch, as many as there are.
    CD3D12Device *pDevice = g_pRenderer->m_pDevice.get();
    const auto Move = [&](ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
    {
      if (Before != After)
        pDevice->TransitionOn(pList, pRes, Before, After);
    };
    // The history to read as a texture, the other to write.
    Move(pRead, m_eHistoryState[nRead], READ);
    Move(pWrite, m_eHistoryState[nWrite], UAV);
    m_eHistoryState[nRead] = READ;
    m_eHistoryState[nWrite] = UAV;
    // The exposure (a tracked resource) read by the resolve through a UAV, the SRVs' slots taken; by the sharpening as an SRV.
    const bool bExposure = (Consts.vFlags[0] & TAA_EXPOSURE) != 0;
    if (bExposure)
      pDevice->Require(In.pExposure, UAV);

    if (bSceneLight)
      pCompute->DispatchRays(pList, eD3D12K_TaaResolveLight, 0, CB.nGPU, 0, 0, Table(nResolve) + SRVS, gx, gy);
    else
      pCompute->Dispatch(pList, eD3D12K_TaaResolve, CB.nGPU, Table(nResolve), Table(nResolve) + SRVS, gx, gy);
    // The new history read by the sharpening and next frame.
    Move(pWrite, UAV, READ);
    m_eHistoryState[nWrite] = READ;
    if (bExposure)
      pDevice->Require(In.pExposure, READ);
    if (bSharpen)
    {
      const int nSharpen = eT_Sharpen0 + nWrite;
      pCompute->Dispatch(pList, eD3D12K_TaaSharpen, CB.nGPU, Table(nSharpen), Table(nSharpen) + SRVS, gx, gy);
    }
    m_nRead = nWrite;
    m_bHistory = true;
    return true;
  }

  const char *GetName() const override { return "TAA"; }
  bool IsAntiAliasing() const override { return true; }
  bool MakesSceneLight() const override { return m_bSceneLight; }

private:
  void Release()
  {
    CD3D12Upload *pUpload = g_pRenderer ? g_pRenderer->m_pUpload.get() : nullptr;
    for (CD3D12Texture **pp : { &m_pHistory[0], &m_pHistory[1] })
    {
      if (*pp && pUpload)
        pUpload->ReleaseTexture(*pp);
      *pp = nullptr;
    }
    if (m_nTable >= 0 && g_pRenderer && g_pRenderer->m_pDescriptors)
      g_pRenderer->m_pDescriptors->FreeRange(m_nTable, eT_Num * TABLE);
    m_nTable = -1;
    for (ComPtr<ID3D12Resource> &p : m_pInputs)
      p.Reset();
    m_bHistory = false;
    m_nRead = 0;
  }

  int Table(int e) const { return m_nTable + e * TABLE; }

  //! The tables for this frame's inputs: made again when one of them is another resource (the old slots freed after the
  //! frames that use them). The inputs are held, so that a new one is never at their address.
  bool UpdateTables(const SUpscaleInputs &In)
  {
    ID3D12Resource *const Inputs[eI_Num] = { In.pColor, In.pDepth, In.pVelocity, In.pOutput, m_bHdr ? In.pExposure : nullptr };
    if (!In.pColor || !In.pDepth || !In.pVelocity || !In.pOutput)
      return false;
    bool bSame = m_nTable >= 0;
    for (int i = 0; i < eI_Num; i++)
      bSame = bSame && m_pInputs[i].Get() == Inputs[i];
    if (bSame)
      return true;
    CD3D12Descriptors *pDesc = g_pRenderer->m_pDescriptors.get();
    if (m_nTable >= 0)
      pDesc->FreeRange(m_nTable, eT_Num * TABLE);
    for (ComPtr<ID3D12Resource> &p : m_pInputs)
      p.Reset();
    m_nTable = pDesc->AllocRange(eT_Num * TABLE);
    if (m_nTable < 0)
      return false;
    for (int i = 0; i < eI_Num; i++)
      m_pInputs[i] = Inputs[i];

    // Null UAVs where a table has none (the SRV slots are null SRVs already).
    const D3D12_UNORDERED_ACCESS_VIEW_DESC NullUAV = UAVDesc(DXGI_FORMAT_R32_FLOAT);
    for (int e = 0; e < eT_Num; e++)
      for (int i = 0; i < CD3D12Compute::COMPUTE_UAVS; i++)
        pDesc->WriteUAV(Table(e) + SRVS + i, nullptr, &NullUAV);

    const D3D12_SHADER_RESOURCE_VIEW_DESC History = SRVDesc(DXGI_FORMAT_R16G16B16A16_FLOAT), Color = SRVDesc(ViewFormat(In.pColor)),
                                          Exposure = SRVDesc(DXGI_FORMAT_R32_FLOAT);
    const D3D12_UNORDERED_ACCESS_VIEW_DESC HistoryUAV = UAVDesc(DXGI_FORMAT_R16G16B16A16_FLOAT), Output = UAVDesc(ViewFormat(In.pOutput)),
                                           ExposureUAV = UAVDesc(DXGI_FORMAT_R32_FLOAT);
    const D3D12_SHADER_RESOURCE_VIEW_DESC Depth = SRVDesc(ViewFormat(In.pDepth)), Velocity = SRVDesc(ViewFormat(In.pVelocity));
    for (int n = 0; n < 2; n++)
    {
      const int r = Table(eT_Resolve0 + n);
      pDesc->WriteSRV(r, In.pColor, &Color);
      pDesc->WriteSRV(r + 1, In.pDepth, &Depth);
      pDesc->WriteSRV(r + 2, In.pVelocity, &Velocity);
      pDesc->WriteSRV(r + 3, m_pHistory[n]->m_pResource, &History);
      pDesc->WriteUAV(r + SRVS, m_pHistory[n ^ 1]->m_pResource, &HistoryUAV);
      pDesc->WriteUAV(r + SRVS + 1, In.pOutput, &Output);
      if (Inputs[eI_Exposure])
        pDesc->WriteUAV(r + SRVS + 2, Inputs[eI_Exposure], &ExposureUAV);
      const int s = Table(eT_Sharpen0 + n);
      pDesc->WriteSRV(s, m_pHistory[n]->m_pResource, &History);
      if (Inputs[eI_Exposure])
        pDesc->WriteSRV(s + 1, Inputs[eI_Exposure], &Exposure);
      pDesc->WriteUAV(s + SRVS, In.pOutput, &Output);
    }
    return true;
  }

  int m_nWidth = 0, m_nHeight = 0;          //!< the display's size
  bool m_bHdr = false;                      //!< the HDR scene's light with its exposure (Create's bHdr)
  bool m_bSceneLight = false;               //!< with m_bHdr: the resolve that makes the scene's light is there
  CD3D12Texture *m_pHistory[2] = {};        //!< R16G16B16A16_FLOAT: [m_nRead] the history, the other one written next
  D3D12_RESOURCE_STATES m_eHistoryState[2] = { UAV, UAV };
  int m_nRead = 0;
  bool m_bHistory = false;                  //!< m_pHistory[m_nRead] holds a frame
  int m_nTable = -1;                        //!< eT_Num tables (CD3D12Descriptors::AllocRange)
  ComPtr<ID3D12Resource> m_pInputs[eI_Num]; //!< the inputs the tables were written for
  bool m_bFailed = false;                   //!< the failure logged
};

}

std::unique_ptr<IUpscaler> CreateUpscalerTaa()
{
  return std::make_unique<CUpscalerTaa>();
}
