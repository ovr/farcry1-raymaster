// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Msaa.cpp
//	Description: MSAA of the main view's scene (d3d12_AA msaa2, msaa4,
//	msaa8). From BeginMainScene to EndMainScene the main view draws into
//	the device's multisampled colour, depth buffer and ambient light
//	(CD3D12Device::SetMsaaTargets, BeginMsaa; the pipelines with
//	SPipelineKey::nSamples); everything after the scene, and what reads
//	the depth buffer in it, takes the main view's own targets as before,
//	which get the samples resolved:
//	  - the colour at the end of the scene by Msaa.hlsl's pass
//	    (Msaa.hlsli): the mean of the display's light of the samples, the
//	    HDR target's tone mapped at the scene's exposure for it;
//	  - the depth buffer the nearest sample (Msaa.hlsl, SV_Depth), before
//	    the sun's mask by rays, the ambient occlusion and the water's copy
//	    of the scene read it, and at the end of the scene; only when draws
//	    wrote it since the last resolve;
//	  - the ambient light (d3d12_AOApply 1) by the GPU's resolve when the
//	    ambient occlusion unbinds it (CD3D12Device::BindAmbient).
//	Not with an upscaler's scene: FSR anti-aliases, and the path
//	tracer's G-buffer has one sample a pixel (logged once, then none).
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"
#include "Upscaler.h"

#include "MsaaResolve_VS.h"
#include "MsaaColor_PS.h"
#include "MsaaDepth_PS.h"

int CD3D12Renderer::MsaaSamples(EAntiAlias eAA)
{
  return eAA == EAntiAlias::Msaa2 ? 2 : eAA == EAntiAlias::Msaa4 ? 4 : eAA == EAntiAlias::Msaa8 ? 8 : 0;
}

namespace
{

//! A pass's pipeline: the triangle over the main view, Ps the pixel shader; bDepth writes the depth buffer alone
//! (SV_Depth, always), else the colour target alone in Format.
ComPtr<ID3D12PipelineState> CreateResolvePSO(ID3D12Device *pDevice, ID3D12RootSignature *pRoot, const void *pPS, size_t nPS, bool bDepth,
                                             DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN)
{
  D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = {};
  Desc.pRootSignature = pRoot;
  Desc.VS = { g_MsaaResolve_VS, sizeof(g_MsaaResolve_VS) };
  Desc.PS = { pPS, nPS };
  Desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  Desc.SampleMask = UINT_MAX;
  Desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  Desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  Desc.RasterizerState.DepthClipEnable = TRUE;
  Desc.DepthStencilState.DepthEnable = bDepth ? TRUE : FALSE;
  Desc.DepthStencilState.DepthWriteMask = bDepth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  Desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  Desc.NumRenderTargets = bDepth ? 0 : 1;
  Desc.RTVFormats[0] = bDepth ? DXGI_FORMAT_UNKNOWN : Format;
  Desc.DSVFormat = bDepth ? D3D12_DEPTH_FORMAT : DXGI_FORMAT_UNKNOWN;
  Desc.SampleDesc.Count = 1;
  ComPtr<ID3D12PipelineState> pPSO;
  const HRESULT hr = pDevice->CreateGraphicsPipelineState(&Desc, IID_PPV_ARGS(&pPSO));
  if (FAILED(hr))
    iLog->LogError("XRenderD3D12: CreateGraphicsPipelineState (MSAA %s resolve) failed (0x%08X)", bDepth ? "depth" : "colour", (unsigned)hr);
  return pPSO;
}

}

bool CD3D12Renderer::EnsureMsaaPasses()
{
  if (m_pMsaaColorPSO[0] && m_pMsaaColorPSO[1] && m_pMsaaDepthPSO)
    return true;
  if (m_bMsaaPassesFailed || !m_pDevice || !m_pDescriptors)
    return false;
  m_bMsaaPassesFailed = true;
  ID3D12Device *pDevice = m_pDevice->GetDevice();
  D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
  for (UINT i = 0; i < 2; i++)
  {
    Ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Ranges[i].NumDescriptors = 1;
    Ranges[i].BaseShaderRegister = i;
  }
  D3D12_ROOT_PARAMETER Params[3] = {};
  Params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  Params[0].Constants.Num32BitValues = sizeof(SMsaaConstants) / 4;
  Params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  for (UINT i = 0; i < 2; i++)
  {
    Params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
    Params[1 + i].DescriptorTable.pDescriptorRanges = &Ranges[i];
    Params[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  D3D12_ROOT_SIGNATURE_DESC Desc = {};
  Desc.NumParameters = 3;
  Desc.pParameters = Params;
  ComPtr<ID3DBlob> pBlob, pErrors;
  HRESULT hr = D3D12SerializeRootSignature(&Desc, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pErrors);
  if (SUCCEEDED(hr))
    hr = pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pMsaaRoot));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: the MSAA resolve's root signature failed (0x%08X)", (unsigned)hr);
    return false;
  }
  m_pMsaaColorPSO[0] = CreateResolvePSO(pDevice, m_pMsaaRoot.Get(), g_MsaaColor_PS, sizeof(g_MsaaColor_PS), false, D3D12_BACKBUFFER_FORMAT);
  m_pMsaaColorPSO[1] = CreateResolvePSO(pDevice, m_pMsaaRoot.Get(), g_MsaaColor_PS, sizeof(g_MsaaColor_PS), false, D3D12_HDR_FORMAT);
  m_pMsaaDepthPSO = CreateResolvePSO(pDevice, m_pMsaaRoot.Get(), g_MsaaDepth_PS, sizeof(g_MsaaDepth_PS), true);
  m_bMsaaPassesFailed = !m_pMsaaColorPSO[0] || !m_pMsaaColorPSO[1] || !m_pMsaaDepthPSO;
  return !m_bMsaaPassesFailed;
}

void CD3D12Renderer::BeginMsaa()
{
  const EAntiAlias eAA = AntiAliasing();
  const int nSamples = MsaaSamples(eAA);
  m_szMsaaOff = nullptr;
  // An upscaler's scene: FSR (DrawPostAA logs it) or the path tracer's.
  if (!nSamples || m_bUpscaling)
  {
    if (nSamples && !(m_pUpscaler && m_pUpscaler->GathersFrames()))
    {
      m_szMsaaOff = "not with the path tracer";
      if (!m_bMsaaLogged)
        iLog->Log("XRenderD3D12: d3d12_AA %s is not used with the path tracer", m_pAAVar ? m_pAAVar->GetString() : "");
      m_bMsaaLogged = true;
    }
    else if (!nSamples)
      m_bMsaaLogged = false;
    // The targets are large (4K, 8 samples: 1 GB): not kept while they are not used.
    if (m_pDevice->GetMsaaColor())
      ReleaseMsaa();
    return;
  }
  if (!EnsureMsaaPasses() || !m_pDevice->SetMsaaTargets(nSamples, m_pDevice->GetMainColorFormat()))
  {
    m_szMsaaOff = "not available";
    if (!m_bMsaaLogged)
      iLog->LogWarning("XRenderD3D12: d3d12_AA %s is not available at %d x %d (the GPU's samples for the targets' formats); none",
                       m_pAAVar ? m_pAAVar->GetString() : "", m_pDevice->GetWidth(), m_pDevice->GetHeight());
    m_bMsaaLogged = true;
    return;
  }
  m_bMsaaLogged = false;
  // The views of the multisampled colour and depth (again for the device's new targets).
  if (m_nMsaaColorSRV < 0 || m_nMsaaSRVGeneration != m_pDevice->GetMsaaGeneration())
  {
    if (m_nMsaaColorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nMsaaColorSRV);
    if (m_nMsaaDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_nMsaaDepthSRV);
    D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
    SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    SRV.Format = m_pDevice->GetMainColorFormat();
    m_nMsaaColorSRV = m_pDescriptors->AllocSRV(m_pDevice->GetMsaaColor(), &SRV);
    SRV.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    m_nMsaaDepthSRV = m_pDescriptors->AllocSRV(m_pDevice->GetMsaaDepth(), &SRV);
    m_nMsaaSRVGeneration = m_pDevice->GetMsaaGeneration();
    if (m_nMsaaColorSRV < 0 || m_nMsaaDepthSRV < 0)
    {
      m_szMsaaOff = "not available";
      ReleaseMsaa();
      return;
    }
  }
  m_pDevice->BeginMsaa();
  m_bMsaaDepthDirty = true;
  m_nMsaaFrame = m_nFrames;
}

void CD3D12Renderer::DrawMsaaResolve(bool bDepth)
{
  ID3D12Resource *pSource = bDepth ? m_pDevice->GetMsaaDepth() : m_pDevice->GetMsaaColor();
  const D3D12_RESOURCE_STATES State = bDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
  m_pDevice->BindMsaaResolveTarget(!bDepth);
  m_pDevice->Transition(pSource, State, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  ID3D12GraphicsCommandList *pList = m_pDevice->GetCmdList();
  // The HDR target's light at the exposure its resolve will take (Hdr.cpp's ResolveHdr: the compensation, the adapted EV
  // of the frame before; none in heat vision).
  const bool bHdr = m_pDevice->IsInHdr();
  const bool bAuto = bHdr && CV_d3d12_AutoExposure && m_Hdr.bAdapted && !m_bHeatVision && m_Hdr.pExposure[m_Hdr.nExposure];
  SMsaaConstants c = {};
  c.vParams[0] = (uint32)m_pDevice->GetMsaaSamples();
  c.vParams[1] = (bHdr ? MSAA_HDR : 0) | (bAuto ? MSAA_AUTO_EXPOSURE : 0);
  c.vExposure[0] = m_bHeatVision ? 0.0f : CV_d3d12_Exposure;
  pList->SetGraphicsRootSignature(m_pMsaaRoot.Get());
  pList->SetGraphicsRoot32BitConstants(0, sizeof(c) / 4, &c, 0);
  pList->SetGraphicsRootDescriptorTable(1, m_pDescriptors->GetGPU(bDepth ? m_nMsaaDepthSRV : m_nMsaaColorSRV));
  pList->SetGraphicsRootDescriptorTable(2, m_pDescriptors->GetGPU(bAuto ? m_Hdr.pExposure[m_Hdr.nExposure]->m_nSRV : D3D12_SRV_WHITE));
  pList->SetPipelineState(bDepth ? m_pMsaaDepthPSO.Get() : m_pMsaaColorPSO[bHdr ? 1 : 0].Get());
  pList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pList->DrawInstanced(3, 1, 0, 0);
  m_pDevice->Transition(pSource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, State);
  // The renderer's root signature and the multisampled targets again.
  m_pDevice->BindRootSignature();
  m_pDevice->SetBackBufferTargets();
}

void CD3D12Renderer::ResolveMsaaDepth()
{
  if (!m_pDevice || !m_pDevice->IsInMsaa() || !m_bMsaaDepthDirty)
    return;
  DrawMsaaResolve(true);
  m_bMsaaDepthDirty = false;
}

void CD3D12Renderer::EndMsaa()
{
  if (!m_pDevice || !m_pDevice->IsInMsaa())
    return;
  GpuSection("msaa");
  DrawMsaaResolve(false);
  ResolveMsaaDepth();
  m_pDevice->EndMsaa();
  if (!m_pDevice->IsInHdr())
    GpuSection("other");
}

void CD3D12Renderer::ReleaseMsaa()
{
  if (m_pDevice)
  {
    m_pDevice->SetMsaaTargets(0, DXGI_FORMAT_UNKNOWN);
    m_pDevice->DeferredRelease(m_pMsaaColorPSO[0]);
    m_pDevice->DeferredRelease(m_pMsaaColorPSO[1]);
    m_pDevice->DeferredRelease(m_pMsaaDepthPSO);
    m_pDevice->DeferredRelease(m_pMsaaRoot);
  }
  if (m_pDescriptors)
  {
    if (m_nMsaaColorSRV >= 0)
      m_pDescriptors->FreeSRV(m_nMsaaColorSRV);
    if (m_nMsaaDepthSRV >= 0)
      m_pDescriptors->FreeSRV(m_nMsaaDepthSRV);
  }
  m_nMsaaColorSRV = m_nMsaaDepthSRV = -1;
  m_bMsaaPassesFailed = false;
  m_bMsaaDepthDirty = false;
}
