// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Device.h
//	Description: The Direct3D 12 device, its queue, the swap chain and
//	the frames in flight.
//
//	One direct queue does everything. A frame is recorded into one
//	command list between BeginFrame and EndFrame; EndFrame first lets the
//	upload batch (D3D12Upload.h) execute what it recorded, then executes the
//	frame's list, so data uploaded during a frame is there for its draws.
//	D3D12_FRAMES frames can be in flight; BeginFrame waits for the slot's
//	previous frame.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <span>

constexpr int D3D12_FRAMES = 2;         //!< frames in flight
constexpr int D3D12_BACKBUFFERS = 3;     //!< swap chain buffers

constexpr DXGI_FORMAT D3D12_BACKBUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;   //!< with alpha (the HUD masks through it)
constexpr DXGI_FORMAT D3D12_DEPTH_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr DXGI_FORMAT D3D12_SHADOW_MAP_FORMAT = DXGI_FORMAT_D32_FLOAT;  //!< drawn into; the texture is R32_TYPELESS, read as R32_FLOAT

class CD3D12Device
{
public:
  CD3D12Device() = default;
  ~CD3D12Device();
  CD3D12Device(const CD3D12Device &) = delete;
  CD3D12Device &operator=(const CD3D12Device &) = delete;

  //! Creates the device and a swap chain for hWnd. bDebug: the debug layer and GPU based validation.
  bool Init(HWND hWnd, int nWidth, int nHeight, bool bDebug);
  //! Waits for the GPU and releases everything.
  void Shutdown();
  //! New back buffer size (waits for the GPU).
  bool Resize(int nWidth, int nHeight);

  //! Opens the frame's command list: waits for the slot, resets it, sets the back buffer and the
  //! depth buffer as render targets, the full viewport, the descriptor heap and the root signature.
  void BeginFrame();
  //! Closes and executes the frame (after the upload batch), presents when bPresent.
  void EndFrame(bool bPresent, bool bVSync);
  bool IsInFrame() const { return m_bInFrame; }
  //! Executes the frame recorded so far, waits for it and opens a new command list in the same
  //! frame slot (e.g. to read the back buffer back). The render targets are set up again.
  void FlushFrame();

  //! Waits until the GPU finished everything submitted.
  void WaitForGPU();
  //! Signals the queue's fence and returns the value.
  uint64 Signal();
  uint64 GetCompletedFence() const;
  //! The fence value the current frame (or, between frames, the next one) signals when it ends;
  //! an object used in this frame can go when GetCompletedFence() reaches it.
  uint64 GetFrameFence() const { return m_nNextFence; }
  //! Takes over one reference of p and releases it once the GPU has finished everything
  //! submitted so far and the current frame.
  void DeferredRelease(IUnknown *p);
  //! The same for an object held in a ComPtr (which is left empty).
  template<class T> void DeferredRelease(ComPtr<T> &p) { DeferredRelease(static_cast<IUnknown *>(p.Detach())); }
  //! Releases what the GPU no longer uses (called once per frame).
  void CollectGarbage();

  ID3D12Device *GetDevice() const { return m_pDevice.Get(); }
  ID3D12CommandQueue *GetQueue() const { return m_pQueue.Get(); }
  //! The frame's command list; valid between BeginFrame and EndFrame.
  ID3D12GraphicsCommandList *GetCmdList() const { return m_pCmdList.Get(); }
  //! 0 .. D3D12_FRAMES-1: the slot of the current frame (per frame resources, the upload ring).
  int GetFrameSlot() const { return m_nFrameSlot; }

  int GetWidth() const { return m_nWidth; }
  int GetHeight() const { return m_nHeight; }
  ID3D12Resource *GetBackBuffer() const { return m_pBackBuffers[m_nBackBuffer].Get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRTV() const;
  D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
  //! Sets the back buffer and the depth buffer as render targets, the full viewport and scissor
  //! (ends drawing off screen).
  void SetBackBufferTargets();

  //! An off screen target of nWidth x nHeight: pColor in the back buffer's format (in the state
  //! PIXEL_SHADER_RESOURCE while it is not drawn into) and pDepth (D24S8, DEPTH_WRITE), their views
  //! written to hRTV and hDSV.
  bool CreateOffscreenTargets(int nWidth, int nHeight, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth,
                              D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV);
  //! The same for a cube of nSize: pColor an array of the six faces (one RTV per face in hRTV, in
  //! the order of D3D12_TEXTURECUBE_FACE), pDepth one face's size; drawn a face at a time with
  //! SetOffscreenTargets.
  bool CreateCubeTargets(int nSize, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth, const D3D12_CPU_DESCRIPTOR_HANDLE hRTV[6],
                         D3D12_CPU_DESCRIPTOR_HANDLE hDSV);
  //! Draws into targets of CreateOffscreenTargets instead of the back buffer, all of them, until
  //! SetBackBufferTargets (in a frame). hDSV may be empty: a colour target alone (a render target
  //! of CD3D12Upload::CreateRenderTarget; SPipelineKey::nTarget 1 for R8G8B8A8).
  void SetOffscreenTargets(ID3D12Resource *pColor, D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nWidth, int nHeight);
  bool IsOffscreen() const { return m_pOffscreen != nullptr; }
  //! A shadow map of nSize x nSize: R32_TYPELESS (D3D12_SHADOW_MAP_FORMAT, its DSV written to hDSV,
  //! read as R32_FLOAT), in the state PIXEL_SHADER_RESOURCE while it is not drawn into.
  bool CreateShadowMap(int nSize, ComPtr<ID3D12Resource> &pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV);
  //! Draws into a shadow map of CreateShadowMap (depth only, no colour target) until
  //! SetBackBufferTargets (in a frame).
  void SetShadowMapTarget(ID3D12Resource *pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nSize);
  //! The size of the targets drawn into.
  int GetTargetWidth() const { return m_pOffscreen ? m_nOffscreenWidth : m_nWidth; }
  int GetTargetHeight() const { return m_pOffscreen ? m_nOffscreenHeight : m_nHeight; }

  //! Clears the colour / the depth buffer drawn into (in a frame).
  void ClearColor(const float rgba[4]);
  void ClearDepth(float fDepth, uint8 nStencil, bool bDepth, bool bStencil);

  //! Textures of the back buffer's size for CopyTargets: pColor in the back buffer's format, pDepth
  //! R24G8_TYPELESS (read as R24_UNORM_X8_TYPELESS); both in PIXEL_SHADER_RESOURCE state. Without
  //! pDepth only the colour copy is made.
  bool CreateTargetCopies(ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> *pDepth);
  //! Copies the back buffer and the depth buffer (when pDepth is given) as drawn so far in this
  //! frame into the textures of CreateTargetCopies (PIXEL_SHADER_RESOURCE before and after).
  void CopyTargets(ID3D12Resource *pColor, ID3D12Resource *pDepth);

  //! Copies the current back buffer (as drawn so far in this frame, or the last presented one
  //! between frames) to pDst: nWidth x nHeight pixels of B, G, R, A, rows from the top.
  //! Waits for the GPU.
  bool ReadBackBuffer(uint8 *pDst, int nWidth, int nHeight);
  //! Copies level 0 of pTexture (2D, 4 bytes a pixel, in State and not drawn into) to pDst: nWidth x
  //! nHeight pixels, rows from the top. Waits for the GPU; in a frame the frame recorded so far is
  //! executed (FlushFrame), and the draws go to the back buffer again.
  bool ReadBackTexture(ID3D12Resource *pTexture, D3D12_RESOURCE_STATES State, uint8 *pDst, int nWidth, int nHeight);

  //! The debug layer's messages to the engine's log.
  void LogDebugMessages();

  //! GPU timing (d3d12_GpuTimes): while on, the GPU time of each frame is split into sections,
  //! each from its GpuSection call to the next one or the end of the frame (the first one of a
  //! frame is "other"). A frame's times are read when the GPU has finished it (BeginFrame of its
  //! slot) and add up in GetGpuTimes.
  void SetGpuTiming(bool bOn) { m_bGpuTiming = bOn; }
  //! From here on the GPU time counts for szSection (a string with static storage).
  void GpuSection(const char *szSection);
  //! The sections' GPU milliseconds added up over GetGpuTimedFrames() frames, in the order they
  //! first came; ResetGpuTimes starts again.
  const std::vector<std::pair<const char *, double>> &GetGpuTimes() const { return m_GpuTimes; }
  int GetGpuTimedFrames() const { return m_nGpuTimedFrames; }
  void ResetGpuTimes()
  {
    m_GpuTimes.clear();
    m_nGpuTimedFrames = 0;
  }

  //! Occlusion queries: BeginOcclusion .. EndOcclusion count the samples drawn in between that
  //! pass the depth test, at most OCCLUSION_QUERIES per frame. A frame's counts come when the GPU
  //! has finished it: from the BeginFrame of its slot on, GetOcclusionResults() holds them in the
  //! order of the queries (D3D12_FRAMES frames later).
  static constexpr int OCCLUSION_QUERIES = 256;
  //! The query's index in the frame, -1 when none is left (or out of a frame).
  int BeginOcclusion();
  void EndOcclusion(int nQuery);
  std::span<const uint64> GetOcclusionResults() const { return m_OcclusionResults; }

private:
  static constexpr int GPU_MARKS = 64;              //!< timestamps per frame (the last one ends the frame)
  //! Adds the timestamps of the current slot's last frame (finished) to m_GpuTimes.
  void ReadGpuTimes();
  //! The occlusion counts of the current slot's last frame (finished) into m_OcclusionResults.
  void ReadOcclusionResults();

  bool CreateSwapChainBuffers();
  void ReleaseSwapChainBuffers();
  //! Resets the slot's allocator and the list and binds the heap, root signature and targets.
  void OpenCmdList();
  void WaitForFence(uint64 nValue);
  //! The off screen colour target (or shadow map) back to PIXEL_SHADER_RESOURCE; nothing off screen
  //! any more.
  void EndOffscreen();

  ComPtr<IDXGIFactory4> m_pFactory;
  ComPtr<IDXGIAdapter1> m_pAdapter;
  ComPtr<ID3D12Device> m_pDevice;
  ComPtr<ID3D12CommandQueue> m_pQueue;
  ComPtr<IDXGISwapChain3> m_pSwapChain;
  ComPtr<ID3D12InfoQueue> m_pInfoQueue;
  bool m_bTearing = false;

  ComPtr<ID3D12CommandAllocator> m_pAllocators[D3D12_FRAMES];
  uint64 m_nFrameFence[D3D12_FRAMES] = {};
  ComPtr<ID3D12GraphicsCommandList> m_pCmdList;
  ComPtr<ID3D12Fence> m_pFence;
  HANDLE m_hFenceEvent = nullptr;
  uint64 m_nNextFence = 1;

  ComPtr<ID3D12DescriptorHeap> m_pRTVHeap;
  ComPtr<ID3D12DescriptorHeap> m_pDSVHeap;
  uint32 m_nRTVSize = 0;
  ComPtr<ID3D12Resource> m_pBackBuffers[D3D12_BACKBUFFERS];
  ComPtr<ID3D12Resource> m_pDepth;
  int m_nBackBuffer = 0;
  ID3D12Resource *m_pOffscreen = nullptr;           //!< SetOffscreenTargets: the colour target drawn into (SetShadowMapTarget: the shadow map)
  bool m_bOffscreenShadowMap = false;               //!< m_pOffscreen is a shadow map
  D3D12_CPU_DESCRIPTOR_HANDLE m_hOffscreenRTV = {}, m_hOffscreenDSV = {};
  int m_nOffscreenWidth = 0, m_nOffscreenHeight = 0;

  int m_nWidth = 0, m_nHeight = 0;
  int m_nFrameSlot = 0;
  bool m_bInFrame = false;

  ComPtr<ID3D12QueryHeap> m_pTimestamps;            //!< GPU_MARKS per frame slot; NULL: no GPU timing
  ComPtr<ID3D12Resource> m_pTimestampReadback;      //!< the resolved timestamps, in the same places
  double m_fTimestampMs = 0;                        //!< milliseconds per tick
  bool m_bGpuTiming = false;
  const char *m_GpuSections[D3D12_FRAMES][GPU_MARKS] = {};   //!< the section each timestamp starts
  int m_nGpuMarks[D3D12_FRAMES] = {};
  std::vector<std::pair<const char *, double>> m_GpuTimes;
  int m_nGpuTimedFrames = 0;

  ComPtr<ID3D12QueryHeap> m_pOcclusion;             //!< OCCLUSION_QUERIES per frame slot; NULL: no queries
  ComPtr<ID3D12Resource> m_pOcclusionReadback;      //!< the resolved counts, in the same places
  int m_nOcclusions[D3D12_FRAMES] = {};             //!< queries begun in the slot's frame
  std::vector<uint64> m_OcclusionResults;

  struct SDeferred
  {
    ComPtr<IUnknown> pObject;
    uint64 nFence;
  };
  std::vector<SDeferred> m_Deferred;
};
