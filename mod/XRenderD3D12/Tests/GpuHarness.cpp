// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: GpuHarness.cpp
//	Description: The unit tests' compute harness (GpuHarness.h): the
//	device, the root signature, a scene for rays, a run of a kernel and
//	its read back, a run of the renderer's CMAA2 kernels.
//
//////////////////////////////////////////////////////////////////////

#include "GpuHarness.h"
#include "TestFramework.h"
#include "Shaders/Constants.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
  //! The descriptor heap's first slot of g_Textures (t0 space1): a run's textures, then null ones.
  constexpr UINT TEXTURE_SLOT = 8;

  void Ok(HRESULT hr, const char *szWhat)
  {
    if (FAILED(hr))
    {
      char sz[160];
      snprintf(sz, sizeof(sz), "%s failed (0x%08X)", szWhat, (unsigned)hr);
      throw std::runtime_error(sz);
    }
  }

  std::string Narrow(const wchar_t *wsz)
  {
    std::string s;
    for (; *wsz; wsz++)
      s += *wsz < 128 ? (char)*wsz : '?';
    return s;
  }

  D3D12_RESOURCE_BARRIER Transition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES eFrom, D3D12_RESOURCE_STATES eTo)
  {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = pRes;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = eFrom;
    b.Transition.StateAfter = eTo;
    return b;
  }

  std::unique_ptr<XrTest::CGpu> &GpuPtr()
  {
    static std::unique_ptr<XrTest::CGpu> s_pGpu;
    return s_pGpu;
  }
}

XrTest::SGpuOptions &XrTest::GpuOptions()
{
  static SGpuOptions s_Options;
  return s_Options;
}

XrTest::CGpu &XrTest::Gpu()
{
  if (!GpuPtr())
    GpuPtr() = std::make_unique<CGpu>();
  return *GpuPtr();
}

bool XrTest::GpuReady()
{
  return GpuPtr() != nullptr;
}

void XrTest::CheckBatchedStats(const char *szWhat, const Barriers::SStats &Immediate, const Barriers::SStats &Batched)
{
  auto Print = [&](const char *szMode, const Barriers::SStats &s) {
    std::printf("  %s %-9s %5u requests, %5u calls (%5u after work), %5u transitions, %4u UAV, %4u UAV of all\n", szWhat, szMode,
                s.nRequests, s.nCalls, s.nDrains, s.nTransitions, s.nUav, s.nUavAll);
  };
  Print("immediate", Immediate);
  Print("batched", Batched);
  CHECK(Batched.nRequests == Immediate.nRequests);
  CHECK(Batched.nCalls < Immediate.nCalls);
  CHECK(Batched.nDrains <= Immediate.nDrains);
  CHECK(Batched.nTransitions <= Immediate.nTransitions);
}

XrTest::CGpu::CGpu()
{
  if (GpuOptions().bDebug)
  {
    ComPtr<ID3D12Debug> pDebug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug))))
      pDebug->EnableDebugLayer();
  }
  ComPtr<IDXGIFactory6> pFactory;
  Ok(CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory)), "CreateDXGIFactory2");
  // The first hardware adapter (by the system's preference for performance) that makes a device, else WARP.
  ComPtr<IDXGIAdapter1> pAdapter;
  for (UINT i = 0; !GpuOptions().bWarp && !m_pDevice &&
                   SUCCEEDED(pFactory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&pAdapter)));
       i++)
  {
    DXGI_ADAPTER_DESC1 Desc;
    pAdapter->GetDesc1(&Desc);
    if (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue;
    if (SUCCEEDED(D3D12CreateDevice(pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice))))
      m_Name = Narrow(Desc.Description);
  }
  if (!m_pDevice)
  {
    ComPtr<IDXGIAdapter> pWarp;
    Ok(pFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarp)), "EnumWarpAdapter");
    Ok(D3D12CreateDevice(pWarp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice)), "D3D12CreateDevice (WARP)");
    m_Name = "WARP";
    m_bWarp = true;
  }
  // The highest shader model the device runs, asked from the highest this SDK knows down.
  for (int n = D3D_HIGHEST_SHADER_MODEL; n >= D3D_SHADER_MODEL_5_1; n--)
  {
    D3D12_FEATURE_DATA_SHADER_MODEL Sm = { (D3D_SHADER_MODEL)n };
    if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &Sm, sizeof(Sm))))
    {
      m_nShaderModel = Sm.HighestShaderModel;
      break;
    }
  }
  if (GpuOptions().bDebug)
    m_pDevice.As(&m_pInfoQueue);
  // Scenes for rays: raytracing tier 1.1 (inline RayQuery).
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5 = {};
  if (SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &Options5, sizeof(Options5))) &&
      Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1)
    m_pDevice.As(&m_pDevice5);

  D3D12_COMMAND_QUEUE_DESC Queue = {};
  Queue.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  Ok(m_pDevice->CreateCommandQueue(&Queue, IID_PPV_ARGS(&m_pQueue)), "CreateCommandQueue");
  Ok(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_pAllocator)), "CreateCommandAllocator");
  Ok(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_pAllocator.Get(), nullptr, IID_PPV_ARGS(&m_pList)),
     "CreateCommandList");
  Ok(m_pList->Close(), "Close");
  if (m_pDevice5)
    m_pList.As(&m_pList4);
  Ok(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence)), "CreateFence");
  m_hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  // Each list's transitions checked against each other (forgotten at its end: its resources go with the run).
  m_Barriers.SetCensus(true);

  // The descriptor heap: 0 g_Buffers[0] (a scene's corners, null without one), 1-4 null textures (2D, cube, 2D array, 3D),
  // TEXTURE_SLOT.. g_Textures (a run's textures, null ones).
  D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  HeapDesc.NumDescriptors = TEXTURE_SLOT + (UINT)MAX_TEST_TEXTURES;
  HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Ok(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_pHeap)), "CreateDescriptorHeap");
  m_nDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC Null = {};
    Null.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Null.Format = DXGI_FORMAT_R32_TYPELESS;
    Null.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    Null.Buffer.NumElements = 1;
    Null.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    m_pDevice->CreateShaderResourceView(nullptr, &Null, CpuHandle(0));
    const D3D12_SRV_DIMENSION Dims[4] = { D3D12_SRV_DIMENSION_TEXTURE2D, D3D12_SRV_DIMENSION_TEXTURECUBE, D3D12_SRV_DIMENSION_TEXTURE2DARRAY,
                                          D3D12_SRV_DIMENSION_TEXTURE3D };
    for (UINT i = 0; i < 4; i++)
    {
      Null = {};
      Null.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      Null.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      Null.ViewDimension = Dims[i];
      if (i == 0)
        Null.Texture2D.MipLevels = 1;
      else if (i == 1)
        Null.TextureCube.MipLevels = 1;
      else if (i == 2)
        Null.Texture2DArray.MipLevels = Null.Texture2DArray.ArraySize = 1;
      else
        Null.Texture3D.MipLevels = 1;
      m_pDevice->CreateShaderResourceView(nullptr, &Null, CpuHandle(1 + i));
    }
    for (UINT i = 0; i < MAX_TEST_TEXTURES; i++)
      NullTexture(TEXTURE_SLOT + i);
  }

  // The root signature of GpuHarness.h.
  constexpr UINT ROOT_DESCRIPTORS = 11, ROOT_TABLES = 5;
  D3D12_ROOT_PARAMETER Params[ROOT_DESCRIPTORS + ROOT_TABLES] = {};
  const struct
  {
    D3D12_ROOT_PARAMETER_TYPE eType;
    UINT nRegister, nSpace;
  } Layout[ROOT_DESCRIPTORS] = {
    { D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 9 }, { D3D12_ROOT_PARAMETER_TYPE_CBV, 2, 0 }, { D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 9 },
    { D3D12_ROOT_PARAMETER_TYPE_SRV, 1, 9 }, { D3D12_ROOT_PARAMETER_TYPE_SRV, 2, 9 }, { D3D12_ROOT_PARAMETER_TYPE_UAV, 0, 9 },
    { D3D12_ROOT_PARAMETER_TYPE_UAV, 3, 0 }, { D3D12_ROOT_PARAMETER_TYPE_CBV, 0, 0 }, { D3D12_ROOT_PARAMETER_TYPE_SRV, 0, 5 },
    { D3D12_ROOT_PARAMETER_TYPE_SRV, 1, 5 }, { D3D12_ROOT_PARAMETER_TYPE_SRV, 2, 5 },
  };
  for (UINT i = 0; i < ROOT_DESCRIPTORS; i++)
  {
    Params[i].ParameterType = Layout[i].eType;
    Params[i].Descriptor.ShaderRegister = Layout[i].nRegister;
    Params[i].Descriptor.RegisterSpace = Layout[i].nSpace;
    Params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  // The unbounded tables: g_Buffers (t0 space4), g_Textures (space1), g_Cubes (2), g_TextureArrays (3), g_Volumes (6).
  const UINT TableSpaces[ROOT_TABLES] = { 4, 1, 2, 3, 6 };
  D3D12_DESCRIPTOR_RANGE Ranges[ROOT_TABLES] = {};
  for (UINT i = 0; i < ROOT_TABLES; i++)
  {
    Ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    Ranges[i].NumDescriptors = UINT_MAX;
    Ranges[i].RegisterSpace = TableSpaces[i];
    D3D12_ROOT_PARAMETER &P = Params[ROOT_DESCRIPTORS + i];
    P.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    P.DescriptorTable.NumDescriptorRanges = 1;
    P.DescriptorTable.pDescriptorRanges = &Ranges[i];
    P.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  // The samplers of Common.hlsli: s0 anisotropic wrap, s1 linear clamp, s2 point wrap, s3 point clamp, s4 the shadow's.
  D3D12_STATIC_SAMPLER_DESC Samplers[5] = {};
  for (UINT i = 0; i < 5; i++)
  {
    D3D12_STATIC_SAMPLER_DESC &S = Samplers[i];
    const bool bWrap = i == 0 || i == 2;
    S.Filter = i == 0 ? D3D12_FILTER_ANISOTROPIC
             : i == 1 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
             : i == 4 ? D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
                      : D3D12_FILTER_MIN_MAG_MIP_POINT;
    S.AddressU = S.AddressV = S.AddressW = bWrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    S.MaxAnisotropy = 16;
    S.ComparisonFunc = i == 4 ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
    S.MaxLOD = D3D12_FLOAT32_MAX;
    S.ShaderRegister = i;
    S.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC Rs = {};
  Rs.NumParameters = ROOT_DESCRIPTORS + ROOT_TABLES;
  Rs.pParameters = Params;
  Rs.NumStaticSamplers = 5;
  Rs.pStaticSamplers = Samplers;
  ComPtr<ID3DBlob> pBlob, pError;
  Ok(D3D12SerializeRootSignature(&Rs, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError), "D3D12SerializeRootSignature");
  Ok(m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pRootSignature)),
     "CreateRootSignature");
  ClearCache();
}

XrTest::CGpu::~CGpu()
{
  if (m_hEvent)
    CloseHandle(m_hEvent);
}

XrTest::ComPtr<ID3D12Resource> XrTest::CGpu::Buffer(D3D12_HEAP_TYPE eHeap, size_t nBytes, D3D12_RESOURCE_STATES eState, bool bUav)
{
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = eHeap;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  Desc.Width = (nBytes + 255) & ~(size_t)255;
  Desc.Height = 1;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.SampleDesc.Count = 1;
  Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Desc.Flags = bUav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> pRes;
  // Committed resources of the default heap start zeroed (no D3D12_HEAP_FLAG_CREATE_NOT_ZEROED).
  Ok(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, eState, nullptr, IID_PPV_ARGS(&pRes)), "CreateCommittedResource");
  return pRes;
}

void XrTest::CGpu::NullTexture(UINT nSlot)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC Null = {};
  Null.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  Null.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  Null.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  Null.Texture2D.MipLevels = 1;
  m_pDevice->CreateShaderResourceView(nullptr, &Null, CpuHandle(nSlot));
}

void XrTest::CGpu::UploadTexture(const STestTexture &Texture, UINT nSlot, std::vector<ComPtr<ID3D12Resource>> &Keep)
{
  UINT nTexel = 0;
  switch (Texture.Format)
  {
  case DXGI_FORMAT_R8_UNORM: nTexel = 1; break;
  case DXGI_FORMAT_R8G8_UNORM: nTexel = 2; break;
  case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R32_FLOAT: nTexel = 4; break;
  case DXGI_FORMAT_R16G16B16A16_FLOAT: nTexel = 8; break;
  case DXGI_FORMAT_R32G32B32A32_FLOAT: nTexel = 16; break;
  default: throw std::runtime_error("a test texture's format the harness does not upload");
  }
  const size_t nRow = (size_t)Texture.nWidth * nTexel;
  if (!Texture.nWidth || !Texture.nHeight || Texture.Data.size() != nRow * Texture.nHeight)
    throw std::runtime_error("a test texture's size does not match its data");
  ComPtr<ID3D12Resource> pTexture = Texture2D(Texture.nWidth, Texture.nHeight, Texture.Format, D3D12_RESOURCE_STATE_COPY_DEST, false);
  CopyRows(pTexture.Get(), Texture, nRow, Keep);
  const D3D12_RESOURCE_BARRIER Read = Transition(pTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_pList->ResourceBarrier(1, &Read);
  D3D12_SHADER_RESOURCE_VIEW_DESC View = {};
  View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  View.Format = Texture.Format;
  View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  View.Texture2D.MipLevels = 1;
  m_pDevice->CreateShaderResourceView(pTexture.Get(), &View, CpuHandle(nSlot));
  Keep.push_back(pTexture);
}

XrTest::ComPtr<ID3D12Resource> XrTest::CGpu::Texture2D(UINT nWidth, UINT nHeight, DXGI_FORMAT Format, D3D12_RESOURCE_STATES eState, bool bUav)
{
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = nWidth;
  Desc.Height = nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = Format;
  Desc.SampleDesc.Count = 1;
  Desc.Flags = bUav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
  ComPtr<ID3D12Resource> pTexture;
  Ok(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, eState, nullptr, IID_PPV_ARGS(&pTexture)),
     "CreateCommittedResource (texture)");
  return pTexture;
}

void XrTest::CGpu::CopyRows(ID3D12Resource *pTexture, const STestTexture &Texture, size_t nRow, std::vector<ComPtr<ID3D12Resource>> &Keep)
{
  // The rows at the pitch a copy wants.
  const D3D12_RESOURCE_DESC Desc = pTexture->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
  UINT64 nBytes = 0;
  m_pDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Layout, nullptr, nullptr, &nBytes);
  std::vector<uint8_t> Rows((size_t)nBytes);
  for (UINT y = 0; y < Texture.nHeight; y++)
    memcpy(Rows.data() + (size_t)Layout.Offset + (size_t)y * Layout.Footprint.RowPitch, Texture.Data.data() + y * nRow, nRow);
  ComPtr<ID3D12Resource> pStaging = Upload(Rows.data(), Rows.size());
  D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
  Dst.pResource = pTexture;
  Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  Src.pResource = pStaging.Get();
  Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  Src.PlacedFootprint = Layout;
  m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  Keep.push_back(pStaging);
}

XrTest::ComPtr<ID3D12Resource> XrTest::CGpu::Upload(const void *pData, size_t nBytes)
{
  ComPtr<ID3D12Resource> pRes = Buffer(D3D12_HEAP_TYPE_UPLOAD, nBytes ? nBytes : 16, D3D12_RESOURCE_STATE_GENERIC_READ, false);
  void *p = nullptr;
  Ok(pRes->Map(0, nullptr, &p), "Map");
  memset(p, 0, (size_t)pRes->GetDesc().Width);
  if (pData && nBytes)
    memcpy(p, pData, nBytes);
  pRes->Unmap(0, nullptr);
  return pRes;
}

D3D12_CPU_DESCRIPTOR_HANDLE XrTest::CGpu::CpuHandle(UINT n) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE h = m_pHeap->GetCPUDescriptorHandleForHeapStart();
  h.ptr += (SIZE_T)n * m_nDescriptorSize;
  return h;
}

D3D12_GPU_DESCRIPTOR_HANDLE XrTest::CGpu::GpuHandle(UINT n) const
{
  D3D12_GPU_DESCRIPTOR_HANDLE h = m_pHeap->GetGPUDescriptorHandleForHeapStart();
  h.ptr += (UINT64)n * m_nDescriptorSize;
  return h;
}

D3D12_GPU_VIRTUAL_ADDRESS XrTest::CGpu::BuildScene(const SRayScene &Scene, std::vector<ComPtr<ID3D12Resource>> &Keep)
{
  // The corners: the BLAS's vertices and g_Buffers[0].
  ComPtr<ID3D12Resource> pCorners = Upload(Scene.Corners.data(), Scene.Corners.size() * sizeof(float));
  D3D12_SHADER_RESOURCE_VIEW_DESC Raw = {};
  Raw.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  Raw.Format = DXGI_FORMAT_R32_TYPELESS;
  Raw.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  Raw.Buffer.NumElements = (UINT)(pCorners->GetDesc().Width / 4);
  Raw.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  m_pDevice->CreateShaderResourceView(pCorners.Get(), &Raw, CpuHandle(0));

  D3D12_RAYTRACING_GEOMETRY_DESC Geom = {};
  Geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  Geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  Geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  Geom.Triangles.VertexCount = (UINT)(Scene.Corners.size() / 3);
  Geom.Triangles.VertexBuffer.StartAddress = pCorners->GetGPUVirtualAddress();
  Geom.Triangles.VertexBuffer.StrideInBytes = 12;
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC Blas = {};
  Blas.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  Blas.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  Blas.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  Blas.Inputs.NumDescs = 1;
  Blas.Inputs.pGeometryDescs = &Geom;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO Info = {};
  m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&Blas.Inputs, &Info);
  ComPtr<ID3D12Resource> pBlas = Buffer(D3D12_HEAP_TYPE_DEFAULT, (size_t)Info.ResultDataMaxSizeInBytes,
                                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true);
  ComPtr<ID3D12Resource> pBlasScratch = Buffer(D3D12_HEAP_TYPE_DEFAULT, (size_t)Info.ScratchDataSizeInBytes,
                                               D3D12_RESOURCE_STATE_COMMON, true);
  Blas.DestAccelerationStructureData = pBlas->GetGPUVirtualAddress();
  Blas.ScratchAccelerationStructureData = pBlasScratch->GetGPUVirtualAddress();

  // The instance: the identity, InstanceID 0 (g_RayInstances[0]).
  D3D12_RAYTRACING_INSTANCE_DESC Instance = {};
  Instance.Transform[0][0] = Instance.Transform[1][1] = Instance.Transform[2][2] = 1;
  Instance.InstanceMask = Scene.nMask & 0xff;
  Instance.AccelerationStructure = pBlas->GetGPUVirtualAddress();
  ComPtr<ID3D12Resource> pInstance = Upload(&Instance, sizeof(Instance));
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC Tlas = {};
  Tlas.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  Tlas.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  Tlas.Inputs.NumDescs = 1;
  Tlas.Inputs.InstanceDescs = pInstance->GetGPUVirtualAddress();
  m_pDevice5->GetRaytracingAccelerationStructurePrebuildInfo(&Tlas.Inputs, &Info);
  ComPtr<ID3D12Resource> pTlas = Buffer(D3D12_HEAP_TYPE_DEFAULT, (size_t)Info.ResultDataMaxSizeInBytes,
                                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true);
  ComPtr<ID3D12Resource> pTlasScratch = Buffer(D3D12_HEAP_TYPE_DEFAULT, (size_t)Info.ScratchDataSizeInBytes,
                                               D3D12_RESOURCE_STATE_COMMON, true);
  Tlas.DestAccelerationStructureData = pTlas->GetGPUVirtualAddress();
  Tlas.ScratchAccelerationStructureData = pTlasScratch->GetGPUVirtualAddress();

  m_pList4->BuildRaytracingAccelerationStructure(&Blas, 0, nullptr);
  D3D12_RESOURCE_BARRIER Uav = {};
  Uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  Uav.UAV.pResource = pBlas.Get();
  m_pList4->ResourceBarrier(1, &Uav);
  m_pList4->BuildRaytracingAccelerationStructure(&Tlas, 0, nullptr);
  Uav.UAV.pResource = pTlas.Get();
  m_pList4->ResourceBarrier(1, &Uav);
  Keep.insert(Keep.end(), { pCorners, pBlas, pBlasScratch, pInstance, pTlas, pTlasScratch });
  return pTlas->GetGPUVirtualAddress();
}

ID3D12PipelineState *XrTest::CGpu::Pipeline(const SBytecode &Shader)
{
  ComPtr<ID3D12PipelineState> &pPso = m_Pipelines[Shader.pData];
  if (!pPso)
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};
    Desc.pRootSignature = m_pRootSignature.Get();
    Desc.CS = { Shader.pData, Shader.nSize };
    Ok(m_pDevice->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&pPso)), "CreateComputePipelineState");
  }
  return pPso.Get();
}

void XrTest::CGpu::Execute()
{
  FlushBarriers(false);
  const HRESULT hrClose = m_pList->Close();
  if (FAILED(hrClose))
    CheckDebugMessages();  // why (with the debug layer)
  Ok(hrClose, "Close");
  ID3D12CommandList *pLists[] = { m_pList.Get() };
  m_pQueue->ExecuteCommandLists(1, pLists);
  m_Barriers.OnListExecuted();
  Ok(m_pQueue->Signal(m_pFence.Get(), ++m_nFence), "Signal");
  if (m_pFence->GetCompletedValue() < m_nFence)
  {
    m_pFence->SetEventOnCompletion(m_nFence, m_hEvent);
    WaitForSingleObject(m_hEvent, INFINITE);
  }
  Ok(m_pDevice->GetDeviceRemovedReason(), "the device (removed)");
  CheckDebugMessages();
  // The harness's own transitions name the states their resources have (the census of each list).
  for (const Barriers::SProblem &p : m_Barriers.TakeProblems())
  {
    char sz[160];
    snprintf(sz, sizeof(sz), "barrier problem %d: subresource %u, expected state 0x%X, got 0x%X", (int)p.eKind, p.nSub,
             (unsigned)p.Expected, (unsigned)p.Got);
    Fail(__FILE__, __LINE__, sz);
  }
}

void XrTest::CGpu::Move(ID3D12Resource *pRes, D3D12_RESOURCE_STATES eFrom, D3D12_RESOURCE_STATES eTo)
{
  m_Barriers.Transition(pRes, eFrom, eTo);
  if (m_Barriers.GetMode() == Barriers::MODE_IMMEDIATE)
    FlushBarriers(false);
}

void XrTest::CGpu::Want(ID3D12Resource *pRes, D3D12_RESOURCE_STATES eTo)
{
  m_Barriers.Require(pRes, eTo);
  if (m_Barriers.GetMode() == Barriers::MODE_IMMEDIATE)
    FlushBarriers(false);
}

void XrTest::CGpu::UavAll()
{
  m_Barriers.UavAll();
  if (m_Barriers.GetMode() == Barriers::MODE_IMMEDIATE)
    FlushBarriers(false);
}

void XrTest::CGpu::FlushBarriers(bool bWork)
{
  m_Barriers.Flush([&](UINT n, const D3D12_RESOURCE_BARRIER *p) { m_pList->ResourceBarrier(n, p); }, bWork);
}

void XrTest::CGpu::CheckDebugMessages()
{
  if (!m_pInfoQueue)
    return;
  for (UINT64 i = 0, n = m_pInfoQueue->GetNumStoredMessages(); i < n; i++)
  {
    SIZE_T nBytes = 0;
    m_pInfoQueue->GetMessage(i, nullptr, &nBytes);
    std::vector<char> Buffer(nBytes);
    D3D12_MESSAGE *pMsg = (D3D12_MESSAGE *)Buffer.data();
    m_pInfoQueue->GetMessage(i, pMsg, &nBytes);
    if (pMsg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
      Fail(__FILE__, __LINE__, std::string("debug layer: ") + pMsg->pDescription);
  }
  m_pInfoQueue->ClearStoredMessages();
}

void XrTest::CGpu::ClearCache()
{
  // A new buffer: zeroed as it is made.
  m_pCache = Buffer(D3D12_HEAP_TYPE_DEFAULT, XR_PT_CACHE_BYTES, D3D12_RESOURCE_STATE_COMMON, true);
}

std::vector<uint32> XrTest::CGpu::Run(const SDispatch &d)
{
  const size_t nOutBytes = d.nOut * 16;
  ComPtr<ID3D12Resource> pTest = Upload(&d.Test, sizeof(d.Test));
  SPathTraceConstants Zero = {};
  ComPtr<ID3D12Resource> pPT = Upload(d.pPathTrace ? d.pPathTrace : &Zero, sizeof(SPathTraceConstants));
  ComPtr<ID3D12Resource> pIn = Upload(d.In.data(), d.In.size() * 4);
  ComPtr<ID3D12Resource> pInstances = Upload(d.pInstances, d.nInstanceBytes);
  ComPtr<ID3D12Resource> pLights = Upload(d.pLights, d.nLightBytes);
  SFrameConstants ZeroFrame = {};
  ComPtr<ID3D12Resource> pFrame = Upload(d.pFrame ? d.pFrame : &ZeroFrame, sizeof(SFrameConstants));
  ComPtr<ID3D12Resource> pRayInstances = Upload(d.pScene ? &d.pScene->Instance : nullptr, sizeof(SRayInstance));
  if (d.pScene && !HasRaytracing())
    throw std::runtime_error("a scene for rays on a device without raytracing tier 1.1");
  if (d.Textures.size() > MAX_TEST_TEXTURES)
    throw std::runtime_error("more test textures than the harness binds");
  ComPtr<ID3D12Resource> pOut = Buffer(D3D12_HEAP_TYPE_DEFAULT, nOutBytes, D3D12_RESOURCE_STATE_COMMON, true);
  ComPtr<ID3D12Resource> pReadback = Buffer(D3D12_HEAP_TYPE_READBACK, nOutBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);

  Ok(m_pAllocator->Reset(), "Reset");
  Ok(m_pList->Reset(m_pAllocator.Get(), Pipeline(d.Shader)), "Reset");
  std::vector<ComPtr<ID3D12Resource>> Keep;
  // Without a scene the root SRV of t0 space5 names the input: no kernel without one reads it.
  const D3D12_GPU_VIRTUAL_ADDRESS nScene = d.pScene ? BuildScene(*d.pScene, Keep) : pIn->GetGPUVirtualAddress();
  for (size_t i = 0; i < d.Textures.size(); i++)
    UploadTexture(d.Textures[i], TEXTURE_SLOT + (UINT)i, Keep);
  D3D12_RESOURCE_BARRIER Before[] = {
    Transition(pOut.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
    Transition(m_pCache.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
  };
  m_pList->ResourceBarrier(2, Before);
  m_pList->SetComputeRootSignature(m_pRootSignature.Get());
  m_pList->SetComputeRootConstantBufferView(0, pTest->GetGPUVirtualAddress());
  m_pList->SetComputeRootConstantBufferView(1, pPT->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(2, pIn->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(3, pInstances->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(4, pLights->GetGPUVirtualAddress());
  m_pList->SetComputeRootUnorderedAccessView(5, pOut->GetGPUVirtualAddress());
  m_pList->SetComputeRootUnorderedAccessView(6, m_pCache->GetGPUVirtualAddress());
  m_pList->SetComputeRootConstantBufferView(7, pFrame->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(8, nScene);
  m_pList->SetComputeRootShaderResourceView(9, pRayInstances->GetGPUVirtualAddress());
  m_pList->SetComputeRootShaderResourceView(10, pLights->GetGPUVirtualAddress());
  ID3D12DescriptorHeap *pHeaps[] = { m_pHeap.Get() };
  m_pList->SetDescriptorHeaps(1, pHeaps);
  for (UINT i = 0; i < 5; i++)
    m_pList->SetComputeRootDescriptorTable(11 + i, GpuHandle(i == 1 ? TEXTURE_SLOT : i));
  m_pList->Dispatch(d.nGroups[0], d.nGroups[1], d.nGroups[2]);
  D3D12_RESOURCE_BARRIER After[] = {
    Transition(pOut.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
    Transition(m_pCache.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
  };
  m_pList->ResourceBarrier(2, After);
  m_pList->CopyBufferRegion(pReadback.Get(), 0, pOut.Get(), 0, nOutBytes);
  Execute();
  // The next run sees null textures where it has none (the GPU is done with this one's).
  for (size_t i = 0; i < d.Textures.size(); i++)
    NullTexture(TEXTURE_SLOT + (UINT)i);

  std::vector<uint32> Out(d.nOut * 4);
  void *p = nullptr;
  D3D12_RANGE Range = { 0, nOutBytes };
  Ok(pReadback->Map(0, &Range, &p), "Map");
  memcpy(Out.data(), p, nOutBytes);
  D3D12_RANGE None = { 0, 0 };
  pReadback->Unmap(0, &None);
  return Out;
}

std::vector<uint32> XrTest::CGpu::RunBarrierSteps(const SBytecode &Step, uint32 nItems, const std::vector<uint32> &Adds, size_t nSplit)
{
  const size_t nBytes = (size_t)nItems * 16;
  constexpr size_t STRIDE = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
  std::vector<uint8_t> Steps(std::max<size_t>(Adds.size(), 1) * STRIDE);
  for (size_t s = 0; s < Adds.size(); s++)
  {
    STestConstants c;
    c.nTest[0] = nItems;
    c.nTest[1] = Adds[s];
    memcpy(&Steps[s * STRIDE], &c, sizeof(c));
  }
  ComPtr<ID3D12Resource> pSteps = Upload(Steps.data(), Steps.size());
  SPathTraceConstants Zero = {};
  ComPtr<ID3D12Resource> pPT = Upload(&Zero, sizeof(Zero));
  SFrameConstants ZeroFrame = {};
  ComPtr<ID3D12Resource> pFrame = Upload(&ZeroFrame, sizeof(ZeroFrame));
  ComPtr<ID3D12Resource> pNone = Upload(nullptr, 256);
  // A buffer (zeroed as it is made) whose state the tracker keeps: it decays to COMMON when a list has run.
  ComPtr<ID3D12Resource> pOut = Buffer(D3D12_HEAP_TYPE_DEFAULT, nBytes, D3D12_RESOURCE_STATE_COMMON, true);
  ComPtr<ID3D12Resource> pReadback = Buffer(D3D12_HEAP_TYPE_READBACK, nBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  m_Barriers.Register(pOut.Get(), D3D12_RESOURCE_STATE_COMMON, 1, true);

  auto Open = [&]() {
    Ok(m_pAllocator->Reset(), "Reset");
    Ok(m_pList->Reset(m_pAllocator.Get(), Pipeline(Step)), "Reset");
    m_Barriers.OnListOpened();
    // Run's bindings (the kernel reads b0 space9 and writes u0 space9 only).
    m_pList->SetComputeRootSignature(m_pRootSignature.Get());
    m_pList->SetComputeRootConstantBufferView(1, pPT->GetGPUVirtualAddress());
    for (UINT n : { 2u, 3u, 4u, 8u, 9u, 10u })
      m_pList->SetComputeRootShaderResourceView(n, pNone->GetGPUVirtualAddress());
    m_pList->SetComputeRootUnorderedAccessView(5, pOut->GetGPUVirtualAddress());
    m_pList->SetComputeRootUnorderedAccessView(6, pOut->GetGPUVirtualAddress());
    m_pList->SetComputeRootConstantBufferView(7, pFrame->GetGPUVirtualAddress());
    ID3D12DescriptorHeap *pHeaps[] = { m_pHeap.Get() };
    m_pList->SetDescriptorHeaps(1, pHeaps);
    for (UINT i = 0; i < 5; i++)
      m_pList->SetComputeRootDescriptorTable(11 + i, GpuHandle(i == 1 ? TEXTURE_SLOT : i));
  };
  Open();
  for (size_t s = 0; s < Adds.size(); s++)
  {
    if (s == nSplit && s > 0)
    {
      Execute();
      Open();
    }
    m_Barriers.Require(pOut.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_Barriers.Uav(pOut.Get());
    m_pList->SetComputeRootConstantBufferView(0, pSteps->GetGPUVirtualAddress() + s * STRIDE);
    FlushBarriers();
    m_pList->Dispatch((nItems + 63) / 64, 1, 1);
  }
  m_Barriers.Require(pOut.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  FlushBarriers();
  m_pList->CopyBufferRegion(pReadback.Get(), 0, pOut.Get(), 0, nBytes);
  Execute();
  m_Barriers.Forget(pOut.Get());

  std::vector<uint32> Out(nBytes / 4);
  void *p = nullptr;
  D3D12_RANGE Range = { 0, nBytes };
  Ok(pReadback->Map(0, &Range, &p), "Map");
  memcpy(Out.data(), p, nBytes);
  D3D12_RANGE None = { 0, 0 };
  pReadback->Unmap(0, &None);
  return Out;
}

bool XrTest::CGpu::HasTypedStore(DXGI_FORMAT Format)
{
  D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = { Format };
  return SUCCEEDED(m_pDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))) &&
         (Support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) && (Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
}

void XrTest::CGpu::CreateWide()
{
  if (m_pWideRootSignature)
    return;
  // D3D12Compute.cpp's CreateWideRootSignature: t0..t3, then u0..u7, in one table; s0 point clamp.
  constexpr UINT SRVS = 4, UAVS = 8;
  D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
  Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  Ranges[0].NumDescriptors = SRVS;
  Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  Ranges[1].NumDescriptors = UAVS;
  Ranges[1].OffsetInDescriptorsFromTableStart = SRVS;
  D3D12_ROOT_PARAMETER Param = {};
  Param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  Param.DescriptorTable.NumDescriptorRanges = 2;
  Param.DescriptorTable.pDescriptorRanges = Ranges;
  Param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_STATIC_SAMPLER_DESC Sampler = {};
  Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
  Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  Sampler.MaxLOD = D3D12_FLOAT32_MAX;
  Sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC Rs = {};
  Rs.NumParameters = 1;
  Rs.pParameters = &Param;
  Rs.NumStaticSamplers = 1;
  Rs.pStaticSamplers = &Sampler;
  ComPtr<ID3DBlob> pBlob, pError;
  Ok(D3D12SerializeRootSignature(&Rs, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError), "D3D12SerializeRootSignature (wide)");
  Ok(m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pWideRootSignature)),
     "CreateRootSignature (wide)");
  D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  HeapDesc.NumDescriptors = SRVS + UAVS;
  HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Ok(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&m_pWideHeap)), "CreateDescriptorHeap (wide)");
  D3D12_INDIRECT_ARGUMENT_DESC Arg = {};
  Arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
  D3D12_COMMAND_SIGNATURE_DESC Sig = {};
  Sig.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
  Sig.NumArgumentDescs = 1;
  Sig.pArgumentDescs = &Arg;
  Ok(m_pDevice->CreateCommandSignature(&Sig, nullptr, IID_PPV_ARGS(&m_pDispatchSignature)), "CreateCommandSignature");
}

ID3D12PipelineState *XrTest::CGpu::WidePipeline(const SBytecode &Shader)
{
  ComPtr<ID3D12PipelineState> &pPso = m_WidePipelines[Shader.pData];
  if (!pPso)
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};
    Desc.pRootSignature = m_pWideRootSignature.Get();
    Desc.CS = { Shader.pData, Shader.nSize };
    Ok(m_pDevice->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&pPso)), "CreateComputePipelineState (wide)");
  }
  return pPso.Get();
}

std::vector<uint8_t> XrTest::CGpu::RunCmaa2(const STestTexture &Picture, const SCmaa2Kernels &Kernels, int nFrames)
{
  const UINT w = Picture.nWidth, h = Picture.nHeight;
  DXGI_FORMAT Typeless, Srgb;
  if (Picture.Format == DXGI_FORMAT_R8G8B8A8_UNORM)
  {
    Typeless = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    Srgb = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  }
  else if (Picture.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
  {
    Typeless = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    Srgb = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
  }
  else
    throw std::runtime_error("CMAA2's picture is not R8G8B8A8_UNORM or B8G8R8A8_UNORM");
  const size_t nRow = (size_t)w * 4;
  if (!w || !h || Picture.Data.size() != nRow * h || nFrames < 1)
    throw std::runtime_error("CMAA2's picture's size does not match its data");
  if (!HasTypedStore(Picture.Format))
    throw std::runtime_error("no typed UAV store of CMAA2's picture's format");
  CreateWide();

  // PostAA.cpp's buffers: the shapes (u2), the quads (u3), the colours (u4), the counters (u6), the arguments (u7).
  const UINT64 nPixels = (UINT64)w * h;
  const UINT64 Bytes[5] = { nPixels / 4 * 4, (nPixels + 5) / 6 * 4, nPixels / 2 * 8, 16 * 4, 4 * 4 };
  ComPtr<ID3D12Resource> Buffers[5];
  for (int i = 0; i < 5; i++)
    Buffers[i] = Buffer(D3D12_HEAP_TYPE_DEFAULT, (size_t)Bytes[i], D3D12_RESOURCE_STATE_COMMON, true);
  ID3D12Resource *pArgs = Buffers[4].Get();
  // The picture as uploaded (each frame's copy source), CMAA2's picture, the edges (4 bits a pixel), the lists' heads.
  ComPtr<ID3D12Resource> pSource = Texture2D(w, h, Typeless, D3D12_RESOURCE_STATE_COPY_DEST, false);
  ComPtr<ID3D12Resource> pColor = Texture2D(w, h, Typeless, D3D12_RESOURCE_STATE_COPY_DEST, true);
  ComPtr<ID3D12Resource> pEdges = Texture2D((w + 1) / 2, h, DXGI_FORMAT_R8_TYPELESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
  ComPtr<ID3D12Resource> pHeads = Texture2D((w + 1) / 2, (h + 1) / 2, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

  // The table: t0 the picture (sRGB), t1..t3 null; u0 the picture (UNORM), u1 the edges, u2..u4 the lists, u5 the heads,
  // u6 the counters, u7 the arguments.
  const D3D12_CPU_DESCRIPTOR_HANDLE Base = m_pWideHeap->GetCPUDescriptorHandleForHeapStart();
  auto Slot = [&](UINT n) {
    D3D12_CPU_DESCRIPTOR_HANDLE c = Base;
    c.ptr += (SIZE_T)n * m_nDescriptorSize;
    return c;
  };
  D3D12_SHADER_RESOURCE_VIEW_DESC SRV = {};
  SRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  SRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  SRV.Texture2D.MipLevels = 1;
  SRV.Format = Srgb;
  m_pDevice->CreateShaderResourceView(pColor.Get(), &SRV, Slot(0));
  SRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  for (UINT i = 1; i < 4; i++)
    m_pDevice->CreateShaderResourceView(nullptr, &SRV, Slot(i));
  D3D12_UNORDERED_ACCESS_VIEW_DESC UAV = {};
  UAV.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  UAV.Format = Picture.Format;
  m_pDevice->CreateUnorderedAccessView(pColor.Get(), nullptr, &UAV, Slot(4));
  UAV.Format = DXGI_FORMAT_R8_UINT;
  m_pDevice->CreateUnorderedAccessView(pEdges.Get(), nullptr, &UAV, Slot(5));
  UAV.Format = DXGI_FORMAT_R32_UINT;
  m_pDevice->CreateUnorderedAccessView(pHeads.Get(), nullptr, &UAV, Slot(9));
  UAV = {};
  UAV.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  const UINT Strides[3] = { 4, 4, 8 };
  for (UINT i = 0; i < 3; i++)
  {
    UAV.Buffer.StructureByteStride = Strides[i];
    UAV.Buffer.NumElements = (UINT)(Bytes[i] / Strides[i]);
    m_pDevice->CreateUnorderedAccessView(Buffers[i].Get(), nullptr, &UAV, Slot(6 + i));
  }
  UAV.Buffer.StructureByteStride = 0;
  UAV.Format = DXGI_FORMAT_R32_TYPELESS;
  UAV.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  UAV.Buffer.NumElements = 16;
  m_pDevice->CreateUnorderedAccessView(Buffers[3].Get(), nullptr, &UAV, Slot(10));
  UAV.Buffer.NumElements = 4;
  m_pDevice->CreateUnorderedAccessView(pArgs, nullptr, &UAV, Slot(11));

  const D3D12_RESOURCE_DESC ColorDesc = pColor->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
  UINT64 nReadBytes = 0;
  m_pDevice->GetCopyableFootprints(&ColorDesc, 0, 1, 0, &Layout, nullptr, nullptr, &nReadBytes);
  ComPtr<ID3D12Resource> pReadback = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nReadBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);

  Ok(m_pAllocator->Reset(), "Reset");
  Ok(m_pList->Reset(m_pAllocator.Get(), nullptr), "Reset");
  std::vector<ComPtr<ID3D12Resource>> Keep;
  CopyRows(pSource.Get(), Picture, nRow, Keep);
  auto UavBarrier = [&]() { UavAll(); };
  auto Dispatch = [&](const SBytecode &k, UINT x, UINT y) {
    m_pList->SetPipelineState(WidePipeline(k));
    FlushBarriers();
    m_pList->Dispatch(x, y, 1);
  };
  auto Indirect = [&](const SBytecode &k) {
    m_pList->SetPipelineState(WidePipeline(k));
    FlushBarriers();
    m_pList->ExecuteIndirect(m_pDispatchSignature.Get(), 1, pArgs, 0, nullptr, 0);
  };
  Move(pSource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
  ID3D12DescriptorHeap *pHeaps[] = { m_pWideHeap.Get() };
  m_pList->SetDescriptorHeaps(1, pHeaps);
  m_pList->SetComputeRootSignature(m_pWideRootSignature.Get());
  m_pList->SetComputeRootDescriptorTable(0, m_pWideHeap->GetGPUDescriptorHandleForHeapStart());
  constexpr D3D12_RESOURCE_STATES UAV_STATE = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, ARGS = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  constexpr D3D12_RESOURCE_STATES READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  for (int f = 0; f < nFrames; f++)
  {
    // As DrawCmaa2Passes: the picture copied in, the counters cleared once, the edges, the shapes and the colours by the
    // arguments of their counts (a barrier between frames: the renderer's are separate submissions).
    if (f > 0)
    {
      Move(pColor.Get(), UAV_STATE, D3D12_RESOURCE_STATE_COPY_DEST);
      UavBarrier();
    }
    FlushBarriers();
    m_pList->CopyResource(pColor.Get(), pSource.Get());
    Move(pColor.Get(), D3D12_RESOURCE_STATE_COPY_DEST, READ);
    Move(pArgs, D3D12_RESOURCE_STATE_COMMON, UAV_STATE);
    if (f == 0)
    {
      Dispatch(Kernels.DispatchArgs, 1, 2);
      UavBarrier();
    }
    Dispatch(Kernels.Edges, (w + 27) / 28, (h + 27) / 28);
    UavBarrier();
    Dispatch(Kernels.DispatchArgs, 2, 1);
    Move(pArgs, UAV_STATE, ARGS);
    UavBarrier();
    Indirect(Kernels.Candidates);
    Move(pArgs, ARGS, UAV_STATE);
    UavBarrier();
    Dispatch(Kernels.DispatchArgs, 1, 2);
    Move(pArgs, UAV_STATE, ARGS);
    UavBarrier();
    Move(pColor.Get(), READ, UAV_STATE);
    Indirect(Kernels.Apply);
    Move(pArgs, ARGS, D3D12_RESOURCE_STATE_COMMON);
  }
  Move(pColor.Get(), UAV_STATE, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
  Dst.pResource = pReadback.Get();
  Dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  Dst.PlacedFootprint = Layout;
  Src.pResource = pColor.Get();
  Src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  FlushBarriers();
  m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  Execute();

  std::vector<uint8_t> Out(nRow * h);
  void *p = nullptr;
  D3D12_RANGE Range = { 0, (SIZE_T)nReadBytes };
  Ok(pReadback->Map(0, &Range, &p), "Map");
  for (UINT y = 0; y < h; y++)
    memcpy(Out.data() + y * nRow, (const uint8_t *)p + (size_t)Layout.Offset + (size_t)y * Layout.Footprint.RowPitch, nRow);
  D3D12_RANGE None = { 0, 0 };
  pReadback->Unmap(0, &None);
  return Out;
}

std::vector<float> XrTest::HalfsToFloats(const uint16_t *p, size_t n)
{
  std::vector<float> Out(n);
  for (size_t i = 0; i < n; i++)
  {
    const uint32_t h = p[i], nSign = (h & 0x8000u) << 16, nExp = (h >> 10) & 0x1f, nMant = h & 0x3ffu;
    uint32_t f;
    if (nExp == 0)
    {
      // Zero or a subnormal: mant * 2^-24.
      const float v = std::ldexp((float)nMant, -24);
      memcpy(&f, &v, 4);
      f |= nSign;
    }
    else if (nExp == 31)
      f = nSign | 0x7f800000u | (nMant << 13);
    else
      f = nSign | ((nExp + 112) << 23) | (nMant << 13);
    memcpy(&Out[i], &f, 4);
  }
  return Out;
}

void XrTest::CGpu::CreateComputeRootSignature()
{
  if (m_pComputeRootSignature)
    return;
  // D3D12Compute.cpp's: 0 CBV b0; 1 table t0..t3; 2 table u0..u4 (its static samplers unused by the ocean's and the
  // wakes' kernels).
  D3D12_DESCRIPTOR_RANGE Ranges[2] = {};
  Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  Ranges[0].NumDescriptors = 4;
  Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  Ranges[1].NumDescriptors = 5;
  D3D12_ROOT_PARAMETER Params[3] = {};
  Params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  Params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (int i = 1; i < 3; i++)
  {
    Params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Params[i].DescriptorTable.NumDescriptorRanges = 1;
    Params[i].DescriptorTable.pDescriptorRanges = &Ranges[i - 1];
    Params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }
  D3D12_ROOT_SIGNATURE_DESC Rs = {};
  Rs.NumParameters = 3;
  Rs.pParameters = Params;
  ComPtr<ID3DBlob> pBlob, pError;
  Ok(D3D12SerializeRootSignature(&Rs, D3D_ROOT_SIGNATURE_VERSION_1, &pBlob, &pError), "D3D12SerializeRootSignature (compute)");
  Ok(m_pDevice->CreateRootSignature(0, pBlob->GetBufferPointer(), pBlob->GetBufferSize(), IID_PPV_ARGS(&m_pComputeRootSignature)),
     "CreateRootSignature (compute)");
}

ID3D12PipelineState *XrTest::CGpu::ComputePipeline(const SBytecode &Shader)
{
  ComPtr<ID3D12PipelineState> &pPso = m_ComputePipelines[Shader.pData];
  if (!pPso)
  {
    D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};
    Desc.pRootSignature = m_pComputeRootSignature.Get();
    Desc.CS = { Shader.pData, Shader.nSize };
    Ok(m_pDevice->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&pPso)), "CreateComputePipelineState (compute)");
  }
  return pPso.Get();
}

XrTest::SWakeOutput XrTest::CGpu::RunWake(const std::vector<float> &Start, const std::vector<SWakeFrame> &Frames, const SWakeKernels &Kernels)
{
  constexpr UINT N = XR_WAKE_SIZE, MIPS = XR_WAKE_MIPS, SRVS = 4, UAVS = 5, TABLE = SRVS + UAVS, KERNELS = 6, HEIGHT_TABLE = 5;
  const bool bHeight = Kernels.Height.pData != nullptr;
  if (!Start.empty() && Start.size() != (size_t)N * N * 2)
    throw std::runtime_error("the wakes' start is not the window's texels");
  if (Frames.empty())
    throw std::runtime_error("the wakes run no frame");
  CreateComputeRootSignature();
  constexpr DXGI_FORMAT STATE = DXGI_FORMAT_R32G32_FLOAT, FOAM = DXGI_FORMAT_R32_FLOAT, VIEW = DXGI_FORMAT_R16G16B16A16_FLOAT;
  constexpr D3D12_RESOURCE_STATES UAV_STATE = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  auto Target = [&](UINT nMips, DXGI_FORMAT Format, D3D12_RESOURCE_STATES eState) {
    D3D12_HEAP_PROPERTIES Heap = {};
    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = Desc.Height = N;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = (UINT16)nMips;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> p;
    Ok(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, eState, nullptr, IID_PPV_ARGS(&p)), "CreateCommittedResource (wakes)");
    return p;
  };
  ComPtr<ID3D12Resource> pState = Target(1, STATE, D3D12_RESOURCE_STATE_COPY_DEST), pRows = Target(1, STATE, UAV_STATE),
                         pColumns = Target(1, STATE, UAV_STATE), pFoam[2] = { Target(1, FOAM, UAV_STATE), Target(1, FOAM, UAV_STATE) },
                         pView = Target(MIPS, VIEW, UAV_STATE);
  constexpr DXGI_FORMAT HEIGHTS = DXGI_FORMAT_R16_FLOAT;
  ComPtr<ID3D12Resource> pHeights = bHeight ? Target(MIPS, HEIGHTS, UAV_STATE) : nullptr;
  // The constants of each step and each view at a multiple of 256 bytes, in the order they run.
  constexpr size_t CB_STRIDE = (sizeof(SWakeConstants) + 255) & ~(size_t)255;
  std::vector<const SWakeConstants *> Order;
  for (const SWakeFrame &f : Frames)
  {
    for (const SWakeConstants &s : f.Steps)
      Order.push_back(&s);
    Order.push_back(&f.View);
  }
  std::vector<uint8_t> CBData(CB_STRIDE * Order.size());
  for (size_t i = 0; i < Order.size(); i++)
    memcpy(&CBData[i * CB_STRIDE], Order[i], sizeof(SWakeConstants));
  ComPtr<ID3D12Resource> pCB = Upload(CBData.data(), CBData.size());

  // The tables as Wake.cpp's EnsureWakeTargets writes them.
  ComPtr<ID3D12DescriptorHeap> pHeap;
  D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  HeapDesc.NumDescriptors = KERNELS * TABLE;
  HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Ok(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&pHeap)), "CreateDescriptorHeap (wakes)");
  auto Cpu = [&](UINT n) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = pHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)n * m_nDescriptorSize;
    return h;
  };
  auto Gpu = [&](UINT n) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = pHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)n * m_nDescriptorSize;
    return h;
  };
  auto SRV = [&](UINT nSlot, ID3D12Resource *p, DXGI_FORMAT Format) {
    D3D12_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = Format;
    v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    v.Texture2D.MipLevels = 1;
    m_pDevice->CreateShaderResourceView(p, &v, Cpu(nSlot));
  };
  auto UAV = [&](UINT nSlot, ID3D12Resource *p, DXGI_FORMAT Format, UINT nMip) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC v = {};
    v.Format = Format;
    v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    v.Texture2D.MipSlice = nMip;
    m_pDevice->CreateUnorderedAccessView(p, nullptr, &v, Cpu(nSlot));
  };
  ID3D12Resource *const pIns[3] = { pState.Get(), pRows.Get(), pColumns.Get() }, *const pOuts[3] = { pRows.Get(), pColumns.Get(), pState.Get() };
  for (UINT k = 0; k < KERNELS; k++)
  {
    if (k == HEIGHT_TABLE)
    {
      // The heights' (Wake.cpp's HEIGHT_TABLE): t0 the window, u0..u3 the levels.
      SRV(k * TABLE, pState.Get(), STATE);
      for (UINT i = 1; i < SRVS; i++)
        SRV(k * TABLE + i, nullptr, STATE);
      for (UINT i = 0; i < UAVS; i++)
        UAV(k * TABLE + SRVS + i, i < MIPS ? pHeights.Get() : nullptr, HEIGHTS, i < MIPS ? i : 0);
      continue;
    }
    const bool bView = k >= 3;
    const UINT f = bView ? k - 3 : 0;
    for (UINT i = 0; i < SRVS; i++)
    {
      if (i == 0)
        SRV(k * TABLE, bView ? pState.Get() : pIns[k], STATE);
      else if (bView && i == 1)
        SRV(k * TABLE + 1, pFoam[1 - f].Get(), FOAM);
      else
        SRV(k * TABLE + i, nullptr, STATE);
    }
    for (UINT i = 0; i < UAVS; i++)
    {
      if (!bView)
        UAV(k * TABLE + SRVS + i, i == 0 ? pOuts[k] : nullptr, STATE, 0);
      else if (i == 0)
        UAV(k * TABLE + SRVS, pFoam[f].Get(), FOAM, 0);
      else
        UAV(k * TABLE + SRVS + i, pView.Get(), VIEW, i - 1);
    }
  }

  // The readback of the window, the foam and every level of the view.
  const D3D12_RESOURCE_DESC StateDesc = pState->GetDesc(), FoamDesc = pFoam[0]->GetDesc(), ViewDesc = pView->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT StateLayout = {}, FoamLayout = {};
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> ViewLayout(MIPS);
  UINT64 nStateBytes = 0, nFoamBytes = 0, nViewBytes = 0;
  m_pDevice->GetCopyableFootprints(&StateDesc, 0, 1, 0, &StateLayout, nullptr, nullptr, &nStateBytes);
  m_pDevice->GetCopyableFootprints(&FoamDesc, 0, 1, 0, &FoamLayout, nullptr, nullptr, &nFoamBytes);
  m_pDevice->GetCopyableFootprints(&ViewDesc, 0, MIPS, 0, ViewLayout.data(), nullptr, nullptr, &nViewBytes);
  ComPtr<ID3D12Resource> pStateBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nStateBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  ComPtr<ID3D12Resource> pFoamBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nFoamBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  ComPtr<ID3D12Resource> pViewBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nViewBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);

  Ok(m_pAllocator->Reset(), "Reset");
  Ok(m_pList->Reset(m_pAllocator.Get(), nullptr), "Reset");
  std::vector<ComPtr<ID3D12Resource>> Keep;
  {
    // The window at the start (zeros without one).
    std::vector<uint8_t> Rows((size_t)nStateBytes);
    for (UINT y = 0; y < N && !Start.empty(); y++)
      memcpy(Rows.data() + (size_t)StateLayout.Offset + (size_t)y * StateLayout.Footprint.RowPitch, &Start[(size_t)y * N * 2], N * 8);
    ComPtr<ID3D12Resource> pStaging = Upload(Rows.data(), Rows.size());
    D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
    Dst.pResource = pState.Get();
    Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Src.pResource = pStaging.Get();
    Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Src.PlacedFootprint = StateLayout;
    m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
    Keep.push_back(pStaging);
  }
  // The states kept by the tracker, as the renderer's (TrackState): the window as its copy left it, the other targets made
  // in UNORDERED_ACCESS; forgotten at the end (their addresses come again).
  std::vector<ID3D12Resource *> Kept = { pState.Get(), pRows.Get(), pColumns.Get(), pFoam[0].Get(), pFoam[1].Get(), pView.Get() };
  if (bHeight)
    Kept.push_back(pHeights.Get());
  struct SForget
  {
    Barriers::CBarrierTracker &t;
    std::vector<ID3D12Resource *> &v;
    ~SForget()
    {
      for (ID3D12Resource *p : v)
        t.Forget(p);
    }
  } Forget{ m_Barriers, Kept };
  for (ID3D12Resource *p : Kept)
    m_Barriers.Register(p, p == pState.Get() ? D3D12_RESOURCE_STATE_COPY_DEST : UAV_STATE, 1, false);
  // Where the renderer has the water read them (PIXEL_SHADER_RESOURCE): a compute list has no such state.
  constexpr D3D12_RESOURCE_STATES PSR = READ;
  // EnsureWakeTargets' states: the foam's textures where the view's kernel reads the one before, the view where the water
  // reads it, the heights where its vertex program does.
  for (ComPtr<ID3D12Resource> &p : pFoam)
    Want(p.Get(), READ);
  Want(pView.Get(), PSR);
  if (bHeight)
    Want(pHeights.Get(), READ);
  ID3D12DescriptorHeap *pHeaps[] = { pHeap.Get() };
  m_pList->SetDescriptorHeaps(1, pHeaps);
  m_pList->SetComputeRootSignature(m_pComputeRootSignature.Get());
  size_t nCB = 0;
  auto Dispatch = [&](const SBytecode &k, UINT nTable, UINT x, UINT y) {
    m_pList->SetPipelineState(ComputePipeline(k));
    m_pList->SetComputeRootDescriptorTable(1, Gpu(nTable * TABLE));
    m_pList->SetComputeRootDescriptorTable(2, Gpu(nTable * TABLE + SRVS));
    FlushBarriers();
    m_pList->Dispatch(x, y, 1);
  };
  // UpdateWakes' order and requests, a frame after another.
  UINT f = 0;
  for (size_t i = 0; i < Frames.size(); i++)
  {
    for (size_t s = 0; s < Frames[i].Steps.size(); s++)
    {
      m_pList->SetComputeRootConstantBufferView(0, pCB->GetGPUVirtualAddress() + nCB++ * CB_STRIDE);
      Want(pState.Get(), READ);
      Want(pRows.Get(), UAV_STATE);
      Dispatch(Kernels.Rows, 0, 1, N);
      Want(pRows.Get(), READ);
      Want(pColumns.Get(), UAV_STATE);
      Dispatch(Kernels.Columns, 1, 1, N);
      Want(pColumns.Get(), READ);
      Want(pState.Get(), UAV_STATE);
      Dispatch(Kernels.RowsInverse, 2, 1, N);
    }
    m_pList->SetComputeRootConstantBufferView(0, pCB->GetGPUVirtualAddress() + nCB++ * CB_STRIDE);
    f = i > 0 ? 1 - f : 0;
    Want(pState.Get(), READ);
    Want(pFoam[1 - f].Get(), READ);
    Want(pFoam[f].Get(), UAV_STATE);
    Want(pView.Get(), UAV_STATE);
    Dispatch(Kernels.View, 3 + f, N / 16, N / 16);
    if (bHeight)
    {
      Want(pHeights.Get(), UAV_STATE);
      Dispatch(Kernels.Height, HEIGHT_TABLE, N / 16, N / 16);
      Want(pHeights.Get(), READ);
    }
    Want(pFoam[f].Get(), READ);
    Want(pView.Get(), PSR);
  }
  Want(pState.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  Want(pFoam[f].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  Want(pView.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (bHeight)
    Want(pHeights.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto Copy = [&](ID3D12Resource *pSrc, UINT nSub, ID3D12Resource *pDst, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &Layout) {
    D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
    Dst.pResource = pDst;
    Dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Dst.PlacedFootprint = Layout;
    Src.pResource = pSrc;
    Src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Src.SubresourceIndex = nSub;
    FlushBarriers();
    m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  };
  Copy(pState.Get(), 0, pStateBack.Get(), StateLayout);
  Copy(pFoam[f].Get(), 0, pFoamBack.Get(), FoamLayout);
  for (UINT m = 0; m < MIPS; m++)
    Copy(pView.Get(), m, pViewBack.Get(), ViewLayout[m]);
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> HeightsLayout(MIPS);
  ComPtr<ID3D12Resource> pHeightsBack;
  UINT64 nHeightsBytes = 0;
  if (bHeight)
  {
    const D3D12_RESOURCE_DESC HeightsDesc = pHeights->GetDesc();
    m_pDevice->GetCopyableFootprints(&HeightsDesc, 0, MIPS, 0, HeightsLayout.data(), nullptr, nullptr, &nHeightsBytes);
    pHeightsBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nHeightsBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
    for (UINT m = 0; m < MIPS; m++)
      Copy(pHeights.Get(), m, pHeightsBack.Get(), HeightsLayout[m]);
  }
  Execute();

  SWakeOutput Out;
  void *p = nullptr;
  D3D12_RANGE None = { 0, 0 };
  D3D12_RANGE Range = { 0, (SIZE_T)nStateBytes };
  Ok(pStateBack->Map(0, &Range, &p), "Map");
  Out.State.resize((size_t)N * N * 2);
  for (UINT y = 0; y < N; y++)
    memcpy(&Out.State[(size_t)y * N * 2], (const uint8_t *)p + (size_t)StateLayout.Offset + (size_t)y * StateLayout.Footprint.RowPitch, N * 8);
  pStateBack->Unmap(0, &None);
  Range = { 0, (SIZE_T)nFoamBytes };
  Ok(pFoamBack->Map(0, &Range, &p), "Map");
  Out.Foam.resize((size_t)N * N);
  for (UINT y = 0; y < N; y++)
    memcpy(&Out.Foam[(size_t)y * N], (const uint8_t *)p + (size_t)FoamLayout.Offset + (size_t)y * FoamLayout.Footprint.RowPitch, N * 4);
  pFoamBack->Unmap(0, &None);
  Range = { 0, (SIZE_T)nViewBytes };
  Ok(pViewBack->Map(0, &Range, &p), "Map");
  for (UINT m = 0; m < MIPS; m++)
  {
    const UINT w = N >> m;
    std::vector<uint16_t> Halfs((size_t)w * w * 4);
    for (UINT y = 0; y < w; y++)
      memcpy(&Halfs[(size_t)y * w * 4], (const uint8_t *)p + (size_t)ViewLayout[m].Offset + (size_t)y * ViewLayout[m].Footprint.RowPitch, (size_t)w * 8);
    Out.View[m] = HalfsToFloats(Halfs.data(), Halfs.size());
  }
  pViewBack->Unmap(0, &None);
  if (bHeight)
  {
    Range = { 0, (SIZE_T)nHeightsBytes };
    Ok(pHeightsBack->Map(0, &Range, &p), "Map");
    for (UINT m = 0; m < MIPS; m++)
    {
      const UINT w = N >> m;
      std::vector<uint16_t> Halfs((size_t)w * w);
      for (UINT y = 0; y < w; y++)
        memcpy(&Halfs[(size_t)y * w], (const uint8_t *)p + (size_t)HeightsLayout[m].Offset + (size_t)y * HeightsLayout[m].Footprint.RowPitch, (size_t)w * 2);
      Out.Heights[m] = HalfsToFloats(Halfs.data(), Halfs.size());
    }
    pHeightsBack->Unmap(0, &None);
  }
  return Out;
}

XrTest::SOceanOutput XrTest::CGpu::RunOcean(const std::vector<float> &H0, const SOceanConstants &Constants, const SOceanKernels &Kernels)
{
  SOceanKernels k = Kernels;
  k.Foam = k.FoamView = {};
  return RunOceanFrames(H0, { Constants }, k);
}

XrTest::SOceanOutput XrTest::CGpu::RunOceanFrames(const std::vector<float> &H0, const std::vector<SOceanConstants> &Frames,
                                                  const SOceanKernels &Kernels)
{
  constexpr UINT N = XR_OCEAN_SIZE, C = XR_OCEAN_CASCADES, MIPS = XR_OCEAN_MIPS, L4 = XR_OCEAN_LEVEL4;
  // The tables: 0 rows, 1 columns, 2 waves, 3 their mips, 4 + t the foam's state into target t, 6 + t the view of it, 8
  // the view's mips, 9 the caustics (their levels in the kernel).
  constexpr UINT SRVS = 4, UAVS = 5, TABLE = SRVS + UAVS, KERNELS = 10, CC = XR_CAUSTICS_CASCADES, CM = XR_CAUSTICS_MIPS;
  if (H0.size() != (size_t)C * N * N * 4)
    throw std::runtime_error("the ocean's h0 is not its cascades' texels");
  if (Frames.empty())
    throw std::runtime_error("the ocean runs no frame");
  const bool bFoam = Kernels.Foam.pData != nullptr, bCaustics = Kernels.Caustics.pData != nullptr;
  if (bFoam && !Kernels.FoamView.pData)
    throw std::runtime_error("the ocean's foam without its view's kernel");
  CreateComputeRootSignature();
  auto Pso = [&](const SBytecode &Shader) { return ComputePipeline(Shader); };
  auto Array = [&](UINT w, UINT nSlices, UINT nMips, DXGI_FORMAT Format, D3D12_RESOURCE_STATES eState, bool bUav) {
    D3D12_HEAP_PROPERTIES Heap = {};
    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = Desc.Height = w;
    Desc.DepthOrArraySize = (UINT16)nSlices;
    Desc.MipLevels = (UINT16)nMips;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = bUav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> p;
    Ok(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, eState, nullptr, IID_PPV_ARGS(&p)), "CreateCommittedResource (ocean)");
    return p;
  };
  constexpr DXGI_FORMAT F32 = DXGI_FORMAT_R32G32B32A32_FLOAT, F16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
  constexpr D3D12_RESOURCE_STATES UAV_STATE = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  ComPtr<ID3D12Resource> pH0 = Array(N, C, 1, F32, D3D12_RESOURCE_STATE_COPY_DEST, false);
  ComPtr<ID3D12Resource> pRows = Array(N, 2 * C, 1, F32, UAV_STATE, true), pFields = Array(N, 2 * C, 1, F32, UAV_STATE, true);
  ComPtr<ID3D12Resource> pWaves = Array(N, C, MIPS, F16, UAV_STATE, true), pLevel4 = Array(L4, C, 1, F16, UAV_STATE, true);
  ComPtr<ID3D12Resource> pFoam[2], pFoamView, pFoamLevel4;
  if (bFoam)
  {
    for (ComPtr<ID3D12Resource> &p : pFoam)
      p = Array(N, C, 1, F32, UAV_STATE, true);
    pFoamView = Array(N, C, MIPS, F16, UAV_STATE, true);
    pFoamLevel4 = Array(L4, C, 1, F16, UAV_STATE, true);
  }
  ComPtr<ID3D12Resource> pCaustics;
  if (bCaustics)
    pCaustics = Array(N, CC, CM, F16, UAV_STATE, true);
  // Each frame's constants at a multiple of 256 bytes.
  constexpr size_t CB_STRIDE = (sizeof(SOceanConstants) + 255) & ~(size_t)255;
  std::vector<uint8_t> CBData(CB_STRIDE * Frames.size());
  for (size_t f = 0; f < Frames.size(); f++)
    memcpy(&CBData[f * CB_STRIDE], &Frames[f], sizeof(SOceanConstants));
  ComPtr<ID3D12Resource> pCB = Upload(CBData.data(), CBData.size());

  // The tables as OceanFFT.cpp's EnsureOceanTargets writes them.
  ComPtr<ID3D12DescriptorHeap> pHeap;
  D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
  HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  HeapDesc.NumDescriptors = KERNELS * TABLE;
  HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  Ok(m_pDevice->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&pHeap)), "CreateDescriptorHeap (ocean)");
  auto Cpu = [&](UINT n) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = pHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)n * m_nDescriptorSize;
    return h;
  };
  auto Gpu = [&](UINT n) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = pHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)n * m_nDescriptorSize;
    return h;
  };
  auto SRV = [&](UINT nSlot, ID3D12Resource *p, DXGI_FORMAT Format, UINT nSlices) {
    D3D12_SHADER_RESOURCE_VIEW_DESC v = {};
    v.Format = Format;
    v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    v.Texture2DArray.MipLevels = 1;
    v.Texture2DArray.ArraySize = nSlices;
    m_pDevice->CreateShaderResourceView(p, &v, Cpu(nSlot));
  };
  auto UAV = [&](UINT nSlot, ID3D12Resource *p, DXGI_FORMAT Format, UINT nSlices, UINT nMip) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC v = {};
    v.Format = Format;
    v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    v.Texture2DArray.MipSlice = nMip;
    v.Texture2DArray.ArraySize = nSlices;
    m_pDevice->CreateUnorderedAccessView(p, nullptr, &v, Cpu(nSlot));
  };
  for (UINT k = 0; k < KERNELS; k++)
    for (UINT i = 0; i < SRVS; i++)
      SRV(k * TABLE + i, nullptr, F32, 2 * C);
  SRV(0 * TABLE, pH0.Get(), F32, C);
  SRV(1 * TABLE, pRows.Get(), F32, 2 * C);
  SRV(2 * TABLE, pFields.Get(), F32, 2 * C);
  SRV(3 * TABLE, pLevel4.Get(), F16, C);
  for (UINT i = 0; i < UAVS; i++)
  {
    UAV(0 * TABLE + SRVS + i, i == 0 ? pRows.Get() : nullptr, F32, 2 * C, 0);
    UAV(1 * TABLE + SRVS + i, i == 0 ? pFields.Get() : nullptr, F32, 2 * C, 0);
    UAV(2 * TABLE + SRVS + i, i < 4 ? pWaves.Get() : pLevel4.Get(), F16, C, i < 4 ? i : 0);
    UAV(3 * TABLE + SRVS + i, pWaves.Get(), F16, C, 4 + i);
  }
  if (bFoam)
  {
    for (UINT t = 0; t < 2; t++)
    {
      // The state: t0 the fields, t1 the other target; u0 the target. Its view: t0 the target; u0..u3 the view's levels
      // 0..3, u4 level 4.
      SRV((4 + t) * TABLE, pFields.Get(), F32, 2 * C);
      SRV((4 + t) * TABLE + 1, pFoam[1 - t].Get(), F32, C);
      SRV((6 + t) * TABLE, pFoam[t].Get(), F32, C);
      for (UINT i = 0; i < UAVS; i++)
      {
        UAV((4 + t) * TABLE + SRVS + i, i == 0 ? pFoam[t].Get() : nullptr, F32, C, 0);
        UAV((6 + t) * TABLE + SRVS + i, i < 4 ? pFoamView.Get() : pFoamLevel4.Get(), F16, C, i < 4 ? i : 0);
      }
    }
    // The view's mips: t0 level 4, u0..u4 levels 4..8.
    SRV(8 * TABLE, pFoamLevel4.Get(), F16, C);
    for (UINT i = 0; i < UAVS; i++)
      UAV(8 * TABLE + SRVS + i, pFoamView.Get(), F16, C, 4 + i);
  }
  if (bCaustics)
  {
    // The caustics: t0 the waves' level 0; u0..u4 their levels 0..4.
    SRV(9 * TABLE, pWaves.Get(), F16, C);
    for (UINT i = 0; i < UAVS; i++)
      UAV(9 * TABLE + SRVS + i, pCaustics.Get(), F16, CC, i);
  }

  // The readback of every sub resource of the fields, of the waves and of the foam.
  const D3D12_RESOURCE_DESC FieldsDesc = pFields->GetDesc(), WavesDesc = pWaves->GetDesc();
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> FieldsLayout(2 * C), WavesLayout(C * MIPS);
  UINT64 nFieldsBytes = 0, nWavesBytes = 0;
  m_pDevice->GetCopyableFootprints(&FieldsDesc, 0, 2 * C, 0, FieldsLayout.data(), nullptr, nullptr, &nFieldsBytes);
  m_pDevice->GetCopyableFootprints(&WavesDesc, 0, C * MIPS, 0, WavesLayout.data(), nullptr, nullptr, &nWavesBytes);
  ComPtr<ID3D12Resource> pFieldsBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nFieldsBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  ComPtr<ID3D12Resource> pWavesBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nWavesBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  // The foam's state in 32 bits (OceanFFT.cpp's FOAM_FORMAT), its view in 16 as the waves.
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> StateLayout(C), FoamLayout(C * MIPS);
  UINT64 nStateBytes = 0, nFoamBytes = 0;
  ComPtr<ID3D12Resource> pStateBack, pFoamBack;
  if (bFoam)
  {
    const D3D12_RESOURCE_DESC StateDesc = pFoam[0]->GetDesc(), FoamDesc = pFoamView->GetDesc();
    m_pDevice->GetCopyableFootprints(&StateDesc, 0, C, 0, StateLayout.data(), nullptr, nullptr, &nStateBytes);
    m_pDevice->GetCopyableFootprints(&FoamDesc, 0, C * MIPS, 0, FoamLayout.data(), nullptr, nullptr, &nFoamBytes);
    pStateBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nStateBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
    pFoamBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nFoamBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  }
  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> CausticsLayout(CC * CM);
  UINT64 nCausticsBytes = 0;
  ComPtr<ID3D12Resource> pCausticsBack;
  if (bCaustics)
  {
    const D3D12_RESOURCE_DESC CausticsDesc = pCaustics->GetDesc();
    m_pDevice->GetCopyableFootprints(&CausticsDesc, 0, CC * CM, 0, CausticsLayout.data(), nullptr, nullptr, &nCausticsBytes);
    pCausticsBack = Buffer(D3D12_HEAP_TYPE_READBACK, (size_t)nCausticsBytes, D3D12_RESOURCE_STATE_COPY_DEST, false);
  }

  Ok(m_pAllocator->Reset(), "Reset");
  Ok(m_pList->Reset(m_pAllocator.Get(), nullptr), "Reset");
  std::vector<ComPtr<ID3D12Resource>> Keep;
  {
    // h0, a slice a cascade.
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Layout(C);
    UINT64 nBytes = 0;
    const D3D12_RESOURCE_DESC Desc = pH0->GetDesc();
    m_pDevice->GetCopyableFootprints(&Desc, 0, C, 0, Layout.data(), nullptr, nullptr, &nBytes);
    std::vector<uint8_t> Rows((size_t)nBytes);
    for (UINT c = 0; c < C; c++)
      for (UINT y = 0; y < N; y++)
        memcpy(Rows.data() + (size_t)Layout[c].Offset + (size_t)y * Layout[c].Footprint.RowPitch, &H0[((size_t)c * N + y) * N * 4], N * 16);
    ComPtr<ID3D12Resource> pStaging = Upload(Rows.data(), Rows.size());
    for (UINT c = 0; c < C; c++)
    {
      D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
      Dst.pResource = pH0.Get();
      Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      Dst.SubresourceIndex = c;
      Src.pResource = pStaging.Get();
      Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      Src.PlacedFootprint = Layout[c];
      m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
    }
    Keep.push_back(pStaging);
  }
  ID3D12DescriptorHeap *pHeaps[] = { pHeap.Get() };
  m_pList->SetDescriptorHeaps(1, pHeaps);
  m_pList->SetComputeRootSignature(m_pComputeRootSignature.Get());
  auto Dispatch = [&](const SBytecode &k, UINT nTable, UINT x, UINT y, UINT z) {
    m_pList->SetPipelineState(Pso(k));
    m_pList->SetComputeRootDescriptorTable(1, Gpu(nTable * TABLE));
    m_pList->SetComputeRootDescriptorTable(2, Gpu(nTable * TABLE + SRVS));
    FlushBarriers();
    m_pList->Dispatch(x, y, z);
  };
  // The states kept by the tracker, as the renderer's (TrackState): h0 as its copy left it, the targets made in
  // UNORDERED_ACCESS; forgotten at the end (their addresses come again).
  std::vector<ID3D12Resource *> Kept = { pH0.Get(), pRows.Get(), pFields.Get(), pWaves.Get(), pLevel4.Get() };
  if (bFoam)
    Kept.insert(Kept.end(), { pFoam[0].Get(), pFoam[1].Get(), pFoamView.Get(), pFoamLevel4.Get() });
  if (bCaustics)
    Kept.push_back(pCaustics.Get());
  struct SForget
  {
    Barriers::CBarrierTracker &t;
    std::vector<ID3D12Resource *> &v;
    ~SForget()
    {
      for (ID3D12Resource *p : v)
        t.Forget(p);
    }
  } Forget{ m_Barriers, Kept };
  for (ID3D12Resource *p : Kept)
    m_Barriers.Register(p, p == pH0.Get() ? D3D12_RESOURCE_STATE_COPY_DEST : UAV_STATE, 1, false);
  // Where the renderer has the water read them (PIXEL_SHADER_RESOURCE): a compute list has no such state.
  constexpr D3D12_RESOURCE_STATES PSR = READ;
  // EnsureOceanTargets' states: the waves, the foam's view and the caustics where the water reads them, the foam's
  // states where the foam kernel does.
  for (ID3D12Resource *p : { pWaves.Get(), pFoamView.Get(), pCaustics.Get() })
    if (p)
      Want(p, PSR);
  if (bFoam)
    for (ComPtr<ID3D12Resource> &p : pFoam)
      Want(p.Get(), READ);
  // UpdateOcean's order and requests, a frame after another.
  UINT t = 0;
  for (size_t f = 0; f < Frames.size(); f++)
  {
    m_pList->SetComputeRootConstantBufferView(0, pCB->GetGPUVirtualAddress() + f * CB_STRIDE);
    Want(pH0.Get(), READ);
    Want(pRows.Get(), UAV_STATE);
    Dispatch(Kernels.Rows, 0, 1, N, C);
    Want(pRows.Get(), READ);
    Want(pFields.Get(), UAV_STATE);
    Dispatch(Kernels.Columns, 1, 1, N, C);
    Want(pFields.Get(), READ);
    Want(pWaves.Get(), UAV_STATE);
    Want(pLevel4.Get(), UAV_STATE);
    Dispatch(Kernels.Waves, 2, N / 16, N / 16, C);
    Want(pLevel4.Get(), READ);
    Dispatch(Kernels.Mips, 3, 1, 1, C);
    if (bCaustics)
    {
      Want(pWaves.Get(), READ);
      Want(pCaustics.Get(), UAV_STATE);
      Dispatch(Kernels.Caustics, 9, N / XR_CAUSTICS_TILE, N / XR_CAUSTICS_TILE, CC);
      Want(pCaustics.Get(), PSR);
    }
    Want(pWaves.Get(), PSR);
    if (bFoam)
    {
      t = f > 0 ? 1 - t : 0;
      Want(pFields.Get(), READ);
      Want(pFoam[1 - t].Get(), READ);
      Want(pFoam[t].Get(), UAV_STATE);
      Dispatch(Kernels.Foam, 4 + t, N / 16, N / 16, C);
      Want(pFoam[t].Get(), READ);
      Want(pFoamView.Get(), UAV_STATE);
      Want(pFoamLevel4.Get(), UAV_STATE);
      Dispatch(Kernels.FoamView, 6 + t, N / 16, N / 16, C);
      Want(pFoamLevel4.Get(), READ);
      Dispatch(Kernels.Mips, 8, 1, 1, C);
      Want(pFoamView.Get(), PSR);
    }
  }
  Want(pFields.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  Want(pWaves.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  if (bFoam)
  {
    Want(pFoam[t].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    Want(pFoamView.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  }
  if (bCaustics)
    Want(pCaustics.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
  auto Copy = [&](ID3D12Resource *pSrc, ID3D12Resource *pDst, const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> &Layout) {
    for (UINT i = 0; i < (UINT)Layout.size(); i++)
    {
      D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
      Dst.pResource = pDst;
      Dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      Dst.PlacedFootprint = Layout[i];
      Src.pResource = pSrc;
      Src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      Src.SubresourceIndex = i;
      FlushBarriers();
      m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
    }
  };
  Copy(pFields.Get(), pFieldsBack.Get(), FieldsLayout);
  Copy(pWaves.Get(), pWavesBack.Get(), WavesLayout);
  if (bFoam)
  {
    Copy(pFoam[t].Get(), pStateBack.Get(), StateLayout);
    Copy(pFoamView.Get(), pFoamBack.Get(), FoamLayout);
  }
  if (bCaustics)
    Copy(pCaustics.Get(), pCausticsBack.Get(), CausticsLayout);
  Execute();

  SOceanOutput Out;
  void *p = nullptr;
  D3D12_RANGE None = { 0, 0 };
  D3D12_RANGE Range = { 0, (SIZE_T)nFieldsBytes };
  Ok(pFieldsBack->Map(0, &Range, &p), "Map");
  Out.Fields.resize((size_t)2 * C * N * N * 4);
  for (UINT s = 0; s < 2 * C; s++)
    for (UINT y = 0; y < N; y++)
      memcpy(&Out.Fields[((size_t)s * N + y) * N * 4], (const uint8_t *)p + (size_t)FieldsLayout[s].Offset + (size_t)y * FieldsLayout[s].Footprint.RowPitch, N * 16);
  pFieldsBack->Unmap(0, &None);
  Range = { 0, (SIZE_T)nWavesBytes };
  Ok(pWavesBack->Map(0, &Range, &p), "Map");
  for (UINT m = 0; m < MIPS; m++)
  {
    const UINT w = N >> m;
    std::vector<uint16_t> Halfs((size_t)C * w * w * 4);
    for (UINT c = 0; c < C; c++)
    {
      // Sub resource m + c * MIPS.
      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &l = WavesLayout[m + c * MIPS];
      for (UINT y = 0; y < w; y++)
        memcpy(&Halfs[((size_t)c * w + y) * w * 4], (const uint8_t *)p + (size_t)l.Offset + (size_t)y * l.Footprint.RowPitch, (size_t)w * 8);
    }
    Out.Waves[m] = HalfsToFloats(Halfs.data(), Halfs.size());
  }
  pWavesBack->Unmap(0, &None);
  if (bFoam)
  {
    Range = { 0, (SIZE_T)nStateBytes };
    Ok(pStateBack->Map(0, &Range, &p), "Map");
    Out.FoamState.resize((size_t)C * N * N * 4);
    for (UINT c = 0; c < C; c++)
      for (UINT y = 0; y < N; y++)
        memcpy(&Out.FoamState[((size_t)c * N + y) * N * 4], (const uint8_t *)p + (size_t)StateLayout[c].Offset + (size_t)y * StateLayout[c].Footprint.RowPitch, N * 16);
    pStateBack->Unmap(0, &None);
    Range = { 0, (SIZE_T)nFoamBytes };
    Ok(pFoamBack->Map(0, &Range, &p), "Map");
    for (UINT m = 0; m < MIPS; m++)
    {
      const UINT w = N >> m;
      std::vector<uint16_t> Halfs((size_t)C * w * w * 4);
      for (UINT c = 0; c < C; c++)
      {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &l = FoamLayout[m + c * MIPS];
        for (UINT y = 0; y < w; y++)
          memcpy(&Halfs[((size_t)c * w + y) * w * 4], (const uint8_t *)p + (size_t)l.Offset + (size_t)y * l.Footprint.RowPitch, (size_t)w * 8);
      }
      Out.Foam[m] = HalfsToFloats(Halfs.data(), Halfs.size());
    }
    pFoamBack->Unmap(0, &None);
  }
  if (bCaustics)
  {
    Range = { 0, (SIZE_T)nCausticsBytes };
    Ok(pCausticsBack->Map(0, &Range, &p), "Map");
    for (UINT m = 0; m < CM; m++)
    {
      const UINT w = N >> m;
      std::vector<uint16_t> Halfs((size_t)CC * w * w * 4);
      for (UINT c = 0; c < CC; c++)
      {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT &l = CausticsLayout[m + c * CM];
        for (UINT y = 0; y < w; y++)
          memcpy(&Halfs[((size_t)c * w + y) * w * 4], (const uint8_t *)p + (size_t)l.Offset + (size_t)y * l.Footprint.RowPitch, (size_t)w * 8);
      }
      Out.Caustics[m] = HalfsToFloats(Halfs.data(), Halfs.size());
    }
    pCausticsBack->Unmap(0, &None);
  }
  return Out;
}
