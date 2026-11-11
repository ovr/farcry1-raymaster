// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Memory.h
//	Description: The renderer's GPU memory from AMD's D3D12 Memory
//	Allocator (third_party/d3d12-memory-allocator, one allocator of the
//	device: CD3D12Device::CreateResource).
//
//	A resource is made with its memory and goes with it. Placed: in one of
//	the allocator's heaps, its memory as the last user left it (a resource
//	written whole before it is read). Committed: a heap of its own, zeroed,
//	for what is read before it is written (targets, histories, counters).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <D3D12MemAlloc.h>
#include <cstdint>
#include <string>
#include <wrl/client.h>

enum class ED3D12Memory
{
  Placed,
  Committed,
};

//! Small buffers placed too: a committed resource costs the kernel a few hundred microseconds to make and to free,
//! and the terrain makes index buffers every frame outdoors.
inline constexpr D3D12MA::ALLOCATOR_FLAGS D3D12_ALLOCATOR_FLAGS =
  D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS | D3D12MA::ALLOCATOR_FLAG_DONT_PREFER_SMALL_BUFFERS_COMMITTED;

//! The allocator's memory for the overlay (r_Stats 2, r_DisplayInfo; CD3D12Device::GetMemoryStats).
struct SD3D12MemoryStats
{
  uint64_t nLocalUsage = 0, nLocalBudget = 0;     //!< the adapter's own memory: the process' use and how much it may use
  uint64_t nSharedUsage = 0, nSharedBudget = 0;   //!< the system memory the GPU reads (upload and read back heaps)
  uint32_t nHeaps = 0, nAllocations = 0;          //!< heaps (committed resources among them) and resources in them
  uint64_t nHeapBytes = 0, nAllocationBytes = 0;
  uint64_t nTypeBytes[3] = {};                    //!< the resources' bytes in the default, upload and read back heaps
};

inline D3D12_RESOURCE_DESC D3D12BufferDesc(uint64_t nBytes, D3D12_RESOURCE_FLAGS eFlags = D3D12_RESOURCE_FLAG_NONE)
{
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = nBytes;
  Desc.Height = Desc.DepthOrArraySize = Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = eFlags;
  return Desc;
}

//! A resource and its allocation, which owns it: releasing this (DeferredRelease, Reset) frees the memory too. A bare
//! ComPtr<ID3D12Resource> keeps only the object: a placed resource's memory goes with the allocation.
class CD3D12Resource
{
public:
  CD3D12Resource() = default;
  CD3D12Resource(const CD3D12Resource &) = delete;
  CD3D12Resource(CD3D12Resource &&) = default;
  //! By a swap: the old allocation goes before the reference to its allocator.
  CD3D12Resource &operator=(CD3D12Resource Other)
  {
    m_pAllocator.Swap(Other.m_pAllocator);
    m_pAlloc.Swap(Other.m_pAlloc);
    return *this;
  }

  //! szName names the allocation (the allocator's statistics), not the resource. D3D12 error codes.
  static HRESULT Create(D3D12MA::Allocator *pAllocator, D3D12_HEAP_TYPE eHeap, const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES eState,
                        const D3D12_CLEAR_VALUE *pClear, ED3D12Memory eMemory, const wchar_t *szName, CD3D12Resource &Out)
  {
    Out.Reset();
    if (!pAllocator)
      return E_FAIL;
    D3D12MA::ALLOCATION_DESC AllocDesc = {};
    AllocDesc.HeapType = eHeap;
    AllocDesc.Flags = eMemory == ED3D12Memory::Committed ? D3D12MA::ALLOCATION_FLAG_COMMITTED : D3D12MA::ALLOCATION_FLAG_NONE;
    const HRESULT hr = pAllocator->CreateResource(&AllocDesc, &Desc, eState, pClear, &Out.m_pAlloc, IID_NULL, nullptr);
    if (FAILED(hr))
      return hr;
    Out.m_pAllocator = pAllocator;
    if (szName)
      Out.m_pAlloc->SetName(szName);
    return hr;
  }

  ID3D12Resource *Get() const { return m_pAlloc ? m_pAlloc->GetResource() : nullptr; }
  ID3D12Resource *operator->() const { return Get(); }
  explicit operator bool() const { return m_pAlloc != nullptr; }
  bool operator==(std::nullptr_t) const { return m_pAlloc == nullptr; }
  void Reset()
  {
    m_pAlloc.Reset();
    m_pAllocator.Reset();
  }
  D3D12MA::Allocation *GetAllocation() const { return m_pAlloc.Get(); }

private:
  // Declared first, so released last: an allocation gives its memory back to the allocator, which a late release
  // (after the device's Shutdown) would find gone.
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_pAllocator;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> m_pAlloc;
};

inline HRESULT CreateD3D12Allocator(ID3D12Device *pDevice, IDXGIAdapter *pAdapter, Microsoft::WRL::ComPtr<D3D12MA::Allocator> &Out)
{
  D3D12MA::ALLOCATOR_DESC Desc = {};
  Desc.Flags = D3D12_ALLOCATOR_FLAGS;
  Desc.pDevice = pDevice;
  Desc.pAdapter = pAdapter;
  return D3D12MA::CreateAllocator(&Desc, &Out);
}

//! bHeaps walks every heap and allocation (CalculateStatistics): the overlay asks twice a second at most.
inline void ReadMemoryStats(D3D12MA::Allocator &Allocator, SD3D12MemoryStats &Out, bool bHeaps)
{
  Out = SD3D12MemoryStats();
  D3D12MA::Budget Local = {}, Shared = {};
  Allocator.GetBudget(&Local, &Shared);
  Out.nLocalUsage = Local.UsageBytes;
  Out.nLocalBudget = Local.BudgetBytes;
  Out.nSharedUsage = Shared.UsageBytes;
  Out.nSharedBudget = Shared.BudgetBytes;
  if (!bHeaps)
    return;
  D3D12MA::TotalStatistics Stats = {};
  Allocator.CalculateStatistics(&Stats);
  Out.nHeaps = Stats.Total.Stats.BlockCount;
  Out.nAllocations = Stats.Total.Stats.AllocationCount;
  Out.nHeapBytes = Stats.Total.Stats.BlockBytes;
  Out.nAllocationBytes = Stats.Total.Stats.AllocationBytes;
  for (int i = 0; i < 3; i++)
    Out.nTypeBytes[i] = Stats.HeapType[i].Stats.AllocationBytes;
}

//! UTF-8 with the byte order mark the allocator writes first (AMD's GpuMemDumpVis.py expects it); empty on failure.
inline std::string MemoryStatsJson(D3D12MA::Allocator &Allocator)
{
  WCHAR *szJson = nullptr;
  Allocator.BuildStatsString(&szJson, TRUE);
  if (!szJson)
    return {};
  const int nBytes = WideCharToMultiByte(CP_UTF8, 0, szJson, -1, nullptr, 0, nullptr, nullptr);
  std::string Json(nBytes > 1 ? nBytes - 1 : 0, '\0');
  if (nBytes > 1)
    WideCharToMultiByte(CP_UTF8, 0, szJson, -1, Json.data(), nBytes, nullptr, nullptr);
  Allocator.FreeStatsString(szJson);
  return Json;
}
