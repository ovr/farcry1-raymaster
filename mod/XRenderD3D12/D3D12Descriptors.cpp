// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Descriptors.cpp
//	Description: Descriptor heaps.
//
//	The texture table is written with CreateShaderResourceView straight
//	into the shader visible heap. A slot a draw may still read is never
//	written: a freed slot keeps its view until the GPU has finished the
//	frame it was freed in, then CollectGarbage writes the null SRV and
//	makes it free again.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"

namespace
{
  constexpr int RTV_COUNT = 256;      //!< render targets alive at once
  constexpr int DSV_COUNT = 256;
  constexpr int FIRST_FREE_SLOT = 2;  //!< after D3D12_SRV_WHITE and D3D12_SRV_NULL_CUBE

  static_assert(D3D12_SRV_WHITE < FIRST_FREE_SLOT && D3D12_SRV_NULL_CUBE < FIRST_FREE_SLOT);
}

CD3D12Descriptors::CD3D12Descriptors() = default;

CD3D12Descriptors::~CD3D12Descriptors()
{
  Shutdown();
}

bool CD3D12Descriptors::Init(ID3D12Device *pDevice)
{
  Shutdown();
  m_pDevice = pDevice;

  D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
  Desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  Desc.NumDescriptors = D3D12_MAX_TEXTURES;
  Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  HRESULT hr = pDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&m_pHeap));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateDescriptorHeap (texture table, %d) failed (0x%08X)", D3D12_MAX_TEXTURES, (unsigned)hr);
    return false;
  }
  m_pHeap->SetName(L"XRenderD3D12 texture table");
  m_nIncrement = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  m_CPUStart = m_pHeap->GetCPUDescriptorHandleForHeapStart();
  m_GPUStart = m_pHeap->GetGPUDescriptorHandleForHeapStart();

  // The whole table is valid from the start.
  for (int i = 0; i < D3D12_MAX_TEXTURES; i++)
    WriteNullSRV(i, i == D3D12_SRV_NULL_CUBE);

  m_FreeSlots.reserve(D3D12_MAX_TEXTURES);
  for (int i = D3D12_MAX_TEXTURES - 1; i >= FIRST_FREE_SLOT; i--)
    m_FreeSlots.push_back(i);

  if (!InitCPUHeap(m_RTV, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, RTV_COUNT) ||
      !InitCPUHeap(m_DSV, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, DSV_COUNT))
    return false;
  return true;
}

void CD3D12Descriptors::Shutdown()
{
  m_pHeap.Reset();
  m_FreeSlots.clear();
  m_Freed.clear();
  m_bFullLogged = false;
  m_RTV = SCPUHeap();
  m_DSV = SCPUHeap();
  m_pDevice = nullptr;
}

bool CD3D12Descriptors::InitCPUHeap(SCPUHeap &Heap, D3D12_DESCRIPTOR_HEAP_TYPE Type, int nCount)
{
  D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
  Desc.Type = Type;
  Desc.NumDescriptors = nCount;
  Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  HRESULT hr = m_pDevice->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Heap.pHeap));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateDescriptorHeap (%s, %d) failed (0x%08X)",
      Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? "RTV" : "DSV", nCount, (unsigned)hr);
    return false;
  }
  Heap.Start = Heap.pHeap->GetCPUDescriptorHandleForHeapStart();
  Heap.nIncrement = m_pDevice->GetDescriptorHandleIncrementSize(Type);
  Heap.nCount = nCount;
  Heap.Free.clear();
  Heap.Free.reserve(nCount);
  for (int i = nCount - 1; i >= 0; i--)
    Heap.Free.push_back(i);
  Heap.bFullLogged = false;
  return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Descriptors::GetSlotCPU(int nSlot) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_CPUStart;
  h.ptr += (SIZE_T)nSlot * m_nIncrement;
  return h;
}

void CD3D12Descriptors::WriteNullSRV(int nSlot, bool bCube)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
  Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (bCube)
  {
    Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    Desc.TextureCube.MipLevels = 1;
  }
  else
  {
    Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    Desc.Texture2D.MipLevels = 1;
  }
  m_pDevice->CreateShaderResourceView(nullptr, &Desc, GetSlotCPU(nSlot));
}

int CD3D12Descriptors::AllocSRV(ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc)
{
  if (!m_pHeap)
    return -1;
  if (m_FreeSlots.empty())
  {
    if (!m_bFullLogged)
      iLog->LogError("XRenderD3D12: the texture table is full (%d slots)", D3D12_MAX_TEXTURES);
    m_bFullLogged = true;
    return -1;
  }
  const int nSlot = m_FreeSlots.back();
  m_FreeSlots.pop_back();
  WriteSRV(nSlot, pResource, pDesc);
  return nSlot;
}

void CD3D12Descriptors::WriteSRV(int nSlot, ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc)
{
  if (!m_pHeap || nSlot < 0 || nSlot >= D3D12_MAX_TEXTURES)
    return;
  if (!pResource && !pDesc)
  {
    // A null resource needs a view description.
    WriteNullSRV(nSlot, nSlot == D3D12_SRV_NULL_CUBE);
    return;
  }
  m_pDevice->CreateShaderResourceView(pResource, pDesc, GetSlotCPU(nSlot));
}

void CD3D12Descriptors::FreeSRV(int nSlot)
{
  // The reserved slots belong to the table itself.
  if (!m_pHeap || nSlot < FIRST_FREE_SLOT || nSlot >= D3D12_MAX_TEXTURES)
    return;
  m_Freed.push_back({ nSlot, g_pRenderer->m_pDevice->GetFrameFence() });
}

void CD3D12Descriptors::CollectGarbage(uint64 nCompletedFence)
{
  size_t nKept = 0;
  for (size_t i = 0; i < m_Freed.size(); i++)
  {
    const SFreedSlot Freed = m_Freed[i];
    if (Freed.nFence <= nCompletedFence)
    {
      WriteNullSRV(Freed.nSlot, false);
      m_FreeSlots.push_back(Freed.nSlot);
    }
    else
      m_Freed[nKept++] = Freed;
  }
  m_Freed.resize(nKept);
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Descriptors::AllocCPU(SCPUHeap &Heap, const char *szWhat)
{
  D3D12_CPU_DESCRIPTOR_HANDLE h = {};
  if (!Heap.pHeap)
    return h;
  if (Heap.Free.empty())
  {
    if (!Heap.bFullLogged)
      iLog->LogError("XRenderD3D12: no %s left (%d)", szWhat, Heap.nCount);
    Heap.bFullLogged = true;
    return h;
  }
  const int nIndex = Heap.Free.back();
  Heap.Free.pop_back();
  h.ptr = Heap.Start.ptr + (SIZE_T)nIndex * Heap.nIncrement;
  return h;
}

void CD3D12Descriptors::FreeCPU(SCPUHeap &Heap, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
  // A command list copies RTVs and DSVs when it records OMSetRenderTargets, so they can be reused
  // at once.
  if (!Heap.pHeap || h.ptr < Heap.Start.ptr)
    return;
  const SIZE_T nIndex = (h.ptr - Heap.Start.ptr) / Heap.nIncrement;
  if (nIndex >= (SIZE_T)Heap.nCount)
    return;
  Heap.Free.push_back((int)nIndex);
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Descriptors::AllocRTV()
{
  return AllocCPU(m_RTV, "RTV");
}

void CD3D12Descriptors::FreeRTV(D3D12_CPU_DESCRIPTOR_HANDLE h)
{
  FreeCPU(m_RTV, h);
}

D3D12_CPU_DESCRIPTOR_HANDLE CD3D12Descriptors::AllocDSV()
{
  return AllocCPU(m_DSV, "DSV");
}

void CD3D12Descriptors::FreeDSV(D3D12_CPU_DESCRIPTOR_HANDLE h)
{
  FreeCPU(m_DSV, h);
}

ID3D12DescriptorHeap *CD3D12Descriptors::GetHeap() const
{
  return m_pHeap.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE CD3D12Descriptors::GetTableStart() const
{
  return m_GPUStart;
}
