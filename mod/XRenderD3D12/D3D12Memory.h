// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Memory.h
//	Description: The renderer's GPU memory, all of it from AMD's D3D12
//	Memory Allocator (third_party/d3d12-memory-allocator, one allocator of
//	the device: CD3D12Device::CreateResource).
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
#include <wrl/client.h>

enum class ED3D12Memory
{
  Placed,
  Committed,
};

//! The allocator's flags. Small buffers placed too: a committed resource costs the kernel a few hundred microseconds to
//! make and to free, and the terrain makes index buffers every frame outdoors.
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

//! A resource with its allocation, which owns it: where a ComPtr<ID3D12Resource> held one the renderer made, so that
//! releasing it (DeferredRelease, Reset) gives back the memory too. A ComPtr may still hold the resource itself to keep
//! its address; the memory goes with the allocation.
class CD3D12Resource
{
public:
  CD3D12Resource() = default;
  CD3D12Resource(const CD3D12Resource &) = default;
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
  friend class CD3D12Device;
  // Declared first, so released last: an allocation gives its memory back to the allocator, which a late release
  // (after the device's Shutdown) would find gone.
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_pAllocator;
  Microsoft::WRL::ComPtr<D3D12MA::Allocation> m_pAlloc;
};
