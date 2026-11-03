// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: GpuHarness.cpp
//	Description: The unit tests' compute harness (GpuHarness.h): the
//	device, the root signature, a scene for rays, a run of a kernel and
//	its read back.
//
//////////////////////////////////////////////////////////////////////

#include "GpuHarness.h"
#include "TestFramework.h"
#include "Shaders/Constants.h"

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
  D3D12_HEAP_PROPERTIES Heap = {};
  Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC Desc = {};
  Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  Desc.Width = Texture.nWidth;
  Desc.Height = Texture.nHeight;
  Desc.DepthOrArraySize = 1;
  Desc.MipLevels = 1;
  Desc.Format = Texture.Format;
  Desc.SampleDesc.Count = 1;
  ComPtr<ID3D12Resource> pTexture;
  Ok(m_pDevice->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pTexture)),
     "CreateCommittedResource (texture)");
  // The rows at the pitch a copy wants.
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layout = {};
  UINT64 nBytes = 0;
  m_pDevice->GetCopyableFootprints(&Desc, 0, 1, 0, &Layout, nullptr, nullptr, &nBytes);
  std::vector<uint8_t> Rows((size_t)nBytes);
  for (UINT y = 0; y < Texture.nHeight; y++)
    memcpy(Rows.data() + (size_t)Layout.Offset + (size_t)y * Layout.Footprint.RowPitch, Texture.Data.data() + y * nRow, nRow);
  ComPtr<ID3D12Resource> pStaging = Upload(Rows.data(), Rows.size());
  D3D12_TEXTURE_COPY_LOCATION Dst = {}, Src = {};
  Dst.pResource = pTexture.Get();
  Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  Src.pResource = pStaging.Get();
  Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  Src.PlacedFootprint = Layout;
  m_pList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  const D3D12_RESOURCE_BARRIER Read = Transition(pTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  m_pList->ResourceBarrier(1, &Read);
  D3D12_SHADER_RESOURCE_VIEW_DESC View = {};
  View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  View.Format = Texture.Format;
  View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  View.Texture2D.MipLevels = 1;
  m_pDevice->CreateShaderResourceView(pTexture.Get(), &View, CpuHandle(nSlot));
  Keep.insert(Keep.end(), { pTexture, pStaging });
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
  Ok(m_pList->Close(), "Close");
  ID3D12CommandList *pLists[] = { m_pList.Get() };
  m_pQueue->ExecuteCommandLists(1, pLists);
  Ok(m_pQueue->Signal(m_pFence.Get(), ++m_nFence), "Signal");
  if (m_pFence->GetCompletedValue() < m_nFence)
  {
    m_pFence->SetEventOnCompletion(m_nFence, m_hEvent);
    WaitForSingleObject(m_hEvent, INFINITE);
  }
  Ok(m_pDevice->GetDeviceRemovedReason(), "the device (removed)");
  CheckDebugMessages();
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
