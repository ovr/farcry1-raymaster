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

using Microsoft::WRL::ComPtr;

namespace
{

struct SKernel
{
  const char *szName;
  const void *pCS;
  size_t nCS;
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
};

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
  return CreateRootSignature();
}

void CD3D12Compute::Shutdown()
{
  for (auto &p : m_pPSOs)
    p.Reset();
  for (bool &b : m_bFailed)
    b = false;
  m_pRootSignature.Reset();
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
  const D3D12_STATIC_SAMPLER_DESC Samplers[2] = { StaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT), StaticSampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR) };

  D3D12_ROOT_SIGNATURE_DESC Desc = {};
  Desc.NumParameters = 3;
  Desc.pParameters = Params;
  Desc.NumStaticSamplers = 2;
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

ID3D12PipelineState *CD3D12Compute::GetPSO(ED3D12Kernel eKernel)
{
  if (eKernel < 0 || eKernel >= eD3D12K_Num || !m_pRootSignature || m_bFailed[eKernel])
    return nullptr;
  if (!m_pPSOs[eKernel])
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};
    Desc.pRootSignature = m_pRootSignature.Get();
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
                             UINT nGroupsX, UINT nGroupsY)
{
  ID3D12PipelineState *pPSO = GetPSO(eKernel);
  CD3D12Descriptors *pDescriptors = g_pRenderer->m_pDescriptors.get();
  if (!pList || !pPSO || !pDescriptors || !nGroupsX || !nGroupsY)
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
  pList->Dispatch(nGroupsX, nGroupsY, 1);
}
