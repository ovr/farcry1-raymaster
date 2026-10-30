// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayTracing.cpp
//	Description: The scene for rays (RayTracing.h) and what the renderer
//	does with it: the capture of the main view's opaque list, the builds
//	after it (CScene::EndEf3D) and the debug view d3d12_RTDebug.
//
//	The builds of a frame, in its command list after the opaque list: the
//	missing BLAS (each with a scratch range of its own), a UAV barrier, the
//	TLAS (its instances in per frame memory, its scratch at the start of the
//	same buffer), a UAV barrier. The draws after it read the TLAS.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include <algorithm>
#include "Renderer.h"
#include "D3D12Device.h"
#include "D3D12Upload.h"
#include "D3D12Pipeline.h"
#include "RayTracing.h"
#include "Shaders/RayConstants.h"

using Microsoft::WRL::ComPtr;

namespace
{
  constexpr uint64 AS_ALIGN = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
  constexpr uint64 BUFFER_ALIGN = 64 * 1024;
  constexpr uint64 POOL_SIZE = 64ull << 20;                   //!< the static BLAS' pools
  constexpr uint64 MIN_SLOT_BUFFER = 1ull << 20;              //!< the frame slots' buffers start this big
  constexpr uint32 MAX_STATIC_BUILD_TRIANGLES = 1u << 20;     //!< the new static BLAS of a frame (one bigger alone)
  constexpr uint32 MAX_FRAME_TRIANGLES = 2u << 20;            //!< the BLAS of a frame
  constexpr int EVICT_FRAMES = 300;                           //!< a static BLAS not used for so many frames goes

  constexpr uint64 AlignUp(uint64 n, uint64 nAlign)
  {
    return (n + nAlign - 1) / nAlign * nAlign;
  }

  bool IsNoRaysEnv()
  {
    char szValue[16];
    const DWORD nLen = GetEnvironmentVariableA("XRENDERD3D12_NORT", szValue, sizeof(szValue));
    return nLen && nLen < sizeof(szValue) && strcmp(szValue, "0") != 0;
  }

  //! The formats with the position at offset 0 as three floats (not the pre-transformed one, nor the
  //! extra streams').
  bool HasWorldPosition(int nFormat)
  {
    return nFormat > 0 && nFormat < VERTEX_FORMAT_NUMS && nFormat != VERTEX_FORMAT_TRP3F_COL4UB_TEX2F && nFormat != VERTEX_FORMAT_T3F_B3F_N3F &&
           nFormat != VERTEX_FORMAT_TEX2F;
  }

  void Set4(float v[4], float x, float y, float z, float w)
  {
    v[0] = x; v[1] = y; v[2] = z; v[3] = w;
  }

  const float s_FullScreen[3][3] = { { -1, -1, 0 }, { -1, 3, 0 }, { 3, -1, 0 } };
}

size_t CD3D12RayScene::SBlasKeyHash::operator()(const SBlasKey &Key) const
{
  // FNV-1a over the bytes (SBlasKey has no padding).
  static_assert(sizeof(SBlasKey) == 48, "SBlasKey is hashed and compared as bytes");
  const uint8 *p = (const uint8 *)&Key;
  uint64 nHash = 14695981039346656037ull;
  for (size_t i = 0; i < sizeof(Key); i++)
  {
    nHash ^= p[i];
    nHash *= 1099511628211ull;
  }
  return (size_t)nHash;
}

CD3D12RayScene::CD3D12RayScene() = default;

CD3D12RayScene::~CD3D12RayScene()
{
  Shutdown();
}

bool CD3D12RayScene::Init(ID3D12Device *pDevice)
{
  Shutdown();
  if (IsNoRaysEnv())
  {
    iLog->Log("XRenderD3D12: no rays (XRENDERD3D12_NORT)");
    return false;
  }
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5 = {};
  if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &Options5, sizeof(Options5))) ||
      Options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
  {
    iLog->Log("XRenderD3D12: no rays: the device has ray tracing tier %d (1.1 needed)", (int)Options5.RaytracingTier);
    return false;
  }
  D3D12_FEATURE_DATA_SHADER_MODEL Model = { D3D_SHADER_MODEL_6_5 };
  if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &Model, sizeof(Model))) || Model.HighestShaderModel < D3D_SHADER_MODEL_6_5)
  {
    iLog->Log("XRenderD3D12: no rays: the device has shader model 0x%x (6.5 needed)", (unsigned)Model.HighestShaderModel);
    return false;
  }
  ComPtr<ID3D12Device5> pDevice5;
  if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&pDevice5))))
  {
    iLog->Log("XRenderD3D12: no rays: no ID3D12Device5");
    return false;
  }
  m_pDevice5 = std::move(pDevice5);
  iLog->Log("XRenderD3D12: rays: ray tracing tier %d, shader model 0x%x", (int)Options5.RaytracingTier, (unsigned)Model.HighestShaderModel);
  return true;
}

void CD3D12RayScene::Shutdown()
{
  m_bCapturing = false;
  m_Draws.clear();
  m_Static.clear();
  m_nStaticBytes = 0;
  m_Pools.clear();
  m_PendingFree.clear();
  for (SFrameSlot &Slot : m_Slots)
    Slot = SFrameSlot();
  m_nBuiltFence = 0;
  m_pDevice5.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Capture

void CD3D12RayScene::BeginCapture(const Matrix44 &ViewProj, const Vec3 &vCamera)
{
  m_Draws.clear();
  m_nSkippedTransform = 0;
  m_ViewProj = ViewProj;
  m_vCamera = vCamera;
  m_nFrame++;
  m_bCapturing = m_pDevice5 != nullptr;
}

void CD3D12RayScene::CaptureDraw(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
  const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst, int nBaseVertex)
{
  // The opaque surfaces: the general program writing the depth, not blended, not a pass over others.
  if (Key.nProgram != eD3D12P_General || eTopology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST || Key.bWireframe || Key.nTarget != 0)
    return;
  if (!(Key.nState & GS_DEPTHWRITE) || (Key.nState & (GS_BLSRC_MASK | GS_BLDST_MASK | GS_NODEPTHTEST | GS_NOCOLMASK)))
    return;
  if ((dc.nFlags & (DF_SCREEN_POS | DF_DETAIL_LAYER | DF_SHADOW_PASS)) || !HasWorldPosition(Key.nVertexFormat))
    return;
  const uint32 nStride = VB.StrideInBytes;
  if (nStride < 12 || !VB.BufferLocation)
    return;

  // Its mWorld must be the object's: times the camera's it is the draw's mWorldViewProj.
  const float *w = dc.mWorld, *vp = m_ViewProj.data, *wvp = dc.mWorldViewProj;
  if (w[3] != 0 || w[7] != 0 || w[11] != 0 || w[15] != 1)
  {
    m_nSkippedTransform++;
    return;
  }
  float fMax = 0;
  for (int i = 0; i < 16; i++)
    fMax = std::max(fMax, fabsf(wvp[i]));
  for (int r = 0; r < 4; r++)
  {
    for (int c = 0; c < 4; c++)
    {
      const float f = w[r * 4] * vp[c] + w[r * 4 + 1] * vp[4 + c] + w[r * 4 + 2] * vp[8 + c] + w[r * 4 + 3] * vp[12 + c];
      if (fabsf(f - wvp[r * 4 + c]) > 1e-3f * fMax + 1e-5f)
      {
        m_nSkippedTransform++;
        return;
      }
    }
  }

  SDraw d = {};
  D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC &Tri = d.Geom.Triangles;
  const bool bAlphaTest = (dc.nFlags & DF_ALPHATEST) != 0;
  d.Geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  d.Geom.Flags = bAlphaTest ? D3D12_RAYTRACING_GEOMETRY_FLAG_NONE : D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  Tri.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  Tri.VertexBuffer.StrideInBytes = nStride;
  const uint32 nAll = VB.SizeInBytes / nStride;
  if (pIB)
  {
    const uint32 nIndexSize = pIB->Format == DXGI_FORMAT_R32_UINT ? 4 : 2;
    if (nBaseVertex < 0 || (uint32)nBaseVertex >= nAll || (uint64)(nFirst + nCount) * nIndexSize > pIB->SizeInBytes)
      return;
    Tri.VertexBuffer.StartAddress = VB.BufferLocation + (uint64)nBaseVertex * nStride;
    Tri.VertexCount = nAll - (uint32)nBaseVertex;
    Tri.IndexFormat = nIndexSize == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    Tri.IndexBuffer = pIB->BufferLocation + (uint64)nFirst * nIndexSize;
    Tri.IndexCount = nCount - nCount % 3;
    if (!Tri.IndexCount)
      return;
  }
  else
  {
    if ((uint64)nFirst + nCount > nAll)
      return;
    Tri.VertexBuffer.StartAddress = VB.BufferLocation + (uint64)nFirst * nStride;
    Tri.VertexCount = nCount - nCount % 3;
    Tri.IndexFormat = DXGI_FORMAT_UNKNOWN;
    if (!Tri.VertexCount)
      return;
  }

  // Static: the vertices and the indices in static buffers, whole.
  const CD3D12Upload &Upload = *g_pRenderer->m_pUpload;
  const CD3D12Buffer *pVBuf = Upload.FindBuffer(Tri.VertexBuffer.StartAddress);
  const CD3D12Buffer *pIBuf = pIB ? Upload.FindBuffer(Tri.IndexBuffer) : nullptr;
  bool bStatic = pVBuf && (!pIB || pIBuf);
  if (bStatic)
  {
    const uint64 nVBOffset = Tri.VertexBuffer.StartAddress - pVBuf->m_nGPU;
    const uint64 nIBOffset = pIBuf ? Tri.IndexBuffer - pIBuf->m_nGPU : 0;
    const uint32 nIndexSize = Tri.IndexFormat == DXGI_FORMAT_R32_UINT ? 4 : 2;
    bStatic = nVBOffset + (uint64)Tri.VertexCount * nStride <= pVBuf->m_nSize + nStride &&
              (!pIBuf || nIBOffset + (uint64)Tri.IndexCount * nIndexSize <= pIBuf->m_nSize);
    d.Key.nVBId = pVBuf->m_nId;
    d.Key.nVBVersion = pVBuf->m_nVersion;
    d.Key.nVBOffset = (uint32)nVBOffset;
    d.Key.nIBId = pIBuf ? pIBuf->m_nId : 0;
    d.Key.nIBVersion = pIBuf ? pIBuf->m_nVersion : 0;
    d.Key.nIBOffset = (uint32)nIBOffset;
    d.Key.nVertices = Tri.VertexCount;
    d.Key.nIndices = Tri.IndexCount;
    d.Key.nStride = nStride;
    d.Key.nFlags = (uint32)d.Geom.Flags;
  }
  d.nKind = (uint8)((bStatic ? XR_RAY_STATIC : XR_RAY_DYNAMIC) | (bAlphaTest ? XR_RAY_ALPHATEST : 0));
  // The instance's transform: object to world for column vectors, mWorld's for row vectors transposed.
  for (int r = 0; r < 3; r++)
  {
    for (int c = 0; c < 4; c++)
      d.Transform[r][c] = w[c * 4 + r];
  }
  m_Draws.push_back(d);
}

//////////////////////////////////////////////////////////////////////////
// Memory

ComPtr<ID3D12Resource> CD3D12RayScene::CreateBuffer(uint64 nSize, D3D12_RESOURCE_STATES State, const wchar_t *szName)
{
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = nSize;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = DXGI_FORMAT_UNKNOWN;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  ComPtr<ID3D12Resource> pBuffer;
  const HRESULT hr = m_pDevice5->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, nullptr, IID_PPV_ARGS(&pBuffer));
  if (FAILED(hr))
  {
    iLog->LogError("XRenderD3D12: CreateCommittedResource (ray scene, %llu bytes) failed (0x%08X)", (unsigned long long)nSize, (unsigned)hr);
    return nullptr;
  }
  pBuffer->SetName(szName);
  return pBuffer;
}

bool CD3D12RayScene::Reserve(SSlotBuffer &Buf, uint64 nSize, D3D12_RESOURCE_STATES State, const wchar_t *szName)
{
  if (Buf.pBuffer && nSize <= Buf.nSize)
    return true;
  const uint64 nNew = AlignUp(std::max(nSize + nSize / 4, MIN_SLOT_BUFFER), BUFFER_ALIGN);
  ComPtr<ID3D12Resource> pBuffer = CreateBuffer(nNew, State, szName);
  if (!pBuffer)
    return false;
  if (Buf.pBuffer)
    g_pRenderer->m_pDevice->DeferredRelease(Buf.pBuffer);
  Buf.pBuffer = std::move(pBuffer);
  Buf.nSize = nNew;
  return true;
}

bool CD3D12RayScene::AllocPool(uint64 nSize, SPoolRange &Out)
{
  nSize = AlignUp(nSize, AS_ALIGN);
  for (size_t i = 0; i < m_Pools.size(); i++)
  {
    SPool &Pool = *m_Pools[i];
    if (!Pool.pBuffer)
      continue;
    for (auto it = Pool.Free.begin(); it != Pool.Free.end(); ++it)
    {
      if (it->second < nSize)
        continue;
      Out.nPool = (int)i;
      Out.nOffset = it->first;
      Out.nSize = nSize;
      const uint64 nRest = it->second - nSize;
      Pool.Free.erase(it);
      if (nRest)
        Pool.Free[Out.nOffset + nSize] = nRest;
      Pool.nUsed += nSize;
      return true;
    }
  }
  // A new pool, where one was released or at the end.
  const uint64 nPoolSize = std::max(POOL_SIZE, AlignUp(nSize, BUFFER_ALIGN));
  ComPtr<ID3D12Resource> pBuffer = CreateBuffer(nPoolSize, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"XRenderD3D12 ray scene BLAS");
  if (!pBuffer)
    return false;
  size_t nPool = 0;
  while (nPool < m_Pools.size() && m_Pools[nPool]->pBuffer)
    nPool++;
  if (nPool == m_Pools.size())
    m_Pools.push_back(std::make_unique<SPool>());
  SPool &Pool = *m_Pools[nPool];
  Pool.pBuffer = std::move(pBuffer);
  Pool.nSize = nPoolSize;
  Pool.nUsed = nSize;
  Pool.Free.clear();
  if (nPoolSize > nSize)
    Pool.Free[nSize] = nPoolSize - nSize;
  Out.nPool = (int)nPool;
  Out.nOffset = 0;
  Out.nSize = nSize;
  return true;
}

void CD3D12RayScene::FreePool(const SPoolRange &Range)
{
  if (Range.nPool >= 0)
    m_PendingFree.emplace_back(Range, g_pRenderer->m_pDevice->GetFrameFence());
}

void CD3D12RayScene::CollectPools()
{
  const uint64 nCompleted = g_pRenderer->m_pDevice->GetCompletedFence();
  size_t nKept = 0;
  for (size_t i = 0; i < m_PendingFree.size(); i++)
  {
    const SPoolRange &Range = m_PendingFree[i].first;
    if (m_PendingFree[i].second > nCompleted)
    {
      m_PendingFree[nKept++] = m_PendingFree[i];
      continue;
    }
    SPool &Pool = *m_Pools[Range.nPool];
    Pool.nUsed -= std::min(Pool.nUsed, Range.nSize);
    auto it = Pool.Free.emplace(Range.nOffset, Range.nSize).first;
    auto Next = std::next(it);
    if (Next != Pool.Free.end() && it->first + it->second == Next->first)
    {
      it->second += Next->second;
      Pool.Free.erase(Next);
    }
    if (it != Pool.Free.begin())
    {
      auto Prev = std::prev(it);
      if (Prev->first + Prev->second == it->first)
      {
        Prev->second += it->second;
        Pool.Free.erase(it);
      }
    }
  }
  m_PendingFree.resize(nKept);
  // An empty pool goes (the first one stays).
  for (size_t i = 1; i < m_Pools.size(); i++)
  {
    SPool &Pool = *m_Pools[i];
    if (Pool.pBuffer && !Pool.nUsed)
    {
      g_pRenderer->m_pDevice->DeferredRelease(Pool.pBuffer);
      Pool.nSize = 0;
      Pool.Free.clear();
    }
  }
}

void CD3D12RayScene::Evict(uint64 nBudget)
{
  std::vector<std::pair<int, const SBlasKey *>> Candidates;
  for (auto it = m_Static.begin(); it != m_Static.end();)
  {
    SStaticBlas &Blas = it->second;
    if (m_nFrame - Blas.nLastFrame > EVICT_FRAMES)
    {
      FreePool(Blas.Mem);
      m_nStaticBytes -= std::min(m_nStaticBytes, Blas.Mem.nSize);
      it = m_Static.erase(it);
      continue;
    }
    if (Blas.nLastFrame != m_nFrame && Blas.bBuilt)
      Candidates.emplace_back(Blas.nLastFrame, &it->first);
    ++it;
  }
  if (m_nStaticBytes <= nBudget)
    return;
  std::sort(Candidates.begin(), Candidates.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  // Erasing others keeps the keys the candidates point to (unordered_map keeps its nodes).
  for (size_t i = 0; i < Candidates.size() && m_nStaticBytes > nBudget; i++)
  {
    auto it = m_Static.find(*Candidates[i].second);
    FreePool(it->second.Mem);
    m_nStaticBytes -= std::min(m_nStaticBytes, it->second.Mem.nSize);
    m_Static.erase(it);
  }
}

//////////////////////////////////////////////////////////////////////////
// Builds

void CD3D12RayScene::Build(FILE *pDump)
{
  m_bCapturing = false;
  m_nBuiltFence = 0;
  CD3D12Device &Device = *g_pRenderer->m_pDevice;
  if (!m_pDevice5 || !Device.IsInFrame())
    return;
  ComPtr<ID3D12GraphicsCommandList4> pList;
  if (FAILED(Device.GetCmdList()->QueryInterface(IID_PPV_ARGS(&pList))))
    return;
  CollectPools();
  const int nSlot = Device.GetFrameSlot();
  SFrameSlot &Slot = m_Slots[nSlot];

  // The BLAS to build: static ones in their pools, this frame's in the slot's buffer.
  struct SBuild
  {
    SDraw *pDraw;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS Flags;
    uint64 nScratch;              //!< offset in the slot's scratch
    uint64 nFrameOffset;          //!< this frame's: offset in the slot's BLAS buffer
    SStaticBlas *pStatic;         //!< a static one's entry
  };
  std::vector<SBuild> Builds;
  uint64 nScratch = 0, nFrameBlas = 0;
  uint32 nNewStaticTris = 0, nFrameTris = 0, nTris = 0;
  int nCached = 0, nNewStatic = 0, nFrame = 0, nWaiting = 0, nOver = 0;
  for (SDraw &d : m_Draws)
  {
    d.nBlas = 0;
    const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC &Tri = d.Geom.Triangles;
    const uint32 nTri = (Tri.IndexCount ? Tri.IndexCount : Tri.VertexCount) / 3;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs = {};
    Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    Inputs.NumDescs = 1;
    Inputs.pGeometryDescs = &d.Geom;
    if (d.nKind & XR_RAY_STATIC)
    {
      SStaticBlas &Blas = m_Static[d.Key];
      Blas.nLastFrame = m_nFrame;
      if (Blas.bBuilt)
      {
        d.nBlas = m_Pools[Blas.Mem.nPool]->pBuffer->GetGPUVirtualAddress() + Blas.Mem.nOffset;
        nCached++;
        nTris += nTri;
        continue;
      }
      if (nNewStaticTris && nNewStaticTris + nTri > MAX_STATIC_BUILD_TRIANGLES)
      {
        nWaiting++;
        continue;
      }
      Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO Info = {};
      m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&Inputs, &Info);
      if (!Info.ResultDataMaxSizeInBytes || !AllocPool(Info.ResultDataMaxSizeInBytes, Blas.Mem))
      {
        nWaiting++;
        continue;
      }
      m_nStaticBytes += Blas.Mem.nSize;
      Blas.nTriangles = nTri;
      Blas.bBuilt = true;
      d.nBlas = m_Pools[Blas.Mem.nPool]->pBuffer->GetGPUVirtualAddress() + Blas.Mem.nOffset;
      Builds.push_back({ &d, Inputs.Flags, nScratch, 0, &Blas });
      nScratch += AlignUp(Info.ScratchDataSizeInBytes, AS_ALIGN);
      nNewStaticTris += nTri;
      nNewStatic++;
    }
    else
    {
      if (nFrameTris + nTri > MAX_FRAME_TRIANGLES)
      {
        nOver++;
        continue;
      }
      Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO Info = {};
      m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&Inputs, &Info);
      if (!Info.ResultDataMaxSizeInBytes)
        continue;
      Builds.push_back({ &d, Inputs.Flags, nScratch, nFrameBlas, nullptr });
      nScratch += AlignUp(Info.ScratchDataSizeInBytes, AS_ALIGN);
      nFrameBlas += AlignUp(Info.ResultDataMaxSizeInBytes, AS_ALIGN);
      nFrameTris += nTri;
      nFrame++;
    }
    nTris += nTri;
  }
  const UINT nInstances = (UINT)std::count_if(m_Draws.begin(), m_Draws.end(), [](const SDraw &d) { return d.nBlas != 0; }) +
                          (UINT)nFrame;

  // The TLAS, its scratch at the start of the slot's (after the barrier behind the BLAS).
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS TopInputs = {};
  TopInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  TopInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  TopInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  TopInputs.NumDescs = nInstances;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO TopInfo = {};
  if (nInstances)
    m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&TopInputs, &TopInfo);
  const bool bMemory = nInstances &&
    Reserve(Slot.Scratch, std::max(nScratch, AlignUp(TopInfo.ScratchDataSizeInBytes, AS_ALIGN)), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            L"XRenderD3D12 ray scene scratch") &&
    (!nFrameBlas || Reserve(Slot.Blas, nFrameBlas, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"XRenderD3D12 ray scene frame BLAS")) &&
    Reserve(Slot.Tlas, TopInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, L"XRenderD3D12 ray scene TLAS");
  if (!bMemory)
  {
    // The static ones made ready above are not built: they wait.
    for (const SBuild &b : Builds)
    {
      if (!b.pStatic)
        continue;
      FreePool(b.pStatic->Mem);
      m_nStaticBytes -= std::min(m_nStaticBytes, b.pStatic->Mem.nSize);
      b.pStatic->Mem = SPoolRange();
      b.pStatic->bBuilt = false;
    }
    return;
  }

  const D3D12_GPU_VIRTUAL_ADDRESS nScratchGPU = Slot.Scratch.pBuffer->GetGPUVirtualAddress();
  for (const SBuild &b : Builds)
  {
    if (!b.pStatic)
      b.pDraw->nBlas = Slot.Blas.pBuffer->GetGPUVirtualAddress() + b.nFrameOffset;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC Desc = {};
    Desc.DestAccelerationStructureData = b.pDraw->nBlas;
    Desc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    Desc.Inputs.Flags = b.Flags;
    Desc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    Desc.Inputs.NumDescs = 1;
    Desc.Inputs.pGeometryDescs = &b.pDraw->Geom;
    Desc.ScratchAccelerationStructureData = nScratchGPU + b.nScratch;
    pList->BuildRaytracingAccelerationStructure(&Desc, 0, nullptr);
  }
  D3D12_RESOURCE_BARRIER Barrier = {};
  Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  Barrier.UAV.pResource = nullptr;
  if (!Builds.empty())
    pList->ResourceBarrier(1, &Barrier);

  const SD3D12FrameAlloc InstanceMem = g_pRenderer->m_pUpload->AllocFrame(nInstances * (uint32)sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
    D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);
  D3D12_RAYTRACING_INSTANCE_DESC *pInstances = (D3D12_RAYTRACING_INSTANCE_DESC *)InstanceMem.pCPU;
  UINT nInstance = 0;
  for (const SDraw &d : m_Draws)
  {
    if (!d.nBlas || nInstance >= nInstances)
      continue;
    D3D12_RAYTRACING_INSTANCE_DESC Instance = {};
    memcpy(Instance.Transform, d.Transform, sizeof(Instance.Transform));
    Instance.InstanceID = nInstance;
    Instance.InstanceMask = d.nKind;
    Instance.InstanceContributionToHitGroupIndex = d.nKind;
    Instance.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    Instance.AccelerationStructure = d.nBlas;
    pInstances[nInstance++] = Instance;
  }
  TopInputs.NumDescs = nInstance;
  TopInputs.InstanceDescs = InstanceMem.nGPU;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC TopDesc = {};
  TopDesc.DestAccelerationStructureData = Slot.Tlas.pBuffer->GetGPUVirtualAddress();
  TopDesc.Inputs = TopInputs;
  TopDesc.ScratchAccelerationStructureData = nScratchGPU;
  pList->BuildRaytracingAccelerationStructure(&TopDesc, 0, nullptr);
  pList->ResourceBarrier(1, &Barrier);
  m_nBuiltFence = Device.GetFrameFence();
  m_nBuiltSlot = nSlot;

  Evict((uint64)std::max(g_pRenderer->CV_d3d12_RTMemory, 16) << 20);
  if (pDump)
  {
    uint64 nPools = 0;
    for (const auto &p : m_Pools)
      nPools += p->nSize;
    fprintf(pDump,
      "ray scene: %u instances of %d draws (%d left out by their transforms), %u triangles; static BLAS %d kept, %d built "
      "(%u triangles), %d waiting; %d of this frame (%u triangles), %d over the limit; static BLAS %.1f MB (%d, pools %.1f MB), "
      "frame BLAS %.1f MB, TLAS %.1f KB, scratch %.1f MB\n",
      nInstance, (int)m_Draws.size(), m_nSkippedTransform, nTris, nCached, nNewStatic, nNewStaticTris, nWaiting, nFrame, nFrameTris, nOver,
      m_nStaticBytes / 1048576.0, (int)m_Static.size(), nPools / 1048576.0, nFrameBlas / 1048576.0, TopInfo.ResultDataMaxSizeInBytes / 1024.0,
      Slot.Scratch.nSize / 1048576.0);
  }
}

D3D12_GPU_VIRTUAL_ADDRESS CD3D12RayScene::GetTLAS() const
{
  if (!m_nBuiltFence || !g_pRenderer->m_pDevice || m_nBuiltFence != g_pRenderer->m_pDevice->GetFrameFence())
    return 0;
  const SSlotBuffer &Tlas = m_Slots[m_nBuiltSlot].Tlas;
  return Tlas.pBuffer ? Tlas.pBuffer->GetGPUVirtualAddress() : 0;
}

//////////////////////////////////////////////////////////////////////////
// The renderer's part

bool CD3D12Renderer::WantsRays() const
{
  return m_pRayScene && m_pRayScene->IsSupported() && CV_d3d12_RTDebug != 0;
}

void CD3D12Renderer::BeginRayCapture()
{
  if (!WantsRays() || !IsAOView())
    return;
  m_pRayScene->BeginCapture(m_CameraView * m_ProjMatrix, m_Camera.GetPos());
}

void CD3D12Renderer::BuildRayScene()
{
  if (!m_pRayScene || !m_pRayScene->IsCapturing())
    return;
  m_pRayScene->EndCapture();
  GpuSection("ray scene");
  m_pRayScene->Build(m_pFrameDump.get());
}

void CD3D12Renderer::DrawRayDebug()
{
  if (!CV_d3d12_RTDebug || !m_pRayScene || !m_pRayScene->GetTLAS() || !m_pDevice || m_pDevice->IsOffscreen())
    return;
  // The rays of the main view's camera at the capture, over the whole back buffer.
  const int w = m_pDevice->GetWidth(), h = m_pDevice->GetHeight();
  int Viewport[4];
  memcpy(Viewport, m_Viewport, sizeof(Viewport));
  const bool bScissor = m_bScissor, bItemScissor = m_bItemScissor;
  m_Viewport[0] = m_Viewport[1] = 0;
  m_Viewport[2] = w;
  m_Viewport[3] = h;
  m_bScissor = m_bItemScissor = false;
  SPipelineKey Key = {};
  Key.nProgram = eD3D12P_RayDebug;
  Key.nState = GS_NODEPTHTEST;
  Key.nCull = R_CULL_NONE;
  SDrawConstants dc = DefaultDrawConstants();
  const Matrix44 ClipToWorld = GetInverted44(m_pRayScene->GetViewProj());
  memcpy(dc.mParams0, ClipToWorld.data, sizeof(dc.mParams0));
  Set4(dc.vParams0, 1.0f / w, 1.0f / h, (float)CV_d3d12_RTDebug, std::max(CV_d3d12_RTDistance, 1.0f));
  const Vec3 &vCamera = m_pRayScene->GetCamera();
  const uint32 nMask = (uint32)CV_d3d12_RTMask & 0xff;
  Set4(dc.vParams1, vCamera.x, vCamera.y, vCamera.z, 0);
  memcpy(&dc.vParams1[3], &nMask, sizeof(nMask));
  DrawImmediate(Key, dc, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, VERTEX_FORMAT_P3F, s_FullScreen, 3);
  memcpy(m_Viewport, Viewport, sizeof(Viewport));
  m_bScissor = bScissor;
  m_bItemScissor = bItemScissor;
}
