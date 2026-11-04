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
#include "ExposureHistogram_CS.h"
#include "ExposureTarget_CS.h"
#include "FrameAverageSum_CS.h"
#include "FrameAverageLimit_CS.h"
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
#include "SmaaEdgesLow_CS.h"
#include "SmaaEdgesMedium_CS.h"
#include "SmaaEdgesHigh_CS.h"
#include "SmaaEdgesUltra_CS.h"
#include "SmaaWeightsLow_CS.h"
#include "SmaaWeightsMedium_CS.h"
#include "SmaaWeightsHigh_CS.h"
#include "SmaaWeightsUltra_CS.h"
#include "Cmaa2EdgesLow_CS.h"
#include "Cmaa2EdgesMedium_CS.h"
#include "Cmaa2EdgesHigh_CS.h"
#include "Cmaa2EdgesUltra_CS.h"
#include "Cmaa2DispatchArgs_CS.h"
#include "Cmaa2Candidates_CS.h"
#include "Cmaa2Apply_CS.h"
#include "TaaDilate_CS.h"
#include "TaaResolve_CS.h"
#include "TaaSharpen_CS.h"
#include "OceanRows_CS.h"
#include "OceanColumns_CS.h"
#include "OceanWaves_CS.h"
#include "OceanMips_CS.h"
#include "OceanFoam_CS.h"
#include "OceanFoamView_CS.h"
#include "OceanCaustics_CS.h"
#include "WakeRows_CS.h"
#include "WakeColumns_CS.h"
#include "WakeRowsInverse_CS.h"
#include "WakeView_CS.h"
#include "WakeHeight_CS.h"
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
  bool bWide;       //!< the wide root signature (DispatchWide)
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
  { "exposure (histogram)", g_ExposureHistogram_CS, sizeof(g_ExposureHistogram_CS) },
  { "exposure (target)", g_ExposureTarget_CS, sizeof(g_ExposureTarget_CS) },
  { "frame average limit (sums)", g_FrameAverageSum_CS, sizeof(g_FrameAverageSum_CS) },
  { "frame average limit", g_FrameAverageLimit_CS, sizeof(g_FrameAverageLimit_CS) },
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
  { "SMAA edges (low)", g_SmaaEdgesLow_CS, sizeof(g_SmaaEdgesLow_CS) },
  { "SMAA edges (medium)", g_SmaaEdgesMedium_CS, sizeof(g_SmaaEdgesMedium_CS) },
  { "SMAA edges (high)", g_SmaaEdgesHigh_CS, sizeof(g_SmaaEdgesHigh_CS) },
  { "SMAA edges (ultra)", g_SmaaEdgesUltra_CS, sizeof(g_SmaaEdgesUltra_CS) },
  { "SMAA weights (low)", g_SmaaWeightsLow_CS, sizeof(g_SmaaWeightsLow_CS) },
  { "SMAA weights (medium)", g_SmaaWeightsMedium_CS, sizeof(g_SmaaWeightsMedium_CS) },
  { "SMAA weights (high)", g_SmaaWeightsHigh_CS, sizeof(g_SmaaWeightsHigh_CS) },
  { "SMAA weights (ultra)", g_SmaaWeightsUltra_CS, sizeof(g_SmaaWeightsUltra_CS) },
  { "CMAA2 edges (low)", g_Cmaa2EdgesLow_CS, sizeof(g_Cmaa2EdgesLow_CS), false, true },
  { "CMAA2 edges (medium)", g_Cmaa2EdgesMedium_CS, sizeof(g_Cmaa2EdgesMedium_CS), false, true },
  { "CMAA2 edges (high)", g_Cmaa2EdgesHigh_CS, sizeof(g_Cmaa2EdgesHigh_CS), false, true },
  { "CMAA2 edges (ultra)", g_Cmaa2EdgesUltra_CS, sizeof(g_Cmaa2EdgesUltra_CS), false, true },
  { "CMAA2 dispatch arguments", g_Cmaa2DispatchArgs_CS, sizeof(g_Cmaa2DispatchArgs_CS), false, true },
  { "CMAA2 candidates", g_Cmaa2Candidates_CS, sizeof(g_Cmaa2Candidates_CS), false, true },
  { "CMAA2 apply", g_Cmaa2Apply_CS, sizeof(g_Cmaa2Apply_CS), false, true },
  { "TAA motion", g_TaaDilate_CS, sizeof(g_TaaDilate_CS) },
  { "TAA resolve", g_TaaResolve_CS, sizeof(g_TaaResolve_CS) },
  { "TAA sharpening", g_TaaSharpen_CS, sizeof(g_TaaSharpen_CS) },
  { "ocean (rows)", g_OceanRows_CS, sizeof(g_OceanRows_CS) },
  { "ocean (columns)", g_OceanColumns_CS, sizeof(g_OceanColumns_CS) },
  { "ocean (waves)", g_OceanWaves_CS, sizeof(g_OceanWaves_CS) },
  { "ocean (mip levels)", g_OceanMips_CS, sizeof(g_OceanMips_CS) },
  { "ocean (foam)", g_OceanFoam_CS, sizeof(g_OceanFoam_CS) },
  { "ocean (foam view)", g_OceanFoamView_CS, sizeof(g_OceanFoamView_CS) },
  { "ocean (caustics)", g_OceanCaustics_CS, sizeof(g_OceanCaustics_CS) },
  { "wakes (rows)", g_WakeRows_CS, sizeof(g_WakeRows_CS) },
  { "wakes (columns)", g_WakeColumns_CS, sizeof(g_WakeColumns_CS) },
  { "wakes (rows, inverse)", g_WakeRowsInverse_CS, sizeof(g_WakeRowsInverse_CS) },
  { "wakes (view)", g_WakeView_CS, sizeof(g_WakeView_CS) },
  { "wakes (heights)", g_WakeHeight_CS, sizeof(g_WakeHeight_CS) },
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
  if (!CreateRootSignature() || !CreateRayRootSignature())
    return false;
  // Without it only its kernels are missing (HasKernel).
  CreateWideRootSignature();
  return true;
}

void CD3D12Compute::Shutdown()
{
  for (auto &p : m_pPSOs)
    p.Reset();
  for (bool &b : m_bFailed)
    b = false;
  m_pRootSignature.Reset();
  m_pRayRootSignature.Reset();
  m_pWideRootSignature.Reset();
  m_pDispatchSignature.Reset();
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

bool CD3D12Compute::CreateWideRootSignature()
{
  D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
  Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  Ranges[0].NumDescriptors = WIDE_SRVS;
  Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  Ranges[1].NumDescriptors = WIDE_UAVS;
  Ranges[1].OffsetInDescriptorsFromTableStart = WIDE_SRVS;

  D3D12_ROOT_PARAMETER Param = {};
  Param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Param.DescriptorTable.NumDescriptorRanges = 2;
  Param.DescriptorTable.pDescriptorRanges = Ranges;
  Param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  const D3D12_STATIC_SAMPLER_DESC Sampler = StaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT);

  D3D12_ROOT_SIGNATURE_DESC Desc = {};
  Desc.NumParameters = 1;
  Desc.pParameters = &Param;
  Desc.NumStaticSamplers = 1;
  Desc.pStaticSamplers = &Sampler;

  ComPtr<ID3DBlob> pBlob, pErrors;
  HRESULT hr = D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pErrors);
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: D3D12SerializeRootSignature (compute, wide) failed (0x%08X): %s", (unsigned)hr,
      pErrors ? (const char *)pErrors->GetBufferPointer() : "");
    return false;
  }
  hr = m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pWideRootSignature));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateRootSignature (compute, wide) failed (0x%08X)", (unsigned)hr);
    return false;
  }
  m_pWideRootSignature->SetName(L"XRenderD3D12 compute root signature, wide");

  // The indirect dispatches change no root arguments, so the command signature has no root signature.
  D3D12_INDIRECT_ARGUMENT_DESC Argument = {};
  Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC Signature = {};
  Signature.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  Signature.NumArgumentDescs = 1;
  Signature.pArgumentDescs = &Argument;
  hr = m_pDevice->CreateCommandSignature(&Signature, nullptr, IID_PPV_ARGS(&m_pDispatchSignature));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommandSignature (dispatch) failed (0x%08X)", (unsigned)hr);
    m_pWideRootSignature.Reset();
    return false;
  }
  return true;
}

ID3D12PipelineState *CD3D12Compute::GetPSO(ED3D12Kernel eKernel)
{
  if (eKernel < 0 || eKernel >= eD3D12K_Num || m_bFailed[eKernel] || !s_Kernels[eKernel].pCS)
    return nullptr;
  ID3D12RootSignature *pRootSignature = s_Kernels[eKernel].bRays ? m_pRayRootSignature.Get()
                                        : s_Kernels[eKernel].bWide ? m_pWideRootSignature.Get() : m_pRootSignature.Get();
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

bool CD3D12Compute::BindWide(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, int nTable)
{
  ID3D12PipelineState *pPSO = GetPSO(eKernel);
  CD3D12Descriptors *pDescriptors = g_pRenderer->m_pDescriptors.get();
  if (!pList || !pPSO || !s_Kernels[eKernel].bWide || !pDescriptors || nTable < 0)
    return false;
  g_pRenderer->m_pDevice->ForgetDrawState();
  pList->SetComputeRootSignature(m_pWideRootSignature.Get());
  pList->SetPipelineState(pPSO);
  pList->SetComputeRootDescriptorTable(0, pDescriptors->GetGPU(nTable));
  return true;
}

void CD3D12Compute::DispatchWide(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, int nTable, UINT nGroupsX, UINT nGroupsY, UINT nGroupsZ)
{
  if (nGroupsX && nGroupsY && nGroupsZ && BindWide(pList, eKernel, nTable))
    pList->Dispatch(nGroupsX, nGroupsY, nGroupsZ);
}

void CD3D12Compute::DispatchWideIndirect(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, int nTable, ID3D12Resource *pArgs, UINT64 nOffset)
{
  if (pArgs && m_pDispatchSignature && BindWide(pList, eKernel, nTable))
    pList->ExecuteIndirect(m_pDispatchSignature.Get(), 1, pArgs, nOffset, nullptr, 0);
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
