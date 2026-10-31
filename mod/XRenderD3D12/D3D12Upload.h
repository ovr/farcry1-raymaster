// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Upload.h
//	Description: GPU buffers and textures and how data gets into them.
//
//	- Per frame memory (AllocFrame): a ring in an upload heap, persistently
//	  mapped, one part per frame slot, reset when the slot starts again.
//	  For constants and for vertices and indices drawn once (2D, text,
//	  dynamic buffers).
//	- Static buffers and textures live in the default heap. Their data is
//	  copied through an upload batch: a command list of its own that
//	  CD3D12Device::EndFrame executes before the frame's list (and Flush
//	  executes earlier when its staging memory grows too big). The engine
//	  creates resources at any time, also outside a frame (level load).
//
//	Releases are deferred until the GPU no longer uses the resource.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <wrl/client.h>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include "D3D12Device.h"

//! Memory for the current frame only.
struct SD3D12FrameAlloc
{
  uint8 *pCPU;                        //!< write only
  D3D12_GPU_VIRTUAL_ADDRESS nGPU;
  ID3D12Resource *pResource;
  uint64 nOffset;                     //!< in pResource
};

//! A buffer in the default heap (vertices, indices), in the state VERTEX_AND_CONSTANT_BUFFER |
//! INDEX_BUFFER | NON_PIXEL_SHADER_RESOURCE | PIXEL_SHADER_RESOURCE between uploads (the shader
//! resource states for the rays: the scene's builds and the shaders read it, RayTracing.h).
class CD3D12Buffer
{
public:
  ID3D12Resource *m_pResource;
  D3D12_GPU_VIRTUAL_ADDRESS m_nGPU;
  uint32 m_nSize;
  uint64 m_nId;                       //!< never the same for two buffers (FindBuffer's callers tell them apart by it)
  uint32 m_nVersion;                  //!< counts the UpdateBuffer calls
  int m_nRawSRV;                      //!< its raw SRV in the texture table (GetRawSRV), -1 none yet
  int m_nHeap;                        //!< the heap of POOL_BUFFERS it is placed in (CD3D12Upload), -1 a committed resource
  uint64 m_nHeapOffset;               //!< where in that heap
  uint64 m_nHeapBytes;                //!< the bytes it takes there

  CD3D12Buffer() : m_pResource(NULL), m_nGPU(0), m_nSize(0), m_nId(0), m_nVersion(0), m_nRawSRV(-1), m_nHeap(-1), m_nHeapOffset(0), m_nHeapBytes(0) {}
};

//! A texture in the default heap, in the state PIXEL_SHADER_RESOURCE between uploads (render
//! targets: see m_bRenderTarget).
class CD3D12Texture
{
public:
  ID3D12Resource *m_pResource;
  DXGI_FORMAT m_Format;
  int m_nWidth, m_nHeight, m_nMips;
  bool m_bCube;                       //!< 6 faces
  int m_nArraySize;                   //!< slices of a texture array (1: a plain texture or a cube)
  bool m_bRenderTarget;
  int m_nSRV;                         //!< index in the texture table (D3D12Descriptors.h), -1 none
  D3D12_CPU_DESCRIPTOR_HANDLE m_RTV;  //!< render targets only
  int m_nHeap;                        //!< the heap of POOL_TEXTURES it is placed in (CD3D12Upload), -1 a committed resource
  uint64 m_nHeapOffset;               //!< where in that heap
  uint64 m_nHeapBytes;                //!< the bytes it takes there

  CD3D12Texture() : m_pResource(NULL), m_Format(DXGI_FORMAT_UNKNOWN), m_nWidth(0), m_nHeight(0), m_nMips(0),
    m_bCube(false), m_nArraySize(1), m_bRenderTarget(false), m_nSRV(-1), m_nHeap(-1), m_nHeapOffset(0), m_nHeapBytes(0) { m_RTV.ptr = 0; }
};

//! The data of one sub resource for CreateTexture / UpdateTexture: rows from the top, nPitch bytes
//! apart (block rows for BC formats).
struct SD3D12SubData
{
  const void *pData;
  uint32 nPitch;
};

class CD3D12Upload
{
public:
  CD3D12Upload();
  ~CD3D12Upload();

  //! nFrameRingSize: bytes of per frame memory per frame slot.
  bool Init(ID3D12Device *pDevice, uint32 nFrameRingSize);
  void Shutdown();

  //! The device starts frame slot nSlot (its previous use has finished on the GPU).
  void BeginFrame(int nSlot);
  //! Memory for this frame. nAlign: 256 for constant buffers, 4 or 16 for vertices and indices.
  //! Never fails (grows by a new block when the ring is full). It belongs to the current frame
  //! slot, so it is for the command list of the frame being recorded.
  SD3D12FrameAlloc AllocFrame(uint32 nSize, uint32 nAlign);

  //! A static buffer, filled with pData (nSize bytes) when not NULL; NULL when the device refuses it.
  CD3D12Buffer *CreateBuffer(const void *pData, uint32 nSize);
  //! The upload batch runs before the frame's command list, so every draw of the current frame
  //! sees the new data, also those recorded before the call: data that changes during a frame
  //! belongs in AllocFrame.
  void UpdateBuffer(CD3D12Buffer *pBuf, uint32 nOffset, const void *pData, uint32 nSize);
  //! Deferred until the GPU is done with it; deletes pBuf.
  void ReleaseBuffer(CD3D12Buffer *pBuf);
  //! The static buffer that holds the GPU address nGPU, NULL when none does (e.g. per frame memory).
  const CD3D12Buffer *FindBuffer(D3D12_GPU_VIRTUAL_ADDRESS nGPU) const;
  //! A raw SRV (a ByteAddressBuffer in the texture table, D3D12Pipeline.h's space4) of the static
  //! buffer or the block of this frame's memory that holds nGPU, made when first asked for and kept
  //! with it; nOffset the bytes from the start of the view to nGPU. -1 when none holds it or there
  //! are MAX_RAW_SRVS already (the rays read vertices and indices through them, RayTracing.h).
  int GetRawSRV(D3D12_GPU_VIRTUAL_ADDRESS nGPU, uint64 &nOffset);
  static constexpr int MAX_RAW_SRVS = 16384;

  //! A 2D texture (or a cube with 6 faces), nMips levels, with an SRV in the texture table.
  //! pSubData: [face * nMips + mip] for every face and mip, or NULL (contents undefined); an
  //! entry with pData NULL leaves that sub resource undefined. NULL when the device refuses it.
  CD3D12Texture *CreateTexture(int nWidth, int nHeight, int nMips, DXGI_FORMAT Format, bool bCube, const SD3D12SubData *pSubData, int nArraySize = 1);
  //! A render target texture (one mip) with an RTV and an SRV; starts in PIXEL_SHADER_RESOURCE.
  //! bUnorderedAccess: compute passes write it too (ALLOW_UNORDERED_ACCESS; the pass makes the UAV).
  CD3D12Texture *CreateRenderTarget(int nWidth, int nHeight, DXGI_FORMAT Format, bool bUnorderedAccess = false);
  //! A texture that compute passes write (ALLOW_UNORDERED_ACCESS), nMips levels, without views (the
  //! pass writes them into its descriptor tables, so Format may be typeless); starts in
  //! UNORDERED_ACCESS. szName names it for the debug layer.
  CD3D12Texture *CreateComputeTarget(int nWidth, int nHeight, int nMips, DXGI_FORMAT Format, const wchar_t *szName);
  //! Replaces a rectangle of one sub resource (x, y, nWidth, nHeight in texels; multiples of 4
  //! for BC formats).
  void UpdateTexture(CD3D12Texture *pTex, int nFace, int nMip, int x, int y, int nWidth, int nHeight, const SD3D12SubData &Data);
  //! Deferred until the GPU is done with it; frees the SRV slot, deletes pTex.
  void ReleaseTexture(CD3D12Texture *pTex);
  //! A texture to draw depth into (Desc with ALLOW_DEPTH_STENCIL, Clear its clear value), starting
  //! in eState; placed in a heap of depth targets when it takes up to POOL_PLACE_MAX bytes, so its
  //! contents are undefined until the first draw into it clears it whole. Without views. D3D12
  //! error codes.
  HRESULT CreateDepthTarget(const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES eState, const D3D12_CLEAR_VALUE &Clear,
    Microsoft::WRL::ComPtr<ID3D12Resource> &pResource, SD3D12Placement &Place);
  //! Deferred until the GPU is done with it; resets pResource and Place.
  void ReleaseDepthTarget(Microsoft::WRL::ComPtr<ID3D12Resource> &pResource, SD3D12Placement &Place);

  //! Executes the recorded uploads now (the device calls it before it executes a frame). It does
  //! not signal the fence: its memory is reused once the next signal (GetFrameFence) completed.
  void Flush();

  //! Bytes of the textures and buffers alive (GetMemoryUsage).
  uint64 GetVideoMemory() const;
  //! Bytes of per frame memory handed out since the current frame slot started (AllocFrame).
  uint64 GetFrameMemory() const { return m_Slots[m_nSlot].nUsed; }

private:
  //! A persistently mapped buffer in the upload heap.
  struct SUploadBlock
  {
    Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
    uint8 *pCPU = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS nGPU = 0;
    uint64 nSize = 0;
    int nRawSRV = -1;                 //!< per frame memory: GetRawSRV's view of it
  };
  //! The per frame memory of one frame slot.
  struct SFrameSlot
  {
    SUploadBlock Ring;
    std::vector<SUploadBlock> Extra;  //!< added when the ring was full; merged into it when the slot comes round
    uint64 nUsed = 0;                 //!< bytes handed out since the slot started (all blocks)
  };
  //! Staging memory of the upload batch.
  struct SStaging
  {
    SUploadBlock Block;
    uint64 nUsed = 0;
    uint64 nFence = 0;                //!< 0: the open batch; else the fence of the batch that read it
  };
  struct SAllocator
  {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> pAllocator;
    uint64 nFence = 0;                //!< free when this fence completed
  };

  bool CreateUploadBlock(uint64 nSize, SUploadBlock &Block, const wchar_t *szName);
  //! Opens the upload batch (when it isn't); false when the device refuses.
  bool OpenBatch();
  //! Gives back the staging memory of the batches the GPU finished (the batch must be closed).
  void RetireStaging();
  //! Staging memory in the open batch (opened, or flushed first when it grew too big).
  bool AllocStaging(uint64 nSize, uint64 nAlign, SD3D12FrameAlloc &Out);
  void Barrier(ID3D12Resource *pResource, UINT nSubresource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After);
  void UploadSubresources(ID3D12Resource *pResource, const D3D12_RESOURCE_DESC &Desc, int nFaces, int nMips, int nDataMips, const SD3D12SubData *pSubData);
  uint64 GetAllocationSize(ID3D12Resource *pResource) const;
  void AddVideoMemory(ID3D12Resource *pResource);
  void SubVideoMemory(ID3D12Resource *pResource);
  //! A range of nBytes (a multiple of the placement alignment) in a heap of the pool, made when
  //! none has room; false when the device refuses a heap.
  bool PlaceRange(int nPool, uint64 nBytes, int &nHeap, uint64 &nOffset);
  //! Gives the range back, joined with the free ranges next to it.
  void FreeRange(int nPool, int nHeap, uint64 nOffset, uint64 nBytes);
  //! Gives back the ranges of the released buffers and textures the GPU is done with.
  void RetireRanges();
  //! Takes over a reference of a released resource of CreatePooled: a placed one goes with its
  //! range (m_PendingRanges), a committed one to DeferredRelease.
  void ReleasePooled(int nPool, ID3D12Resource *pResource, int nHeap, uint64 nHeapOffset, uint64 nHeapBytes);
  //! A placed resource in the pool when it takes up to POOL_PLACE_MAX bytes, else a committed one
  //! (nHeap -1); pClear for depth and render targets. D3D12 error codes.
  HRESULT CreatePooled(int nPool, const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES eState, ID3D12Resource **ppResource, int &nHeap,
    uint64 &nHeapOffset, uint64 &nHeapBytes, const D3D12_CLEAR_VALUE *pClear = nullptr);
  //! A raw SRV of all of pResource (nSize bytes), -1 when refused.
  int CreateRawSRV(ID3D12Resource *pResource, uint64 nSize);
  void FreeRawSRV(int &nSlot);

  ID3D12Device *m_pDevice = nullptr;
  uint32 m_nFrameRingSize = 0;

  // Per frame memory
  SFrameSlot m_Slots[D3D12_FRAMES];
  int m_nSlot = 0;
  int m_nCurBlock = -1;               //!< -1 the ring of m_nSlot, else its Extra[m_nCurBlock]
  uint64 m_nCurOffset = 0;
  std::vector<uint8> m_Scratch;       //!< handed out when the device refuses more memory
  bool m_bFrameFailLogged = false;

  // Upload batch
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_pCmdList;
  std::vector<SAllocator> m_Allocators;
  int m_nOpenAllocator = -1;
  bool m_bOpen = false;
  std::vector<SStaging> m_Staging;    //!< of the open batch and of the batches in flight
  std::vector<SUploadBlock> m_FreeStaging;
  int m_nCurStaging = -1;             //!< the staging block being filled in the open batch, -1 none
  uint64 m_nBatchBytes = 0;           //!< staging bytes of the open batch
  uint64 m_nInFlightBytes = 0;        //!< staging bytes of the executed batches not retired yet

  uint64 m_nVideoMemory = 0;

  // Static buffers
  std::map<D3D12_GPU_VIRTUAL_ADDRESS, CD3D12Buffer *> m_Buffers;   //!< by their GPU address
  //! The addresses FindBuffer found a buffer for, by a hash of the address: the rays ask for the
  //! same ones every frame (the draws' vertices and indices), and an address keeps its buffer while
  //! that lives (buffers never overlap); ReleaseBuffer takes the buffer's entries out.
  struct SFoundBuffer
  {
    D3D12_GPU_VIRTUAL_ADDRESS nGPU = 0;
    CD3D12Buffer *pBuf = nullptr;
  };
  static constexpr int FOUND_BUFFERS_BITS = 11;
  mutable SFoundBuffer m_Found[1 << FOUND_BUFFERS_BITS];
  //! The static buffer that holds nGPU (m_Found, else m_Buffers), NULL when none does.
  CD3D12Buffer *LookUpBuffer(D3D12_GPU_VIRTUAL_ADDRESS nGPU) const;
  //! The heaps the buffers and textures of up to POOL_PLACE_MAX bytes are placed in: a committed
  //! resource costs the kernel a few hundred microseconds to make and as much to free (the
  //! terrain's index buffers change with its detail, a few every frame outdoors; the textures of a
  //! place load when it comes into view; the shadow maps of the characters a turn brings into
  //! view, a dozen at once), a placed one hardly anything. Buffers, textures and depth targets each
  //! have heaps of their own (resource heap tier 1).
  static constexpr uint64 POOL_HEAP_SIZE = 64ull << 20;
  static constexpr uint64 POOL_PLACE_MAX = 8ull << 20;
  enum { POOL_BUFFERS, POOL_TEXTURES, POOL_DEPTH_TARGETS, POOL_NUM };
  struct SHeapPool
  {
    std::vector<Microsoft::WRL::ComPtr<ID3D12Heap>> Heaps;
    std::map<std::pair<int, uint64>, uint64> FreeRanges;     //!< (heap, offset) -> bytes of the free ranges
    std::set<std::tuple<uint64, int, uint64>> FreeBySize;    //!< the same as (bytes, heap, offset), for the best fit
  };
  SHeapPool m_Pools[POOL_NUM];
  //! The ranges of the released buffers and textures, free once the GPU passed nFence. The
  //! resource goes with its range (not DeferredRelease): one placed in the range after it would
  //! share its addresses while it lives, which the debug layer warns of on every use.
  struct SPendingRange
  {
    int nPool, nHeap;
    uint64 nOffset, nBytes, nFence;
    Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
  };
  std::vector<SPendingRange> m_PendingRanges;
  uint64 m_nNextBufferId = 1;
  int m_nRawSRVs = 0;                 //!< GetRawSRV's views alive
};
