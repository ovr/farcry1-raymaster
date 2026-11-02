// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Compute.cpp
//	Description: The compute passes' root signature and pipeline states.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Compute.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"

// The kernels, compiled by dxc at build time (Shaders/Shaders.cmake).
#include "Gtao_PrefilterDepths_CS.h"
#include "Gtao_Normals_CS.h"
#include "Gtao_Low_CS.h"
#include "Gtao_Medium_CS.h"
#include "Gtao_High_CS.h"
#include "Gtao_Ultra_CS.h"
#include "Gtao_Denoise_CS.h"
#include "Gtao_DenoiseLast_CS.h"
#include "AoResolve_CS.h"
#include "Meter_CS.h"
#include "Bloom_CS.h"
#include "DepthTiles_CS.h"
#include "DepthTilesUp_CS.h"
#include "VolumetricInject_CS.h"
#include "VolumetricIntegrate_CS.h"
#include "HdrHistogram_CS.h"
#include "HdrOutputStats_CS.h"
#include "RaySunTrace_CS.h"
#include "RaySunTemporal_CS.h"
#include "RaySunFilter_CS.h"
#if XRENDERD3D12_FFX_DENOISER
#include "RaySunFfxInputs_CS.h"
#include "RaySunFfxPrepare_CS.h"
#include "RaySunFfxClassify_CS.h"
#include "RaySunFfxFilter_CS.h"
#include "RaySunFfxPack_CS.h"
#endif
#include "RayAOTrace_CS.h"
#include "RayAOTemporal_CS.h"
#include "RayAOFilter_CS.h"
#include "RayGITrace_CS.h"
#include "RayGITemporal_CS.h"
#include "RayGIFilter_CS.h"
#include "PathTraceTrace_CS.h"
#include "PathTraceTemporal_CS.h"
#include "PathTraceFilter_CS.h"
#include "PathTraceSpecular_CS.h"
#include "PathCacheUpdate_CS.h"
#include "PathCacheResolve_CS.h"
#if XRENDERD3D12_NRD
#include "PathNrdPack_CS.h"
#include "PathNrdResolve_CS.h"
#endif

using Microsoft::WRL::ComPtr;

namespace
{

struct SKernel
{
  const char *szName;
  const void *pCS;
  size_t nCS;
  bool bRays;       //!< the root signature with rays (DispatchRays)
};
const SKernel s_Kernels[eD3D12K_Num] =
{
  { "XeGTAO prefilter depths", g_Gtao_PrefilterDepths_CS, sizeof(g_Gtao_PrefilterDepths_CS) },
  { "XeGTAO normals", g_Gtao_Normals_CS, sizeof(g_Gtao_Normals_CS) },
  { "XeGTAO low", g_Gtao_Low_CS, sizeof(g_Gtao_Low_CS) },
  { "XeGTAO medium", g_Gtao_Medium_CS, sizeof(g_Gtao_Medium_CS) },
  { "XeGTAO high", g_Gtao_High_CS, sizeof(g_Gtao_High_CS) },
  { "XeGTAO ultra", g_Gtao_Ultra_CS, sizeof(g_Gtao_Ultra_CS) },
  { "XeGTAO denoise", g_Gtao_Denoise_CS, sizeof(g_Gtao_Denoise_CS) },
  { "XeGTAO denoise (last)", g_Gtao_DenoiseLast_CS, sizeof(g_Gtao_DenoiseLast_CS) },
  { "AO resolve", g_AoResolve_CS, sizeof(g_AoResolve_CS) },
  { "HDR meter", g_Meter_CS, sizeof(g_Meter_CS) },
  { "bloom", g_Bloom_CS, sizeof(g_Bloom_CS) },
  { "depth tiles", g_DepthTiles_CS, sizeof(g_DepthTiles_CS) },
  { "depth tiles (levels 5, 6)", g_DepthTilesUp_CS, sizeof(g_DepthTilesUp_CS) },
  { "volumetric light", g_VolumetricInject_CS, sizeof(g_VolumetricInject_CS) },
  { "volumetric light (integration)", g_VolumetricIntegrate_CS, sizeof(g_VolumetricIntegrate_CS) },
  { "HDR statistics (histogram)", g_HdrHistogram_CS, sizeof(g_HdrHistogram_CS) },
  { "HDR statistics (output)", g_HdrOutputStats_CS, sizeof(g_HdrOutputStats_CS) },
  { "sun's shadow by rays", g_RaySunTrace_CS, sizeof(g_RaySunTrace_CS), true },
  { "sun's shadow by rays (history)", g_RaySunTemporal_CS, sizeof(g_RaySunTemporal_CS), true },
  { "sun's shadow by rays (filter)", g_RaySunFilter_CS, sizeof(g_RaySunFilter_CS), true },
  { "ambient occlusion by rays", g_RayAOTrace_CS, sizeof(g_RayAOTrace_CS), true },
  { "ambient occlusion by rays (history)", g_RayAOTemporal_CS, sizeof(g_RayAOTemporal_CS), true },
  { "ambient occlusion by rays (filter)", g_RayAOFilter_CS, sizeof(g_RayAOFilter_CS), true },
  { "ambient light by rays", g_RayGITrace_CS, sizeof(g_RayGITrace_CS), true },
  { "ambient light by rays (history)", g_RayGITemporal_CS, sizeof(g_RayGITemporal_CS), true },
  { "ambient light by rays (filter)", g_RayGIFilter_CS, sizeof(g_RayGIFilter_CS), true },
  { "path tracer", g_PathTraceTrace_CS, sizeof(g_PathTraceTrace_CS), true },
  { "path tracer (history)", g_PathTraceTemporal_CS, sizeof(g_PathTraceTemporal_CS), true },
  { "path tracer (filter)", g_PathTraceFilter_CS, sizeof(g_PathTraceFilter_CS), true },
  { "path tracer (highlights)", g_PathTraceSpecular_CS, sizeof(g_PathTraceSpecular_CS), true },
#if XRENDERD3D12_NRD
  { "path tracer (NRD's inputs)", g_PathNrdPack_CS, sizeof(g_PathNrdPack_CS), true },
  { "path tracer (NRD's light)", g_PathNrdResolve_CS, sizeof(g_PathNrdResolve_CS), true },
#else
  // Without NRD built in: no kernels (GetPSO has none).
  { "path tracer (NRD's inputs)", nullptr, 0, true },
  { "path tracer (NRD's light)", nullptr, 0, true },
#endif
#if XRENDERD3D12_FFX_DENOISER
  { "sun's shadow by rays (FidelityFX Denoiser's inputs)", g_RaySunFfxInputs_CS, sizeof(g_RaySunFfxInputs_CS), true },
  { "sun's shadow by rays (FidelityFX Denoiser's masks)", g_RaySunFfxPrepare_CS, sizeof(g_RaySunFfxPrepare_CS), true },
  { "sun's shadow by rays (FidelityFX Denoiser's tiles)", g_RaySunFfxClassify_CS, sizeof(g_RaySunFfxClassify_CS), true },
  { "sun's shadow by rays (FidelityFX Denoiser's filter)", g_RaySunFfxFilter_CS, sizeof(g_RaySunFfxFilter_CS), true },
  { "sun's shadow by rays (FidelityFX Denoiser's mask)", g_RaySunFfxPack_CS, sizeof(g_RaySunFfxPack_CS), true },
#else
  // Without its submodule: no kernels (GetPSO has none).
  { "sun's shadow by rays (FidelityFX Denoiser's inputs)", nullptr, 0, true },
  { "sun's shadow by rays (FidelityFX Denoiser's masks)", nullptr, 0, true },
  { "sun's shadow by rays (FidelityFX Denoiser's tiles)", nullptr, 0, true },
  { "sun's shadow by rays (FidelityFX Denoiser's filter)", nullptr, 0, true },
  { "sun's shadow by rays (FidelityFX Denoiser's mask)", nullptr, 0, true },
#endif
  { "path tracer (radiance cache)", g_PathCacheUpdate_CS, sizeof(g_PathCacheUpdate_CS), true },
  { "path tracer (radiance cache's light)", g_PathCacheResolve_CS, sizeof(g_PathCacheResolve_CS), true },
};
static_assert(sizeof(s_Kernels) / sizeof(s_Kernels[0]) == eD3D12K_Num, "a kernel for each ED3D12Kernel");

D3D12_STATIC_SAMPLER_DESC StaticSampler(UINT nRegister, D3D12_FILTER Filter)
{
  D3D12_STATIC_SAMPLER_DESC s = {};
  s.Filter = Filter;
  s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  s.MaxAnisotropy = 1;
  s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  s.MaxLOD = D3D12_FLOAT32_MAX;
  s.ShaderRegister = nRegister;
  s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  return s;
}

}

CD3D12Compute::CD3D12Compute() = default;

CD3D12Compute::~CD3D12Compute()
{
  Shutdown();
}

bool CD3D12Compute::Init(ID3D12Device *pDevice)
{
  Shutdown();
  m_pDevice = pDevice;
  return CreateRootSignature() && CreateRayRootSignature();
}

void CD3D12Compute::Shutdown()
{
  for (auto &p : m_pPSOs)
    p.Reset();
  for (bool &b : m_bFailed)
    b = false;
  m_pRootSignature.Reset();
  m_pRayRootSignature.Reset();
  m_pDevice = nullptr;
}

bool CD3D12Compute::CreateRootSignature()
{
  D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
  Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  Ranges[0].NumDescriptors = COMPUTE_SRVS;
  Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  Ranges[1].NumDescriptors = COMPUTE_UAVS;

  D3D12_ROOT_PARAMETER Params[3] = {};
  Params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  Params[0].Descriptor.ShaderRegister = 0;
  Params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (int i = 0; i < 2; i++)
  {
    Params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
    Params[1 + i].DescriptorTable.pDescriptorRanges = &Ranges[i];
    Params[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_STATIC_SAMPLER_DESC Samplers[3] = { StaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT), StaticSampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR),
                                            StaticSampler(2, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT) };
  Samplers[2].AddressU = Samplers[2].AddressV = Samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  Samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
  Samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  D3D12_ROOT_SIGNATURE_DESC Desc = {};
  Desc.NumParameters = 3;
  Desc.pParameters = Params;
  Desc.NumStaticSamplers = 3;
  Desc.pStaticSamplers = Samplers;

  ComPtr<ID3DBlob> pBlob, pErrors;
  HRESULT hr = D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pErrors);
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: D3D12SerializeRootSignature (compute) failed (0x%08X): %s", (unsigned)hr,
      pErrors ? (const char *)pErrors->GetBufferPointer() : "");
    return false;
  }
  hr = m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateRootSignature (compute) failed (0x%08X)", (unsigned)hr);
    return false;
  }
  m_pRootSignature->SetName(L"XRenderD3D12 compute root signature");
  return true;
}

bool CD3D12Compute::CreateRayRootSignature()
{
  // The texture table as the draws see it (D3D12Pipeline.cpp), the UAVs after it.
  D3D12_DESCRIPTOR_RANGE Ranges[6] = {};
  const UINT Spaces[5] = { 1, 2, 3, 4, 6 };
  for (int i = 0; i < 5; i++)
  {
    Ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Ranges[i].NumDescriptors = UINT_MAX;
    Ranges[i].RegisterSpace = Spaces[i];
  }
  Ranges[5].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  Ranges[5].NumDescriptors = COMPUTE_UAVS;

  D3D12_ROOT_PARAMETER Params[7] = {};
  for (D3D12_ROOT_PARAMETER &p : Params)
    p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  Params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  Params[0].Descriptor.ShaderRegister = 0;
  Params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  Params[1].Descriptor.ShaderRegister = 2;
  Params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Params[2].DescriptorTable.NumDescriptorRanges = 5;
  Params[2].DescriptorTable.pDescriptorRanges = Ranges;
  Params[ROOT_RAY_SCENE].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  Params[ROOT_RAY_SCENE].Descriptor.ShaderRegister = 0;
  Params[ROOT_RAY_SCENE].Descriptor.RegisterSpace = 5;
  Params[ROOT_RAY_INSTANCES].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  Params[ROOT_RAY_INSTANCES].Descriptor.ShaderRegister = 1;
  Params[ROOT_RAY_INSTANCES].Descriptor.RegisterSpace = 5;
  Params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Params[5].DescriptorTable.NumDescriptorRanges = 1;
  Params[5].DescriptorTable.pDescriptorRanges = &Ranges[5];
  Params[ROOT_RAY_LIGHTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  Params[ROOT_RAY_LIGHTS].Descriptor.ShaderRegister = 2;
  Params[ROOT_RAY_LIGHTS].Descriptor.RegisterSpace = 5;

  D3D12_STATIC_SAMPLER_DESC Samplers[5];
  Samplers[XR_SAMPLER_LINEAR_WRAP] = StaticSampler(XR_SAMPLER_LINEAR_WRAP, D3D12_FILTER_ANISOTROPIC);
  Samplers[XR_SAMPLER_LINEAR_CLAMP] = StaticSampler(XR_SAMPLER_LINEAR_CLAMP, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
  Samplers[XR_SAMPLER_POINT_WRAP] = StaticSampler(XR_SAMPLER_POINT_WRAP, D3D12_FILTER_MIN_MAG_MIP_POINT);
  Samplers[XR_SAMPLER_POINT_CLAMP] = StaticSampler(XR_SAMPLER_POINT_CLAMP, D3D12_FILTER_MIN_MAG_MIP_POINT);
  Samplers[4] = StaticSampler(4, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT);
  for (int i : { XR_SAMPLER_LINEAR_WRAP, XR_SAMPLER_POINT_WRAP })
    Samplers[i].AddressU = Samplers[i].AddressV = Samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  Samplers[XR_SAMPLER_LINEAR_WRAP].MaxAnisotropy = 8;
  Samplers[4].AddressU = Samplers[4].AddressV = Samplers[4].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  Samplers[4].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
  Samplers[4].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  D3D12_ROOT_SIGNATURE_DESC Desc = {};
  Desc.NumParameters = 7;
  Desc.pParameters = Params;
  Desc.NumStaticSamplers = 5;
  Desc.pStaticSamplers = Samplers;

  ComPtr<ID3DBlob> pBlob, pErrors;
  HRESULT hr = D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pErrors);
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: D3D12SerializeRootSignature (compute with rays) failed (0x%08X): %s", (unsigned)hr,
      pErrors ? (const char *)pErrors->GetBufferPointer() : "");
    return false;
  }
  hr = m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRayRootSignature));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateRootSignature (compute with rays) failed (0x%08X)", (unsigned)hr);
    return false;
  }
  m_pRayRootSignature->SetName(L"XRenderD3D12 compute root signature with rays");
  return true;
}

ID3D12PipelineState *CD3D12Compute::GetPSO(ED3D12Kernel eKernel)
{
  if (eKernel < 0 || eKernel >= eD3D12K_Num || m_bFailed[eKernel] || !s_Kernels[eKernel].pCS)
    return nullptr;
  ID3D12RootSignature *pRootSignature = s_Kernels[eKernel].bRays ? m_pRayRootSignature.Get() : m_pRootSignature.Get();
  if (!pRootSignature)
    return nullptr;
  if (!m_pPSOs[eKernel])
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};
    Desc.pRootSignature = pRootSignature;
    Desc.CS = { s_Kernels[eKernel].pCS, s_Kernels[eKernel].nCS };
    const HRESULT hr = m_pDevice->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&m_pPSOs[eKernel]));
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: CreateComputePipelineState (%s) failed (0x%08X)", s_Kernels[eKernel].szName, (unsigned)hr);
      m_bFailed[eKernel] = true;
      return nullptr;
    }
  }
  return m_pPSOs[eKernel].Get();
}

void CD3D12Compute::Dispatch(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, D3D12_GPU_VIRTUAL_ADDRESS CB, int nSRVs, int nUAVs,
                             UINT nGroupsX, UINT nGroupsY, UINT nGroupsZ)
{
  ID3D12PipelineState *pPSO = GetPSO(eKernel);
  CD3D12Descriptors *pDescriptors = g_pRenderer->m_pDescriptors.get();
  if (!pList || !pPSO || !pDescriptors || !nGroupsX || !nGroupsY || !nGroupsZ)
    return;
  // Its pipeline state in place of the last draw's.
  g_pRenderer->m_pDevice->ForgetDrawState();
  pList->SetComputeRootSignature(m_pRootSignature.Get());
  pList->SetPipelineState(pPSO);
  if (CB)
    pList->SetComputeRootConstantBufferView(0, CB);
  if (nSRVs >= 0)
    pList->SetComputeRootDescriptorTable(1, pDescriptors->GetGPU(nSRVs));
  if (nUAVs >= 0)
    pList->SetComputeRootDescriptorTable(2, pDescriptors->GetGPU(nUAVs));
  pList->Dispatch(nGroupsX, nGroupsY, nGroupsZ);
}

void CD3D12Compute::DispatchRays(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, D3D12_GPU_VIRTUAL_ADDRESS FrameCB, D3D12_GPU_VIRTUAL_ADDRESS PassCB,
                                 D3D12_GPU_VIRTUAL_ADDRESS nRayScene, D3D12_GPU_VIRTUAL_ADDRESS nRayInstances, int nUAVs, UINT nGroupsX, UINT nGroupsY,
                                 D3D12_GPU_VIRTUAL_ADDRESS nLights)
{
  ID3D12PipelineState *pPSO = s_Kernels[eKernel].bRays ? GetPSO(eKernel) : nullptr;
  CD3D12Descriptors *pDescriptors = g_pRenderer->m_pDescriptors.get();
  if (!pList || !pPSO || !pDescriptors || !nGroupsX || !nGroupsY)
    return;
  g_pRenderer->m_pDevice->ForgetDrawState();
  pList->SetComputeRootSignature(m_pRayRootSignature.Get());
  pList->SetPipelineState(pPSO);
  if (FrameCB)
    pList->SetComputeRootConstantBufferView(0, FrameCB);
  if (PassCB)
    pList->SetComputeRootConstantBufferView(1, PassCB);
  pList->SetComputeRootDescriptorTable(2, pDescriptors->GetTableStart());
  if (nRayScene)
    pList->SetComputeRootShaderResourceView(ROOT_RAY_SCENE, nRayScene);
  if (nRayInstances)
    pList->SetComputeRootShaderResourceView(ROOT_RAY_INSTANCES, nRayInstances);
  if (nUAVs >= 0)
    pList->SetComputeRootDescriptorTable(5, pDescriptors->GetGPU(nUAVs));
  if (nLights)
    pList->SetComputeRootShaderResourceView(ROOT_RAY_LIGHTS, nLights);
  pList->Dispatch(nGroupsX, nGroupsY, 1);
}
