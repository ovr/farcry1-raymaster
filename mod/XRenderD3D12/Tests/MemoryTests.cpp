// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: MemoryTests.cpp
//	Description: The renderer's GPU memory (D3D12Memory.h): an allocator
//	with the renderer's flags on the test device, CD3D12Resource's placed
//	and committed resources, their ranges, their lifetime against the
//	allocator's, the statistics the overlay and d3d12_MemoryDump read, the
//	names of what is still held and the per frame churn.
//
//////////////////////////////////////////////////////////////////////

#include <d3d12.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "D3D12Memory.h"
#include "GpuHarness.h"
#include "TestFramework.h"

namespace
{
  using Microsoft::WRL::ComPtr;

  // One owner per allocation: a copy would let DeferredRelease free memory another copy still draws with.
  static_assert(!std::is_copy_constructible_v<CD3D12Resource> && std::is_move_constructible_v<CD3D12Resource>);

  ComPtr<D3D12MA::Allocator> MakeAllocator()
  {
    ComPtr<D3D12MA::Allocator> p;
    CHECK(SUCCEEDED(CreateD3D12Allocator(XrTest::Gpu().Device(), XrTest::Gpu().Adapter(), p)));
    return p;
  }

  D3D12_RESOURCE_DESC TextureDesc(UINT nSize, DXGI_FORMAT Format, D3D12_RESOURCE_FLAGS eFlags)
  {
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = Desc.Height = nSize;
    Desc.DepthOrArraySize = Desc.MipLevels = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = eFlags;
    return Desc;
  }

  CD3D12Resource Make(D3D12MA::Allocator *pAllocator, D3D12_HEAP_TYPE eHeap, const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES eState,
                      ED3D12Memory eMemory, const D3D12_CLEAR_VALUE *pClear = nullptr)
  {
    CD3D12Resource r;
    CHECK(SUCCEEDED(CD3D12Resource::Create(pAllocator, eHeap, Desc, eState, pClear, eMemory, L"XRenderD3D12Tests memory", r)));
    CHECK(r && r.Get());
    return r;
  }
}

// Placed resources lie in the allocator's heaps, committed ones in heaps of their own; each in the heap type asked for.
XR_TEST(Memory_PlacedAndCommitted)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  const CD3D12Resource Placed = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc(64 << 10), D3D12_RESOURCE_STATE_COMMON,
                                     ED3D12Memory::Placed);
  const CD3D12Resource Committed = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                        D3D12BufferDesc(64 << 10, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_COMMON,
                                        ED3D12Memory::Committed);
  const CD3D12Resource Texture = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, TextureDesc(64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE),
                                      D3D12_RESOURCE_STATE_COPY_DEST, ED3D12Memory::Placed);
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  const CD3D12Resource Target = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT,
                                     TextureDesc(64, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                     D3D12_RESOURCE_STATE_RENDER_TARGET, ED3D12Memory::Committed, &Clear);
  const CD3D12Resource Readback = Make(pAllocator.Get(), D3D12_HEAP_TYPE_READBACK, D3D12BufferDesc(4096), D3D12_RESOURCE_STATE_COPY_DEST,
                                       ED3D12Memory::Committed);
  CHECK(Placed.GetAllocation()->GetHeap() != nullptr);
  CHECK(Texture.GetAllocation()->GetHeap() != nullptr);
  CHECK(Committed.GetAllocation()->GetHeap() == nullptr);
  CHECK(Target.GetAllocation()->GetHeap() == nullptr);
  CHECK(Readback.GetAllocation()->GetHeap() == nullptr);
  CHECK(Placed.GetAllocation()->GetSize() >= (64u << 10));
  CHECK(Placed->GetDesc().Width == (64u << 10));
  D3D12_HEAP_PROPERTIES Props = {};
  D3D12_HEAP_FLAGS eFlags = D3D12_HEAP_FLAG_NONE;
  CHECK(SUCCEEDED(Readback->GetHeapProperties(&Props, &eFlags)) && Props.Type == D3D12_HEAP_TYPE_READBACK);
  CHECK(SUCCEEDED(Target->GetHeapProperties(&Props, &eFlags)) && Props.Type == D3D12_HEAP_TYPE_DEFAULT);
  D3D12_HEAP_DESC HeapDesc = Placed.GetAllocation()->GetHeap()->GetDesc();
  CHECK(HeapDesc.Properties.Type == D3D12_HEAP_TYPE_DEFAULT);
}

// The placed buffers of one heap do not share memory, also after some of them went and others came in their place.
XR_TEST(Memory_NoOverlap)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  std::vector<CD3D12Resource> Buffers;
  for (int i = 0; i < 24; i++)
    Buffers.push_back(Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc((UINT64)(1 + i % 5) * 48 * 1024), D3D12_RESOURCE_STATE_COMMON,
                           ED3D12Memory::Placed));
  for (size_t i = 0; i < Buffers.size(); i += 3)
    Buffers[i].Reset();
  for (int i = 0; i < 8; i++)
    Buffers.push_back(Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc((UINT64)(1 + i % 3) * 40 * 1024), D3D12_RESOURCE_STATE_COMMON,
                           ED3D12Memory::Placed));
  int nPairs = 0;
  for (size_t i = 0; i < Buffers.size(); i++)
  {
    for (size_t j = i + 1; j < Buffers.size(); j++)
    {
      const D3D12MA::Allocation *a = Buffers[i].GetAllocation(), *b = Buffers[j].GetAllocation();
      if (!a || !b || a->GetHeap() != b->GetHeap())
        continue;
      nPairs++;
      const bool bApart = a->GetOffset() + a->GetSize() <= b->GetOffset() || b->GetOffset() + b->GetSize() <= a->GetOffset();
      CHECK_MSG(bApart, "buffers " + std::to_string(i) + " and " + std::to_string(j));
    }
  }
  CHECK(nPairs > 0);
}

// A released placed resource's memory comes again: a level's index buffers and textures come and go without new heaps.
XR_TEST(Memory_Reuse)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  ID3D12Heap *pHeap = nullptr;
  UINT64 nOffset = 0;
  for (int i = 0; i < 64; i++)
  {
    CD3D12Resource r = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc(256 << 10), D3D12_RESOURCE_STATE_COMMON, ED3D12Memory::Placed);
    if (!i)
    {
      pHeap = r.GetAllocation()->GetHeap();
      nOffset = r.GetAllocation()->GetOffset();
    }
    CHECK(r.GetAllocation()->GetHeap() == pHeap);
    CHECK(r.GetAllocation()->GetOffset() == nOffset);
  }
  D3D12MA::TotalStatistics Stats = {};
  pAllocator->CalculateStatistics(&Stats);
  CHECK(Stats.Total.Stats.BlockCount == 1);
  CHECK(Stats.Total.Stats.AllocationCount == 0);
}

// A resource outlives its allocator's owner (a release after the device's Shutdown): it holds the allocator.
XR_TEST(Memory_OutlivesAllocator)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  CD3D12Resource r = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc(4096), D3D12_RESOURCE_STATE_COMMON, ED3D12Memory::Placed);
  D3D12MA::Allocator *pRaw = pAllocator.Get();
  pAllocator.Reset();
  D3D12MA::TotalStatistics Stats = {};
  pRaw->CalculateStatistics(&Stats);
  CHECK(Stats.Total.Stats.AllocationCount == 1);
  CD3D12Resource Other;
  Other = std::move(r);
  CHECK(!r && r == nullptr);
  CHECK(Other.Get() != nullptr && !(Other == nullptr));
  Other.Reset();
  CHECK(Other == nullptr && !Other.Get());
}

// An upload buffer is mapped and written by the CPU as the frame's ring and the static uploads are.
XR_TEST(Memory_UploadMapped)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  CD3D12Resource r = Make(pAllocator.Get(), D3D12_HEAP_TYPE_UPLOAD, D3D12BufferDesc(1 << 20), D3D12_RESOURCE_STATE_GENERIC_READ,
                          ED3D12Memory::Placed);
  void *pCPU = nullptr;
  const D3D12_RANGE NoRead = { 0, 0 };
  CHECK(SUCCEEDED(r->Map(0, &NoRead, &pCPU)) && pCPU);
  if (pCPU)
  {
    std::memset(pCPU, 0x5A, 1 << 20);
    CHECK(((const unsigned char *)pCPU)[(1 << 20) - 1] == 0x5A);
    r->Unmap(0, nullptr);
  }
  CHECK(r->GetGPUVirtualAddress() != 0);
}

// The statistics count what lives (the overlay's lines) and the dump is JSON with its totals (d3d12_MemoryDump).
XR_TEST(Memory_Statistics)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  std::vector<CD3D12Resource> Live;
  UINT64 nBytes = 0;
  for (int i = 0; i < 6; i++)
  {
    Live.push_back(Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc((UINT64)(i + 1) << 16), D3D12_RESOURCE_STATE_COMMON,
                        i % 2 ? ED3D12Memory::Committed : ED3D12Memory::Placed));
    nBytes += Live.back().GetAllocation()->GetSize();
  }
  SD3D12MemoryStats M;
  ReadMemoryStats(*pAllocator.Get(), M, true);
  CHECK(M.nAllocations == 6);
  CHECK(M.nAllocationBytes == nBytes);
  CHECK(M.nHeaps >= 4 && M.nHeapBytes >= M.nAllocationBytes);
  CHECK(M.nTypeBytes[0] == nBytes && M.nTypeBytes[1] == 0 && M.nTypeBytes[2] == 0);
  CHECK(M.nLocalBudget > 0);
  SD3D12MemoryStats Cheap;
  ReadMemoryStats(*pAllocator.Get(), Cheap, false);
  CHECK(Cheap.nHeaps == 0 && Cheap.nAllocations == 0 && Cheap.nLocalBudget > 0);

  std::string Json = MemoryStatsJson(*pAllocator.Get());
  // The byte order mark in UTF-8 (AMD's GpuMemDumpVis.py reads the file so), then the object.
  CHECK(Json.rfind("\xEF\xBB\xBF{", 0) == 0);
  while (!Json.empty() && isspace((unsigned char)Json.back()))
    Json.pop_back();
  CHECK(!Json.empty() && Json.back() == '}');
  CHECK(Json.find("\"Total\"") != std::string::npos);
  CHECK(Json.find("XRenderD3D12Tests memory") != std::string::npos);
}

// The ray scene's pools as CD3D12RayScene::AllocPool keeps them: aligned ranges apart, and freed ones join again (the
// whole pool fits).
XR_TEST(Memory_VirtualPool)
{
  constexpr UINT64 POOL = 64ull << 20, ALIGN = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
  D3D12MA::VIRTUAL_BLOCK_DESC BlockDesc = {};
  BlockDesc.Size = POOL;
  ComPtr<D3D12MA::VirtualBlock> pBlock;
  CHECK(SUCCEEDED(D3D12MA::CreateVirtualBlock(&BlockDesc, &pBlock)));
  if (!pBlock)
    return;
  struct SRange
  {
    D3D12MA::VirtualAllocation Alloc;
    UINT64 nOffset, nSize;
  };
  std::vector<SRange> Ranges;
  for (int i = 0; i < 300; i++)
  {
    D3D12MA::VIRTUAL_ALLOCATION_DESC Desc = {};
    Desc.Size = (UINT64)(1 + i * 7919 % 97) * 2304;
    Desc.Alignment = ALIGN;
    SRange r = { {}, 0, Desc.Size };
    if (SUCCEEDED(pBlock->Allocate(&Desc, &r.Alloc, &r.nOffset)))
      Ranges.push_back(r);
  }
  CHECK(Ranges.size() == 300);
  std::vector<SRange> Sorted = Ranges;
  std::sort(Sorted.begin(), Sorted.end(), [](const SRange &a, const SRange &b) { return a.nOffset < b.nOffset; });
  for (size_t i = 0; i < Sorted.size(); i++)
  {
    CHECK(Sorted[i].nOffset % ALIGN == 0);
    CHECK(Sorted[i].nOffset + Sorted[i].nSize <= POOL);
    if (i)
      CHECK(Sorted[i - 1].nOffset + Sorted[i - 1].nSize <= Sorted[i].nOffset);
  }
  for (size_t i = 0; i < Ranges.size(); i += 2)
    pBlock->FreeAllocation(Ranges[i].Alloc);
  for (size_t i = 1; i < Ranges.size(); i += 2)
    pBlock->FreeAllocation(Ranges[i].Alloc);
  CHECK(pBlock->IsEmpty());
  D3D12MA::VIRTUAL_ALLOCATION_DESC Whole = {};
  Whole.Size = POOL;
  Whole.Alignment = ALIGN;
  D3D12MA::VirtualAllocation Alloc = {};
  UINT64 nOffset = 1;
  CHECK(SUCCEEDED(pBlock->Allocate(&Whole, &Alloc, &nOffset)) && nOffset == 0);
  pBlock->Clear();
}

// What is still held is named by its owner, the most first (the device's shutdown and level loads log it); unnamed
// allocations are left out and a freed one is gone from the list.
XR_TEST(Memory_LiveAllocationNames)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  std::vector<CD3D12Resource> Live(6);
  const wchar_t *Names[6] = { L"XRenderD3D12 tests B", L"XRenderD3D12 tests A", L"XRenderD3D12 tests A", nullptr,
                              L"XRenderD3D12 tests A", L"XRenderD3D12 tests B" };
  for (int i = 0; i < 6; i++)
    CHECK(SUCCEEDED(CD3D12Resource::Create(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, D3D12BufferDesc(4096), D3D12_RESOURCE_STATE_COMMON,
                                           nullptr, i == 2 ? ED3D12Memory::Committed : ED3D12Memory::Placed, Names[i], Live[i])));
  auto Found = LiveAllocationNames(*pAllocator.Get());
  CHECK(Found.size() == 2);
  if (Found.size() == 2)
  {
    CHECK(Found[0].first == "XRenderD3D12 tests A" && Found[0].second == 3);
    CHECK(Found[1].first == "XRenderD3D12 tests B" && Found[1].second == 2);
  }
  Live[0].Reset();
  Live[5].Reset();
  Found = LiveAllocationNames(*pAllocator.Get());
  CHECK(Found.size() == 1 && Found[0].second == 3);
  Live.clear();
  CHECK(LiveAllocationNames(*pAllocator.Get()).empty());
}

// The path tracer's pairs that take turns (d3d12_PTAlias): an alias lies in its base's memory, which it keeps when the
// base goes, and is counted once (the overlay's bytes, the churn).
XR_TEST(Memory_Alias)
{
  ComPtr<D3D12MA::Allocator> pAllocator = MakeAllocator();
  const D3D12_RESOURCE_DESC Desc = TextureDesc(256, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  CD3D12Resource Base = Make(pAllocator.Get(), D3D12_HEAP_TYPE_DEFAULT, Desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ED3D12Memory::Aliased);
  CHECK(Base.GetAllocation()->GetHeap() != nullptr && !Base.IsAlias());
  CHECK(Base.GetBytes() == Base.GetAllocation()->GetSize() && Base.GetBytes() >= 256ull * 256 * 8);
  CD3D12Resource Alias;
  CHECK(SUCCEEDED(CD3D12Resource::CreateAlias(Base, Desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, Alias)));
  CHECK(Alias && Alias.IsAlias() && Alias.Get() && Alias.Get() != Base.Get());
  CHECK(Alias.GetAllocation() == Base.GetAllocation() && Alias.GetBytes() == 0);
  D3D12MA::TotalStatistics Stats = {};
  pAllocator->CalculateStatistics(&Stats);
  CHECK(Stats.Total.Stats.AllocationCount == 1);
  // Larger than the memory: refused.
  CD3D12Resource Big;
  CHECK(FAILED(CD3D12Resource::CreateAlias(Base, TextureDesc(512, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS, Big)) && !Big);
  Base.Reset();
  pAllocator->CalculateStatistics(&Stats);
  CHECK(Stats.Total.Stats.AllocationCount == 1);
  CHECK(Alias.Get() && Alias->GetDesc().Width == 256);
  Alias.Reset();
  pAllocator->CalculateStatistics(&Stats);
  CHECK(Stats.Total.Stats.AllocationCount == 0);
}

// The overlay's churn counts every resource made and freed, from several threads at once (loading threads make
// resources), and starts again from zero after each Take.
XR_TEST(Memory_Churn)
{
  CD3D12MemoryChurn Churn;
  std::vector<std::thread> Threads;
  for (int t = 0; t < 4; t++)
    Threads.emplace_back([&Churn, t]
    {
      for (int i = 0; i < 1000; i++)
      {
        Churn.Made(256);
        if (i % 2 == t % 2)
          Churn.Freed(64);
      }
    });
  for (std::thread &t : Threads)
    t.join();
  const CD3D12MemoryChurn::SCounts c = Churn.Take();
  CHECK(c.nMade == 4000 && c.nMadeBytes == 4000ull * 256);
  CHECK(c.nFreed == 2000 && c.nFreedBytes == 2000ull * 64);
  const CD3D12MemoryChurn::SCounts Again = Churn.Take();
  CHECK(Again.nMade == 0 && Again.nFreed == 0 && Again.nMadeBytes == 0 && Again.nFreedBytes == 0);
}
