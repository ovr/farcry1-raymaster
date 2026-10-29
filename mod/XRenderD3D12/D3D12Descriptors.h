// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Descriptors.h
//	Description: Descriptor heaps.
//
//	Textures are bindless: one shader visible CBV/SRV/UAV heap holds the
//	texture table, D3D12_MAX_TEXTURES SRVs from slot 0, which the root
//	signature binds once per command list (D3D12Pipeline.h). A draw names
//	its textures by index in its constants. Unused slots hold a null SRV,
//	so the whole table is always valid. Slot 0 is reserved for the white
//	texture (D3D12_SRV_WHITE), slot 1 holds a null cube SRV for good
//	(D3D12_SRV_NULL_CUBE).
//	RTVs and DSVs of render targets come from CPU only heaps.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <wrl/client.h>
#include <vector>

static const int D3D12_MAX_TEXTURES = 65536;
static const int D3D12_SRV_WHITE = 0;
//! A null TEXTURECUBE SRV (samples 0): the cube index of DF_CUBE0 when there is no cube texture,
//! since a cube must not be sampled through the 2D null SRVs of the free slots.
static const int D3D12_SRV_NULL_CUBE = 1;

class CD3D12Descriptors
{
public:
  CD3D12Descriptors();
  ~CD3D12Descriptors();

  bool Init(ID3D12Device *pDevice);
  void Shutdown();

  //! A slot of the texture table with an SRV of pResource (pDesc NULL: the default view), -1 when
  //! the table is full.
  int AllocSRV(ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc);
  //! Writes another SRV into an allocated slot (only between frames, or for a slot no draw of the
  //! current frame uses).
  void WriteSRV(int nSlot, ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc);
  //! Gives the slot back: it keeps its view until the GPU finished the current frame, then
  //! CollectGarbage makes it a null SRV and free again. The reserved slots are ignored.
  void FreeSRV(int nSlot);

  D3D12_CPU_DESCRIPTOR_HANDLE AllocRTV();
  void FreeRTV(D3D12_CPU_DESCRIPTOR_HANDLE h);
  D3D12_CPU_DESCRIPTOR_HANDLE AllocDSV();
  void FreeDSV(D3D12_CPU_DESCRIPTOR_HANDLE h);

  ID3D12DescriptorHeap *GetHeap() const;
  //! GPU handle of slot 0 (the root signature's texture table).
  D3D12_GPU_DESCRIPTOR_HANDLE GetTableStart() const;
  //! Makes the slots freed before the GPU finished nCompletedFence available again.
  void CollectGarbage(uint64 nCompletedFence);

private:
  //! A CPU only heap of RTVs or DSVs with its free list.
  struct SCPUHeap
  {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE Start = {};
    uint32 nIncrement = 0;
    int nCount = 0;
    std::vector<int> Free;            //!< the lowest index last
    bool bFullLogged = false;
  };
  //! A slot freed in the frame that signals nFence.
  struct SFreedSlot
  {
    int nSlot;
    uint64 nFence;
  };

  bool InitCPUHeap(SCPUHeap &Heap, D3D12_DESCRIPTOR_HEAP_TYPE Type, int nCount);
  D3D12_CPU_DESCRIPTOR_HANDLE AllocCPU(SCPUHeap &Heap, const char *szWhat);
  void FreeCPU(SCPUHeap &Heap, D3D12_CPU_DESCRIPTOR_HANDLE h);
  D3D12_CPU_DESCRIPTOR_HANDLE GetSlotCPU(int nSlot) const;
  void WriteNullSRV(int nSlot, bool bCube);

  ID3D12Device *m_pDevice = nullptr;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pHeap;   //!< shader visible, the texture table
  uint32 m_nIncrement = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE m_CPUStart = {};
  D3D12_GPU_DESCRIPTOR_HANDLE m_GPUStart = {};
  std::vector<int> m_FreeSlots;       //!< the lowest slot last
  std::vector<SFreedSlot> m_Freed;    //!< waiting for the GPU, in the order freed
  bool m_bFullLogged = false;
  SCPUHeap m_RTV, m_DSV;
};
