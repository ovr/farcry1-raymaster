// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: GpuHarness.h
//	Description: A tiny standalone Direct3D 12 compute harness for the
//	unit tests of the renderer's shader code: a device of its own (the
//	first hardware adapter that makes one, else WARP; --warp: WARP), no
//	window, nothing of the renderer. Run() dispatches one test kernel
//	(Tests/Shaders, compiled by dxc into Shaders/Test_<entry>.h) with the
//	bindings of TestCommon.hlsli and gives back what it wrote to u0 space9.
//	The root signature (one for every kernel):
//	  0  CBV b0 space9  STestConstants
//	  1  CBV b2         SPathTraceConstants (g_PT of PathTrace.hlsl)
//	  2  SRV t0 space9  the input (uint4 words)
//	  3  SRV t1 space9  SRayInstance[] (the layout test)
//	  4  SRV t2 space9  SPTLight[] (the layout test)
//	  5  UAV u0 space9  the output (uint4 words)
//	  6  UAV u3         the radiance cache (g_PathCache of PathCache.hlsli,
//	                    XR_PT_CACHE_BYTES), kept from run to run
//	  7  CBV b0         SFrameConstants (g_Frame of Common.hlsli)
//	  8  SRV t0 space5  the scene for rays (g_RayScene of RayShade.hlsli):
//	                    the TLAS of SDispatch::pScene
//	  9  SRV t1 space5  its SRayInstance (g_RayInstances)
//	  10 SRV t2 space5  SPTLight[] (g_PTLights of PathShade.hlsli), the
//	                    same as 4
//	  11 tables of the descriptor heap, unbounded: t0 space4 the raw
//	  .. buffers (g_Buffers: 0 the scene's corners), t0 space1 the 2D
//	  15 textures (g_Textures: SDispatch::Textures, null ones after them),
//	     t0 space2, 3 and 6 the other textures (null ones)
//	  static samplers s0-s4 as Common.hlsli has them
//	Every run waits for the GPU: the tests are small.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_TESTS_GPUHARNESS_H
#define XRENDERD3D12_TESTS_GPUHARNESS_H

#include "TestTypes.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace XrTest
{
  using Microsoft::WRL::ComPtr;

  struct SGpuOptions
  {
    bool bWarp = false;   //!< WARP, not a hardware adapter
    bool bDebug = false;  //!< the debug layer: its errors fail the test that made them
  };
  SGpuOptions &GpuOptions();

  //! A compiled kernel (the array dxc's -Fh writes).
  struct SBytecode
  {
    const void *pData;
    size_t nSize;
  };
#define XR_BYTECODE(name) XrTest::SBytecode{ g_##name, sizeof(g_##name) }

  //! b0 space9 of the test kernels (STestConstants of TestCommon.hlsli).
  struct STestConstants
  {
    uint32 nTest[4] = {};
    float vTest[4] = {};
    float vTest2[4] = {};
  };

  //! The most textures of a run (SDispatch::Textures).
  constexpr size_t MAX_TEST_TEXTURES = 8;
  //! A 2D texture of a run: one level of nWidth x nHeight texels in Format (R8G8B8A8_UNORM, R8G8_UNORM, R8_UNORM,
  //! R16G16B16A16_FLOAT, R32G32B32A32_FLOAT or R32_FLOAT), Data its texels row by row from the top, packed.
  struct STestTexture
  {
    uint32 nWidth = 0, nHeight = 0;
    DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::vector<uint8_t> Data;
  };
  //! A scene for rays: one instance of triangles (three corners each, not indexed, opaque) with the identity transform,
  //! its BLAS and TLAS built for the run. Its corners are g_Buffers[0] (12 bytes each), which Instance.nGeom and
  //! Instance.nLayout name as the renderer's would.
  struct SRayScene
  {
    std::vector<float> Corners;   //!< x y z of each corner, three a triangle
    uint32 nMask = 0xff;          //!< the instance's mask
    SRayInstance Instance{};      //!< g_RayInstances[0]
  };

  //! The renderer's CMAA2 kernels (AntiAliasCmaa2.hlsl; the edges' at one preset) for CGpu::RunCmaa2.
  struct SCmaa2Kernels
  {
    SBytecode Edges, DispatchArgs, Candidates, Apply;
  };

  //! The FFT ocean's kernels (the renderer's Shaders/OceanFFT.hlsl, as Shaders.cmake compiles them) for CGpu::RunOcean.
  struct SOceanKernels
  {
    SBytecode Rows, Columns, Waves, Mips;
  };
  //! What RunOcean leaves: the fields (2 * XR_OCEAN_CASCADES slices of XR_OCEAN_SIZE x XR_OCEAN_SIZE RGBA32F texels, slice
  //! after slice, row after row) and each of the waves' XR_OCEAN_MIPS levels (XR_OCEAN_CASCADES slices of RGBA16F texels,
  //! as floats).
  struct SOceanOutput
  {
    std::vector<float> Fields;
    std::vector<float> Waves[XR_OCEAN_MIPS];
  };

  //! What a run binds; the output is nOut uint4 items, zeroed before the kernel runs.
  struct SDispatch
  {
    SBytecode Shader{};
    uint32 nGroups[3] = { 1, 1, 1 };
    STestConstants Test;
    const void *pPathTrace = nullptr;     //!< b2: an SPathTraceConstants (else zeros)
    std::vector<uint32> In;               //!< t0 space9, uint4 items
    const void *pInstances = nullptr;     //!< t1 space9
    size_t nInstanceBytes = 0;
    const void *pLights = nullptr;        //!< t2 space9 and t2 space5
    size_t nLightBytes = 0;
    const void *pFrame = nullptr;         //!< b0: an SFrameConstants (else zeros)
    const SRayScene *pScene = nullptr;    //!< t0, t1 space5 and g_Buffers (needs HasRaytracing)
    std::vector<STestTexture> Textures;   //!< g_Textures[0..] (t0 space1), at most MAX_TEST_TEXTURES
    size_t nOut = 1;
  };

  class CGpu
  {
  public:
    CGpu();
    ~CGpu();

    const std::string &AdapterName() const { return m_Name; }
    bool IsWarp() const { return m_bWarp; }
    //! Whether the debug layer reports to the tests (--debug, and the Graphics Tools installed).
    bool HasDebugLayer() const { return m_pInfoQueue != nullptr; }
    //! Whether the device runs Shader Model 6.5 (the path tracer's kernels).
    bool HasShaderModel65() const { return m_nShaderModel >= D3D_SHADER_MODEL_6_5; }
    //! Whether it builds scenes for rays and runs inline RayQuery (raytracing tier 1.1; SDispatch::pScene).
    bool HasRaytracing() const { return m_pDevice5 && m_pList4 && HasShaderModel65(); }

    //! Runs a kernel; the words it wrote to u0 space9 (4 an item).
    std::vector<uint32> Run(const SDispatch &d);
    //! CMAA2 over a picture (R8G8B8A8_UNORM or B8G8R8A8_UNORM, the back buffer's) as PostAA.cpp's DrawCmaa2Passes runs
    //! it: the wide root signature of D3D12Compute.h, its buffers' sizes and views, its kernels in its order (the last two
    //! by ExecuteIndirect), nFrames times on the same buffers with the picture copied in each time (the counters cleared
    //! once, before the first). The picture the last frame leaves, its texels as Picture's. Throws when the device has no
    //! typed UAV store of the format (the renderer leaves the picture then).
    std::vector<uint8_t> RunCmaa2(const STestTexture &Picture, const SCmaa2Kernels &Kernels, int nFrames = 1);
    //! The FFT ocean's pass as OceanFFT.cpp's UpdateOcean runs it: the standard root signature of D3D12Compute.h (b0; t0..t3;
    //! u0..u4), its targets in their formats, its four kernels in its order with its barriers. H0: the cascades' h0 texels
    //! (OceanSpectrum.h's SCascade::H0, one cascade after another), Constants the frame's.
    SOceanOutput RunOcean(const std::vector<float> &H0, const SOceanConstants &Constants, const SOceanKernels &Kernels);
    //! Whether the device stores the format through a typed UAV (CMAA2's output).
    bool HasTypedStore(DXGI_FORMAT Format);
    //! The radiance cache emptied (every word 0).
    void ClearCache();

  private:
    ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE eHeap, size_t nBytes, D3D12_RESOURCE_STATES eState, bool bUav);
    ComPtr<ID3D12Resource> Upload(const void *pData, size_t nBytes);
    ID3D12PipelineState *Pipeline(const SBytecode &Shader);
    //! The scene's BLAS and TLAS built into the open list (the buffers kept in Keep until the run ends); the TLAS.
    D3D12_GPU_VIRTUAL_ADDRESS BuildScene(const SRayScene &Scene, std::vector<ComPtr<ID3D12Resource>> &Keep);
    //! The texture uploaded by the open list into a resource of its own (kept in Keep until the run ends), its SRV in
    //! the heap's slot nSlot.
    void UploadTexture(const STestTexture &Texture, UINT nSlot, std::vector<ComPtr<ID3D12Resource>> &Keep);
    //! A 2D texture of one level in the default heap.
    ComPtr<ID3D12Resource> Texture2D(UINT nWidth, UINT nHeight, DXGI_FORMAT Format, D3D12_RESOURCE_STATES eState, bool bUav);
    //! nRow bytes a row of Texture's data copied by the open list into pTexture (in COPY_DEST; the staging buffer kept in
    //! Keep).
    void CopyRows(ID3D12Resource *pTexture, const STestTexture &Texture, size_t nRow, std::vector<ComPtr<ID3D12Resource>> &Keep);
    //! The wide root signature, its heap and the command signature of an indirect dispatch (RunCmaa2), made at the first.
    void CreateWide();
    ID3D12PipelineState *WidePipeline(const SBytecode &Shader);
    //! A null 2D texture's SRV in the heap's slot nSlot.
    void NullTexture(UINT nSlot);
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT n) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT n) const;
    void Execute();
    void CheckDebugMessages();

    std::string m_Name;
    bool m_bWarp = false;
    D3D_SHADER_MODEL m_nShaderModel = D3D_SHADER_MODEL_5_1;
    ComPtr<ID3D12Device> m_pDevice;
    ComPtr<ID3D12CommandQueue> m_pQueue;
    ComPtr<ID3D12CommandAllocator> m_pAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_pList;
    ComPtr<ID3D12Fence> m_pFence;
    HANDLE m_hEvent = nullptr;
    uint64_t m_nFence = 0;
    ComPtr<ID3D12RootSignature> m_pRootSignature;
    std::map<const void *, ComPtr<ID3D12PipelineState>> m_Pipelines;
    ComPtr<ID3D12Resource> m_pCache;
    ComPtr<ID3D12InfoQueue> m_pInfoQueue;
    ComPtr<ID3D12Device5> m_pDevice5;               //!< with raytracing tier 1.1, else null
    ComPtr<ID3D12GraphicsCommandList4> m_pList4;
    ComPtr<ID3D12DescriptorHeap> m_pHeap;           //!< shader visible: 0 g_Buffers[0], 1-4 null textures, TEXTURE_SLOT.. the
                                                    //!< run's g_Textures (null ones after them)
    UINT m_nDescriptorSize = 0;
    ComPtr<ID3D12RootSignature> m_pWideRootSignature;   //!< D3D12Compute.h's: one table t0..t3, u0..u7; s0 point clamp
    ComPtr<ID3D12DescriptorHeap> m_pWideHeap;           //!< shader visible, that table
    ComPtr<ID3D12CommandSignature> m_pDispatchSignature;
    std::map<const void *, ComPtr<ID3D12PipelineState>> m_WidePipelines;
    ComPtr<ID3D12RootSignature> m_pComputeRootSignature;  //!< D3D12Compute.h's standard one (RunOcean), made at the first
    std::map<const void *, ComPtr<ID3D12PipelineState>> m_ComputePipelines;
  };

  //! n half floats (IEEE 754 binary16) as floats.
  std::vector<float> HalfsToFloats(const uint16_t *p, size_t n);

  //! The tests' device, made at the first use (throws std::runtime_error when there is none).
  CGpu &Gpu();
  //! Whether Gpu() was made.
  bool GpuReady();
}

#endif
