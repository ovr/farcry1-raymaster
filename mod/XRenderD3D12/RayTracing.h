// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayTracing.h
//	Description: The scene for rays (DXR 1.1, inline RayQuery in the
//	shaders): the main view's opaque draws as acceleration structures.
//
//	- The device: DXR tier 1.1 and Shader Model 6.5 (Init), not with
//	  XRENDERD3D12_NORT=1. Without them there are no rays, and the cvars
//	  that want them change nothing.
//	- Capture: while the main view's opaque list is drawn (CScene::EndEf3D,
//	  BeginCapture .. EndCapture), DrawGeometry hands its draws to
//	  CaptureDraw: those of the general program that write the depth, do
//	  not blend and are triangle lists in world or object space. A draw
//	  whose mWorld is not the one of its mWorldViewProj is left out.
//	- BLAS: one per draw's geometry, the vertex positions (at offset 0, three
//	  floats) and the indices as DrawGeometry has them, untransformed.
//	  Geometry in static buffers (CD3D12Upload::FindBuffer) keeps its BLAS
//	  from frame to frame, by the buffers' ids, versions and ranges; those
//	  not used for a while, or the least recently used beyond d3d12_RTMemory,
//	  go. They live in pools of 64 MB. Geometry in per frame memory gets a
//	  BLAS of the frame (built fast) in the frame slot's memory. The builds
//	  of a frame are limited (MAX_*_TRIANGLES); what is over waits for the
//	  next frames.
//	- TLAS: built every frame after the opaque list (Build), one instance
//	  per draw, the instance's transform its mWorld, its mask and
//	  InstanceContributionToHitGroupIndex its kind (Shaders/RayConstants.h),
//	  culling off.
//	- Binding: root SRV ROOT_RAY_SCENE (D3D12Pipeline.h), for the programs
//	  with rays, from GetTLAS in the same frame.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <wrl/client.h>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include "D3D12Device.h"

struct SPipelineKey;
struct SDrawConstants;

class CD3D12RayScene
{
public:
  CD3D12RayScene();
  ~CD3D12RayScene();
  CD3D12RayScene(const CD3D12RayScene &) = delete;
  CD3D12RayScene &operator=(const CD3D12RayScene &) = delete;

  //! False when the device has no rays (logged why, once); the scene then stays empty.
  bool Init(ID3D12Device *pDevice);
  //! Releases everything at once (the GPU is idle).
  void Shutdown();
  bool IsSupported() const { return m_pDevice5 != nullptr; }

  //! The main view's opaque list starts: its draws until EndCapture make this frame's scene.
  //! ViewProj: its camera's world to clip (the draws' mWorld times it is their mWorldViewProj).
  void BeginCapture(const Matrix44 &ViewProj, const Vec3 &vCamera);
  void EndCapture() { m_bCapturing = false; }
  bool IsCapturing() const { return m_bCapturing; }
  //! A draw of DrawGeometry while capturing; it picks those for the scene.
  void CaptureDraw(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
    const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst, int nBaseVertex);
  //! Records the builds of the missing BLAS and of the TLAS of the captured draws into the frame's
  //! command list; pDump: a line about them (d3d12_DumpFrame).
  void Build(FILE *pDump);
  //! The TLAS built in this frame, 0 when there is none.
  D3D12_GPU_VIRTUAL_ADDRESS GetTLAS() const;
  //! The camera of the capture (Build's frame).
  const Matrix44 &GetViewProj() const { return m_ViewProj; }
  const Vec3 &GetCamera() const { return m_vCamera; }

private:
  //! The geometry of a static BLAS: the buffers by id and version, the ranges in them.
  struct SBlasKey
  {
    uint64 nVBId, nIBId;            //!< CD3D12Buffer::m_nId (nIBId 0: not indexed)
    uint32 nVBVersion, nIBVersion;
    uint32 nVBOffset, nIBOffset;    //!< bytes from the buffers' start: the first vertex, the first index
    uint32 nVertices, nIndices, nStride;
    uint32 nFlags;                  //!< the geometry's flags (opaque or not)

    bool operator==(const SBlasKey &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
  };
  struct SBlasKeyHash
  {
    size_t operator()(const SBlasKey &Key) const;
  };
  //! A part of a pool.
  struct SPoolRange
  {
    int nPool = -1;
    uint64 nOffset = 0, nSize = 0;
  };
  struct SPool
  {
    Microsoft::WRL::ComPtr<ID3D12Resource> pBuffer;   //!< RAYTRACING_ACCELERATION_STRUCTURE
    uint64 nSize = 0, nUsed = 0;
    std::map<uint64, uint64> Free;                      //!< offset -> size, merged with their neighbours
  };
  struct SStaticBlas
  {
    SPoolRange Mem;
    uint32 nTriangles = 0;
    int nLastFrame = 0;             //!< m_nFrame of its last use
    bool bBuilt = false;
  };
  //! A draw for the scene.
  struct SDraw
  {
    D3D12_RAYTRACING_GEOMETRY_DESC Geom;
    float Transform[3][4];
    uint8 nKind;                    //!< XR_RAY_*
    SBlasKey Key;                   //!< XR_RAY_STATIC
    D3D12_GPU_VIRTUAL_ADDRESS nBlas;
  };
  //! A buffer of a frame slot that grows (RAYTRACING_ACCELERATION_STRUCTURE or UNORDERED_ACCESS).
  struct SSlotBuffer
  {
    Microsoft::WRL::ComPtr<ID3D12Resource> pBuffer;
    uint64 nSize = 0;
  };
  struct SFrameSlot
  {
    SSlotBuffer Scratch, Blas, Tlas;
  };

  //! A buffer of nSize bytes with unordered access in the default heap; NULL when refused (logged).
  Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(uint64 nSize, D3D12_RESOURCE_STATES State, const wchar_t *szName);
  //! At least nSize bytes in Buf (a new buffer, the old one released when the GPU is done with it).
  bool Reserve(SSlotBuffer &Buf, uint64 nSize, D3D12_RESOURCE_STATES State, const wchar_t *szName);
  bool AllocPool(uint64 nSize, SPoolRange &Out);
  //! Back to the pool once the GPU finished the current frame.
  void FreePool(const SPoolRange &Range);
  void CollectPools();
  //! The static BLAS not used in this frame, least recently used first, until the pools hold at most
  //! nBudget bytes; and those not used for a while.
  void Evict(uint64 nBudget);

  Microsoft::WRL::ComPtr<ID3D12Device5> m_pDevice5;
  bool m_bCapturing = false;
  Matrix44 m_ViewProj;
  Vec3 m_vCamera = Vec3(0, 0, 0);
  int m_nFrame = 0;                 //!< counts BeginCapture
  uint64 m_nBuiltFence = 0;         //!< the frame fence of the last Build (GetTLAS)
  int m_nBuiltSlot = 0;
  std::vector<SDraw> m_Draws;
  int m_nSkippedTransform = 0;      //!< draws left out by their transforms in this capture

  std::unordered_map<SBlasKey, SStaticBlas, SBlasKeyHash> m_Static;
  uint64 m_nStaticBytes = 0;        //!< the pool memory of m_Static
  std::vector<std::unique_ptr<SPool>> m_Pools;   //!< the same index for good (an empty pool's buffer goes)
  std::vector<std::pair<SPoolRange, uint64>> m_PendingFree;   //!< with the fence after which they are free
  SFrameSlot m_Slots[D3D12_FRAMES];
};
