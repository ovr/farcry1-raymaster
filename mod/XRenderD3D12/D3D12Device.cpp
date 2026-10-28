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

  // The messages are kept for LogDebugMessages; nothing breaks into a debugger.
  if (bDebug && SUCCEEDED(m_pDevice.As(&m_pInfoQueue)))
    m_pInfoQueue->SetMessageCountLimit(4096);

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
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  HeapDesc.NumDescriptors = D3D12_BACKBUFFERS;
  if (Failed(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_pRTVHeap)), "CreateDescriptorHeap (RTV)"))
    return false;
  m_nRTVSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  HeapDesc.NumDescriptors = 1;
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
  return true;
}

void CD3D12Device::ReleaseSwapChainBuffers()
{
  for (auto &p : m_pBackBuffers)
    p.Reset();
  m_pDepth.Reset();
}

void CD3D12Device::Shutdown()
{
  if (m_pQueue && m_pFence)
    WaitForGPU();
  m_Deferred.clear();
  ReleaseSwapChainBuffers();
  m_pCmdList.Reset();
  for (auto &p : m_pAllocators)
    p.Reset();
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
  ReleaseSwapChainBuffers();
  if (Failed(m_pSwapChain->ResizeBuffers(D3D12_BACKBUFFERS, nWidth, nHeight, D3D12_BACKBUFFER_FORMAT,
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
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_pRTVHeap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += m_nBackBuffer * m_nRTVSize;
  return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Device::GetDSV() const
{
  return m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();
}

bool CD3D12Device::CreateOffscreenTargets(int nWidth, int nHeight, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth,
                                          D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV)
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
  Desc.Format = D3D12_BACKBUFFER_FORMAT;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  // No optimised clear colour: it is cleared to the scene's clear colour.
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
    IID_PPV_ARGS(&pColor)), "CreateCommittedResource (off screen colour)"))
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
  m_pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, &hDSV);
  const D3D12_VIEWPORT vp = { 0, 0, (float)nWidth, (float)nHeight, 0, 1 };
  const D3D12_RECT rc = { 0, 0, nWidth, nHeight };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
}

void CD3D12Device::EndOffscreen()
{
  if (!m_pOffscreen)
    return;
  Barrier(m_pCmdList.Get(), m_pOffscreen, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_pOffscreen = nullptr;
}

void CD3D12Device::SetBackBufferTargets()
{
  EndOffscreen();
  const D3D12_CPU_DESCRIPTOR_HANDLE hRTV = GetBackBufferRTV(), hDSV = GetDSV();
  m_pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, &hDSV);
  const D3D12_VIEWPORT vp = { 0, 0, (float)m_nWidth, (float)m_nHeight, 0, 1 };
  const D3D12_RECT rc = { 0, 0, m_nWidth, m_nHeight };
  m_pCmdList->RSSetViewports(1, &vp);
  m_pCmdList->RSSetScissorRects(1, &rc);
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
  CollectGarbage();
  if (g_pRenderer->m_pUpload)
    g_pRenderer->m_pUpload->BeginFrame(m_nFrameSlot);
  m_nBackBuffer = m_pSwapChain->GetCurrentBackBufferIndex();
  m_bInFrame = true;
  OpenCmdList();
  Barrier(m_pCmdList.Get(), GetBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (g_pRenderer->m_pDescriptors && g_pRenderer->m_pPipeline)
  {
    ID3D12DescriptorHeap *pHeap = g_pRenderer->m_pDescriptors->GetHeap();
    m_pCmdList->SetDescriptorHeaps(1, &pHeap);
    m_pCmdList->SetGraphicsRootSignature(g_pRenderer->m_pPipeline->GetRootSignature());
    m_pCmdList->SetGraphicsRootDescriptorTable(2, g_pRenderer->m_pDescriptors->GetTableStart());
  }
  SetBackBufferTargets();
}

void CD3D12Device::EndFrame(bool bPresent, bool bVSync)
{
  if (!m_bInFrame)
    return;
  EndOffscreen();
  Barrier(m_pCmdList.Get(), GetBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
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
  if (g_pRenderer->m_pDescriptors && g_pRenderer->m_pPipeline)
  {
    ID3D12DescriptorHeap *pHeap = g_pRenderer->m_pDescriptors->GetHeap();
    m_pCmdList->SetDescriptorHeaps(1, &pHeap);
    m_pCmdList->SetGraphicsRootSignature(g_pRenderer->m_pPipeline->GetRootSignature());
    m_pCmdList->SetGraphicsRootDescriptorTable(2, g_pRenderer->m_pDescriptors->GetTableStart());
  }
  SetBackBufferTargets();
}

void CD3D12Device::ClearColor(const float rgba[4])
{
  if (m_bInFrame)
    m_pCmdList->ClearRenderTargetView(m_pOffscreen ? m_hOffscreenRTV : GetBackBufferRTV(), rgba, 0, nullptr);
}

void CD3D12Device::ClearDepth(float fDepth, uint8 nStencil, bool bDepth, bool bStencil)
{
  const auto Flags = (D3D12_CLEAR_FLAGS)((bDepth ? D3D12_CLEAR_FLAG_DEPTH : 0) | (bStencil ? D3D12_CLEAR_FLAG_STENCIL : 0));
  if (m_bInFrame && Flags)
    m_pCmdList->ClearDepthStencilView(m_pOffscreen ? m_hOffscreenDSV : GetDSV(), Flags, fDepth, nStencil, 0, nullptr);
}

bool CD3D12Device::CreateTargetCopies(ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth)
{
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
  Desc.Format = D3D12_BACKBUFFER_FORMAT;
  Desc.SampleDesc.Count = 1;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
    IID_PPV_ARGS(&pColor)), "CreateCommittedResource (colour copy)"))
    return false;
  // The depth copy: the same format family and layout as the depth buffer, read through an SRV.
  Desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if (Failed(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
    IID_PPV_ARGS(&pDepth)), "CreateCommittedResource (depth copy)"))
  {
    pColor.Reset();
    return false;
  }
  return true;
}

void CD3D12Device::CopyTargets(ID3D12Resource *pColor, ID3D12Resource *pDepth)
{
  if (!m_bInFrame || !pColor || !pDepth)
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
  Set(b[0], GetBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Set(b[1], m_pDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
  Set(b[2], pColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  Set(b[3], pDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  m_pCmdList->ResourceBarrier(4, b);
  m_pCmdList->CopyResource(pColor, GetBackBuffer());
  m_pCmdList->CopyResource(pDepth, m_pDepth.Get());
  for (auto &r : b)
    std::swap(r.Transition.StateBefore, r.Transition.StateAfter);
  m_pCmdList->ResourceBarrier(4, b);
}

bool CD3D12Device::ReadBackBuffer(uint8 *pDst, int nWidth, int nHeight)
{
  if (!m_pDevice || !pDst || nWidth <= 0 || nHeight <= 0)
    return false;
  const bool bInFrame = m_bInFrame;
  // Between frames the buffer drawn last (and presented) is the one before the current one.
  ID3D12Resource *pBack = bInFrame ? GetBackBuffer() : m_pBackBuffers[(m_pSwapChain->GetCurrentBackBufferIndex() + D3D12_BACKBUFFERS - 1) % D3D12_BACKBUFFERS].Get();
  const D3D12_RESOURCE_DESC Desc = pBack->GetDesc();
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
  const D3D12_RESOURCE_STATES State = bInFrame ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PRESENT;
  Barrier(m_pCmdList.Get(), pBack, State, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION Src = {}, Dst = {};
  Src.pResource = pBack;
  Src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  Dst.pResource = pReadback.Get();
  Dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  Dst.PlacedFootprint = Footprint;
  m_pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  Barrier(m_pCmdList.Get(), pBack, D3D12_RESOURCE_STATE_COPY_SOURCE, State);
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
  memset(pDst, 0, (size_t)nWidth * nHeight * 4);
  for (int y = 0; y < h; y++)
    memcpy(pDst + (size_t)y * nWidth * 4, pData + Footprint.Offset + (size_t)y * Footprint.Footprint.RowPitch, (size_t)w * 4);
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
