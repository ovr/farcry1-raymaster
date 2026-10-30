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

  CD3D12Buffer() : m_pResource(NULL), m_nGPU(0), m_nSize(0), m_nId(0), m_nVersion(0), m_nRawSRV(-1) {}
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

  CD3D12Texture() : m_pResource(NULL), m_Format(DXGI_FORMAT_UNKNOWN), m_nWidth(0), m_nHeight(0), m_nMips(0),
    m_bCube(false), m_nArraySize(1), m_bRenderTarget(false), m_nSRV(-1) { m_RTV.ptr = 0; }
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
  CD3D12Texture *CreateRenderTarget(int nWidth, int nHeight, DXGI_FORMAT Format);
  //! A texture that compute passes write (ALLOW_UNORDERED_ACCESS), nMips levels, without views (the
  //! pass writes them into its descriptor tables, so Format may be typeless); starts in
  //! UNORDERED_ACCESS. szName names it for the debug layer.
  CD3D12Texture *CreateComputeTarget(int nWidth, int nHeight, int nMips, DXGI_FORMAT Format, const wchar_t *szName);
  //! Replaces a rectangle of one sub resource (x, y, nWidth, nHeight in texels; multiples of 4
  //! for BC formats).
  void UpdateTexture(CD3D12Texture *pTex, int nFace, int nMip, int x, int y, int nWidth, int nHeight, const SD3D12SubData &Data);
  //! Deferred until the GPU is done with it; frees the SRV slot, deletes pTex.
  void ReleaseTexture(CD3D12Texture *pTex);

  //! Executes the recorded uploads now (the device calls it before it executes a frame). It does
  //! not signal the fence: its memory is reused once the next signal (GetFrameFence) completed.
  void Flush();

  //! Bytes of the textures and buffers alive (GetMemoryUsage).
  uint64 GetVideoMemory() const;

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
  uint64 m_nNextBufferId = 1;
  int m_nRawSRVs = 0;                 //!< GetRawSRV's views alive
};
