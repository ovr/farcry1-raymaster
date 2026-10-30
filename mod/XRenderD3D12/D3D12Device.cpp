// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Device.cpp
//	Description: The Direct3D 12 device, its queue, the swap chain and the
//	frames in flight.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Descriptors.h"
#include "D3D12Pipeline.h"

namespace
{

void Barrier(ID3D12GraphicsCommandList *pList, ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = pRes;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = Before;
  b.Transition.StateAfter = After;
  pList->ResourceBarrier(1, &b);
}

//! The scene targets outside BeginScene .. EndScene: read by an upscaler's compute or pixel shaders.
constexpr D3D12_RESOURCE_STATES SCENE_READ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

//! Logs a failed call; true if hr failed.
bool Failed(HRESULT hr, const char *szWhat)
{
  if (SUCCEEDED(hr))
    return false;
  if (iLog)
    iLog->LogError("XRenderD3D12: %s failed (0x%08X)", szWhat, (unsigned)hr);
  return true;
}

}

CD3D12Device::~CD3D12Device()
{
  Shutdown();
}

bool CD3D12Device::Init(HWND hWnd, int nWidth, int nHeight, bool bDebug)
{
  UINT nFactoryFlags = 0;
  if (bDebug)
  {
    ComPtr<ID3D12Debug> pDebug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug))))
    {
      pDebug->EnableDebugLayer();
      ComPtr<ID3D12Debug1> pDebug1;
      if (SUCCEEDED(pDebug.As(&pDebug1)))
        pDebug1->SetEnableGPUBasedValidation(TRUE);
      nFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
      iLog->Log("XRenderD3D12: debug layer and GPU based validation on");
    }
    else
      iLog->LogWarning("XRenderD3D12: the debug layer is not installed (Graphics Tools)");
  }

  if (Failed(CreateDXGIFactory2(nFactoryFlags, IID_PPV_ARGS(&m_pFactory)), "CreateDXGIFactory2"))
    return false;
  m_nFactoryFlags = nFactoryFlags;
  m_hWnd = hWnd;

  // The first hardware adapter that makes a feature level 11_0 device, high performance first.
  ComPtr<IDXGIFactory6> pFactory6;
  m_pFactory.As(&pFactory6);
  for (UINT i = 0; !m_pDevice; i++)
  {
    ComPtr<IDXGIAdapter1> pAdapter;
    HRESULT hr = pFactory6 ? pFactory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter))
                           : m_pFactory->EnumAdapters1(i, &pAdapter);
    if (FAILED(hr))
      break;
    DXGI_ADAPTER_DESC1 Desc;
    pAdapter->GetDesc1(&Desc);
    if (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;
    if (SUCCEEDED(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice))))
    {
      m_pAdapter = pAdapter;
      char szName[128];
      WideCharToMultiByte(CP_UTF8, 0, Desc.Description, -1, szName, sizeof(szName), nullptr, nullptr);
      iLog->Log("XRenderD3D12: adapter '%s', %u MB video memory", szName, (unsigned)(Desc.DedicatedVideoMemory >> 20));
    }
  }
  if (!m_pDevice)
  {
    iLog->LogError("XRenderD3D12: no Direct3D 12 adapter (feature level 11_0)");
    return false;
  }

  // The messages are kept for LogDebugMessages; nothing breaks into a debugger. Not kept: the
  // hint that a clear of an off screen target is not to its optimised clear colour (the
  // reflection's is the scene's clear colour, which changes).
  if (bDebug && SUCCEEDED(m_pDevice.As(&m_pInfoQueue)))
  {
    m_pInfoQueue->SetMessageCountLimit(4096);
    D3D12_MESSAGE_ID DenyIDs[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE };
    D3D12_INFO_QUEUE_FILTER Filter = {};
    Filter.DenyList.NumIDs = (UINT)std::size(DenyIDs);
    Filter.DenyList.pIDList = DenyIDs;
    m_pInfoQueue->AddStorageFilterEntries(&Filter);
  }

  D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
  QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (Failed(m_pDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&m_pQueue)), "CreateCommandQueue"))
    return false;

  ComPtr<IDXGIFactory5> pFactory5;
  if (SUCCEEDED(m_pFactory.As(&pFactory5)))
  {
    BOOL bTearing = FALSE;
    if (SUCCEEDED(pFactory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bTearing, sizeof(bTearing))))
      m_bTearing = bTearing != FALSE;
  }

  m_nWidth = std::max(nWidth, 1);
  m_nHeight = std::max(nHeight, 1);
  DXGI_SWAP_CHAIN_DESC1 SwapDesc = {};
  SwapDesc.Width = m_nWidth;
  SwapDesc.Height = m_nHeight;
  SwapDesc.Format = D3D12_BACKBUFFER_FORMAT;
  SwapDesc.SampleDesc.Count = 1;
  SwapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  SwapDesc.BufferCount = D3D12_BACKBUFFERS;
  SwapDesc.Scaling = DXGI_SCALING_STRETCH;
  SwapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  SwapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  SwapDesc.Flags = m_bTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
  ComPtr<IDXGISwapChain1> pSwapChain1;
  if (Failed(m_pFactory->CreateSwapChainForHwnd(m_pQueue.Get(), hWnd, &SwapDesc, nullptr, nullptr, &pSwapChain1), "CreateSwapChainForHwnd")
    || Failed(pSwapChain1.As(&m_pSwapChain), "IDXGISwapChain3"))
    return false;
  // The window is the engine's: no DXGI full screen switch on Alt+Enter.
  m_pFactory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);

  D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
  // The back buffers' views, then the scene targets' (colour, motion vectors, the upscaler's
  // depth; the depth buffer's after the back buffers' in the DSV heap), the HDR target's, the
  // frame target's and the highlight layer's (SceneRTV).
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  HeapDesc.NumDescriptors = D3D12_BACKBUFFERS + 6;
  if (Failed(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_pRTVHeap)), "CreateDescriptorHeap (RTV)"))
    return false;
  m_nRTVSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  HeapDesc.NumDescriptors = 2;
  if (Failed(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_pDSVHeap)), "CreateDescriptorHeap (DSV)"))
    return false;

  for (auto &pAllocator : m_pAllocators)
  {
    if (Failed(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pAllocator)), "CreateCommandAllocator"))
      return false;
  }
  if (Failed(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_pCmdList)), "CreateCommandList"))
    return false;
  m_pCmdList->Close();
  if (Failed(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)), "CreateFence"))
    return false;
  m_hFenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

  // GPU timing: a timestamp query heap and its readback buffer (none: no timing).
  uint64 nFrequency = 0;
  D3D12_QUERY_HEAP_DESC QueryDesc = {};
  QueryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  QueryDesc.Count = D3D12_FRAMES * GPU_MARKS;
  if (SUCCEEDED(m_pQueue->GetTimestampFrequency(&nFrequency)) && nFrequency &&
      SUCCEEDED(m_pDevice->CreateQueryHeap(&QueryDesc, IID_PPV_ARGS(&m_pTimestamps))))
  {
    D3D12_HEAP_PROPERTIES Heap = {};
    Heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = sizeof(uint64) * D3D12_FRAMES * GPU_MARKS;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&m_pTimestampReadback))))
      m_pTimestamps.Reset();
    m_fTimestampMs = 1000.0 / (double)nFrequency;
  }

  // Occlusion queries and their readback buffer (none: no queries).
  QueryDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  QueryDesc.Count = D3D12_FRAMES * OCCLUSION_QUERIES;
  if (SUCCEEDED(m_pDevice->CreateQueryHeap(&QueryDesc, IID_PPV_ARGS(&m_pOcclusion))))
  {
    D3D12_HEAP_PROPERTIES Heap = {};
    Heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = sizeof(uint64) * D3D12_FRAMES * OCCLUSION_QUERIES;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  IID_PPV_ARGS(&m_pOcclusionReadback))))
      m_pOcclusion.Reset();
  }
  return CreateSwapChainBuffers();
}

bool CD3D12Device::CreateSwapChainBuffers()
{
  D3D12_CPU_DESCRIPTOR_HANDLE hRTV = m_pRTVHeap->GetCPUDescriptorHandleForHeapStart();
  for (int i = 0; i < D3D12_BACKBUFFERS; i++)
  {
    if (Failed(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pBackBuffers[i])), "IDXGISwapChain::GetBuffer"))
      return false;
    m_pDevice->CreateRenderTargetView(m_pBackBuffers[i].Get(), nullptr, hRTV);
    hRTV.ptr += m_nRTVSize;
  }

  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = m_nWidth;
  Desc.Height = m_nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = D3D12_DEPTH_FORMAT;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = D3D12_DEPTH_FORMAT;
  Clear.DepthStencil.Depth = 1.0f;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear, IID_PPV_ARGS(&m_pDepth)),
    "CreateCommittedResource (depth)"))
    return false;
  m_pDevice->CreateDepthStencilView(m_pDepth.Get(), nullptr, m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());
  m_nBackBuffer = m_pSwapChain->GetCurrentBackBufferIndex();
  return !m_bLayer || CreateLayer();
}

void CD3D12Device::ReleaseSwapChainBuffers()
{
  for (auto &p : m_pBackBuffers)
    p.Reset();
  m_pDepth.Reset();
  m_pFrame.Reset();
  m_pHighlight.Reset();
  m_bHighlightBound = false;
}

void CD3D12Device::Shutdown()
{
  if (m_pQueue && m_pFence)
    WaitForGPU();
  ReleaseSceneTargets();
  SetHdrTarget(false);
  m_Deferred.clear();
  ReleaseSwapChainBuffers();
  m_pCmdList.Reset();
  for (auto &p : m_pAllocators)
    p.Reset();
  m_pTimestamps.Reset();
  m_pTimestampReadback.Reset();
  m_pOcclusion.Reset();
  m_pOcclusionReadback.Reset();
  m_OcclusionResults.clear();
  for (int &n : m_nOcclusions)
    n = 0;
  m_pFence.Reset();
  if (m_hFenceEvent)
  {
    CloseHandle(m_hFenceEvent);
    m_hFenceEvent = nullptr;
  }
  m_pRTVHeap.Reset();
  m_pDSVHeap.Reset();
  m_pSwapChain.Reset();
  m_pQueue.Reset();
  LogDebugMessages();
  m_pInfoQueue.Reset();
  m_pDevice.Reset();
  m_pAdapter.Reset();
  m_pFactory.Reset();
  m_bInFrame = false;
}

bool CD3D12Device::Resize(int nWidth, int nHeight)
{
  if (!m_pSwapChain)
    return false;
  if (nWidth <= 0 || nHeight <= 0 || (nWidth == m_nWidth && nHeight == m_nHeight))
    return true;
  const bool bInFrame = m_bInFrame;
  if (bInFrame)
    EndFrame(false, false);
  WaitForGPU();
  ReleaseSceneTargets();
  SetHdrTarget(false);
  ReleaseSwapChainBuffers();
  if (Failed(m_pSwapChain->ResizeBuffers(D3D12_BACKBUFFERS, nWidth, nHeight, m_SwapChainFormat,
    m_bTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0), "IDXGISwapChain::ResizeBuffers"))
    return false;
  m_nWidth = nWidth;
  m_nHeight = nHeight;
  const bool bOk = CreateSwapChainBuffers();
  if (bInFrame)
    BeginFrame();
  return bOk;
}

uint64 CD3D12Device::Signal()
{
  const uint64 nValue = m_nNextFence++;
  m_pQueue->Signal(m_pFence.Get(), nValue);
  return nValue;
}

uint64 CD3D12Device::GetCompletedFence() const
{
  return m_pFence ? m_pFence->GetCompletedValue() : 0;
}

void CD3D12Device::WaitForFence(uint64 nValue)
{
  if (m_pFence->GetCompletedValue() >= nValue)
    return;
  m_pFence->SetEventOnCompletion(nValue, m_hFenceEvent);
  if (WaitForSingleObject(m_hFenceEvent, 10000) == WAIT_TIMEOUT)
    iLog->LogError("XRenderD3D12: the GPU did not finish within 10 s (device removed reason 0x%08X)", (unsigned)m_pDevice->GetDeviceRemovedReason());
}

void CD3D12Device::WaitForGPU()
{
  if (g_pRenderer->m_pUpload)
    g_pRenderer->m_pUpload->Flush();
  WaitForFence(Signal());
}

void CD3D12Device::DeferredRelease(IUnknown *p)
{
  if (!p)
    return;
  SDeferred d;
  d.pObject.Attach(p);
  d.nFence = m_nNextFence;
  m_Deferred.push_back(std::move(d));
}

void CD3D12Device::CollectGarbage()
{
  const uint64 nDone = GetCompletedFence();
  std::erase_if(m_Deferred, [nDone](const SDeferred &d) { return d.nFence <= nDone; });
  if (g_pRenderer->m_pDescriptors)
    g_pRenderer->m_pDescriptors->CollectGarbage(nDone);
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Device::GetBackBufferRTV() const
{
  return m_pFrame ? SceneRTV(4) : SwapChainRTV();
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Device::SwapChainRTV() const
{
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_pRTVHeap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += m_nBackBuffer * m_nRTVSize;
  return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Device::GetDSV() const
{
  return m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();
}

bool CD3D12Device::CreateOffscreenTargets(int nWidth, int nHeight, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth,
                                          D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, DXGI_FORMAT Format)
{
  if (!m_pDevice || nWidth <= 0 || nHeight <= 0 || !hRTV.ptr || !hDSV.ptr)
    return false;
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = nWidth;
  Desc.Height = nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = Format;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  // Optimised for a clear to 0 (the sprites'; the reflection is cleared to the scene's clear
  // colour).
  D3D12_CLEAR_VALUE ColorClear = {};
  ColorClear.Format = Format;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    &ColorClear, IID_PPV_ARGS(&pColor)), "CreateCommittedResource (off screen colour)"))
    return false;
  Desc.Format = D3D12_DEPTH_FORMAT;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = D3D12_DEPTH_FORMAT;
  Clear.DepthStencil.Depth = 1.0f;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear, IID_PPV_ARGS(&pDepth)),
    "CreateCommittedResource (off screen depth)"))
  {
    pColor.Reset();
    return false;
  }
  m_pDevice->CreateRenderTargetView(pColor.Get(), nullptr, hRTV);
  m_pDevice->CreateDepthStencilView(pDepth.Get(), nullptr, hDSV);
  return true;
}

bool CD3D12Device::CreateCubeTargets(int nSize, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth, const D3D12_CPU_DESCRIPTOR_HANDLE hRTV[6],
                                     D3D12_CPU_DESCRIPTOR_HANDLE hDSV, DXGI_FORMAT Format)
{
  if (!m_pDevice || nSize <= 0 || !hDSV.ptr || std::any_of(hRTV, hRTV + 6, [](const D3D12_CPU_DESCRIPTOR_HANDLE &h) { return !h.ptr; }))
    return false;
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = nSize;
  Desc.Height = nSize;
  Desc.DepthOrArraySize = 6;
  Desc.MipLevels = 1;
  Desc.Format = Format;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE ColorClear = {};
  ColorClear.Format = Format;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    &ColorClear, IID_PPV_ARGS(&pColor)), "CreateCommittedResource (cube colour)"))
    return false;
  Desc.DepthOrArraySize = 1;
  Desc.Format = D3D12_DEPTH_FORMAT;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = D3D12_DEPTH_FORMAT;
  Clear.DepthStencil.Depth = 1.0f;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &Clear, IID_PPV_ARGS(&pDepth)),
    "CreateCommittedResource (cube depth)"))
  {
    pColor.Reset();
    return false;
  }
  for (UINT i = 0; i < 6; i++)
  {
    D3D12_RENDER_TARGET_VIEW_DESC View = {};
    View.Format = Format;
    View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    View.Texture2DArray.FirstArraySlice = i;
    View.Texture2DArray.ArraySize = 1;
    m_pDevice->CreateRenderTargetView(pColor.Get(), &View, hRTV[i]);
  }
  m_pDevice->CreateDepthStencilView(pDepth.Get(), nullptr, hDSV);
  return true;
}

void CD3D12Device::SetOffscreenTargets(ID3D12Resource *pColor, D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nWidth, int nHeight)
{
  if (!m_bInFrame || !pColor)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), pColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_pOffscreen = pColor;
  m_hOffscreenRTV = hRTV;
  m_hOffscreenDSV = hDSV;
  m_nOffscreenWidth = nWidth;
  m_nOffscreenHeight = nHeight;
  m_OffscreenFormat = pColor->GetDesc().Format;
  m_pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, hDSV.ptr ? &hDSV : nullptr);
  const D3D12_VIEWPORT vp = { 0, 0, (float)nWidth, (float)nHeight, 0, 1 };
  const D3D12_RECT rc = { 0, 0, nWidth, nHeight };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
}

bool CD3D12Device::CreateShadowMap(int nSize, ComPtr<ID3D12Resource> &pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV)
{
  if (!m_pDevice || nSize <= 0 || !hDSV.ptr)
    return false;
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = nSize;
  Desc.Height = nSize;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = DXGI_FORMAT_R32_TYPELESS;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = D3D12_SHADOW_MAP_FORMAT;
  Clear.DepthStencil.Depth = 1.0f;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &Clear,
    IID_PPV_ARGS(&pDepth)), "CreateCommittedResource (shadow map)"))
    return false;
  D3D12_DEPTH_STENCIL_VIEW_DESC View = {};
  View.Format = D3D12_SHADOW_MAP_FORMAT;
  View.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  m_pDevice->CreateDepthStencilView(pDepth.Get(), &View, hDSV);
  return true;
}

void CD3D12Device::SetShadowMapTarget(ID3D12Resource *pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nSize)
{
  if (!m_bInFrame || !pDepth)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), pDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_pOffscreen = pDepth;
  m_bOffscreenShadowMap = true;
  m_OffscreenFormat = DXGI_FORMAT_UNKNOWN;
  m_hOffscreenRTV = {};
  m_hOffscreenDSV = hDSV;
  m_nOffscreenWidth = nSize;
  m_nOffscreenHeight = nSize;
  m_pCmdList->OMSetRenderTargets(0, nullptr, FALSE, &hDSV);
  const D3D12_VIEWPORT vp = { 0, 0, (float)nSize, (float)nSize, 0, 1 };
  const D3D12_RECT rc = { 0, 0, nSize, nSize };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
}

void CD3D12Device::EndOffscreen()
{
  if (!m_pOffscreen)
    return;
  Barrier(m_pCmdList.Get(), m_pOffscreen, m_bOffscreenShadowMap ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pOffscreen = nullptr;
  m_bOffscreenShadowMap = false;
}

void CD3D12Device::SetBackBufferTargets()
{
  EndOffscreen();
  if (m_bInScene)
  {
    const D3D12_CPU_DESCRIPTOR_HANDLE hRTV[3] = { SceneRTV(0), SceneRTV(1), SceneRTV(2) }, hDSV = SceneDSV();
    m_pCmdList->OMSetRenderTargets(3, hRTV, FALSE, &hDSV);
  }
  else if (m_bInHdr)
  {
    const D3D12_CPU_DESCRIPTOR_HANDLE hRTV = SceneRTV(3), hDSV = GetDSV();
    m_pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, &hDSV);
  }
  else
  {
    const D3D12_CPU_DESCRIPTOR_HANDLE hRTV = GetBackBufferRTV(), hDSV = GetDSV();
    m_pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, &hDSV);
  }
  const int w = GetMainWidth(), h = GetMainHeight();
  const D3D12_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
  const D3D12_RECT rc = { 0, 0, w, h };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
}

void CD3D12Device::BindRootSignature()
{
  if (!m_bInFrame || !g_pRenderer->m_pDescriptors || !g_pRenderer->m_pPipeline)
    return;
  ID3D12DescriptorHeap *pHeap = g_pRenderer->m_pDescriptors->GetHeap();
  m_pCmdList->SetDescriptorHeaps(1, &pHeap);
  m_pCmdList->SetGraphicsRootSignature(g_pRenderer->m_pPipeline->GetRootSignature());
  m_pCmdList->SetGraphicsRootDescriptorTable(2, g_pRenderer->m_pDescriptors->GetTableStart());
}

void CD3D12Device::Transition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
  if (m_bInFrame && pRes)
    Barrier(m_pCmdList.Get(), pRes, Before, After);
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Device::SceneRTV(int n) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_pRTVHeap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += (D3D12_BACKBUFFERS + n) * m_nRTVSize;
  return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Device::SceneDSV() const
{
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  return h;
}

bool CD3D12Device::SetSceneSize(int nWidth, int nHeight, DXGI_FORMAT ColorFormat)
{
  if (nWidth <= 0 || nHeight <= 0)
  {
    ReleaseSceneTargets();
    return true;
  }
  if (m_pSceneColor && nWidth == m_nSceneWidth && nHeight == m_nSceneHeight && m_nSceneOutputWidth == m_nWidth &&
      m_nSceneOutputHeight == m_nHeight && ColorFormat == m_SceneColorFormat)
    return true;
  ReleaseSceneTargets();
  if (!m_pDevice)
    return false;
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = nWidth;
  Desc.Height = nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  D3D12_CLEAR_VALUE Clear = {};
  bool bOk = true;
  Desc.Format = Clear.Format = ColorFormat;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  bOk = bOk && !Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, SCENE_READ, &Clear, IID_PPV_ARGS(&m_pSceneColor)),
                       "CreateCommittedResource (scene colour)");
  Desc.Format = Clear.Format = D3D12_VELOCITY_FORMAT;
  bOk = bOk && !Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, SCENE_READ, &Clear, IID_PPV_ARGS(&m_pSceneVelocity)),
                       "CreateCommittedResource (scene motion vectors)");
  Desc.Format = Clear.Format = D3D12_UPSCALE_DEPTH_FORMAT;
  Clear.Color[0] = 1.0f;
  bOk = bOk && !Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, SCENE_READ, &Clear, IID_PPV_ARGS(&m_pUpscaleDepth)),
                       "CreateCommittedResource (upscaler depth)");
  Desc.Format = DXGI_FORMAT_R32_TYPELESS;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  Clear.Format = D3D12_SCENE_DEPTH_FORMAT;
  Clear.DepthStencil.Depth = 1.0f;
  bOk = bOk && !Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, SCENE_READ, &Clear, IID_PPV_ARGS(&m_pSceneDepth)),
                       "CreateCommittedResource (scene depth)");
  Desc.Width = m_nWidth;
  Desc.Height = m_nHeight;
  Desc.Format = D3D12_UPSCALE_FORMAT;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  bOk = bOk && !Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                          IID_PPV_ARGS(&m_pSceneOutput)), "CreateCommittedResource (upscaler output)");
  if (!bOk)
  {
    ReleaseSceneTargets();
    return false;
  }
  m_pDevice->CreateRenderTargetView(m_pSceneColor.Get(), nullptr, SceneRTV(0));
  m_pDevice->CreateRenderTargetView(m_pSceneVelocity.Get(), nullptr, SceneRTV(1));
  m_pDevice->CreateRenderTargetView(m_pUpscaleDepth.Get(), nullptr, SceneRTV(2));
  D3D12_DEPTH_STENCIL_VIEW_DESC View = {};
  View.Format = D3D12_SCENE_DEPTH_FORMAT;
  View.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  m_pDevice->CreateDepthStencilView(m_pSceneDepth.Get(), &View, SceneDSV());
  m_nSceneWidth = nWidth;
  m_nSceneHeight = nHeight;
  m_nSceneOutputWidth = m_nWidth;
  m_nSceneOutputHeight = m_nHeight;
  m_SceneColorFormat = ColorFormat;
  return true;
}

void CD3D12Device::ReleaseSceneTargets()
{
  EndScene();
  DeferredRelease(m_pSceneColor);
  DeferredRelease(m_pSceneVelocity);
  DeferredRelease(m_pUpscaleDepth);
  DeferredRelease(m_pSceneDepth);
  DeferredRelease(m_pSceneOutput);
  m_nSceneWidth = m_nSceneHeight = 0;
  m_nSceneOutputWidth = m_nSceneOutputHeight = 0;
}

void CD3D12Device::BeginScene()
{
  if (!m_bInFrame || m_bInScene || !m_pSceneColor)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), m_pSceneColor.Get(), SCENE_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
  Barrier(m_pCmdList.Get(), m_pSceneVelocity.Get(), SCENE_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
  Barrier(m_pCmdList.Get(), m_pUpscaleDepth.Get(), SCENE_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
  Barrier(m_pCmdList.Get(), m_pSceneDepth.Get(), SCENE_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  m_bInScene = true;
  SetBackBufferTargets();
}

void CD3D12Device::EndScene()
{
  if (!m_bInScene)
    return;
  m_bInScene = false;
  if (!m_bInFrame)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), m_pSceneColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, SCENE_READ);
  Barrier(m_pCmdList.Get(), m_pSceneVelocity.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, SCENE_READ);
  Barrier(m_pCmdList.Get(), m_pUpscaleDepth.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, SCENE_READ);
  Barrier(m_pCmdList.Get(), m_pSceneDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, SCENE_READ);
  SetBackBufferTargets();
}

void CD3D12Device::ClearVelocity()
{
  static const float Zero[4] = {};
  if (m_bInFrame && m_bInScene)
    m_pCmdList->ClearRenderTargetView(SceneRTV(1), Zero, 0, nullptr);
}

bool CD3D12Device::SetHdrTarget(bool bOn)
{
  if (!bOn || (m_pHdrColor && (m_pHdrColor->GetDesc().Width != (UINT64)m_nWidth || m_pHdrColor->GetDesc().Height != (UINT)m_nHeight)))
  {
    EndHdr();
    DeferredRelease(m_pHdrColor);
  }
  if (!bOn || m_pHdrColor)
    return true;
  if (!m_pDevice)
    return false;
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = m_nWidth;
  Desc.Height = m_nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = D3D12_HDR_FORMAT;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = D3D12_HDR_FORMAT;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, SCENE_READ, &Clear, IID_PPV_ARGS(&m_pHdrColor)),
    "CreateCommittedResource (HDR colour)"))
    return false;
  m_pDevice->CreateRenderTargetView(m_pHdrColor.Get(), nullptr, SceneRTV(3));
  return true;
}

void CD3D12Device::BeginHdr()
{
  if (!m_bInFrame || m_bInHdr || m_bInScene || !m_pHdrColor)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), m_pHdrColor.Get(), SCENE_READ, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_bInHdr = true;
  SetBackBufferTargets();
}

void CD3D12Device::EndHdr()
{
  if (!m_bInHdr)
    return;
  m_bInHdr = false;
  if (!m_bInFrame)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), m_pHdrColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, SCENE_READ);
  SetBackBufferTargets();
}

bool CD3D12Device::CreateLayer()
{
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = m_nWidth;
  Desc.Height = m_nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = D3D12_BACKBUFFER_FORMAT;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  // The frame target is cleared to any colour, as a swap chain's buffer: no clear value.
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
    IID_PPV_ARGS(&m_pFrame)), "CreateCommittedResource (frame target)"))
    return false;
  Desc.Format = D3D12_HIGHLIGHT_FORMAT;
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = D3D12_HIGHLIGHT_FORMAT;
  Clear.Color[0] = Clear.Color[1] = Clear.Color[2] = 1;
  Clear.Color[3] = -1;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &Clear,
    IID_PPV_ARGS(&m_pHighlight)), "CreateCommittedResource (highlight layer)"))
  {
    m_pFrame.Reset();
    return false;
  }
  m_pDevice->CreateRenderTargetView(m_pFrame.Get(), nullptr, SceneRTV(4));
  m_pDevice->CreateRenderTargetView(m_pHighlight.Get(), nullptr, SceneRTV(5));
  m_FrameState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  return true;
}

void CD3D12Device::SetFrameState(D3D12_RESOURCE_STATES State)
{
  if (!m_pFrame || State == m_FrameState)
    return;
  Barrier(m_pCmdList.Get(), m_pFrame.Get(), m_FrameState, State);
  m_FrameState = State;
}

void CD3D12Device::BindHighlight(bool bOn)
{
  if (!m_bInFrame || !m_pHighlight || bOn == m_bHighlightBound)
    return;
  m_bHighlightBound = bOn;
  if (!bOn)
  {
    Barrier(m_pCmdList.Get(), m_pHighlight.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    SetBackBufferTargets();
    return;
  }
  EndOffscreen();
  Barrier(m_pCmdList.Get(), m_pHighlight.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  const D3D12_CPU_DESCRIPTOR_HANDLE hRTV[2] = { GetBackBufferRTV(), SceneRTV(5) };
  m_pCmdList->OMSetRenderTargets(2, hRTV, FALSE, nullptr);
  const D3D12_VIEWPORT vp = { 0, 0, (float)m_nWidth, (float)m_nHeight, 0, 1 };
  const D3D12_RECT rc = { 0, 0, m_nWidth, m_nHeight };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
}

void CD3D12Device::SetFrameReadable(bool bReadable)
{
  if (m_bInFrame && m_pFrame && !m_bOutput)
    SetFrameState(bReadable ? D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void CD3D12Device::BeginOutput()
{
  if (!m_bInFrame || !m_pFrame || m_bOutput)
    return;
  EndScene();
  EndHdr();
  EndOffscreen();
  BindHighlight(false);
  SetFrameState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  Barrier(m_pCmdList.Get(), m_pBackBuffers[m_nBackBuffer].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_bOutput = true;
  const D3D12_CPU_DESCRIPTOR_HANDLE hRTV = SwapChainRTV();
  m_pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, nullptr);
  const D3D12_VIEWPORT vp = { 0, 0, (float)m_nWidth, (float)m_nHeight, 0, 1 };
  const D3D12_RECT rc = { 0, 0, m_nWidth, m_nHeight };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
}

namespace
{

//! Windows' SDR white level of the display whose GDI name is szDevice, in nits; 0 if unknown.
float SdrWhiteNits(const wchar_t *szDevice)
{
  std::vector<DISPLAYCONFIG_PATH_INFO> Paths;
  std::vector<DISPLAYCONFIG_MODE_INFO> Modes;
  LONG nResult = ERROR_INSUFFICIENT_BUFFER;
  // The configuration may change between the two calls.
  for (int nTry = 0; nTry < 3 && nResult == ERROR_INSUFFICIENT_BUFFER; nTry++)
  {
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS)
      return 0;
    Paths.resize(nPaths);
    Modes.resize(nModes);
    nResult = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, Paths.data(), &nModes, Modes.data(), nullptr);
    Paths.resize(nPaths);
  }
  if (nResult != ERROR_SUCCESS)
    return 0;
  for (const DISPLAYCONFIG_PATH_INFO &Path : Paths)
  {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME Source = {};
    Source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    Source.header.size = sizeof(Source);
    Source.header.adapterId = Path.sourceInfo.adapterId;
    Source.header.id = Path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&Source.header) != ERROR_SUCCESS || wcscmp(Source.viewGdiDeviceName, szDevice) != 0)
      continue;
    DISPLAYCONFIG_SDR_WHITE_LEVEL White = {};
    White.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
    White.header.size = sizeof(White);
    White.header.adapterId = Path.targetInfo.adapterId;
    White.header.id = Path.targetInfo.id;
    // 1000 is 80 nits.
    if (DisplayConfigGetDeviceInfo(&White.header) == ERROR_SUCCESS)
      return White.SDRWhiteLevel * 80.0f / 1000.0f;
  }
  return 0;
}

}

const SDisplayInfo &CD3D12Device::GetDisplayInfo()
{
  if (!m_pFactory || !m_pDevice)
    return m_Display;
  const HMONITOR hMonitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
  const uint64 nNow = GetTickCount64();
  const bool bCurrent = m_pFactory->IsCurrent() != FALSE;
  if (m_bDisplayInfo && bCurrent && hMonitor == m_hDisplayMonitor && nNow - m_nDisplayInfoTime < 1000)
    return m_Display;
  // A stale factory's adapters and outputs keep the old state: a new one (the swap chain stays).
  if (!bCurrent)
  {
    ComPtr<IDXGIFactory4> pFactory;
    if (SUCCEEDED(CreateDXGIFactory2(m_nFactoryFlags, IID_PPV_ARGS(&pFactory))))
      m_pFactory = pFactory;
  }
  m_bDisplayInfo = true;
  m_hDisplayMonitor = hMonitor;
  m_nDisplayInfoTime = nNow;
  m_Display = SDisplayInfo();
  ComPtr<IDXGIAdapter1> pAdapter;
  if (FAILED(m_pFactory->EnumAdapterByLuid(m_pDevice->GetAdapterLuid(), IID_PPV_ARGS(&pAdapter))))
    return m_Display;
  ComPtr<IDXGIOutput> pOutput;
  for (UINT i = 0; SUCCEEDED(pAdapter->EnumOutputs(i, &pOutput)); i++)
  {
    ComPtr<IDXGIOutput6> pOutput6;
    DXGI_OUTPUT_DESC1 Desc;
    if (FAILED(pOutput.As(&pOutput6)) || FAILED(pOutput6->GetDesc1(&Desc)) || Desc.Monitor != hMonitor)
      continue;
    m_Display.bHdr = Desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    m_Display.fMaxNits = Desc.MaxLuminance;
    m_Display.fMaxFullFrameNits = Desc.MaxFullFrameLuminance;
    m_Display.fMinNits = Desc.MinLuminance;
    m_Display.fSdrWhiteNits = SdrWhiteNits(Desc.DeviceName);
    WideCharToMultiByte(CP_UTF8, 0, Desc.DeviceName, -1, m_Display.szName, sizeof(m_Display.szName), nullptr, nullptr);
    break;
  }
  return m_Display;
}

bool CD3D12Device::SetOutput(EDisplayOutput Output, bool bLayer, float fPeakNits, float fFrameAverageNits)
{
  if (!m_pSwapChain || m_bInFrame)
    return false;
  bLayer = bLayer || Output != EDisplayOutput::Sdr;
  const DXGI_FORMAT Format = Output == EDisplayOutput::Hdr10 ? D3D12_HDR10_FORMAT : Output == EDisplayOutput::ScRgb ? D3D12_SCRGB_FORMAT : D3D12_BACKBUFFER_FORMAT;
  const DXGI_COLOR_SPACE_TYPE ColorSpace = Output == EDisplayOutput::Hdr10 ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 :
                                           Output == EDisplayOutput::ScRgb ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  bool bOk = true;
  if (Format != m_SwapChainFormat || bLayer != m_bLayer)
  {
    WaitForGPU();
    ReleaseSwapChainBuffers();
    m_bLayer = bLayer;
    if (Format != m_SwapChainFormat)
    {
      const auto Resize = [this](DXGI_FORMAT f)
      {
        return !Failed(m_pSwapChain->ResizeBuffers(D3D12_BACKBUFFERS, m_nWidth, m_nHeight, f, m_bTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0),
                       "IDXGISwapChain::ResizeBuffers (output)");
      };
      UINT nSupport = 0;
      bOk = Resize(Format) && SUCCEEDED(m_pSwapChain->CheckColorSpaceSupport(ColorSpace, &nSupport)) &&
            (nSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) && SUCCEEDED(m_pSwapChain->SetColorSpace1(ColorSpace));
      m_SwapChainFormat = Format;
      m_Output = Output;
      if (!bOk && Format != D3D12_BACKBUFFER_FORMAT)
      {
        // Back to SDR.
        Resize(D3D12_BACKBUFFER_FORMAT);
        m_pSwapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        m_SwapChainFormat = D3D12_BACKBUFFER_FORMAT;
        m_Output = EDisplayOutput::Sdr;
      }
      m_fMetadataPeak = m_fMetadataFrameAverage = -1;
    }
    if (!CreateSwapChainBuffers())
      return false;
  }
  // HDR10's metadata: the mastering display Rec. 2020 with the peak, the content up to the peak.
  // Only an HDR10 swap chain has it.
  ComPtr<IDXGISwapChain4> pSwapChain4;
  const float fPeak = m_Output == EDisplayOutput::Hdr10 ? fPeakNits : 0, fFrameAverage = m_Output == EDisplayOutput::Hdr10 ? fFrameAverageNits : 0;
  if ((fPeak != m_fMetadataPeak || fFrameAverage != m_fMetadataFrameAverage) && SUCCEEDED(m_pSwapChain.As(&pSwapChain4)))
  {
    m_fMetadataPeak = fPeak;
    m_fMetadataFrameAverage = fFrameAverage;
    if (m_Output == EDisplayOutput::Hdr10)
    {
      DXGI_HDR_METADATA_HDR10 Metadata = {};
      const auto Chromaticity = [](UINT16 xy[2], float x, float y)
      {
        xy[0] = (UINT16)lroundf(x * 50000);
        xy[1] = (UINT16)lroundf(y * 50000);
      };
      Chromaticity(Metadata.RedPrimary, 0.708f, 0.292f);
      Chromaticity(Metadata.GreenPrimary, 0.170f, 0.797f);
      Chromaticity(Metadata.BluePrimary, 0.131f, 0.046f);
      Chromaticity(Metadata.WhitePoint, 0.3127f, 0.3290f);
      Metadata.MaxMasteringLuminance = (UINT)lroundf(fPeak);
      Metadata.MinMasteringLuminance = 1;         // 0.0001 nits
      Metadata.MaxContentLightLevel = (UINT16)std::min(lroundf(fPeak), 65535L);
      Metadata.MaxFrameAverageLightLevel = (UINT16)std::min(lroundf(fFrameAverage), 65535L);
      pSwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(Metadata), &Metadata);
    }
    else
      pSwapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
  }
  return bOk;
}

void CD3D12Device::OpenCmdList()
{
  m_pAllocators[m_nFrameSlot]->Reset();
  m_pCmdList->Reset(m_pAllocators[m_nFrameSlot].Get(), nullptr);
}

void CD3D12Device::BeginFrame()
{
  if (m_bInFrame || !m_pDevice)
    return;
  WaitForFence(m_nFrameFence[m_nFrameSlot]);
  ReadGpuTimes();
  ReadOcclusionResults();
  CollectGarbage();
  if (g_pRenderer->m_pUpload)
    g_pRenderer->m_pUpload->BeginFrame(m_nFrameSlot);
  m_nBackBuffer = m_pSwapChain->GetCurrentBackBufferIndex();
  m_bInFrame = true;
  OpenCmdList();
  GpuSection("other");
  if (m_pFrame)
  {
    // The swap chain's buffer is written only at the end (EndFrame). No highlights of the scene
    // yet.
    static const float NoHighlight[4] = { 1, 1, 1, -1 };
    SetFrameState(D3D12_RESOURCE_STATE_RENDER_TARGET);
    Barrier(m_pCmdList.Get(), m_pHighlight.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_pCmdList->ClearRenderTargetView(SceneRTV(5), NoHighlight, 0, nullptr);
    Barrier(m_pCmdList.Get(), m_pHighlight.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  }
  else
    Barrier(m_pCmdList.Get(), GetBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  BindRootSignature();
  SetBackBufferTargets();
}

void CD3D12Device::EndFrame(bool bPresent, bool bVSync)
{
  if (!m_bInFrame)
    return;
  EndScene();
  EndHdr();
  EndOffscreen();
  BindHighlight(false);
  // The timestamp that ends the frame's last section; all of them to the readback buffer.
  if (const int n = m_nGpuMarks[m_nFrameSlot]; n > 0)
  {
    const uint32 nFirst = m_nFrameSlot * GPU_MARKS;
    m_pCmdList->EndQuery(m_pTimestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, nFirst + n);
    m_GpuSections[m_nFrameSlot][n] = nullptr;
    m_nGpuMarks[m_nFrameSlot] = n + 1;
    m_pCmdList->ResolveQueryData(m_pTimestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, nFirst, n + 1, m_pTimestampReadback.Get(), sizeof(uint64) * nFirst);
  }
  if (const int n = m_nOcclusions[m_nFrameSlot]; n > 0)
  {
    const uint32 nFirst = m_nFrameSlot * OCCLUSION_QUERIES;
    m_pCmdList->ResolveQueryData(m_pOcclusion.Get(), D3D12_QUERY_TYPE_OCCLUSION, nFirst, n, m_pOcclusionReadback.Get(), sizeof(uint64) * nFirst);
  }
  ID3D12Resource *pSwapBuffer = m_pBackBuffers[m_nBackBuffer].Get();
  if (!m_pFrame || m_bOutput)
    Barrier(m_pCmdList.Get(), pSwapBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  else if (bPresent && m_SwapChainFormat == D3D12_BACKBUFFER_FORMAT)
  {
    // No output pass in SDR: the frame target as it is.
    SetFrameState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(m_pCmdList.Get(), pSwapBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    m_pCmdList->CopyResource(pSwapBuffer, m_pFrame.Get());
    Barrier(m_pCmdList.Get(), pSwapBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
  }
  if (m_pFrame)
    SetFrameState(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_bOutput = false;
  m_pCmdList->Close();
  if (g_pRenderer->m_pUpload)
    g_pRenderer->m_pUpload->Flush();
  ID3D12CommandList *pList = m_pCmdList.Get();
  m_pQueue->ExecuteCommandLists(1, &pList);
  if (bPresent)
  {
    const HRESULT hr = m_pSwapChain->Present(bVSync ? 1 : 0, !bVSync && m_bTearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
    static bool s_bLogged = false;
    if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) && !s_bLogged)
    {
      s_bLogged = true;
      iLog->LogError("XRenderD3D12: device removed (0x%08X, reason 0x%08X)", (unsigned)hr, (unsigned)m_pDevice->GetDeviceRemovedReason());
    }
  }
  m_nFrameFence[m_nFrameSlot] = Signal();
  m_nFrameSlot = (m_nFrameSlot + 1) % D3D12_FRAMES;
  m_bInFrame = false;
  LogDebugMessages();
}

void CD3D12Device::FlushFrame()
{
  if (!m_bInFrame)
    return;
  // The back buffer stays a render target across the two lists.
  m_pCmdList->Close();
  if (g_pRenderer->m_pUpload)
    g_pRenderer->m_pUpload->Flush();
  ID3D12CommandList *pList = m_pCmdList.Get();
  m_pQueue->ExecuteCommandLists(1, &pList);
  WaitForFence(Signal());
  OpenCmdList();
  BindRootSignature();
  SetBackBufferTargets();
}

void CD3D12Device::GpuSection(const char *szSection)
{
  int &n = m_nGpuMarks[m_nFrameSlot];
  if (!m_bInFrame || !m_bGpuTiming || !m_pTimestamps || n >= GPU_MARKS - 1 || (n > 0 && m_GpuSections[m_nFrameSlot][n - 1] == szSection))
    return;
  m_pCmdList->EndQuery(m_pTimestamps.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_nFrameSlot * GPU_MARKS + n);
  m_GpuSections[m_nFrameSlot][n++] = szSection;
}

void CD3D12Device::ReadGpuTimes()
{
  const int n = m_nGpuMarks[m_nFrameSlot];
  m_nGpuMarks[m_nFrameSlot] = 0;
  if (n < 2)
    return;
  const size_t nFirst = (size_t)m_nFrameSlot * GPU_MARKS;
  const D3D12_RANGE Range = { sizeof(uint64) * nFirst, sizeof(uint64) * (nFirst + n) };
  const uint64 *pTicks = nullptr;
  if (FAILED(m_pTimestampReadback->Map(0, &Range, (void **)&pTicks)))
    return;
  pTicks += nFirst;
  for (int i = 0; i + 1 < n; i++)
  {
    const char *szSection = m_GpuSections[m_nFrameSlot][i];
    const double fMs = pTicks[i + 1] > pTicks[i] ? (double)(pTicks[i + 1] - pTicks[i]) * m_fTimestampMs : 0.0;
    auto It = std::find_if(m_GpuTimes.begin(), m_GpuTimes.end(), [szSection](const auto &e) { return strcmp(e.first, szSection) == 0; });
    if (It == m_GpuTimes.end())
      m_GpuTimes.emplace_back(szSection, fMs);
    else
      It->second += fMs;
  }
  const D3D12_RANGE Written = {};
  m_pTimestampReadback->Unmap(0, &Written);
  m_nGpuTimedFrames++;
}

int CD3D12Device::BeginOcclusion()
{
  int &n = m_nOcclusions[m_nFrameSlot];
  if (!m_bInFrame || !m_pOcclusion || n >= OCCLUSION_QUERIES)
    return -1;
  m_pCmdList->BeginQuery(m_pOcclusion.Get(), D3D12_QUERY_TYPE_OCCLUSION, m_nFrameSlot * OCCLUSION_QUERIES + n);
  return n++;
}

void CD3D12Device::EndOcclusion(int nQuery)
{
  if (m_bInFrame && m_pOcclusion && nQuery >= 0 && nQuery < m_nOcclusions[m_nFrameSlot])
    m_pCmdList->EndQuery(m_pOcclusion.Get(), D3D12_QUERY_TYPE_OCCLUSION, m_nFrameSlot * OCCLUSION_QUERIES + nQuery);
}

void CD3D12Device::ReadOcclusionResults()
{
  const int n = m_nOcclusions[m_nFrameSlot];
  m_nOcclusions[m_nFrameSlot] = 0;
  m_OcclusionResults.clear();
  if (n <= 0)
    return;
  const size_t nFirst = (size_t)m_nFrameSlot * OCCLUSION_QUERIES;
  const D3D12_RANGE Range = { sizeof(uint64) * nFirst, sizeof(uint64) * (nFirst + n) };
  const uint64 *pCounts = nullptr;
  if (FAILED(m_pOcclusionReadback->Map(0, &Range, (void **)&pCounts)))
    return;
  m_OcclusionResults.assign(pCounts + nFirst, pCounts + nFirst + n);
  const D3D12_RANGE Written = {};
  m_pOcclusionReadback->Unmap(0, &Written);
}

void CD3D12Device::ClearColor(const float rgba[4])
{
  if (m_bInFrame && !m_bOffscreenShadowMap)
    m_pCmdList->ClearRenderTargetView(m_pOffscreen ? m_hOffscreenRTV : m_bInScene ? SceneRTV(0) : m_bInHdr ? SceneRTV(3) : GetBackBufferRTV(), rgba, 0, nullptr);
}

void CD3D12Device::ClearDepth(float fDepth, uint8 nStencil, bool bDepth, bool bStencil)
{
  // The scene's depth buffer has no stencil.
  const bool bScene = IsSceneBound();
  const auto Flags = (D3D12_CLEAR_FLAGS)((bDepth ? D3D12_CLEAR_FLAG_DEPTH : 0) | (bStencil && !bScene ? D3D12_CLEAR_FLAG_STENCIL : 0));
  if (m_bInFrame && Flags)
    m_pCmdList->ClearDepthStencilView(m_pOffscreen ? m_hOffscreenDSV : bScene ? SceneDSV() : GetDSV(), Flags, fDepth, nStencil, 0, nullptr);
  if (m_bInFrame && bScene && bDepth)
  {
    const float Depth[4] = { fDepth, 0, 0, 0 };
    m_pCmdList->ClearRenderTargetView(SceneRTV(2), Depth, 0, nullptr);
  }
}

bool CD3D12Device::CreateTargetCopies(ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> *pDepth)
{
  if (!m_pDevice)
    return false;
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = GetMainWidth();
  Desc.Height = GetMainHeight();
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = GetMainColorFormat();
  Desc.SampleDesc.Count = 1;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
    IID_PPV_ARGS(&pColor)), "CreateCommittedResource (colour copy)"))
    return false;
  if (!pDepth)
    return true;
  // The depth copy: the same format family and layout as the depth buffer, read through an SRV.
  Desc.Format = m_bInScene ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R24G8_TYPELESS;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
    IID_PPV_ARGS(&*pDepth)), "CreateCommittedResource (depth copy)"))
  {
    pColor.Reset();
    return false;
  }
  return true;
}

void CD3D12Device::CopyTargets(ID3D12Resource *pColor, ID3D12Resource *pDepth)
{
  if (!m_bInFrame || !pColor)
    return;
  ID3D12Resource *pMainColor = m_bInScene ? m_pSceneColor.Get() : m_bInHdr ? m_pHdrColor.Get() : GetBackBuffer();
  ID3D12Resource *pMainDepth = m_bInScene ? m_pSceneDepth.Get() : m_pDepth.Get();
  const auto Matches = [](ID3D12Resource *pCopy, ID3D12Resource *pSource, DXGI_FORMAT Format)
  {
    const D3D12_RESOURCE_DESC a = pCopy->GetDesc(), b = pSource->GetDesc();
    return a.Width == b.Width && a.Height == b.Height && a.Format == Format;
  };
  if (!Matches(pColor, pMainColor, GetMainColorFormat()) ||
      (pDepth && !Matches(pDepth, pMainDepth, m_bInScene ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R24G8_TYPELESS)))
    return;
  D3D12_RESOURCE_BARRIER b[4] = {};
  const auto Set = [](D3D12_RESOURCE_BARRIER &r, ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
  {
    r.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    r.Transition.pResource = pRes;
    r.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    r.Transition.StateBefore = Before;
    r.Transition.StateAfter = After;
  };
  Set(b[0], pMainColor, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Set(b[1], pColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  Set(b[2], pMainDepth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Set(b[3], pDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  const UINT nBarriers = pDepth ? 4 : 2;
  m_pCmdList->ResourceBarrier(nBarriers, b);
  m_pCmdList->CopyResource(pColor, pMainColor);
  if (pDepth)
    m_pCmdList->CopyResource(pDepth, pMainDepth);
  for (auto &r : b)
    std::swap(r.Transition.StateBefore, r.Transition.StateAfter);
  m_pCmdList->ResourceBarrier(nBarriers, b);
}

bool CD3D12Device::ReadBackBuffer(uint8 *pDst, int nWidth, int nHeight)
{
  if (!m_pDevice || !m_pSwapChain)
    return false;
  // With the highlight layer the frame target (between frames: the frame before's).
  if (m_pFrame)
    return ReadBackTexture(m_pFrame.Get(), m_FrameState, pDst, nWidth, nHeight);
  // Between frames the buffer drawn last (and presented) is the one before the current one.
  ID3D12Resource *pBack = m_bInFrame ? GetBackBuffer() : m_pBackBuffers[(m_pSwapChain->GetCurrentBackBufferIndex() + D3D12_BACKBUFFERS - 1) % D3D12_BACKBUFFERS].Get();
  return ReadBackTexture(pBack, m_bInFrame ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PRESENT, pDst, nWidth, nHeight);
}

bool CD3D12Device::ReadBackTexture(ID3D12Resource *pTexture, D3D12_RESOURCE_STATES State, uint8 *pDst, int nWidth, int nHeight)
{
  if (!m_pDevice || !pTexture || !pDst || nWidth <= 0 || nHeight <= 0)
    return false;
  const bool bInFrame = m_bInFrame;
  const D3D12_RESOURCE_DESC Desc = pTexture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
  UINT64 nTotal = 0;
  m_pDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, nullptr, nullptr, &nTotal);

  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC BufDesc = {};
  BufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  BufDesc.Width = nTotal;
  BufDesc.Height = 1;
  BufDesc.DepthOrArraySize = 1;
  BufDesc.MipLevels = 1;
  BufDesc.SampleDesc.Count = 1;
  BufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> pReadback;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &BufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pReadback)),
    "CreateCommittedResource (readback)"))
    return false;

  if (!bInFrame)
  {
    WaitForGPU();
    OpenCmdList();
  }
  Barrier(m_pCmdList.Get(), pTexture, State, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION Src = {}, Dst = {};
  Src.pResource = pTexture;
  Src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  Dst.pResource = pReadback.Get();
  Dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  Dst.PlacedFootprint = Footprint;
  m_pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  Barrier(m_pCmdList.Get(), pTexture, D3D12_RESOURCE_STATE_COPY_SOURCE, State);
  if (bInFrame)
    FlushFrame();
  else
  {
    m_pCmdList->Close();
    ID3D12CommandList *pList = m_pCmdList.Get();
    m_pQueue->ExecuteCommandLists(1, &pList);
    WaitForFence(Signal());
  }

  uint8 *pData = nullptr;
  const D3D12_RANGE Range = { 0, (SIZE_T)nTotal };
  if (FAILED(pReadback->Map(0, &Range, (void **)&pData)))
    return false;
  const int w = std::min(nWidth, (int)Desc.Width);
  const int h = std::min(nHeight, (int)Desc.Height);
  const size_t nPixel = Desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || Desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM ? 8 : 4;
  memset(pDst, 0, (size_t)nWidth * nHeight * nPixel);
  for (int y = 0; y < h; y++)
    memcpy(pDst + (size_t)y * nWidth * nPixel, pData + Footprint.Offset + (size_t)y * Footprint.Footprint.RowPitch, (size_t)w * nPixel);
  const D3D12_RANGE None = { 0, 0 };
  pReadback->Unmap(0, &None);
  return true;
}

void CD3D12Device::LogDebugMessages()
{
  if (!m_pInfoQueue || !iLog)
    return;
  const UINT64 n = m_pInfoQueue->GetNumStoredMessages();
  std::vector<uint8> Buffer;
  for (UINT64 i = 0; i < n; i++)
  {
    SIZE_T nSize = 0;
    m_pInfoQueue->GetMessage(i, nullptr, &nSize);
    Buffer.resize(nSize);
    auto *pMsg = (D3D12_MESSAGE *)Buffer.data();
    if (FAILED(m_pInfoQueue->GetMessage(i, pMsg, &nSize)))
      continue;
    if (pMsg->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
      iLog->LogError("[D3D12] %s", pMsg->pDescription);
    else if (pMsg->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
      iLog->LogWarning("[D3D12] %s", pMsg->pDescription);
  }
  m_pInfoQueue->ClearStoredMessages();
}
