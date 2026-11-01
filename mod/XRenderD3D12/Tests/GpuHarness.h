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
    const void *pLights = nullptr;        //!< t2 space9
    size_t nLightBytes = 0;
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

    //! Runs a kernel; the words it wrote to u0 space9 (4 an item).
    std::vector<uint32> Run(const SDispatch &d);
    //! The radiance cache emptied (every word 0).
    void ClearCache();

  private:
    ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE eHeap, size_t nBytes, D3D12_RESOURCE_STATES eState, bool bUav);
    ComPtr<ID3D12Resource> Upload(const void *pData, size_t nBytes);
    ID3D12PipelineState *Pipeline(const SBytecode &Shader);
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
  };

  //! The tests' device, made at the first use (throws std::runtime_error when there is none).
  CGpu &Gpu();
  //! Whether Gpu() was made.
  bool GpuReady();
}

#endif
