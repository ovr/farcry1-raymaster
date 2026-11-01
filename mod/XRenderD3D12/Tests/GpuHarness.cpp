// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: GpuHarness.cpp
//	Description: The unit tests' compute harness (GpuHarness.h): the
//	device, the root signature, a run of a kernel and its read back.
//
//////////////////////////////////////////////////////////////////////

#include "GpuHarness.h"
#include "TestFramework.h"

#include <cstring>
#include <stdexcept>

namespace
{
  void Ok(HRESULT hr, const char *szWhat)
  {
    if (FAILED(hr))
    {
      char sz[160];
      snprintf(sz, sizeof(sz), "%s failed (0x%08X)", szWhat, (unsigned)hr);
      throw std::runtime_error(sz);
    }
  }

  std::string Narrow(const wchar_t *wsz)
  {
    std::string s;
    for (; *wsz; wsz++)
      s += *wsz < 128 ? (char)*wsz : '?';
    return s;
  }

  D3D12_RESOURCE_BARRIER Transition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES eFrom, D3D12_RESOURCE_STATES eTo)
  {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = pRes;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = eFrom;
    b.Transition.StateAfter = eTo;
    return b;
  }

  std::unique_ptr<XrTest::CGpu> &GpuPtr()
  {
    static std::unique_ptr<XrTest::CGpu> s_pGpu;
    return s_pGpu;
  }
}

XrTest::SGpuOptions &XrTest::GpuOptions()
{
  static SGpuOptions s_Options;
  return s_Options;
}

XrTest::CGpu &XrTest::Gpu()
{
  if (!GpuPtr())
    GpuPtr() = std::make_unique<CGpu>();
  return *GpuPtr();
}

bool XrTest::GpuReady()
{
  return GpuPtr() != nullptr;
}

XrTest::CGpu::CGpu()
{
  if (GpuOptions().bDebug)
  {
    ComPtr<ID3D12Debug> pDebug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug))))
      pDebug->EnableDebugLayer();
  }
  ComPtr<IDXGIFactory6> pFactory;
  Ok(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)), "CreateDXGIFactory2");
  // The first hardware adapter (by the system's preference for performance) that makes a device, else WARP.
  ComPtr<IDXGIAdapter1> pAdapter;
  for (UINT i = 0; !GpuOptions().bWarp && !m_pDevice &&
                   SUCCEEDED(pFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)));
       i++)
  {
    DXGI_ADAPTER_DESC1 Desc;
    pAdapter->GetDesc1(&Desc);
    if (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;
    if (SUCCEEDED(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice))))
      m_Name = Narrow(Desc.Description);
  }
  if (!m_pDevice)
  {
    ComPtr<IDXGIAdapter> pWarp;
    Ok(pFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarp)), "EnumWarpAdapter");
    Ok(D3D12CreateDevice(pWarp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)), "D3D12CreateDevice (WARP)");
    m_Name = "WARP";
    m_bWarp = true;
  }
  // The highest shader model the device runs, asked from the highest this SDK knows down.
  for (int n = D3D_HIGHEST_SHADER_MODEL; n >= D3D_SHADER_MODEL_5_1; n--)
  {
    D3D12_FEATURE_DATA_SHADER_MODEL Sm = { (D3D_SHADER_MODEL)n };
    if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &Sm, sizeof(Sm))))
    {
      m_nShaderModel = Sm.HighestShaderModel;
      break;
    }
  }
  if (GpuOptions().bDebug)
    m_pDevice.As(&m_pInfoQueue);

  D3D12_COMMAND_QUEUE_DESC Queue = {};
  Queue.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Ok(m_pDevice->CreateCommandQueue(&Queue, IID_PPV_ARGS(&m_pQueue)), "CreateCommandQueue");
  Ok(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_pAllocator)), "CreateCommandAllocator");
  Ok(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_pAllocator.Get(), nullptr, IID_PPV_ARGS(&m_pList)),
     "CreateCommandList");
  Ok(m_pList->Close(), "Close");
  Ok(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)), "CreateFence");
  m_hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

  // The root signature of GpuHarness.h.
  D3D12_ROOT_PARAMETER Params[7] = {};
  const struct
  {
    D3D12_ROOT_PARAMETER_TYPE eType;
    UINT nRegister, nSpace;
  } Layout[7] = {
    { D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 9 }, { D3D12_ROOT_PARAMETER_TYPE_CBV, 2, 0 }, { D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 9 },
    { D3D12_ROOT_PARAMETER_TYPE_SRV, 1, 9 }, { D3D12_ROOT_PARAMETER_TYPE_SRV, 2, 9 }, { D3D12_ROOT_PARAMETER_TYPE_UAV, 0, 9 },
    { D3D12_ROOT_PARAMETER_TYPE_UAV, 3, 0 },
  };
  for (int i = 0; i < 7; i++)
  {
    Params[i].ParameterType = Layout[i].eType;
    Params[i].Descriptor.ShaderRegister = Layout[i].nRegister;
    Params[i].Descriptor.RegisterSpace = Layout[i].nSpace;
    Params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC Rs = {};
  Rs.NumParameters = 7;
  Rs.pParameters = Params;
  ComPtr<ID3DBlob> pBlob, pError;
  Ok(D3D12SerializeRootSignature(&Rs, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError), "D3D12SerializeRootSignature");
  Ok(m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)),
     "CreateRootSignature");
  ClearCache();
}

XrTest::CGpu::~CGpu()
{
  if (m_hEvent)
    CloseHandle(m_hEvent);
}

XrTest::ComPtr<ID3D12Resource> XrTest::CGpu::Buffer(D3D12_HEAP_TYPE eHeap, size_t nBytes, D3D12_RESOURCE_STATES eState, bool bUav)
{
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = eHeap;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = (nBytes + 255) & ~(size_t)255;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = bUav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> pRes;
  // Committed resources of the default heap start zeroed (no D3D12_HEAP_FLAG_CREATE_NOT_ZEROED).
  Ok(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, eState, nullptr, IID_PPV_ARGS(&pRes)), "CreateCommittedResource");
  return pRes;
}

XrTest::ComPtr<ID3D12Resource> XrTest::CGpu::Upload(const void *pData, size_t nBytes)
{
  ComPtr<ID3D12Resource> pRes = Buffer(D3D12_HEAP_TYPE_UPLOAD, nBytes ? nBytes : 16, D3D12_RESOURCE_STATE_GENERIC_READ, false);
  void *p = nullptr;
  Ok(pRes->Map(0, nullptr, &p), "Map");
  memset(p, 0, (size_t)pRes->GetDesc().Width);
  if (pData && nBytes)
    memcpy(p, pData, nBytes);
  pRes->Unmap(0, nullptr);
  return pRes;
}

ID3D12PipelineState *XrTest::CGpu::Pipeline(const SBytecode &Shader)
{
  ComPtr<ID3D12PipelineState> &pPso = m_Pipelines[Shader.pData];
  if (!pPso)
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};
    Desc.pRootSignature = m_pRootSignature.Get();
    Desc.CS = { Shader.pData, Shader.nSize };
    Ok(m_pDevice->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&pPso)), "CreateComputePipelineState");
  }
  return pPso.Get();
}

void XrTest::CGpu::Execute()
{
  Ok(m_pList->Close(), "Close");
  ID3D12CommandList *pLists[] = { m_pList.Get() };
  m_pQueue->ExecuteCommandLists(1, pLists);
  Ok(m_pQueue->Signal(m_pFence.Get(), ++m_nFence), "Signal");
  if (m_pFence->GetCompletedValue() < m_nFence)
  {
    m_pFence->SetEventOnCompletion(m_nFence, m_hEvent);
    WaitForSingleObject(m_hEvent, INFINITE);
  }
  Ok(m_pDevice->GetDeviceRemovedReason(), "the device (removed)");
  CheckDebugMessages();
}

void XrTest::CGpu::CheckDebugMessages()
{
  if (!m_pInfoQueue)
    return;
  for (UINT64 i = 0, n = m_pInfoQueue->GetNumStoredMessages(); i < n; i++)
  {
    SIZE_T nBytes = 0;
    m_pInfoQueue->GetMessage(i, nullptr, &nBytes);
    std::vector<char> Buffer(nBytes);
    D3D12_MESSAGE *pMsg = (D3D12_MESSAGE *)Buffer.data();
    m_pInfoQueue->GetMessage(i, pMsg, &nBytes);
    if (pMsg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
      Fail(__FILE__, __LINE__, std::string("debug layer: ") + pMsg->pDescription);
  }
  m_pInfoQueue->ClearStoredMessages();
}

void XrTest::CGpu::ClearCache()
{
  // A new buffer: zeroed as it is made.
  m_pCache = Buffer(D3D12_HEAP_TYPE_DEFAULT, XR_PT_CACHE_BYTES, D3D12_RESOURCE_STATE_COMMON, true);
}

std::vector<uint32> XrTest::CGpu::Run(const SDispatch &d)
{
  const size_t nOutBytes = d.nOut * 16;
  ComPtr<ID3D12Resource> pTest = Upload(&d.Test, sizeof(d.Test));
  SPathTraceConstants Zero = {};
  ComPtr<ID3D12Resource> pPT = Upload(d.pPathTrace ? d.pPathTrace : &Zero, sizeof(SPathTraceConstants));
  ComPtr<ID3D12Resource> pIn = Upload(d.In.data(), d.In.size() * 4);
  ComPtr<ID3D12Resource> pInstances = Upload(d.pInstances, d.nInstanceBytes);
  ComPtr<ID3D12Resource> pLights = Upload(d.pLights, d.nLightBytes);
  ComPtr<ID3D12Resource> pOut = Buffer(D3D12_HEAP_TYPE_DEFAULT, nOutBytes, D3D12_RESOURCE_STATE_COMMON, true);
  ComPtr<ID3D12Resource> pReadback = Buffer(D3D12_HEAP_TYPE_READBACK, nOutBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);

  Ok(m_pAllocator->Reset(), "Reset");
  Ok(m_pList->Reset(m_pAllocator.Get(), Pipeline(d.Shader)), "Reset");
  D3D12_RESOURCE_BARRIER Before[] = {
    Transition(pOut.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    Transition(m_pCache.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
  };
  m_pList->ResourceBarrier(2, Before);
  m_pList->SetComputeRootSignature(m_pRootSignature.Get());
  m_pList->SetComputeRootConstantBufferView(0, pTest->GetGPUVirtualAddress());
  m_pList->SetComputeRootConstantBufferView(1, pPT->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(2, pIn->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(3, pInstances->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(4, pLights->GetGPUVirtualAddress());
  m_pList->SetComputeRootUnorderedAccessView(5, pOut->GetGPUVirtualAddress());
  m_pList->SetComputeRootUnorderedAccessView(6, m_pCache->GetGPUVirtualAddress());
  m_pList->Dispatch(d.nGroups[0], d.nGroups[1], d.nGroups[2]);
  D3D12_RESOURCE_BARRIER After[] = {
    Transition(pOut.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
    Transition(m_pCache.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
  };
  m_pList->ResourceBarrier(2, After);
  m_pList->CopyBufferRegion(pReadback.Get(), 0, pOut.Get(), 0, nOutBytes);
  Execute();

  std::vector<uint32> Out(d.nOut * 4);
  void *p = nullptr;
  D3D12_RANGE Range = { 0, nOutBytes };
  Ok(pReadback->Map(0, &Range, &p), "Map");
  memcpy(Out.data(), p, nOutBytes);
  D3D12_RANGE None = { 0, 0 };
  pReadback->Unmap(0, &None);
  return Out;
}
