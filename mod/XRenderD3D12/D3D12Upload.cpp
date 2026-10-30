// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Upload.cpp
//	Description: GPU buffers and textures and how data gets into them.
//
//	Per frame memory: a ring per frame slot, an upload heap buffer mapped
//	for good. When a frame needs more, the slot gets extra blocks; when the
//	slot starts again (its frame finished on the GPU) they are released and
//	the ring is made big enough for all of it.
//
//	Upload batch: one command list on the device's direct queue, recorded
//	with a command allocator of its own per batch. The data goes through
//	staging blocks (upload heap, suballocated). Flush executes the batch
//	without a signal of its own: the allocator and the staging blocks wait
//	for the next fence value the queue signals (CD3D12Device::GetFrameFence,
//	which comes after the batch), so that the frame's fence keeps covering
//	the frame's own command list. Buffers end every batch in BUFFER_STATE
//	(their draws' states and the shader resource states of the rays),
//	textures in PIXEL_SHADER_RESOURCE.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <algorithm>
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Descriptors.h"
#include "D3D12Upload.h"

using Microsoft::WRL::ComPtr;

namespace
{
  constexpr uint64 BLOCK_ALIGN = 64 * 1024;                 //!< size granularity of the upload heap blocks
  constexpr uint64 STAGING_BLOCK = 4 * 1024 * 1024;         //!< staging blocks; larger uploads get a block of their own
  constexpr uint64 MAX_BATCH_BYTES = 64 * 1024 * 1024;      //!< the open batch is executed when it would grow beyond
  constexpr uint64 MAX_IN_FLIGHT_BYTES = 256 * 1024 * 1024; //!< more staging in flight waits for the GPU
  constexpr size_t MAX_FREE_STAGING = 16;                   //!< staging blocks kept for reuse

  constexpr D3D12_RESOURCE_STATES BUFFER_STATE = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  constexpr D3D12_RESOURCE_STATES TEXTURE_STATE = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  //! n rounded up to a multiple of nAlign (any nAlign > 0).
  constexpr uint64 AlignUp(uint64 n, uint64 nAlign)
  {
    return nAlign > 1 ? (n + nAlign - 1) / nAlign * nAlign : n;
  }

  bool IsBlockCompressed(DXGI_FORMAT Format)
  {
    return (Format >= DXGI_FORMAT_BC1_TYPELESS && Format <= DXGI_FORMAT_BC5_SNORM) ||
      (Format >= DXGI_FORMAT_BC6H_TYPELESS && Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
  }

  int MaxMips(int nWidth, int nHeight)
  {
    int nMips = 1;
    for (int n = std::max(nWidth, nHeight); n > 1; n >>= 1)
      nMips++;
    return nMips;
  }

  D3D12_RESOURCE_DESC BufferDesc(uint64 nSize)
  {
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = nSize;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_UNKNOWN;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return Desc;
  }

  D3D12_RESOURCE_DESC TextureDesc(int nWidth, int nHeight, int nArraySize, int nMips, DXGI_FORMAT Format, D3D12_RESOURCE_FLAGS Flags)
  {
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = (UINT64)nWidth;
    Desc.Height = (UINT)nHeight;
    Desc.DepthOrArraySize = (UINT16)nArraySize;
    Desc.MipLevels = (UINT16)nMips;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    Desc.Flags = Flags;
    return Desc;
  }

  D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE Type)
  {
    D3D12_HEAP_PROPERTIES Heap = {};
    Heap.Type = Type;
    Heap.CreationNodeMask = 1;
    Heap.VisibleNodeMask = 1;
    return Heap;
  }

  //! nRows rows of nRowSize bytes from Src (nPitch apart; 0: packed) to pDst (nDstPitch apart).
  void CopyRows(uint8 *pDst, uint64 nDstPitch, const SD3D12SubData &Src, UINT nRows, UINT64 nRowSize)
  {
    const uint64 nSrcPitch = Src.nPitch ? Src.nPitch : nRowSize;
    const size_t nCopy = (size_t)std::min<uint64>(nRowSize, nSrcPitch);
    const uint8 *pSrc = (const uint8 *)Src.pData;
    for (UINT r = 0; r < nRows; r++)
      memcpy(pDst + r * nDstPitch, pSrc + r * nSrcPitch, nCopy);
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC TextureSRVDesc(DXGI_FORMAT Format, int nMips, bool bCube, int nArraySize = 1)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC Desc = {};
    Desc.Format = Format;
    Desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (bCube)
    {
      Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      Desc.TextureCube.MostDetailedMip = 0;
      Desc.TextureCube.MipLevels = (UINT)nMips;
    }
    else if (nArraySize > 1)
    {
      Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      Desc.Texture2DArray.MostDetailedMip = 0;
      Desc.Texture2DArray.MipLevels = (UINT)nMips;
      Desc.Texture2DArray.FirstArraySlice = 0;
      Desc.Texture2DArray.ArraySize = (UINT)nArraySize;
    }
    else
    {
      Desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      Desc.Texture2D.MostDetailedMip = 0;
      Desc.Texture2D.MipLevels = (UINT)nMips;
    }
    return Desc;
  }
}

CD3D12Upload::CD3D12Upload() = default;

CD3D12Upload::~CD3D12Upload()
{
  Shutdown();
}

bool CD3D12Upload::Init(ID3D12Device *pDevice, uint32 nFrameRingSize)
{
  Shutdown();
  m_pDevice = pDevice;
  m_nFrameRingSize = (uint32)AlignUp(std::max<uint32>(nFrameRingSize, (uint32)BLOCK_ALIGN), BLOCK_ALIGN);
  for (SFrameSlot &Slot : m_Slots)
  {
    if (!CreateUploadBlock(m_nFrameRingSize, Slot.Ring, L"XRenderD3D12 frame memory"))
      return false;
  }
  m_nSlot = 0;
  m_nCurBlock = -1;
  m_nCurOffset = 0;
  return true;
}

void CD3D12Upload::Shutdown()
{
  // The GPU is idle by now (the device waits before the subsystems go).
  if (m_bOpen && m_pCmdList)
    m_pCmdList->Close();
  m_bOpen = false;
  m_nOpenAllocator = -1;
  m_nCurStaging = -1;
  m_pCmdList.Reset();
  m_Allocators.clear();
  m_Staging.clear();
  m_FreeStaging.clear();
  m_nBatchBytes = 0;
  m_nInFlightBytes = 0;
  for (SFrameSlot &Slot : m_Slots)
    Slot = SFrameSlot();
  m_Scratch.clear();
  m_Scratch.shrink_to_fit();
  m_nCurBlock = -1;
  m_nCurOffset = 0;
  m_Buffers.clear();
  m_pDevice = nullptr;
}

bool CD3D12Upload::CreateUploadBlock(uint64 nSize, SUploadBlock &Block, const wchar_t *szName)
{
  const D3D12_HEAP_PROPERTIES Heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC Desc = BufferDesc(nSize);
  ComPtr<ID3D12Resource> pResource;
  HRESULT hr = m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_GENERIC_READ,
    nullptr, IID_PPV_ARGS(&pResource));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (upload heap, %llu bytes) failed (0x%08X)", (unsigned long long)nSize, (unsigned)hr);
    return false;
  }
  void *pCPU = nullptr;
  const D3D12_RANGE NoRead = { 0, 0 };
  hr = pResource->Map(0, &NoRead, &pCPU);
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: Map (upload heap, %llu bytes) failed (0x%08X)", (unsigned long long)nSize, (unsigned)hr);
    return false;
  }
  if (szName)
    pResource->SetName(szName);
  Block.pResource = std::move(pResource);
  Block.pCPU = (uint8 *)pCPU;
  Block.nGPU = Block.pResource->GetGPUVirtualAddress();
  Block.nSize = nSize;
  return true;
}

//////////////////////////////////////////////////////////////////////////
// Per frame memory

void CD3D12Upload::BeginFrame(int nSlot)
{
  if (nSlot < 0 || nSlot >= D3D12_FRAMES)
    return;
  m_nSlot = nSlot;
  SFrameSlot &Slot = m_Slots[nSlot];
  if (!Slot.Extra.empty() && m_pDevice)
  {
    // The slot's last frame needed more than its ring: one ring for all of it (with some room).
    // The GPU has finished that frame, so the old blocks can go at once.
    const uint64 nSize = AlignUp(std::max(Slot.Ring.nSize, Slot.nUsed + Slot.nUsed / 4), BLOCK_ALIGN);
    Slot.Extra.clear();
    SUploadBlock Ring;
    if (CreateUploadBlock(nSize, Ring, L"XRenderD3D12 frame memory"))
    {
      Slot.Ring = std::move(Ring);
      iLog->Log("XRenderD3D12: frame memory of slot %d grown to %llu KB", nSlot, (unsigned long long)(nSize / 1024));
    }
  }
  Slot.nUsed = 0;
  m_nCurBlock = -1;
  m_nCurOffset = 0;
}

SD3D12FrameAlloc CD3D12Upload::AllocFrame(uint32 nSize, uint32 nAlign)
{
  SFrameSlot &Slot = m_Slots[m_nSlot];
  SUploadBlock *pBlock = m_nCurBlock < 0 ? &Slot.Ring : &Slot.Extra[m_nCurBlock];
  uint64 nOffset = AlignUp(m_nCurOffset, nAlign);
  if (!pBlock->pResource || nOffset + nSize > pBlock->nSize)
  {
    SUploadBlock Block;
    const uint64 nBlockSize = AlignUp(std::max<uint64>(m_nFrameRingSize, nSize), BLOCK_ALIGN);
    if (!m_pDevice || !CreateUploadBlock(nBlockSize, Block, L"XRenderD3D12 frame memory (extra)"))
    {
      // Memory the GPU never reads, so that the caller can still write.
      if (!m_bFrameFailLogged)
        iLog->LogError("XRenderD3D12: no per frame memory for %u bytes", nSize);
      m_bFrameFailLogged = true;
      if (m_Scratch.size() < nSize)
        m_Scratch.resize(nSize);
      SD3D12FrameAlloc Alloc = {};
      Alloc.pCPU = m_Scratch.data();
      Alloc.nGPU = Slot.Ring.nGPU;
      Alloc.pResource = Slot.Ring.pResource.Get();
      Alloc.nOffset = 0;
      return Alloc;
    }
    Slot.Extra.push_back(std::move(Block));
    m_nCurBlock = (int)Slot.Extra.size() - 1;
    pBlock = &Slot.Extra.back();
    nOffset = 0;
  }
  m_nCurOffset = nOffset + nSize;
  Slot.nUsed += nSize + (nAlign > 1 ? nAlign - 1 : 0);

  SD3D12FrameAlloc Alloc;
  Alloc.pCPU = pBlock->pCPU + nOffset;
  Alloc.nGPU = pBlock->nGPU + nOffset;
  Alloc.pResource = pBlock->pResource.Get();
  Alloc.nOffset = nOffset;
  return Alloc;
}

//////////////////////////////////////////////////////////////////////////
// Upload batch

void CD3D12Upload::RetireStaging()
{
  const uint64 nCompleted = g_pRenderer->m_pDevice->GetCompletedFence();
  size_t nKept = 0;
  for (size_t i = 0; i < m_Staging.size(); i++)
  {
    SStaging &Staging = m_Staging[i];
    if (Staging.nFence != 0 && Staging.nFence <= nCompleted)
    {
      m_nInFlightBytes -= std::min(m_nInFlightBytes, Staging.nUsed);
      if (Staging.Block.nSize == STAGING_BLOCK && m_FreeStaging.size() < MAX_FREE_STAGING)
        m_FreeStaging.push_back(std::move(Staging.Block));
      Staging.Block = SUploadBlock();
    }
    else
    {
      if (nKept != i)
        m_Staging[nKept] = std::move(Staging);
      nKept++;
    }
  }
  m_Staging.resize(nKept);
}

bool CD3D12Upload::OpenBatch()
{
  if (m_bOpen)
    return true;
  if (!m_pDevice)
    return false;

  // Level loads create textures without frames, so without signals: don't let the staging
  // memory pile up (the wait signals and waits for everything submitted).
  if (m_nInFlightBytes > MAX_IN_FLIGHT_BYTES)
    g_pRenderer->m_pDevice->WaitForGPU();
  RetireStaging();

  const uint64 nCompleted = g_pRenderer->m_pDevice->GetCompletedFence();
  int nAllocator = -1;
  for (size_t i = 0; i < m_Allocators.size(); i++)
  {
    if (m_Allocators[i].nFence <= nCompleted)
    {
      nAllocator = (int)i;
      break;
    }
  }
  HRESULT hr;
  if (nAllocator < 0)
  {
    SAllocator Allocator;
    hr = m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator.pAllocator));
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: CreateCommandAllocator (uploads) failed (0x%08X)", (unsigned)hr);
      return false;
    }
    Allocator.pAllocator->SetName(L"XRenderD3D12 uploads");
    m_Allocators.push_back(std::move(Allocator));
    nAllocator = (int)m_Allocators.size() - 1;
  }
  else
  {
    hr = m_Allocators[nAllocator].pAllocator->Reset();
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: ID3D12CommandAllocator::Reset (uploads) failed (0x%08X)", (unsigned)hr);
      return false;
    }
  }
  SAllocator &Allocator = m_Allocators[nAllocator];

  if (!m_pCmdList)
  {
    hr = m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.pAllocator.Get(), nullptr, IID_PPV_ARGS(&m_pCmdList));
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: CreateCommandList (uploads) failed (0x%08X)", (unsigned)hr);
      return false;
    }
    m_pCmdList->SetName(L"XRenderD3D12 uploads");
  }
  else
  {
    hr = m_pCmdList->Reset(Allocator.pAllocator.Get(), nullptr);
    if (FAILED(hr))
    {
      iLog->LogError("XRenderD3D12: ID3D12GraphicsCommandList::Reset (uploads) failed (0x%08X)", (unsigned)hr);
      return false;
    }
  }
  Allocator.nFence = UINT64_MAX;      // until the batch is executed
  m_nOpenAllocator = nAllocator;
  m_bOpen = true;
  m_nCurStaging = -1;
  m_nBatchBytes = 0;
  return true;
}

void CD3D12Upload::Flush()
{
  if (!m_bOpen)
    return;
  HRESULT hr = m_pCmdList->Close();
  if (SUCCEEDED(hr))
  {
    ID3D12CommandList *pLists[] = { m_pCmdList.Get() };
    g_pRenderer->m_pDevice->GetQueue()->ExecuteCommandLists(1, pLists);
  }
  else
    iLog->LogError("XRenderD3D12: ID3D12GraphicsCommandList::Close (uploads) failed (0x%08X); the uploads are lost", (unsigned)hr);

  // The next signal on the queue (the frame's end, or a wait for the GPU) comes after the batch.
  const uint64 nFence = g_pRenderer->m_pDevice->GetFrameFence();
  m_Allocators[m_nOpenAllocator].nFence = nFence;
  for (SStaging &Staging : m_Staging)
  {
    if (Staging.nFence == 0)
    {
      Staging.nFence = nFence;
      m_nInFlightBytes += Staging.nUsed;
    }
  }
  m_bOpen = false;
  m_nOpenAllocator = -1;
  m_nCurStaging = -1;
  m_nBatchBytes = 0;
}

bool CD3D12Upload::AllocStaging(uint64 nSize, uint64 nAlign, SD3D12FrameAlloc &Out)
{
  if (m_bOpen && m_nBatchBytes > 0 && m_nBatchBytes + nSize > MAX_BATCH_BYTES)
    Flush();
  if (!OpenBatch())
    return false;

  SStaging *pStaging = m_nCurStaging >= 0 ? &m_Staging[m_nCurStaging] : nullptr;
  uint64 nOffset = pStaging ? AlignUp(pStaging->nUsed, nAlign) : 0;
  if (!pStaging || nOffset + nSize > pStaging->Block.nSize)
  {
    SStaging New;
    const bool bOwnBlock = nSize > STAGING_BLOCK;
    if (!bOwnBlock && !m_FreeStaging.empty())
    {
      New.Block = std::move(m_FreeStaging.back());
      m_FreeStaging.pop_back();
    }
    else if (!CreateUploadBlock(bOwnBlock ? AlignUp(nSize, BLOCK_ALIGN) : STAGING_BLOCK, New.Block, L"XRenderD3D12 staging"))
      return false;
    m_Staging.push_back(std::move(New));
    pStaging = &m_Staging.back();
    // A block of its own takes one upload; the shared block goes on being filled.
    if (!bOwnBlock)
      m_nCurStaging = (int)m_Staging.size() - 1;
    nOffset = 0;
  }
  pStaging->nUsed = nOffset + nSize;
  m_nBatchBytes += nSize;

  Out.pCPU = pStaging->Block.pCPU + nOffset;
  Out.nGPU = pStaging->Block.nGPU + nOffset;
  Out.pResource = pStaging->Block.pResource.Get();
  Out.nOffset = nOffset;
  return true;
}

void CD3D12Upload::Barrier(ID3D12Resource *pResource, UINT nSubresource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After)
{
  D3D12_RESOURCE_BARRIER Barrier = {};
  Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  Barrier.Transition.pResource = pResource;
  Barrier.Transition.Subresource = nSubresource;
  Barrier.Transition.StateBefore = Before;
  Barrier.Transition.StateAfter = After;
  m_pCmdList->ResourceBarrier(1, &Barrier);
}

//////////////////////////////////////////////////////////////////////////
// Video memory

uint64 CD3D12Upload::GetAllocationSize(ID3D12Resource *pResource) const
{
  if (!pResource || !m_pDevice)
    return 0;
  const D3D12_RESOURCE_DESC Desc = pResource->GetDesc();
  return m_pDevice->GetResourceAllocationInfo(0, 1, &Desc).SizeInBytes;
}

void CD3D12Upload::AddVideoMemory(ID3D12Resource *pResource)
{
  m_nVideoMemory += GetAllocationSize(pResource);
}

void CD3D12Upload::SubVideoMemory(ID3D12Resource *pResource)
{
  m_nVideoMemory -= std::min(m_nVideoMemory, GetAllocationSize(pResource));
}

uint64 CD3D12Upload::GetVideoMemory() const
{
  return m_nVideoMemory;
}

//////////////////////////////////////////////////////////////////////////
// Buffers

CD3D12Buffer *CD3D12Upload::CreateBuffer(const void *pData, uint32 nSize)
{
  if (!m_pDevice)
    return nullptr;
  const D3D12_HEAP_PROPERTIES Heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC Desc = BufferDesc(std::max<uint64>(nSize, 16));
  ComPtr<ID3D12Resource> pResource;
  // Buffers start in COMMON whatever the initial state says.
  HRESULT hr = m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COMMON,
    nullptr, IID_PPV_ARGS(&pResource));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (buffer, %u bytes) failed (0x%08X)", nSize, (unsigned)hr);
    return nullptr;
  }

  SD3D12FrameAlloc Staging;
  if (pData && nSize && AllocStaging(nSize, 16, Staging))
  {
    memcpy(Staging.pCPU, pData, nSize);
    Barrier(pResource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    m_pCmdList->CopyBufferRegion(pResource.Get(), 0, Staging.pResource, Staging.nOffset, nSize);
    Barrier(pResource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, BUFFER_STATE);
  }
  else if (OpenBatch())
    Barrier(pResource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, BUFFER_STATE);

  CD3D12Buffer *pBuf = new CD3D12Buffer;
  pBuf->m_pResource = pResource.Detach();
  pBuf->m_nGPU = pBuf->m_pResource->GetGPUVirtualAddress();
  pBuf->m_nSize = nSize;
  pBuf->m_nId = m_nNextBufferId++;
  m_Buffers[pBuf->m_nGPU] = pBuf;
  AddVideoMemory(pBuf->m_pResource);
  return pBuf;
}

void CD3D12Upload::UpdateBuffer(CD3D12Buffer *pBuf, uint32 nOffset, const void *pData, uint32 nSize)
{
  if (!pBuf || !pBuf->m_pResource || !pData || nOffset >= pBuf->m_nSize)
    return;
  nSize = std::min(nSize, pBuf->m_nSize - nOffset);
  if (!nSize)
    return;
  SD3D12FrameAlloc Staging;
  if (!AllocStaging(nSize, 16, Staging))
    return;
  memcpy(Staging.pCPU, pData, nSize);
  Barrier(pBuf->m_pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, BUFFER_STATE, D3D12_RESOURCE_STATE_COPY_DEST);
  m_pCmdList->CopyBufferRegion(pBuf->m_pResource, nOffset, Staging.pResource, Staging.nOffset, nSize);
  Barrier(pBuf->m_pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, BUFFER_STATE);
  pBuf->m_nVersion++;
}

void CD3D12Upload::ReleaseBuffer(CD3D12Buffer *pBuf)
{
  if (!pBuf)
    return;
  if (pBuf->m_pResource)
  {
    SubVideoMemory(pBuf->m_pResource);
    g_pRenderer->m_pDevice->DeferredRelease(pBuf->m_pResource);
  }
  auto it = m_Buffers.find(pBuf->m_nGPU);
  if (it != m_Buffers.end() && it->second == pBuf)
    m_Buffers.erase(it);
  delete pBuf;
}

const CD3D12Buffer *CD3D12Upload::FindBuffer(D3D12_GPU_VIRTUAL_ADDRESS nGPU) const
{
  auto it = m_Buffers.upper_bound(nGPU);
  if (it == m_Buffers.begin())
    return nullptr;
  --it;
  const CD3D12Buffer *pBuf = it->second;
  return nGPU - pBuf->m_nGPU < pBuf->m_nSize ? pBuf : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Textures

void CD3D12Upload::UploadSubresources(ID3D12Resource *pResource, const D3D12_RESOURCE_DESC &Desc, int nFaces, int nMips, int nDataMips, const SD3D12SubData *pSubData)
{
  const UINT nSubresources = (UINT)(nFaces * nMips);
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Layouts(nSubresources);
  std::vector<UINT> Rows(nSubresources);
  std::vector<UINT64> RowSizes(nSubresources);
  UINT64 nTotal = 0;
  m_pDevice->GetCopyableFootprints(&Desc, 0, nSubresources, 0, Layouts.data(), Rows.data(), RowSizes.data(), &nTotal);

  SD3D12FrameAlloc Staging;
  if (nTotal == UINT64_MAX || !AllocStaging(nTotal, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, Staging))
  {
    // Undefined contents, but in the state every user expects.
    if (OpenBatch())
      Barrier(pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, TEXTURE_STATE);
    return;
  }

  for (int nFace = 0; nFace < nFaces; nFace++)
  {
    for (int nMip = 0; nMip < nMips; nMip++)
    {
      const UINT i = (UINT)(nFace * nMips + nMip);
      const SD3D12SubData &Src = pSubData[nFace * nDataMips + nMip];
      if (!Src.pData)
        continue;
      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &Layout = Layouts[i];
      CopyRows(Staging.pCPU + Layout.Offset, Layout.Footprint.RowPitch, Src, Rows[i], RowSizes[i]);

      D3D12_TEXTURE_COPY_LOCATION Dst = {};
      Dst.pResource = pResource;
      Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      Dst.SubresourceIndex = i;
      D3D12_TEXTURE_COPY_LOCATION From = {};
      From.pResource = Staging.pResource;
      From.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      From.PlacedFootprint = Layout;
      From.PlacedFootprint.Offset += Staging.nOffset;
      m_pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &From, nullptr);
    }
  }
  Barrier(pResource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, TEXTURE_STATE);
}

CD3D12Texture *CD3D12Upload::CreateTexture(int nWidth, int nHeight, int nMips, DXGI_FORMAT Format, bool bCube, const SD3D12SubData *pSubData, int nArraySize)
{
  if (!m_pDevice || nWidth <= 0 || nHeight <= 0)
    return nullptr;
  // pSubData is laid out with the caller's mip count; more mips than the chain has are left out.
  const int nDataMips = std::max(nMips, 1);
  nMips = std::min(nDataMips, MaxMips(nWidth, nHeight));
  nArraySize = bCube ? 1 : std::clamp(nArraySize, 1, D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION);
  const int nFaces = bCube ? 6 : nArraySize;

  const D3D12_HEAP_PROPERTIES Heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC Desc = TextureDesc(nWidth, nHeight, nFaces, nMips, Format, D3D12_RESOURCE_FLAG_NONE);
  ComPtr<ID3D12Resource> pResource;
  HRESULT hr = m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc,
    pSubData ? D3D12_RESOURCE_STATE_COPY_DEST : TEXTURE_STATE, nullptr, IID_PPV_ARGS(&pResource));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (texture %dx%dx%d, %d mips, format %d%s) failed (0x%08X)",
      nWidth, nHeight, nFaces, nMips, (int)Format, bCube ? ", cube" : "", (unsigned)hr);
    return nullptr;
  }
  if (pSubData)
    UploadSubresources(pResource.Get(), Desc, nFaces, nMips, nDataMips, pSubData);

  CD3D12Texture *pTex = new CD3D12Texture;
  pTex->m_pResource = pResource.Detach();
  pTex->m_Format = Format;
  pTex->m_nWidth = nWidth;
  pTex->m_nHeight = nHeight;
  pTex->m_nMips = nMips;
  pTex->m_bCube = bCube;
  pTex->m_nArraySize = nArraySize;
  pTex->m_bRenderTarget = false;
  const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = TextureSRVDesc(Format, nMips, bCube, nArraySize);
  pTex->m_nSRV = g_pRenderer->m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV);
  AddVideoMemory(pTex->m_pResource);
  return pTex;
}

CD3D12Texture *CD3D12Upload::CreateRenderTarget(int nWidth, int nHeight, DXGI_FORMAT Format)
{
  if (!m_pDevice || nWidth <= 0 || nHeight <= 0)
    return nullptr;
  const D3D12_HEAP_PROPERTIES Heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC Desc = TextureDesc(nWidth, nHeight, 1, 1, Format, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  // Clears to transparent black are the fast ones.
  D3D12_CLEAR_VALUE Clear = {};
  Clear.Format = Format;
  ComPtr<ID3D12Resource> pResource;
  HRESULT hr = m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, TEXTURE_STATE, &Clear, IID_PPV_ARGS(&pResource));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (render target %dx%d, format %d) failed (0x%08X)",
      nWidth, nHeight, (int)Format, (unsigned)hr);
    return nullptr;
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE RTV = g_pRenderer->m_pDescriptors->AllocRTV();
  if (!RTV.ptr)
    return nullptr;
  m_pDevice->CreateRenderTargetView(pResource.Get(), nullptr, RTV);

  CD3D12Texture *pTex = new CD3D12Texture;
  pTex->m_pResource = pResource.Detach();
  pTex->m_Format = Format;
  pTex->m_nWidth = nWidth;
  pTex->m_nHeight = nHeight;
  pTex->m_nMips = 1;
  pTex->m_bCube = false;
  pTex->m_bRenderTarget = true;
  pTex->m_RTV = RTV;
  const D3D12_SHADER_RESOURCE_VIEW_DESC SRV = TextureSRVDesc(Format, 1, false);
  pTex->m_nSRV = g_pRenderer->m_pDescriptors->AllocSRV(pTex->m_pResource, &SRV);
  AddVideoMemory(pTex->m_pResource);
  return pTex;
}

CD3D12Texture *CD3D12Upload::CreateComputeTarget(int nWidth, int nHeight, int nMips, DXGI_FORMAT Format, const wchar_t *szName)
{
  if (!m_pDevice || nWidth <= 0 || nHeight <= 0 || nMips <= 0)
    return nullptr;
  const D3D12_HEAP_PROPERTIES Heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC Desc = TextureDesc(nWidth, nHeight, 1, nMips, Format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  ComPtr<ID3D12Resource> pResource;
  HRESULT hr = m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                  IID_PPV_ARGS(&pResource));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (compute target %dx%d, %d mips, format %d) failed (0x%08X)",
      nWidth, nHeight, nMips, (int)Format, (unsigned)hr);
    return nullptr;
  }
  if (szName)
    pResource->SetName(szName);

  CD3D12Texture *pTex = new CD3D12Texture;
  pTex->m_pResource = pResource.Detach();
  pTex->m_Format = Format;
  pTex->m_nWidth = nWidth;
  pTex->m_nHeight = nHeight;
  pTex->m_nMips = nMips;
  AddVideoMemory(pTex->m_pResource);
  return pTex;
}

void CD3D12Upload::UpdateTexture(CD3D12Texture *pTex, int nFace, int nMip, int x, int y, int nWidth, int nHeight, const SD3D12SubData &Data)
{
  if (!pTex || !pTex->m_pResource || !Data.pData || !m_pDevice)
    return;
  if (nFace < 0 || nFace >= (pTex->m_bCube ? 6 : 1) || nMip < 0 || nMip >= pTex->m_nMips || x < 0 || y < 0)
    return;
  const bool bBlocks = IsBlockCompressed(pTex->m_Format);
  if (bBlocks && ((x & 3) || (y & 3)))
  {
    iLog->LogError("XRenderD3D12: UpdateTexture at %d, %d: a block compressed texture is updated in whole blocks", x, y);
    return;
  }
  // Clipped to the mip (whose blocks cover a multiple of 4 texels).
  const int nMipWidth = std::max(pTex->m_nWidth >> nMip, 1);
  const int nMipHeight = std::max(pTex->m_nHeight >> nMip, 1);
  nWidth = std::min(nWidth, nMipWidth - x);
  nHeight = std::min(nHeight, nMipHeight - y);
  if (nWidth <= 0 || nHeight <= 0)
    return;
  if (bBlocks)
  {
    nWidth = (int)AlignUp(nWidth, 4);
    nHeight = (int)AlignUp(nHeight, 4);
  }

  // The footprint of the rectangle, as if it were a texture of its own.
  const D3D12_RESOURCE_DESC Desc = TextureDesc(nWidth, nHeight, 1, 1, pTex->m_Format, D3D12_RESOURCE_FLAG_NONE);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
  UINT nRows = 0;
  UINT64 nRowSize = 0, nTotal = 0;
  m_pDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Layout, &nRows, &nRowSize, &nTotal);
  if (nTotal == UINT64_MAX)
    return;
  SD3D12FrameAlloc Staging;
  if (!AllocStaging(nTotal, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, Staging))
    return;
  CopyRows(Staging.pCPU + Layout.Offset, Layout.Footprint.RowPitch, Data, nRows, nRowSize);

  const UINT nSubresource = (UINT)(nMip + nFace * pTex->m_nMips);
  D3D12_TEXTURE_COPY_LOCATION Dst = {};
  Dst.pResource = pTex->m_pResource;
  Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  Dst.SubresourceIndex = nSubresource;
  D3D12_TEXTURE_COPY_LOCATION From = {};
  From.pResource = Staging.pResource;
  From.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  From.PlacedFootprint = Layout;
  From.PlacedFootprint.Offset += Staging.nOffset;

  Barrier(pTex->m_pResource, nSubresource, TEXTURE_STATE, D3D12_RESOURCE_STATE_COPY_DEST);
  m_pCmdList->CopyTextureRegion(&Dst, (UINT)x, (UINT)y, 0, &From, nullptr);
  Barrier(pTex->m_pResource, nSubresource, D3D12_RESOURCE_STATE_COPY_DEST, TEXTURE_STATE);
}

void CD3D12Upload::ReleaseTexture(CD3D12Texture *pTex)
{
  if (!pTex)
    return;
  if (pTex->m_nSRV >= 0)
    g_pRenderer->m_pDescriptors->FreeSRV(pTex->m_nSRV);
  if (pTex->m_bRenderTarget && pTex->m_RTV.ptr)
    g_pRenderer->m_pDescriptors->FreeRTV(pTex->m_RTV);
  if (pTex->m_pResource)
  {
    SubVideoMemory(pTex->m_pResource);
    g_pRenderer->m_pDevice->DeferredRelease(pTex->m_pResource);
  }
  delete pTex;
}
