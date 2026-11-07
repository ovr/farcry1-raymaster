// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: NrdHost.cpp
//	Description: NVIDIA NRD's passes run by the renderer's device
//	(NrdHost.h).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "NrdHost.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include <map>
#include <vector>

#if XRENDERD3D12_NRD
#include <NRD.h>
#endif

using Microsoft::WRL::ComPtr;

#if XRENDERD3D12_NRD

namespace
{

//! The only denoiser of the instance: the paths' diffuse light.
constexpr nrd::Identifier NRD_DIFFUSE = 0;

DXGI_FORMAT DxgiFormat(nrd::Format f)
{
  switch (f)
  {
  case nrd::Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
  case nrd::Format::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
  case nrd::Format::R8_UINT: return DXGI_FORMAT_R8_UINT;
  case nrd::Format::R8_SINT: return DXGI_FORMAT_R8_SINT;
  case nrd::Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
  case nrd::Format::RG8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
  case nrd::Format::RG8_UINT: return DXGI_FORMAT_R8G8_UINT;
  case nrd::Format::RG8_SINT: return DXGI_FORMAT_R8G8_SINT;
  case nrd::Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
  case nrd::Format::RGBA8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
  case nrd::Format::RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
  case nrd::Format::RGBA8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
  case nrd::Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  case nrd::Format::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
  case nrd::Format::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
  case nrd::Format::R16_UINT: return DXGI_FORMAT_R16_UINT;
  case nrd::Format::R16_SINT: return DXGI_FORMAT_R16_SINT;
  case nrd::Format::R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;
  case nrd::Format::RG16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
  case nrd::Format::RG16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
  case nrd::Format::RG16_UINT: return DXGI_FORMAT_R16G16_UINT;
  case nrd::Format::RG16_SINT: return DXGI_FORMAT_R16G16_SINT;
  case nrd::Format::RG16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
  case nrd::Format::RGBA16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
  case nrd::Format::RGBA16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
  case nrd::Format::RGBA16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
  case nrd::Format::RGBA16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
  case nrd::Format::RGBA16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case nrd::Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
  case nrd::Format::R32_SINT: return DXGI_FORMAT_R32_SINT;
  case nrd::Format::R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
  case nrd::Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
  case nrd::Format::RG32_SINT: return DXGI_FORMAT_R32G32_SINT;
  case nrd::Format::RG32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
  case nrd::Format::RGB32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
  case nrd::Format::RGB32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
  case nrd::Format::RGB32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
  case nrd::Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
  case nrd::Format::RGBA32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
  case nrd::Format::RGBA32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case nrd::Format::R10_G10_B10_A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
  case nrd::Format::R10_G10_B10_A2_UINT: return DXGI_FORMAT_R10G10B10A2_UINT;
  case nrd::Format::R11_G11_B10_UFLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
  case nrd::Format::R9_G9_B9_E5_UFLOAT: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
  default: return DXGI_FORMAT_UNKNOWN;
  }
}

//! A texture NRD reads (SRV), as the kernels read theirs between dispatches.
constexpr D3D12_RESOURCE_STATES NRD_READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

}

struct CD3D12Nrd::SImpl
{
  ID3D12Device *pDevice = nullptr;
  CD3D12Descriptors *pDescriptors = nullptr;
  CD3D12Upload *pUpload = nullptr;
  nrd::Instance *pInstance = nullptr;
  int nWidth = 0, nHeight = 0;

  //! By NRD's pipeline index: made at the first Init, kept (the same denoiser has the same pipelines).
  struct SPipeline
  {
    ComPtr<ID3D12RootSignature> pRoot;
    ComPtr<ID3D12PipelineState> pPSO;
  };
  std::vector<SPipeline> Pipelines;

  //! NRD's own textures: its permanent pool, then its transient one (their states kept by the device: TrackState).
  struct STexture
  {
    CD3D12Texture *pTex = nullptr;
  };
  std::vector<STexture> Pool;
  uint32_t nPermanent = 0;

  //! The descriptor tables of the passes: by the pipeline and each texture with its view.
  struct STable
  {
    int nFirst;
    int nCount;
  };
  std::map<std::vector<uintptr_t>, STable> Tables;

  // The frame before's view.
  SNrdView Prev = {};
  bool bPrev = false;
  bool bClear = true;           //!< the next frame clears NRD's textures (new ones)
  uint32_t nFrameIndex = 0;
  int nPasses = 0;

  bool CreatePipelines(const nrd::InstanceDesc &Desc);
  bool CreatePool(const nrd::InstanceDesc &Desc);
  int GetTable(const nrd::DispatchDesc &d, CD3D12Texture *const *pTextures);
};

bool CD3D12Nrd::SImpl::CreatePipelines(const nrd::InstanceDesc &Desc)
{
  if (Pipelines.size() == Desc.pipelinesNum)
    return true;
  Pipelines.clear();
  Pipelines.resize(Desc.pipelinesNum);
  D3D12_STATIC_SAMPLER_DESC Samplers[(size_t)nrd::Sampler::MAX_NUM] = {};
  const uint32_t nSamplers = std::min(Desc.samplersNum, (uint32_t)nrd::Sampler::MAX_NUM);
  for (uint32_t i = 0; i < nSamplers; i++)
  {
    D3D12_STATIC_SAMPLER_DESC &s = Samplers[i];
    s.Filter = Desc.samplers[i] == nrd::Sampler::NEAREST_CLAMP ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.MaxAnisotropy = 1;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    s.ShaderRegister = Desc.samplersBaseRegisterIndex + i;
    s.RegisterSpace = Desc.constantBufferAndSamplersSpaceIndex;
    s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  for (uint32_t p = 0; p < Desc.pipelinesNum; p++)
  {
    const nrd::PipelineDesc &Pipe = Desc.pipelines[p];
    D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
    const uint32_t nRanges = std::min(Pipe.resourceRangesNum, 2u);
    for (uint32_t r = 0; r < nRanges; r++)
    {
      Ranges[r].RangeType = Pipe.resourceRanges[r].descriptorType == nrd::DescriptorType::TEXTURE ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV
                                                                                                  : D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
      Ranges[r].NumDescriptors = Pipe.resourceRanges[r].descriptorsNum;
      Ranges[r].BaseShaderRegister = Desc.resourcesBaseRegisterIndex;
      Ranges[r].RegisterSpace = Desc.resourcesSpaceIndex;
      Ranges[r].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    }
    D3D12_ROOT_PARAMETER Params[2] = {};
    Params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Params[0].DescriptorTable.NumDescriptorRanges = nRanges;
    Params[0].DescriptorTable.pDescriptorRanges = Ranges;
    Params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    Params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    Params[1].Descriptor.ShaderRegister = Desc.constantBufferRegisterIndex;
    Params[1].Descriptor.RegisterSpace = Desc.constantBufferAndSamplersSpaceIndex;
    Params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC RootDesc = {};
    RootDesc.NumParameters = 2;
    RootDesc.pParameters = Params;
    RootDesc.NumStaticSamplers = nSamplers;
    RootDesc.pStaticSamplers = Samplers;

    ComPtr<ID3DBlob> pBlob, pErrors;
    HRESULT hr = D3D12SerializeRootSignature(&RootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pErrors);
    if (SUCCEEDED(hr))
      hr = pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&Pipelines[p].pRoot));
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: NRD's root signature of \"%s\" failed (0x%08X): %s", Pipe.shaderIdentifier, (unsigned)hr,
                     pErrors ? (const char *)pErrors->GetBufferPointer() : "");
      Pipelines.clear();
      return false;
    }
    if (!Pipe.computeShaderDXIL.bytecode || !Pipe.computeShaderDXIL.size)
    {
      iLog->LogError("XRenderD3D12: NRD has no DXIL for \"%s\"", Pipe.shaderIdentifier);
      Pipelines.clear();
      return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC PSO = {};
    PSO.pRootSignature = Pipelines[p].pRoot.Get();
    PSO.CS = { Pipe.computeShaderDXIL.bytecode, (SIZE_T)Pipe.computeShaderDXIL.size };
    hr = pDevice->CreateComputePipelineState(&PSO, IID_PPV_ARGS(&Pipelines[p].pPSO));
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: NRD's pipeline state of \"%s\" failed (0x%08X)", Pipe.shaderIdentifier, (unsigned)hr);
      Pipelines.clear();
      return false;
    }
    Pipelines[p].pRoot->SetName(L"XRenderD3D12 NRD root signature");
    Pipelines[p].pPSO->SetName(L"XRenderD3D12 NRD pipeline state");
  }
  return true;
}

bool CD3D12Nrd::SImpl::CreatePool(const nrd::InstanceDesc &Desc)
{
  nPermanent = Desc.permanentPoolSize;
  const uint32_t nAll = Desc.permanentPoolSize + Desc.transientPoolSize;
  Pool.assign(nAll, {});
  for (uint32_t i = 0; i < nAll; i++)
  {
    const nrd::TextureDesc &t = i < nPermanent ? Desc.permanentPool[i] : Desc.transientPool[i - nPermanent];
    const DXGI_FORMAT Format = DxgiFormat(t.format);
    const int f = std::max<int>(t.downsampleFactor, 1);
    if (Format == DXGI_FORMAT_UNKNOWN ||
        !(Pool[i].pTex = pUpload->CreateComputeTarget((nWidth + f - 1) / f, (nHeight + f - 1) / f, 1, Format,
                                                       i < nPermanent ? L"XRenderD3D12 NRD's permanent texture" : L"XRenderD3D12 NRD's transient texture")))
    {
      iLog->LogError("XRenderD3D12: NRD's texture %u (format %u, 1/%d) is not available", i, (unsigned)t.format, f);
      return false;
    }
    // Made in UNORDERED_ACCESS; each pass asks for the state it wants (Denoise).
    g_pRenderer->m_pDevice->TrackState(Pool[i].pTex->m_pResource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }
  return true;
}

int CD3D12Nrd::SImpl::GetTable(const nrd::DispatchDesc &d, CD3D12Texture *const *pTextures)
{
  std::vector<uintptr_t> Key;
  Key.reserve(1 + 2 * d.resourcesNum);
  Key.push_back(d.pipelineIndex);
  for (uint32_t i = 0; i < d.resourcesNum; i++)
  {
    Key.push_back((uintptr_t)pTextures[i]);
    Key.push_back((uintptr_t)d.resources[i].descriptorType);
  }
  const auto it = Tables.find(Key);
  if (it != Tables.end())
    return it->second.nFirst;
  const int nFirst = pDescriptors->AllocRange((int)d.resourcesNum);
  if (nFirst < 0)
    return -1;
  for (uint32_t i = 0; i < d.resourcesNum; i++)
  {
    CD3D12Texture *pTex = pTextures[i];
    if (d.resources[i].descriptorType == nrd::DescriptorType::TEXTURE)
    {
      D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
      SRV.Format = pTex->m_Format;
      SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      SRV.Texture2D.MipLevels = 1;
      pDescriptors->WriteSRV(nFirst + (int)i, pTex->m_pResource, &SRV);
    }
    else
    {
      D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
      UAV.Format = pTex->m_Format;
      UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      pDescriptors->WriteUAV(nFirst + (int)i, pTex->m_pResource, &UAV);
    }
  }
  Tables.emplace(std::move(Key), STable{ nFirst, (int)d.resourcesNum });
  return nFirst;
}

bool CD3D12Nrd::IsBuilt()
{
  return true;
}

const char *CD3D12Nrd::GetVersion()
{
  static char s_Version[32];
  const nrd::LibraryDesc *pLib = nrd::GetLibraryDesc();
  if (pLib && !s_Version[0])
    snprintf(s_Version, sizeof(s_Version), "%u.%u.%u", pLib->versionMajor, pLib->versionMinor, pLib->versionBuild);
  return s_Version;
}

SNrdSettings CD3D12Nrd::GetDefaults()
{
  const nrd::ReblurSettings r;
  SNrdSettings s;
  s.fHitDistance[0] = r.hitDistanceParameters.A;
  s.fHitDistance[1] = r.hitDistanceParameters.B;
  s.fHitDistance[2] = r.hitDistanceParameters.C;
  s.nMaxFrames = r.maxAccumulatedFrameNum;
  return s;
}

CD3D12Nrd::CD3D12Nrd() : m_p(std::make_unique<SImpl>())
{
}

CD3D12Nrd::~CD3D12Nrd()
{
  Release();
}

bool CD3D12Nrd::Init(ID3D12Device *pDevice, CD3D12Descriptors *pDescriptors, CD3D12Upload *pUpload, int nWidth, int nHeight)
{
  Release();
  SImpl &s = *m_p;
  if (!pDevice || !pDescriptors || !pUpload || nWidth <= 0 || nHeight <= 0 || nWidth > 65535 || nHeight > 65535)
    return false;
  s.pDevice = pDevice;
  s.pDescriptors = pDescriptors;
  s.pUpload = pUpload;
  s.nWidth = nWidth;
  s.nHeight = nHeight;
  const nrd::DenoiserDesc Denoiser = { NRD_DIFFUSE, nrd::Denoiser::REBLUR_DIFFUSE };
  nrd::InstanceCreationDesc Create = {};
  Create.denoisers = &Denoiser;
  Create.denoisersNum = 1;
  const nrd::Result r = nrd::CreateInstance(Create, s.pInstance);
  if (r != nrd::Result::SUCCESS || !s.pInstance)
  {
    iLog->LogError("XRenderD3D12: NRD's instance failed (%u)", (unsigned)r);
    s.pInstance = nullptr;
    return false;
  }
  const nrd::InstanceDesc *pDesc = nrd::GetInstanceDesc(*s.pInstance);
  if (!pDesc || !s.CreatePipelines(*pDesc) || !s.CreatePool(*pDesc))
  {
    Release();
    return false;
  }
  s.bPrev = false;
  s.bClear = true;
  s.nFrameIndex = 0;
  iLog->Log("XRenderD3D12: NVIDIA NRD %s, REBLUR_DIFFUSE at %d x %d: %u pipelines, %u + %u textures", GetVersion(), nWidth, nHeight,
            pDesc->pipelinesNum, pDesc->permanentPoolSize, pDesc->transientPoolSize);
  return true;
}

void CD3D12Nrd::Release()
{
  SImpl &s = *m_p;
  if (s.pInstance)
    nrd::DestroyInstance(*s.pInstance);
  s.pInstance = nullptr;
  for (SImpl::STexture &t : s.Pool)
    if (t.pTex && s.pUpload)
      s.pUpload->ReleaseTexture(t.pTex);
  s.Pool.clear();
  if (s.pDescriptors)
    for (const auto &t : s.Tables)
      s.pDescriptors->FreeRange(t.second.nFirst, t.second.nCount);
  s.Tables.clear();
  s.nWidth = s.nHeight = 0;
  s.nPasses = 0;
}

bool CD3D12Nrd::IsReady() const
{
  return m_p->pInstance != nullptr;
}

int CD3D12Nrd::GetWidth() const
{
  return m_p->nWidth;
}

int CD3D12Nrd::GetHeight() const
{
  return m_p->nHeight;
}

bool CD3D12Nrd::Denoise(ID3D12GraphicsCommandList *pList, const SNrdView &View, const SNrdSettings &Settings, const SNrdTextures &Textures)
{
  SImpl &s = *m_p;
  s.nPasses = 0;
  // (the device keeps the textures' states on the frame's list only)
  CD3D12Device *pDevice = g_pRenderer->m_pDevice.get();
  if (!pList || pList != pDevice->GetCmdList() || !s.pInstance || !Textures.pMotion || !Textures.pNormalRoughness || !Textures.pViewZ ||
      !Textures.pDiffuse || !Textures.pOut)
    return false;

  nrd::CommonSettings c = {};
  const SNrdView &Prev = s.bPrev ? s.Prev : View;
  memcpy(c.viewToClipMatrix, View.mViewToClip, sizeof(c.viewToClipMatrix));
  memcpy(c.viewToClipMatrixPrev, Prev.mViewToClip, sizeof(c.viewToClipMatrixPrev));
  memcpy(c.worldToViewMatrix, View.mWorldToView, sizeof(c.worldToViewMatrix));
  memcpy(c.worldToViewMatrixPrev, Prev.mWorldToView, sizeof(c.worldToViewMatrixPrev));
  c.motionVectorScale[0] = c.motionVectorScale[1] = 1;
  c.motionVectorScale[2] = 0;
  for (int i = 0; i < 2; i++)
  {
    c.cameraJitter[i] = std::clamp(View.vJitter[i], -0.5f, 0.5f);
    c.cameraJitterPrev[i] = std::clamp(Prev.vJitter[i], -0.5f, 0.5f);
  }
  c.resourceSize[0] = c.resourceSizePrev[0] = c.rectSize[0] = c.rectSizePrev[0] = (uint16_t)s.nWidth;
  c.resourceSize[1] = c.resourceSizePrev[1] = c.rectSize[1] = c.rectSizePrev[1] = (uint16_t)s.nHeight;
  c.denoisingRange = std::max(View.fDenoisingRange, 1.0f);
  c.frameIndex = s.nFrameIndex;
  c.accumulationMode = s.bClear ? nrd::AccumulationMode::CLEAR_AND_RESTART
                       : View.bRestart || !s.bPrev ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;
  if (nrd::SetCommonSettings(*s.pInstance, c) != nrd::Result::SUCCESS)
    return false;
  nrd::ReblurSettings r;
  r.hitDistanceParameters.A = Settings.fHitDistance[0];
  r.hitDistanceParameters.B = Settings.fHitDistance[1];
  r.hitDistanceParameters.C = Settings.fHitDistance[2];
  r.maxAccumulatedFrameNum = std::min(Settings.nMaxFrames, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
  r.maxFastAccumulatedFrameNum = std::min(r.maxFastAccumulatedFrameNum, r.maxAccumulatedFrameNum);
  if (nrd::SetDenoiserSettings(*s.pInstance, NRD_DIFFUSE, &r) != nrd::Result::SUCCESS)
    return false;
  const nrd::DispatchDesc *pDispatches = nullptr;
  uint32_t nDispatches = 0;
  const nrd::Identifier Id = NRD_DIFFUSE;
  if (nrd::GetComputeDispatches(*s.pInstance, &Id, 1, pDispatches, nDispatches) != nrd::Result::SUCCESS)
    return false;

  // The caller's textures (their states kept by the device too).
  const auto UserOf = [&](nrd::ResourceType Type) -> CD3D12Texture *
  {
    switch (Type)
    {
    case nrd::ResourceType::IN_MV: return Textures.pMotion;
    case nrd::ResourceType::IN_NORMAL_ROUGHNESS: return Textures.pNormalRoughness;
    case nrd::ResourceType::IN_VIEWZ: return Textures.pViewZ;
    case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: return Textures.pDiffuse;
    case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return Textures.pOut;
    default: return nullptr;
    }
  };

  bool bOk = true;
  D3D12_GPU_VIRTUAL_ADDRESS nConstants = 0;
  std::vector<CD3D12Texture *> Bound;
  for (uint32_t n = 0; n < nDispatches && bOk; n++)
  {
    const nrd::DispatchDesc &d = pDispatches[n];
    if (d.pipelineIndex >= s.Pipelines.size() || !s.Pipelines[d.pipelineIndex].pPSO)
    {
      bOk = false;
      break;
    }
    // Its textures with the states they want, asked of the device (a pass given up on half way leaves the states as the
    // barriers queued so far make them, which the device knows).
    Bound.assign(d.resourcesNum, nullptr);
    pDevice->UavBarrierAllOn(pList);
    for (uint32_t i = 0; i < d.resourcesNum && bOk; i++)
    {
      const nrd::ResourceDesc &rd = d.resources[i];
      if (rd.type == nrd::ResourceType::PERMANENT_POOL || rd.type == nrd::ResourceType::TRANSIENT_POOL)
      {
        const uint32_t nIndex = rd.indexInPool + (rd.type == nrd::ResourceType::TRANSIENT_POOL ? s.nPermanent : 0);
        if (nIndex < s.Pool.size())
          Bound[i] = s.Pool[nIndex].pTex;
      }
      else
        Bound[i] = UserOf(rd.type);
      if (!Bound[i])
      {
        iLog->LogError("XRenderD3D12: NRD's pass \"%s\" wants %s, which the renderer does not have", d.name ? d.name : "?",
                       nrd::GetResourceTypeString(rd.type));
        bOk = false;
        break;
      }
      pDevice->Require(Bound[i]->m_pResource,
                       rd.descriptorType == nrd::DescriptorType::TEXTURE ? NRD_READ : D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (!bOk)
      break;
    const int nTable = s.GetTable(d, Bound.data());
    if (nTable < 0)
    {
      bOk = false;
      break;
    }
    if (!nConstants || (d.constantBufferDataSize && !d.constantBufferDataMatchesPreviousDispatch))
    {
      const uint32_t nSize = std::max<uint32_t>(d.constantBufferDataSize, 16);
      const SD3D12FrameAlloc Mem = s.pUpload->AllocFrame(nSize, 256);
      if (!Mem.pCPU)
      {
        bOk = false;
        break;
      }
      memset(Mem.pCPU, 0, nSize);
      if (d.constantBufferDataSize && d.constantBufferData)
        memcpy(Mem.pCPU, d.constantBufferData, d.constantBufferDataSize);
      nConstants = Mem.nGPU;
    }
    const SImpl::SPipeline &P = s.Pipelines[d.pipelineIndex];
    pList->SetComputeRootSignature(P.pRoot.Get());
    pList->SetPipelineState(P.pPSO.Get());
    pList->SetComputeRootDescriptorTable(0, s.pDescriptors->GetGPU(nTable));
    pList->SetComputeRootConstantBufferView(1, nConstants);
    pDevice->FlushBarriersFor(pList);
    pList->Dispatch(d.gridWidth, d.gridHeight, 1);  // barriers flushed
    s.nPasses++;
  }

  // NRD's writes done (the caller's textures left as its last passes wanted them: the next kernel asks for its own).
  pDevice->UavBarrierAllOn(pList);

  s.Prev = View;
  s.bPrev = bOk;
  s.bClear = !bOk;
  s.nFrameIndex++;
  return bOk;
}

void CD3D12Nrd::GetStats(int &nPasses, int &nTables, int &nTextures) const
{
  nPasses = m_p->nPasses;
  nTables = (int)m_p->Tables.size();
  nTextures = (int)m_p->Pool.size();
}

#else

// Without NRD: nothing to run.
struct CD3D12Nrd::SImpl
{
};

bool CD3D12Nrd::IsBuilt()
{
  return false;
}

const char *CD3D12Nrd::GetVersion()
{
  return "";
}

SNrdSettings CD3D12Nrd::GetDefaults()
{
  return { { 3.0f, 0.1f, 20.0f }, 30 };
}

CD3D12Nrd::CD3D12Nrd() : m_p(std::make_unique<SImpl>())
{
}

CD3D12Nrd::~CD3D12Nrd() = default;

bool CD3D12Nrd::Init(ID3D12Device *, CD3D12Descriptors *, CD3D12Upload *, int, int)
{
  return false;
}

void CD3D12Nrd::Release()
{
}

bool CD3D12Nrd::IsReady() const
{
  return false;
}

int CD3D12Nrd::GetWidth() const
{
  return 0;
}

int CD3D12Nrd::GetHeight() const
{
  return 0;
}

bool CD3D12Nrd::Denoise(ID3D12GraphicsCommandList *, const SNrdView &, const SNrdSettings &, const SNrdTextures &)
{
  return false;
}

void CD3D12Nrd::GetStats(int &nPasses, int &nTables, int &nTextures) const
{
  nPasses = nTables = nTextures = 0;
}

#endif
