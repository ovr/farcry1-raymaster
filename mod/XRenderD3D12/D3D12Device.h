// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
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
constexpr DXGI_FORMAT D3D12_SCENE_DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT; //!< the scene targets' depth (SetSceneSize): R32_TYPELESS, an upscaler reads it as R32_FLOAT
constexpr DXGI_FORMAT D3D12_VELOCITY_FORMAT = DXGI_FORMAT_R16G16_FLOAT; //!< the scene targets' motion vectors
constexpr DXGI_FORMAT D3D12_UPSCALE_DEPTH_FORMAT = DXGI_FORMAT_R32_FLOAT; //!< the scene targets' depth for an upscaler (with the water's)
constexpr DXGI_FORMAT D3D12_UPSCALE_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM; //!< an upscaler's output (unordered access, which B8G8R8A8 may not have)
constexpr DXGI_FORMAT D3D12_HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;  //!< the main view's colour with r_HDRRendering (Hdr.cpp)
constexpr DXGI_FORMAT D3D12_HDR10_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM; //!< the swap chain of the HDR10 output (PQ, Rec. 2020; HdrOutput.cpp)
constexpr DXGI_FORMAT D3D12_SCRGB_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< the swap chain of the scRGB output (linear Rec. 709, 1 = 80 nits)
constexpr DXGI_FORMAT D3D12_HIGHLIGHT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< the highlight layer (HdrOutput.cpp)

//! What the swap chain shows (d3d12_HDROutput, HdrOutput.cpp).
enum class EDisplayOutput
{
  Sdr,      //!< D3D12_BACKBUFFER_FORMAT, sRGB (G22_P709): the frame as it is drawn
  Hdr10,    //!< D3D12_HDR10_FORMAT, G2084_P2020
  ScRgb,    //!< D3D12_SCRGB_FORMAT, G10_P709
};

//! The display the window is on (CD3D12Device::GetDisplayInfo).
struct SDisplayInfo
{
  bool bHdr = false;                //!< in HDR mode (Windows: Use HDR): its colour space is G2084_P2020
  float fMaxNits = 0;               //!< its peak luminance (DXGI_OUTPUT_DESC1), 0: not reported
  float fMaxFullFrameNits = 0;
  float fMinNits = 0;
  float fSdrWhiteNits = 0;          //!< the brightness Windows gives SDR content on it (the SDR white level), 0: unknown
  char szName[32] = {};             //!< its GDI name (\\.\DISPLAY1), empty: not found
};

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
  //! What everything draws into: the frame target with the highlight layer (SetOutput), else the
  //! swap chain's current buffer.
  ID3D12Resource *GetBackBuffer() const { return m_pFrame ? m_pFrame.Get() : m_pBackBuffers[m_nBackBuffer].Get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRTV() const;

  //! The display the window is on, looked up again when DXGI's factory is no longer current (a
  //! display came or went, or its HDR mode changed), when the window went to another monitor, and
  //! once a second (the SDR white level).
  const SDisplayInfo &GetDisplayInfo();
  //! The swap chain's output (its format and colour space; HDR10's metadata from fPeakNits and
  //! fFrameAverageNits) and, with bLayer, the frame target and the highlight layer (HdrOutput.cpp;
  //! always with an HDR output). Outside a frame; waits for the GPU. False when the swap chain
  //! cannot show the output's colour space: it stays SDR.
  bool SetOutput(EDisplayOutput Output, bool bLayer, float fPeakNits, float fFrameAverageNits);
  EDisplayOutput GetOutput() const { return m_Output; }
  //! With the highlight layer the back buffer is the frame target (D3D12_BACKBUFFER_FORMAT, the
  //! swap chain's size): in RENDER_TARGET state in a frame, else PIXEL_SHADER_RESOURCE. At the end
  //! of the frame the output pass draws it into the swap chain's buffer, or in SDR it is copied there.
  bool HasLayer() const { return m_pFrame != nullptr; }
  ID3D12Resource *GetFrameTarget() const { return m_pFrame.Get(); }
  //! The highlight layer (D3D12_HIGHLIGHT_FORMAT, the swap chain's size): cleared to (1, 1, 1, -1)
  //! when a frame begins, in PIXEL_SHADER_RESOURCE state outside BindHighlight.
  ID3D12Resource *GetHighlight() const { return m_pHighlight.Get(); }
  //! true: the back buffer and the highlight layer bound (no depth buffer, the full viewport) for a
  //! pass that writes both; false: the back buffer alone again.
  void BindHighlight(bool bOn);
  //! The frame target as a shader resource while the frame goes on (true), a render target again (false).
  void SetFrameReadable(bool bReadable);
  //! The output pass: the frame target a shader resource, the swap chain's buffer bound (full
  //! viewport, no depth). EndFrame then presents it.
  void BeginOutput();
  D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;
  //! Sets the back buffer and the depth buffer as render targets, the full viewport and scissor
  //! (ends drawing off screen); between BeginScene and EndScene the scene targets instead.
  void SetBackBufferTargets();
  //! Binds the descriptor heap, the root signature and the texture table (after a pass that set
  //! its own, as an upscaler's).
  void BindRootSignature();
  //! A transition barrier of all of pRes.
  void Transition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After);

  //! The scene targets of an upscaler: the main view drawn at nWidth x nHeight into a colour target
  //! (ColorFormat: D3D12_BACKBUFFER_FORMAT, or D3D12_HDR_FORMAT), the motion vectors
  //! (D3D12_VELOCITY_FORMAT), the depth for the upscaler (D3D12_UPSCALE_DEPTH_FORMAT: the nearest
  //! of the depth writes and of the water, which does not write the depth buffer) and a depth
  //! buffer (D3D12_SCENE_DEPTH_FORMAT), and the upscaler's output of the back buffer's size
  //! (D3D12_UPSCALE_FORMAT). Made again when the size (or the back buffer's) or the format
  //! changed; 0 x 0 releases them. Outside BeginScene .. EndScene the four are in the state
  //! PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE and the output in UNORDERED_ACCESS.
  bool SetSceneSize(int nWidth, int nHeight, DXGI_FORMAT ColorFormat);
  int GetSceneWidth() const { return m_nSceneWidth; }
  int GetSceneHeight() const { return m_nSceneHeight; }
  ID3D12Resource *GetSceneColor() const { return m_pSceneColor.Get(); }
  ID3D12Resource *GetSceneDepth() const { return m_pSceneDepth.Get(); }
  ID3D12Resource *GetSceneVelocity() const { return m_pSceneVelocity.Get(); }
  ID3D12Resource *GetUpscaleDepth() const { return m_pUpscaleDepth.Get(); }
  ID3D12Resource *GetSceneOutput() const { return m_pSceneOutput.Get(); }
  //! From here (in a frame) until EndScene the main view draws into the scene targets:
  //! SetBackBufferTargets binds the colour, the motion vectors and the upscaler's depth
  //! (SPipelineKey::nTarget 3) with the scene's depth buffer.
  void BeginScene();
  //! The scene targets back to shader resources (for the upscaler); the back buffer bound again.
  void EndScene();
  bool IsInScene() const { return m_bInScene; }
  //! The scene targets are drawn into (in the scene, not off screen).
  bool IsSceneBound() const { return m_bInScene && !m_pOffscreen; }
  //! Clears the motion vectors to 0 (in the scene).
  void ClearVelocity();
  //! The size of the main view's target: the scene targets' in the scene, else the back buffer's.
  int GetMainWidth() const { return m_bInScene ? m_nSceneWidth : m_nWidth; }
  int GetMainHeight() const { return m_bInScene ? m_nSceneHeight : m_nHeight; }

  //! The HDR target of the main view without an upscaler: a colour target of the back buffer's
  //! size (D3D12_HDR_FORMAT) drawn with the back buffer's depth buffer. bOn makes it (again when
  //! the back buffer's size changed), false releases it. Outside BeginHdr .. EndHdr it is in the
  //! state PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE.
  bool SetHdrTarget(bool bOn);
  ID3D12Resource *GetHdrColor() const { return m_pHdrColor.Get(); }
  //! From here (in a frame) until EndHdr the main view draws into the HDR target instead of the
  //! back buffer (SetBackBufferTargets binds it with the back buffer's depth buffer).
  void BeginHdr();
  //! The HDR target back to a shader resource; the back buffer bound again.
  void EndHdr();
  bool IsInHdr() const { return m_bInHdr; }
  //! The colour format of the main view's target: the scene's colour in the scene, the HDR
  //! target's in BeginHdr .. EndHdr, else the back buffer's.
  DXGI_FORMAT GetMainColorFormat() const { return m_bInScene ? m_SceneColorFormat : m_bInHdr ? D3D12_HDR_FORMAT : D3D12_BACKBUFFER_FORMAT; }

  //! An off screen target of nWidth x nHeight: pColor in Format (the back buffer's, or
  //! D3D12_HDR_FORMAT; in the state PIXEL_SHADER_RESOURCE while it is not drawn into) and pDepth
  //! (D24S8, DEPTH_WRITE), their views written to hRTV and hDSV. pColor gets nMips mip levels: hRTV
  //! is the view of level 0, phMipRTV[i - 1] gets that of level i (the depth buffer has level 0
  //! only).
  bool CreateOffscreenTargets(int nWidth, int nHeight, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth,
                              D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, DXGI_FORMAT Format = D3D12_BACKBUFFER_FORMAT,
                              int nMips = 1, const D3D12_CPU_DESCRIPTOR_HANDLE *phMipRTV = nullptr);
  //! The same for a cube of nSize: pColor an array of the six faces (one RTV per face in hRTV, in
  //! the order of D3D12_TEXTURECUBE_FACE), pDepth one face's size; drawn a face at a time with
  //! SetOffscreenTargets.
  bool CreateCubeTargets(int nSize, ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth, const D3D12_CPU_DESCRIPTOR_HANDLE hRTV[6],
                         D3D12_CPU_DESCRIPTOR_HANDLE hDSV, DXGI_FORMAT Format = D3D12_BACKBUFFER_FORMAT);
  //! Draws into targets of CreateOffscreenTargets instead of the back buffer, all of them, until
  //! SetBackBufferTargets (in a frame). hDSV may be empty: a colour target alone (a render target
  //! of CD3D12Upload::CreateRenderTarget; SPipelineKey::nTarget 1 for R8G8B8A8). nSubresource:
  //! only that sub resource of pColor (the mip level hRTV views) becomes a render target; the
  //! others stay shader resources (the level above, read while this one is made from it).
  void SetOffscreenTargets(ID3D12Resource *pColor, D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nWidth, int nHeight,
                           UINT nSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  bool IsOffscreen() const { return m_pOffscreen != nullptr; }
  //! The colour format of the target drawn into: the off screen target's, else the main view's.
  DXGI_FORMAT GetTargetColorFormat() const { return m_pOffscreen ? m_OffscreenFormat : GetMainColorFormat(); }
  //! A shadow map of nSize x nSize: R32_TYPELESS (D3D12_SHADOW_MAP_FORMAT, its DSV written to hDSV,
  //! read as R32_FLOAT), in the state PIXEL_SHADER_RESOURCE while it is not drawn into.
  bool CreateShadowMap(int nSize, ComPtr<ID3D12Resource> &pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV);
  //! The same with nSlices slices (a texture array), the DSV of slice i written to phDSV[i].
  bool CreateShadowMapArray(int nSize, int nSlices, ComPtr<ID3D12Resource> &pDepth, const D3D12_CPU_DESCRIPTOR_HANDLE *phDSV);
  //! Draws into a shadow map of CreateShadowMap (depth only, no colour target) until
  //! SetBackBufferTargets (in a frame); hDSV may be the DSV of a slice of CreateShadowMapArray (the
  //! slices of one array one after another stay in the state of a depth target).
  void SetShadowMapTarget(ID3D12Resource *pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nSize);
  //! The size of the targets drawn into.
  int GetTargetWidth() const { return m_pOffscreen ? m_nOffscreenWidth : GetMainWidth(); }
  int GetTargetHeight() const { return m_pOffscreen ? m_nOffscreenHeight : GetMainHeight(); }

  //! Clears the colour / the depth buffer drawn into (in a frame); the scene's depth buffer with
  //! the upscaler's depth.
  void ClearColor(const float rgba[4]);
  void ClearDepth(float fDepth, uint8 nStencil, bool bDepth, bool bStencil);

  //! Textures of the main view's target (GetMainWidth x GetMainHeight) for CopyTargets: pColor in
  //! its format (GetMainColorFormat), pDepth in the depth buffer's family (read as
  //! GetMainDepthSRVFormat); both in PIXEL_SHADER_RESOURCE state. Without pDepth only the colour
  //! copy is made.
  bool CreateTargetCopies(ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> *pDepth);
  //! How a depth copy of CreateTargetCopies is read: R24_UNORM_X8_TYPELESS (the back buffer's
  //! D24S8), R32_FLOAT in the scene.
  DXGI_FORMAT GetMainDepthSRVFormat() const { return m_bInScene ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS; }
  //! Copies the main view's target (the back buffer, the HDR target or the scene's colour) and its depth buffer
  //! (when pDepth is given) as drawn so far in this frame into the textures of CreateTargetCopies
  //! (PIXEL_SHADER_RESOURCE before and after); nothing when their size or format is another.
  void CopyTargets(ID3D12Resource *pColor, ID3D12Resource *pDepth);

  //! Copies the current back buffer (as drawn so far in this frame, or the last presented one
  //! between frames) to pDst: nWidth x nHeight pixels of B, G, R, A, rows from the top.
  //! Waits for the GPU.
  bool ReadBackBuffer(uint8 *pDst, int nWidth, int nHeight);
  //! Copies level 0 of pTexture (2D, 4 bytes a pixel, or 8 for the R16G16B16A16 formats; in State
  //! and not drawn into) to pDst: nWidth x nHeight pixels, rows from the top. Waits for the GPU; in
  //! a frame the frame recorded so far is executed (FlushFrame), and the draws go to the back
  //! buffer again.
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
  void ReleaseSceneTargets();
  //! The frame target and the highlight layer of the swap chain's size (with m_bLayer).
  bool CreateLayer();
  //! The frame target from its state to State.
  void SetFrameState(D3D12_RESOURCE_STATES State);
  //! The views after the back buffers': n 0 the scene's colour, 1 its motion vectors, 2 the
  //! upscaler's depth; 3 the HDR target; 4 the frame target, 5 the highlight layer.
  D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV(int n) const;
  //! The swap chain's current buffer's view.
  D3D12_CPU_DESCRIPTOR_HANDLE SwapChainRTV() const;
  D3D12_CPU_DESCRIPTOR_HANDLE SceneDSV() const;
  //! Resets the slot's allocator and the list and binds the heap, root signature and targets.
  void OpenCmdList();
  void WaitForFence(uint64 nValue);
  //! The off screen colour target (or shadow map) back to PIXEL_SHADER_RESOURCE; nothing off screen
  //! any more.
  void EndOffscreen();

  ComPtr<IDXGIFactory4> m_pFactory;
  UINT m_nFactoryFlags = 0;
  ComPtr<IDXGIAdapter1> m_pAdapter;
  ComPtr<ID3D12Device> m_pDevice;
  ComPtr<ID3D12CommandQueue> m_pQueue;
  ComPtr<IDXGISwapChain3> m_pSwapChain;
  ComPtr<ID3D12InfoQueue> m_pInfoQueue;
  bool m_bTearing = false;
  HWND m_hWnd = nullptr;

  EDisplayOutput m_Output = EDisplayOutput::Sdr;    //!< SetOutput
  DXGI_FORMAT m_SwapChainFormat = D3D12_BACKBUFFER_FORMAT;
  float m_fMetadataPeak = 0, m_fMetadataFrameAverage = 0;   //!< HDR10's metadata as set
  bool m_bLayer = false;                            //!< the frame target and the highlight layer are wanted
  ComPtr<ID3D12Resource> m_pFrame, m_pHighlight;    //!< CreateLayer
  D3D12_RESOURCE_STATES m_FrameState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  bool m_bHighlightBound = false;                   //!< BindHighlight
  bool m_bOutput = false;                           //!< BeginOutput .. EndFrame
  SDisplayInfo m_Display;
  bool m_bDisplayInfo = false;                      //!< m_Display was looked up
  HMONITOR m_hDisplayMonitor = nullptr;             //!< the monitor it is of
  uint64 m_nDisplayInfoTime = 0;                    //!< GetTickCount64 when

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
  UINT m_nOffscreenSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;   //!< the sub resource of m_pOffscreen drawn into
  D3D12_CPU_DESCRIPTOR_HANDLE m_hOffscreenRTV = {}, m_hOffscreenDSV = {};
  int m_nOffscreenWidth = 0, m_nOffscreenHeight = 0;
  DXGI_FORMAT m_OffscreenFormat = DXGI_FORMAT_UNKNOWN;
  ComPtr<ID3D12Resource> m_pSceneColor, m_pSceneVelocity, m_pUpscaleDepth, m_pSceneDepth, m_pSceneOutput;   //!< SetSceneSize
  int m_nSceneWidth = 0, m_nSceneHeight = 0;
  int m_nSceneOutputWidth = 0, m_nSceneOutputHeight = 0;
  DXGI_FORMAT m_SceneColorFormat = D3D12_BACKBUFFER_FORMAT;
  bool m_bInScene = false;                          //!< BeginScene .. EndScene
  ComPtr<ID3D12Resource> m_pHdrColor;               //!< SetHdrTarget
  bool m_bInHdr = false;                            //!< BeginHdr .. EndHdr

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
