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

#include "D3D12Barriers.h"
#include "D3D12Memory.h"
#include "DisplayPoll.h"

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
constexpr DXGI_FORMAT D3D12_UPSCALE_HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< an upscaler's output of the HDR scene's light (d3d12_UpscaleHdr)
constexpr DXGI_FORMAT D3D12_HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;  //!< the main view's colour with r_HDRRendering (Hdr.cpp)
constexpr DXGI_FORMAT D3D12_HDR10_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM; //!< the swap chain of the HDR10 output (PQ, Rec. 2020; HdrOutput.cpp)
constexpr DXGI_FORMAT D3D12_SCRGB_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< the swap chain of the scRGB output (linear Rec. 709, 1 = 80 nits)
constexpr DXGI_FORMAT D3D12_HIGHLIGHT_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< the highlight layer (HdrOutput.cpp)
constexpr DXGI_FORMAT D3D12_AMBIENT_FORMAT = DXGI_FORMAT_R11G11B10_FLOAT; //!< the ambient light of the ambient occlusion (BindAmbient)
constexpr DXGI_FORMAT D3D12_PT_ALBEDO_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT; //!< the path tracer's G-buffer: the albedo (BindPathTargets)
constexpr DXGI_FORMAT D3D12_PT_NORMAL_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;  //!< its normal, the fog's share and the material
constexpr DXGI_FORMAT D3D12_PT_SUN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;        //!< the sun's visibility, the ambient light / 2
constexpr DXGI_FORMAT D3D12_PT_SPECULAR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;   //!< the highlights' colour / 2
constexpr DXGI_FORMAT D3D12_PT_LIGHTMAP_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;   //!< the light map's light / 4

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

  //! Creates the device and a swap chain for hWnd. nDebug: 1 the debug layer and GPU based
  //! validation, 2 the debug layer alone. nDred: DRED (what the GPU did when the device is
  //! removed, ReportDeviceRemoved), 1 the page faults and our markers (GpuSection), 2 the page
  //! faults and every command.
  bool Init(HWND hWnd, int nWidth, int nHeight, int nDebug, int nDred);
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

  //! Waits until the GPU finished everything submitted (the uploads flushed first). In a frame the
  //! frame's own list is not submitted yet: the wait does not signal the frame's fence (it would
  //! come before that list), only a fence of its own, so what the frame released stays until the
  //! frame's end. Its list keeps its state.
  void WaitForGPU();
  //! Signals the queue's fence and returns the value; in a frame only after the frame's list so
  //! far was executed (EndFrame, FlushFrame), as the frame's releases wait for that value.
  uint64 Signal();
  uint64 GetCompletedFence() const;
  //! The fence value the current frame (or, between frames, the next one) signals when it ends;
  //! an object used in this frame can go when GetCompletedFence() reaches it. It changes in a
  //! frame only at FlushFrame, which executes the list first.
  uint64 GetFrameFence() const { return m_nNextFence; }
  //! Takes over one reference of p and releases it once the GPU has finished everything
  //! submitted so far and the current frame.
  void DeferredRelease(IUnknown *p);
  //! The same for an object held in a ComPtr (which is left empty).
  template<class T> void DeferredRelease(ComPtr<T> &p) { DeferredRelease(static_cast<IUnknown *>(p.Detach())); }
  //! The same for a resource with its memory (left empty).
  void DeferredRelease(CD3D12Resource &r);
  //! Releases what the GPU no longer uses (called once per frame).
  void CollectGarbage();

  ID3D12Device *GetDevice() const { return m_pDevice.Get(); }
  //! A resource with its memory from the device's allocator (D3D12Memory.h); a buffer starts in COMMON whatever eState
  //! says. szName names the allocation (d3d12_MemoryDump) and, with names on (NamesObjects), the resource.
  HRESULT CreateResource(D3D12_HEAP_TYPE eHeap, const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES eState, const D3D12_CLEAR_VALUE *pClear,
                         ED3D12Memory eMemory, const wchar_t *szName, CD3D12Resource &Out);
  //! A resource in Base's memory (D3D12Memory.h's CreateAlias), named as CreateResource names.
  HRESULT CreateAlias(const CD3D12Resource &Base, const D3D12_RESOURCE_DESC &Desc, D3D12_RESOURCE_STATES eState, const wchar_t *szName,
                      CD3D12Resource &Out);
  //! The memory pBefore and pAfter share given to pAfter from here on (an aliasing barrier, queued in order), pAfter in
  //! eWrite (UNORDERED_ACCESS, RENDER_TARGET or DEPTH_WRITE) and discarded: the next pass must write all it reads.
  void ActivateAlias(ID3D12Resource *pBefore, ID3D12Resource *pAfter, D3D12_RESOURCE_STATES eWrite);
  ID3D12CommandQueue *GetQueue() const { return m_pQueue.Get(); }
  //! The frame's command list; valid between BeginFrame and EndFrame. The caller may set state of
  //! its own on it: what DrawGeometry set is forgotten (GetStateEpoch).
  ID3D12GraphicsCommandList *GetCmdList() const
  {
    m_nStateEpoch++;
    return m_pCmdList.Get();
  }
  //! The frame's command list for DrawGeometry, which leaves out the state it set already while
  //! GetStateEpoch stays the same.
  ID3D12GraphicsCommandList *GetDrawCmdList() const { return m_pCmdList.Get(); }
  //! Changes whenever the command list's state may have changed other than by DrawGeometry: a new
  //! list, targets bound, the root signature, a GetCmdList, ForgetDrawState.
  uint32 GetStateEpoch() const { return m_nStateEpoch; }
  void ForgetDrawState() const { m_nStateEpoch++; }
  //! In a frame, an open command list of the compute queue for work beside the frame's (the ray
  //! scene's builds), its GPU time that of szSection (a string with static storage) in
  //! GetComputeGpuTimes; NULL without the queue. SubmitAsyncCompute runs it.
  ID3D12GraphicsCommandList4 *BeginAsyncCompute(const char *szSection);
  //! Executes the list of BeginAsyncCompute once the upload batch so far is done, beside the frame's
  //! work on the GPU; the frame's list goes on. It waits for the compute list from WaitAsyncCompute
  //! on: the first draw or pass that reads what it made, else FlushFrame, WaitForGPU or EndFrame (the
  //! frame's fence after it).
  void SubmitAsyncCompute();
  //! The frame's list from here on after the compute list of SubmitAsyncCompute (nothing when there
  //! is none or it waited already): its list so far executed, the GPU waiting in the section
  //! "ray scene", the frame going on in the same slot's allocator (the descriptor heap, the root
  //! signature and the targets set again).
  void WaitAsyncCompute();
  //! 0 .. D3D12_FRAMES-1: the slot of the current frame (per frame resources, the upload ring).
  int GetFrameSlot() const { return m_nFrameSlot; }

  int GetWidth() const { return m_nWidth; }
  int GetHeight() const { return m_nHeight; }
  //! What everything draws into: the frame target with the highlight layer (SetOutput), else the
  //! swap chain's current buffer.
  ID3D12Resource *GetBackBuffer() const { return m_pFrame ? m_pFrame.Get() : m_pBackBuffers[m_nBackBuffer].Get(); }
  D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRTV() const;

  //! The display the window is on, looked up again (0.14-0.18 ms in step 329's tests, on the main
  //! thread) when DXGI's factory is no longer current (a display came or went, or its HDR mode
  //! changed), when the window went to another monitor, after InvalidateDisplayInfo, and with the
  //! game in the background once a second (DisplayPoll.h); otherwise the last lookup's.
  const SDisplayInfo &GetDisplayInfo();
  //! The next GetDisplayInfo looks the display up again (WM_DISPLAYCHANGE, the focus back: Windows'
  //! SDR white level is set in its Settings, with the game in the background).
  void InvalidateDisplayInfo() { m_DisplayLast.bStale = true; }
  //! The game has the focus (WM_ACTIVATEAPP): GetDisplayInfo looks on a timer only without it.
  void SetAppActive(bool bActive) { m_bDisplayAppActive = bActive; }
  bool IsAppActive() const { return m_bDisplayAppActive; }
  //! The display's lookups so far and their milliseconds (d3d12_FrameLog).
  uint32 GetDisplayLookups() const { return m_nDisplayLookups; }
  double GetDisplayLookupMs() const { return m_fDisplayLookupMs; }
  //! The swap chain's output (its format and colour space; HDR10's metadata from fPeakNits and
  //! fFrameAverageNits) and, with bLayer, the frame target and the highlight layer (HdrOutput.cpp;
  //! always with an HDR output). Outside a frame; waits for the GPU. False when the swap chain
  //! cannot show the output's colour space: it stays SDR.
  bool SetOutput(EDisplayOutput Output, bool bLayer, float fPeakNits, float fFrameAverageNits);
  EDisplayOutput GetOutput() const { return m_Output; }
  //! With the highlight layer the back buffer is the frame target (D3D12_BACKBUFFER_FORMAT, the
  //! swap chain's size). At the end of the frame the output pass draws it into the swap chain's
  //! buffer, or in SDR it is copied there. Its state is kept by the device (TrackState): a render
  //! target where it is drawn into (the bound targets', FlushBarriersForDraw), else where a pass
  //! asks for it (Require: HdrOutput's and HdrStats' kernels read it).
  bool HasLayer() const { return m_pFrame != nullptr; }
  ID3D12Resource *GetFrameTarget() const { return m_pFrame.Get(); }
  //! The highlight layer (D3D12_HIGHLIGHT_FORMAT, the swap chain's size): cleared to (1, 1, 1, -1)
  //! when a frame begins, in PIXEL_SHADER_RESOURCE state outside BindHighlight (kept by the device:
  //! a pass that reads it otherwise asks, Require).
  ID3D12Resource *GetHighlight() const { return m_pHighlight.Get(); }
  //! true: the back buffer and the highlight layer bound (no depth buffer, the full viewport) for a
  //! pass that writes both; false: the back buffer alone again.
  void BindHighlight(bool bOn);
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
  //! The frame's barriers (D3D12Barriers.h). With d3d12_BarrierBatch they queue up and are given before the next
  //! work on the frame's list in one call (a chain of transitions of a resource merged, a round trip with no work in
  //! it left out), else each at once. Outside a frame nothing.
  //! A transition barrier of pRes, of all of it or of one subresource.
  void Transition(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After,
                  UINT nSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  //! A UAV barrier of pRes: the unordered access writes before it are done for the accesses after it (queued: left out
  //! next to a transition of all of pRes into or out of UNORDERED_ACCESS, or under UavBarrierAll).
  void UavBarrier(ID3D12Resource *pRes);
  //! A UAV barrier of all resources.
  void UavBarrierAll();
  //! A pass's own barrier array: queued as Transition, UavBarrier and UavBarrierAll are (in the immediate mode given as
  //! it is, in a call of its own). Outside a frame given at once.
  void ResourceBarriers(UINT n, const D3D12_RESOURCE_BARRIER *pBarriers);
  //! ResourceBarriers when pList is the frame's list, else given on pList at once (that list's own).
  void ResourceBarriersOn(ID3D12GraphicsCommandList *pList, UINT n, const D3D12_RESOURCE_BARRIER *pBarriers);
  //! Transition and UavBarrierAll for a pass that records on pList: queued when it is the frame's list, else given on
  //! pList at once.
  void TransitionOn(ID3D12GraphicsCommandList *pList, ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After);
  void UavBarrierAllOn(ID3D12GraphicsCommandList *pList);
  //! A library left pRes in State by barriers of its own (d3d12_BarrierCheck's census learns it).
  void Assume(ID3D12Resource *pRes, D3D12_RESOURCE_STATES State)
  {
    if (m_bInFrame && pRes)
      m_Barriers.Assume(pRes, State);
  }
  //! A resource whose state the device keeps from here on (the tracker's Register; nSubresources of them, all in
  //! Initial): Require asks for a state without naming the one before, a Transition's before is checked against it. A
  //! buffer's state decays to COMMON when the frame's list has run and is given back by a transition at the next list's
  //! start. It is released by DeferredRelease (which forgets it); the upload list never writes it
  //! (d3d12_BarrierCheck tells, CheckUntracked).
  void TrackState(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Initial, UINT nSubresources = 1);
  //! A resource TrackState keeps wanted in After (all of it or one subresource): nothing where a read state it is in
  //! holds After, two shader reads combine. Outside a frame nothing.
  void Require(ID3D12Resource *pRes, D3D12_RESOURCE_STATES After, UINT nSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  bool IsTracked(ID3D12Resource *pRes) const { return m_Barriers.IsRegistered(pRes); }
  //! A released resource forgotten by the barriers' tracker and check (DeferredRelease).
  void ForgetState(ID3D12Resource *pRes)
  {
    m_Barriers.Forget(pRes);
    m_Bound.Remove(pRes);
  }
  //! d3d12_BarrierCheck: the upload list's szWhat writes pRes, which must not be a resource TrackState keeps.
  void CheckUntracked(ID3D12Resource *pRes, const char *szWhat);
  //! What is queued given to the frame's list: before work recorded on it (bWork: a draw, a dispatch, a copy, a
  //! clear, a library's passes; the next UAV barriers wait for it) or before barriers given on it directly (false).
  //! Every place that records work on the frame's list calls it first (the test Barrier_SourceLint).
  void FlushBarriers(bool bWork = true)
  {
    if (m_bInFrame)
      m_Barriers.Flush([this](UINT n, const D3D12_RESOURCE_BARRIER *p) { m_pCmdList->ResourceBarrier(n, p); }, bWork);
  }
  //! FlushBarriers before work on pList when it is the frame's list (another list's work has its own barriers).
  void FlushBarriersFor(ID3D12GraphicsCommandList *pList)
  {
    if (pList == m_pCmdList.Get())
      FlushBarriers();
  }
  //! FlushBarriers before a draw or a clear: first the bound render targets and depth target whose states the device
  //! keeps wanted in the states they are drawn in (a pass that made one readable and draws on gets it back as a target;
  //! nothing where no state changed since the last draw).
  void FlushBarriersForDraw()
  {
    if (!m_bInFrame)
      return;
    m_Bound.Require(m_Barriers);
    FlushBarriers();
  }
  //! Copies and clears on the frame's list after what is queued.
  void CopyResource(ID3D12Resource *pDst, ID3D12Resource *pSrc);
  void CopyBufferRegion(ID3D12Resource *pDst, UINT64 nDstOffset, ID3D12Resource *pSrc, UINT64 nSrcOffset, UINT64 nBytes);
  void ClearRenderTargetView(D3D12_CPU_DESCRIPTOR_HANDLE hRTV, const float Color[4]);
  //! d3d12_BarrierBatch (0 each barrier at once, 1 batches, 2 batches that GpuSection gives too) and
  //! d3d12_BarrierCheck (the transitions' befores checked, the problems logged), taken at the next BeginFrame.
  void SetBarrierMode(int nBatch, bool bCheck)
  {
    m_nBarrierBatchWanted = nBatch;
    m_bBarrierCheckWanted = bCheck;
  }
  //! The barriers the last frame gave (its ResourceBarrier calls, those after work, the barriers by kind).
  const Barriers::SStats &GetBarrierStats() const { return m_LastBarrierStats; }

  //! The scene targets of an upscaler: the main view drawn at nWidth x nHeight into a colour target
  //! (ColorFormat: D3D12_BACKBUFFER_FORMAT, or D3D12_HDR_FORMAT), the motion vectors
  //! (D3D12_VELOCITY_FORMAT), the depth for the upscaler (D3D12_UPSCALE_DEPTH_FORMAT: the nearest
  //! of the depth writes and of the water, which does not write the depth buffer) and a depth
  //! buffer (D3D12_SCENE_DEPTH_FORMAT), and the upscaler's output of the back buffer's size
  //! (OutputFormat: D3D12_UPSCALE_FORMAT, or D3D12_UPSCALE_HDR_FORMAT for the HDR scene's light).
  //! Made again when the size (or the back buffer's) or a format changed; 0 x 0 releases them.
  //! Outside BeginScene .. EndScene the four are in the state PIXEL_SHADER_RESOURCE |
  //! NON_PIXEL_SHADER_RESOURCE and the output in UNORDERED_ACCESS; their states are kept by the
  //! device (TrackState: a pass asks for another, Require).
  bool SetSceneSize(int nWidth, int nHeight, DXGI_FORMAT ColorFormat, DXGI_FORMAT OutputFormat = D3D12_UPSCALE_FORMAT);
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

  //! The ambient light of the ambient occlusion (AmbientOcclusion.cpp, d3d12_AOApply 1), a target of
  //! the main view's size in D3D12_AMBIENT_FORMAT: from here (in a frame) SetBackBufferTargets binds
  //! hRTV as the fourth colour target of the main view (SV_Target3, SPipelineKey::bAmbient), after the
  //! scene's motion vectors and the upscaler's depth or, without an upscaler, two null targets of their
  //! formats; an empty handle unbinds it. pResource (its resource) is drawn in RENDER_TARGET (the bound targets',
  //! FlushBarriersForDraw); the caller asks for the states it reads it in. With MSAA (BeginMsaa) the draws
  //! write a multisampled ambient light in its place, cleared when bound and resolved into pResource when unbound.
  void BindAmbient(D3D12_CPU_DESCRIPTOR_HANDLE hRTV, ID3D12Resource *pResource = nullptr);
  //! The main view's draws write the ambient light too (bound, not off screen).
  bool IsAmbientBound() const { return m_hAmbientRTV.ptr && !m_pOffscreen; }
  //! MSAA of the main view's scene (d3d12_AA msaa2, msaa4, msaa8; Msaa.cpp): multisampled targets of the main view's
  //! size with nSamples samples, the colour in ColorFormat (the back buffer's or the HDR target's), the depth
  //! buffer's (R24G8_TYPELESS, drawn as D3D12_DEPTH_FORMAT) and the ambient light's (D3D12_AMBIENT_FORMAT); made
  //! anew for another count, format or size (GetMsaaGeneration counts them). nSamples 0 releases them. False:
  //! the GPU has not that count for one of the formats, or no resolve of the colour's or the ambient light's
  //! (none made).
  bool SetMsaaTargets(int nSamples, DXGI_FORMAT ColorFormat);
  //! From here (in a frame, not in an upscaler's scene) SetBackBufferTargets binds the multisampled targets in
  //! place of the main view's colour, depth buffer and ambient light, ClearColor and ClearDepth clear them, and
  //! CopyTargets resolves the colour (the depth from the depth buffer, which the caller resolves first).
  void BeginMsaa();
  //! The main view's own targets bound again; the caller resolved the multisampled ones into them.
  void EndMsaa();
  bool IsInMsaa() const { return m_bInMsaa; }
  int GetMsaaSamples() const { return m_nMsaaSamples; }
  uint32 GetMsaaGeneration() const { return m_nMsaaGeneration; }
  //! The samples of the targets the draws go to: SPipelineKey::nSamples (1 off screen and without MSAA).
  int GetBoundSamples() const { return m_bInMsaa && !m_pOffscreen ? m_nMsaaSamples : 1; }
  //! The multisampled colour (in D3D12_RESOURCE_STATE_RENDER_TARGET) and depth buffer (in
  //! D3D12_RESOURCE_STATE_DEPTH_WRITE) between frames and passes.
  ID3D12Resource *GetMsaaColor() const { return m_pMsaaColor.Get(); }
  ID3D12Resource *GetMsaaDepth() const { return m_pMsaaDepth.Get(); }
  //! For Msaa.cpp's passes: the main view's own colour target alone (bColor: the HDR target or the back buffer)
  //! or its depth buffer alone, with the viewport of the main view.
  void BindMsaaResolveTarget(bool bColor);
  //! The path tracer's G-buffer (PathTrace.cpp, Shaders/PathGBuffer.hlsli), targets of the scene's size: from here
  //! (in a frame, in the scene) SetBackBufferTargets binds hAlbedo (D3D12_PT_ALBEDO_FORMAT), hNormal
  //! (D3D12_PT_NORMAL_FORMAT), hSun (D3D12_PT_SUN_FORMAT), hSpecular (D3D12_PT_SPECULAR_FORMAT) and hLightmap
  //! (D3D12_PT_LIGHTMAP_FORMAT) as the scene's fourth to eighth colour targets (SV_Target3 to 7, SPipelineKey::bPathTrace)
  //! in place of the ambient light's; empty handles unbind them. ppResources (5, in that order): their resources, drawn
  //! in RENDER_TARGET (the bound targets', FlushBarriersForDraw); the caller asks for the states it reads them in.
  void BindPathTargets(D3D12_CPU_DESCRIPTOR_HANDLE hAlbedo, D3D12_CPU_DESCRIPTOR_HANDLE hNormal, D3D12_CPU_DESCRIPTOR_HANDLE hSun,
                       D3D12_CPU_DESCRIPTOR_HANDLE hSpecular, D3D12_CPU_DESCRIPTOR_HANDLE hLightmap, ID3D12Resource *const *ppResources = nullptr);
  //! The main view's draws write the path tracer's G-buffer too (bound, in the scene, not off screen).
  bool IsPathBound() const { return m_hPathRTV[0].ptr && m_bInScene && !m_pOffscreen; }
  //! true: from here (in a frame) SetBackBufferTargets binds the main view's depth buffer read only
  //! (GetMainDepth in a read state with DEPTH_READ, which a pixel shader can then read too, or still
  //! in DEPTH_WRITE); false: writable again (DEPTH_WRITE at the next draw, FlushBarriersForDraw). The
  //! caller asks for the states it reads it in (Require).
  void SetDepthReadOnly(bool bReadOnly);

  //! An off screen target of nWidth x nHeight: pColor in Format (the back buffer's, or
  //! D3D12_HDR_FORMAT; in the state PIXEL_SHADER_RESOURCE while it is not drawn into, its levels'
  //! states kept by the device each: TrackState) and pDepth (D24S8, DEPTH_WRITE), their views written
  //! to hRTV and hDSV. pColor gets nMips mip levels: hRTV is the view of level 0, phMipRTV[i - 1] gets
  //! that of level i (the depth buffer has level 0 only).
  bool CreateOffscreenTargets(int nWidth, int nHeight, CD3D12Resource &pColor, CD3D12Resource &pDepth,
                              D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, DXGI_FORMAT Format = D3D12_BACKBUFFER_FORMAT,
                              int nMips = 1, const D3D12_CPU_DESCRIPTOR_HANDLE *phMipRTV = nullptr);
  //! The same for a cube of nSize: pColor an array of the six faces (one RTV per face in hRTV, in
  //! the order of D3D12_TEXTURECUBE_FACE), pDepth one face's size; drawn a face at a time with
  //! SetOffscreenTargets.
  bool CreateCubeTargets(int nSize, CD3D12Resource &pColor, CD3D12Resource &pDepth, const D3D12_CPU_DESCRIPTOR_HANDLE hRTV[6],
                         D3D12_CPU_DESCRIPTOR_HANDLE hDSV, DXGI_FORMAT Format = D3D12_BACKBUFFER_FORMAT);
  //! Draws into targets of CreateOffscreenTargets instead of the back buffer, all of them, until
  //! SetBackBufferTargets (in a frame). hDSV may be empty: a colour target alone (a render target
  //! of CD3D12Upload::CreateRenderTarget; SPipelineKey::nTarget 1 for R8G8B8A8). nSubresource:
  //! only that sub resource of pColor (the mip level hRTV views) becomes a render target; the
  //! others stay shader resources (the level above, read while this one is made from it).
  void SetOffscreenTargets(ID3D12Resource *pColor, D3D12_CPU_DESCRIPTOR_HANDLE hRTV, D3D12_CPU_DESCRIPTOR_HANDLE hDSV, int nWidth, int nHeight,
                           UINT nSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  //! After SetOffscreenTargets of a colour target alone: pSecond (a render target of the same size) bound after it
  //! as the second colour target, for a pass that writes both; EndOffscreen ends it with the first.
  void BindSecondTarget(ID3D12Resource *pSecond, D3D12_CPU_DESCRIPTOR_HANDLE hRTV);
  //! The off screen colour target (or shadow map) back to PIXEL_SHADER_RESOURCE; nothing off screen
  //! any more (a compute pass reads it next).
  void EndOffscreen();
  bool IsOffscreen() const { return m_pOffscreen != nullptr; }
  //! The colour format of the target drawn into: the off screen target's, else the main view's.
  DXGI_FORMAT GetTargetColorFormat() const { return m_pOffscreen ? m_OffscreenFormat : GetMainColorFormat(); }
  //! A shadow map of nSize x nSize: R32_TYPELESS (D3D12_SHADOW_MAP_FORMAT, its DSV written to hDSV,
  //! read as R32_FLOAT), in the state PIXEL_SHADER_RESOURCE while it is not drawn into; placed memory, undefined
  //! until cleared.
  bool CreateShadowMap(int nSize, CD3D12Resource &pDepth, D3D12_CPU_DESCRIPTOR_HANDLE hDSV);
  //! The same with nSlices slices (a texture array), the DSV of slice i written to phDSV[i], the DSV
  //! of all of them to phDSV[nSlices] (drawn into with SV_RenderTargetArrayIndex).
  bool CreateShadowMapArray(int nSize, int nSlices, CD3D12Resource &pDepth, const D3D12_CPU_DESCRIPTOR_HANDLE *phDSV);
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
  bool CreateTargetCopies(CD3D12Resource &pColor, CD3D12Resource *pDepth);
  //! How a depth copy of CreateTargetCopies is read: R24_UNORM_X8_TYPELESS (the back buffer's
  //! D24S8), R32_FLOAT in the scene.
  DXGI_FORMAT GetMainDepthSRVFormat() const { return m_bInScene ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R24_UNORM_X8_TYPELESS; }
  //! The main view's depth buffer: the scene's in the scene, else the back buffer's (R24G8_TYPELESS, its
  //! DSV D3D12_DEPTH_FORMAT), read through an SRV of GetMainDepthSRVFormat. DEPTH_WRITE while drawn into.
  ID3D12Resource *GetMainDepth() const { return m_bInScene ? m_pSceneDepth.Get() : m_pDepth.Get(); }
  //! Copies the main view's target (the back buffer, the HDR target or the scene's colour) and its depth buffer
  //! (when pDepth is given) as drawn so far in this frame into the textures of CreateTargetCopies
  //! (PIXEL_SHADER_RESOURCE before and after); nothing when their size or format is another. With
  //! pBox only that part of the colour, to the same place.
  void CopyTargets(ID3D12Resource *pColor, ID3D12Resource *pDepth, const D3D12_BOX *pBox = nullptr);

  //! Copies the current back buffer (as drawn so far in this frame, or the last presented one
  //! between frames) to pDst: nWidth x nHeight pixels of B, G, R, A, rows from the top.
  //! Waits for the GPU.
  bool ReadBackBuffer(uint8 *pDst, int nWidth, int nHeight);
  //! Copies level 0 of pTexture (2D, 4 bytes a pixel, or 8 for the R16G16B16A16 formats; not drawn
  //! into; in State unless the device keeps its state, then in its own, and back in it after) to pDst:
  //! nWidth x nHeight pixels, rows from the top. Waits for the GPU; in a frame the frame recorded so
  //! far is executed (FlushFrame), and the draws go to the back buffer again.
  bool ReadBackTexture(ID3D12Resource *pTexture, D3D12_RESOURCE_STATES State, uint8 *pDst, int nWidth, int nHeight);

  //! The debug layer's messages to the engine's log and to XRenderD3D12_debug.txt (the engine's
  //! log loses its last lines when the game ends a time demo).
  void LogDebugMessages();

  //! Once, when the device is removed (hr: what failed, from szWhere): the reason to the
  //! engine's log, and with DRED what the GPU did, a summary to the log and all of it to
  //! XRenderD3D12_dred.txt: the command lists in flight (done, not begun, or stopped at a
  //! command, with our last marker), the page fault's address and the objects there, existing
  //! and freed (D3D12Dred.cpp). Nothing while the device is not removed.
  void ReportDeviceRemoved(HRESULT hr, const char *szWhere);
  //! DRED's level (Init's nDred; 0 off).
  int GetDred() const { return m_nDred; }
  //! The objects are named for DRED's or the debug layer's reports (SetName costs time).
  bool NamesObjects() const { return m_nDred != 0 || m_pInfoQueue; }
  //! With DRED: szSection (a string with static storage) as a marker in pList (in the PIX event
  //! runtime's format, which PIX shows too), DRED's breadcrumb context; nothing without.
  void DredMarker(ID3D12GraphicsCommandList *pList, const char *szSection);
  //! d3d12_DredTest, in a frame: 1 removes the device (ID3D12Device5::RemoveDevice), 2 makes a
  //! GPU page fault (a write to the address of a buffer just freed, named "XRenderD3D12 DRED
  //! test", in the section "dred test"; the driver resets the GPU).
  void DredTest(int nMode);

  //! GPU timing (d3d12_GpuTimes): while on, the GPU time of each frame is split into sections,
  //! each from its GpuSection call to the next one or the end of the frame (the first one of a
  //! frame is "other"; from a FlushFrame until its second list runs it is "flush", the GPU waiting
  //! for the CPU). A frame's times are read when the GPU has finished it (BeginFrame of its slot)
  //! and add up in GetGpuTimes; the compute queue's lists beside them in GetComputeGpuTimes (not
  //! part of the frame's time).
  void SetGpuTiming(bool bOn) { m_bGpuTiming = bOn; }
  //! From here on the GPU time counts for szSection (a string with static storage).
  void GpuSection(const char *szSection);
  //! The sections' GPU milliseconds added up over GetGpuTimedFrames() frames, in the order they
  //! first came; ResetGpuTimes starts again.
  const std::vector<std::pair<const char *, double>> &GetGpuTimes() const { return m_GpuTimes; }
  const std::vector<std::pair<const char *, double>> &GetComputeGpuTimes() const { return m_ComputeGpuTimes; }
  int GetGpuTimedFrames() const { return m_nGpuTimedFrames; }
  void ResetGpuTimes()
  {
    m_GpuTimes.clear();
    m_ComputeGpuTimes.clear();
    m_nGpuTimedFrames = 0;
  }
  //! The CPU's milliseconds in the last frame: BeginFrame's wait for the GPU (to finish the frame
  //! D3D12_FRAMES back) and EndFrame's Present.
  double GetWaitMs() const { return m_fWaitMs; }
  double GetPresentMs() const { return m_fPresentMs; }
  //! The adapter's name (UTF-8).
  const char *GetAdapterName() const { return m_szAdapter; }
  //! The memory's use and budgets (cheap); with bHeaps also what the allocator's heaps hold (it walks them: twice a
  //! second at most). False without an allocator.
  bool GetMemoryStats(SD3D12MemoryStats &Out, bool bHeaps) const;
  //! The allocator's JSON statistics (D3D12MA::Allocator::BuildStatsString with every allocation) to szPath, UTF-8.
  bool DumpMemory(const char *szPath) const;
  //! The allocator's resources and bytes to the log (szWhen: "after a level's load"); bNames with their names, the most
  //! first.
  void LogMemory(const char *szWhen, bool bNames) const;
  //! Resources made (CreateResource) and freed (DeferredRelease) since the last call.
  CD3D12MemoryChurn::SCounts TakeMemoryChurn() { return m_Churn.Take(); }

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
  static constexpr int COMPUTE_LISTS = 4;           //!< compute lists timed per frame (two timestamps each)
  //! Adds the timestamps of the current slot's last frame (finished) to m_GpuTimes.
  void ReadGpuTimes();
  //! The occlusion counts of the current slot's last frame (finished) into m_OcclusionResults.
  void ReadOcclusionResults();

  bool CreateSwapChainBuffers();
  void ReleaseSwapChainBuffers();
  void ReleaseSceneTargets();
  //! The frame target and the highlight layer of the swap chain's size (with m_bLayer).
  bool CreateLayer();
  //! Require of a resource whose state the device keeps, else a transition from Untracked (the state it is in).
  void RequireOr(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Untracked, D3D12_RESOURCE_STATES After,
                 UINT nSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  //! The state the tracker keeps of a resource (with queued requests; its first subresource), else Untracked.
  D3D12_RESOURCE_STATES StateOr(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Untracked) const;
  //! A transition given on the frame's list as it is, after what is queued (the device's own; between frames on the
  //! list ReadBackTexture opened).
  void RawBarrier(ID3D12Resource *pRes, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After,
                  UINT nSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
  //! The frame list was executed / a list opened for the frame: the tracker told (and the check's problems logged).
  void OnFrameListExecuted();
  void LogBarrierProblems();
  //! The views after the back buffers': n 0 the scene's colour, 1 its motion vectors, 2 the
  //! upscaler's depth; 3 the HDR target; 4 the frame target, 5 the highlight layer; 6 and 7 null
  //! views of the motion vectors' and the upscaler depth's formats (BindAmbient); 8 MSAA's colour,
  //! 9 its ambient light, 10 and 11 the null views of 6 and 7 multisampled.
  D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV(int n) const;
  //! MSAA's depth buffer's view, read only or not.
  D3D12_CPU_DESCRIPTOR_HANDLE MsaaDSV(bool bReadOnly) const;
  //! pSrc (multisampled, in SrcState) resolved into pDst (in DstState) as Format; both back in their states.
  void ResolveInto(ID3D12Resource *pDst, D3D12_RESOURCE_STATES DstState, ID3D12Resource *pSrc, D3D12_RESOURCE_STATES SrcState,
                   DXGI_FORMAT Format);
  //! The swap chain's current buffer's view.
  D3D12_CPU_DESCRIPTOR_HANDLE SwapChainRTV() const;
  D3D12_CPU_DESCRIPTOR_HANDLE SceneDSV() const;
  //! The read only view of the depth buffer (GetDSV) or of the scene's (SceneDSV).
  D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDSV(bool bScene) const;
  //! Resets the slot's allocator and the list and binds the heap, root signature and targets.
  void OpenCmdList();
  //! The frame goes on in a new list of the slot's allocator after the one just executed (its memory
  //! is kept until the slot's fence): the heap, the root signature and the targets bound again.
  void ContinueCmdList();
  //! The direct queue's work submitted from here on after the compute list of SubmitAsyncCompute.
  void QueueWaitAsyncCompute();
  void WaitForFence(uint64 nValue);
  //! Waits on the CPU until pFence reached nValue (10 s at most, logged).
  void WaitForValue(ID3D12Fence *pFence, uint64 nValue);
  //! GetDisplayInfo's lookup: m_Display from the output of hMonitor (its description, Windows' SDR white level).
  void LookUpDisplay(HMONITOR hMonitor);

  ComPtr<IDXGIFactory4> m_pFactory;
  UINT m_nFactoryFlags = 0;
  ComPtr<IDXGIAdapter1> m_pAdapter;
  ComPtr<ID3D12Device> m_pDevice;
  ComPtr<D3D12MA::Allocator> m_pAllocator;          //!< CreateResource's (outside it: the swap chain, FSR's and CACAO's own, d3d12_DredTest's)
  CD3D12MemoryChurn m_Churn;
  ComPtr<ID3D12CommandQueue> m_pQueue;
  ComPtr<IDXGISwapChain3> m_pSwapChain;
  ComPtr<ID3D12InfoQueue> m_pInfoQueue;
  std::unique_ptr<FILE, int (*)(FILE *)> m_pDebugLog{ nullptr, &fclose };  //!< XRenderD3D12_debug.txt, with the debug layer
  std::unique_ptr<FILE, int (*)(FILE *)> m_pBarrierLog{ nullptr, &fclose };  //!< XRenderD3D12_barriers.txt, with d3d12_BarrierCheck
  int m_nDred = 0;                                  //!< GetDred
  bool m_bRemovedReported = false;                  //!< ReportDeviceRemoved done
  const char *m_szDredSection = nullptr;            //!< the frame list's last marker (DredMarker)
  std::vector<std::pair<const char *, std::vector<uint64>>> m_DredMarkers;   //!< the markers' encoded blobs (DredMarker)
  bool m_bTearing = false;
  HWND m_hWnd = nullptr;

  EDisplayOutput m_Output = EDisplayOutput::Sdr;    //!< SetOutput
  DXGI_FORMAT m_SwapChainFormat = D3D12_BACKBUFFER_FORMAT;
  float m_fMetadataPeak = 0, m_fMetadataFrameAverage = 0;   //!< HDR10's metadata as set
  bool m_bLayer = false;                            //!< the frame target and the highlight layer are wanted
  CD3D12Resource m_pFrame, m_pHighlight;            //!< CreateLayer (their states kept by the tracker)
  bool m_bHighlightBound = false;                   //!< BindHighlight
  bool m_bOutput = false;                           //!< BeginOutput .. EndFrame
  SDisplayInfo m_Display;
  DisplayPoll::SLast m_DisplayLast;                 //!< m_Display's lookup: done, stale, its monitor, GetTickCount64 when
  bool m_bDisplayAppActive = true;                  //!< SetAppActive
  uint32 m_nDisplayLookups = 0;                     //!< GetDisplayLookups
  double m_fDisplayLookupMs = 0;                    //!< GetDisplayLookupMs

  ComPtr<ID3D12CommandAllocator> m_pAllocators[D3D12_FRAMES];
  uint64 m_nFrameFence[D3D12_FRAMES] = {};
  ComPtr<ID3D12GraphicsCommandList> m_pCmdList;
  mutable uint32 m_nStateEpoch = 0;                 //!< GetStateEpoch
  ComPtr<ID3D12Fence> m_pFence;
  HANDLE m_hFenceEvent = nullptr;
  uint64 m_nNextFence = 1;
  ComPtr<ID3D12Fence> m_pFlushFence;                //!< WaitForGPU's in a frame (not the frames' fence)
  uint64 m_nFlushFence = 0;
  // The compute queue (BeginAsyncCompute): a slot's allocator is free when the slot's frame fence is
  // reached, the frame's list having waited for the compute list.
  ComPtr<ID3D12CommandQueue> m_pComputeQueue;
  ComPtr<ID3D12CommandAllocator> m_pComputeAllocators[D3D12_FRAMES];
  ComPtr<ID3D12GraphicsCommandList4> m_pComputeList;
  ComPtr<ID3D12Fence> m_pUploadFence, m_pComputeFence;  //!< the uploads done (for the compute queue), the compute list done (for the direct one)
  uint64 m_nUploadFence = 0, m_nComputeFence = 0;
  bool m_bComputeAllocatorReset = false;            //!< the slot's compute allocator reset in this frame
  bool m_bComputePending = false;                   //!< a compute list submitted that the frame's list has not waited for
  bool m_bComputeTimed = false;                     //!< the open compute list has its first timestamp

  ComPtr<ID3D12DescriptorHeap> m_pRTVHeap;
  ComPtr<ID3D12DescriptorHeap> m_pDSVHeap;
  uint32 m_nRTVSize = 0;
  ComPtr<ID3D12Resource> m_pBackBuffers[D3D12_BACKBUFFERS];
  CD3D12Resource m_pDepth;
  int m_nBackBuffer = 0;
  ID3D12Resource *m_pOffscreen = nullptr;           //!< SetOffscreenTargets: the colour target drawn into (SetShadowMapTarget: the shadow map)
  ID3D12Resource *m_pOffscreenSecond = nullptr;     //!< BindSecondTarget
  D3D12_CPU_DESCRIPTOR_HANDLE m_hOffscreenSecondRTV = {};
  bool m_bOffscreenShadowMap = false;               //!< m_pOffscreen is a shadow map
  UINT m_nOffscreenSubresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;   //!< the sub resource of m_pOffscreen drawn into
  D3D12_CPU_DESCRIPTOR_HANDLE m_hOffscreenRTV = {}, m_hOffscreenDSV = {};
  int m_nOffscreenWidth = 0, m_nOffscreenHeight = 0;
  DXGI_FORMAT m_OffscreenFormat = DXGI_FORMAT_UNKNOWN;
  CD3D12Resource m_pSceneColor, m_pSceneVelocity, m_pUpscaleDepth, m_pSceneDepth, m_pSceneOutput;   //!< SetSceneSize
  int m_nSceneWidth = 0, m_nSceneHeight = 0;
  int m_nSceneOutputWidth = 0, m_nSceneOutputHeight = 0;
  DXGI_FORMAT m_SceneColorFormat = D3D12_BACKBUFFER_FORMAT;
  DXGI_FORMAT m_SceneOutputFormat = D3D12_UPSCALE_FORMAT;
  bool m_bInScene = false;                          //!< BeginScene .. EndScene
  CD3D12Resource m_pHdrColor;                       //!< SetHdrTarget
  bool m_bInHdr = false;                            //!< BeginHdr .. EndHdr
  D3D12_CPU_DESCRIPTOR_HANDLE m_hAmbientRTV = {};   //!< BindAmbient
  ID3D12Resource *m_pAmbientResource = nullptr;     //!< BindAmbient: what the multisampled ambient light resolves into
  CD3D12Resource m_pMsaaColor, m_pMsaaDepth, m_pMsaaAmbient;   //!< SetMsaaTargets
  int m_nMsaaSamples = 0;                           //!< of m_pMsaaColor (0: none)
  DXGI_FORMAT m_MsaaColorFormat = DXGI_FORMAT_UNKNOWN;
  uint32 m_nMsaaGeneration = 0;                     //!< GetMsaaGeneration
  bool m_bInMsaa = false;                           //!< BeginMsaa .. EndMsaa
  D3D12_CPU_DESCRIPTOR_HANDLE m_hPathRTV[5] = {};   //!< BindPathTargets
  ID3D12Resource *m_pPathResources[5] = {};
  bool m_bDepthReadOnly = false;                    //!< SetDepthReadOnly

  int m_nWidth = 0, m_nHeight = 0;
  int m_nFrameSlot = 0;
  bool m_bInFrame = false;

  Barriers::CBarrierTracker m_Barriers;             //!< the frame list's barriers (Transition, UavBarrier)
  Barriers::CBoundTargets m_Bound;                  //!< the targets bound to the frame's list (FlushBarriersForDraw)
  int m_nBarrierBatchWanted = 0;                    //!< SetBarrierMode, taken at BeginFrame
  bool m_bBarrierCheckWanted = false;
  bool m_bBarrierSections = false;                  //!< d3d12_BarrierBatch 2: GpuSection gives what is queued
  bool m_bBarrierCheck = false;                     //!< d3d12_BarrierCheck in this frame
  Barriers::SStats m_LastBarrierStats;              //!< GetBarrierStats

  ComPtr<ID3D12QueryHeap> m_pTimestamps;            //!< GPU_MARKS per frame slot; NULL: no GPU timing
  CD3D12Resource m_pTimestampReadback;              //!< the resolved timestamps, in the same places
  double m_fTimestampMs = 0;                        //!< milliseconds per tick
  bool m_bGpuTiming = false;
  const char *m_GpuSections[D3D12_FRAMES][GPU_MARKS] = {};   //!< the section each timestamp starts
  int m_nGpuMarks[D3D12_FRAMES] = {};
  std::vector<std::pair<const char *, double>> m_GpuTimes;
  ComPtr<ID3D12QueryHeap> m_pComputeTimestamps;     //!< COMPUTE_LISTS pairs per frame slot; NULL: the compute lists not timed
  CD3D12Resource m_pComputeTimestampReadback;
  double m_fComputeTimestampMs = 0;
  const char *m_ComputeSections[D3D12_FRAMES][COMPUTE_LISTS] = {};
  int m_nComputeLists[D3D12_FRAMES] = {};
  std::vector<std::pair<const char *, double>> m_ComputeGpuTimes;
  int m_nGpuTimedFrames = 0;
  double m_fWaitMs = 0;                             //!< GetWaitMs
  double m_fPresentMs = 0;                          //!< GetPresentMs
  char m_szAdapter[128] = {};                       //!< GetAdapterName

  ComPtr<ID3D12QueryHeap> m_pOcclusion;             //!< OCCLUSION_QUERIES per frame slot; NULL: no queries
  CD3D12Resource m_pOcclusionReadback;              //!< the resolved counts, in the same places
  int m_nOcclusions[D3D12_FRAMES] = {};             //!< queries begun in the slot's frame
  std::vector<uint64> m_OcclusionResults;

  struct SDeferred
  {
    CD3D12Resource Memory;
    ComPtr<IUnknown> pObject;
    uint64 nFence;
  };
  std::vector<SDeferred> m_Deferred;
};
