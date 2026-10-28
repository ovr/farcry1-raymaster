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

constexpr int D3D12_FRAMES = 2;          //!< frames in flight
constexpr int D3D12_BACKBUFFERS = 3;     //!< swap chain buffers

constexpr DXGI_FORMAT D3D12_BACKBUFFER_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM;   //!< with alpha (the HUD masks through it)
constexpr DXGI_FORMAT D3D12_DEPTH_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;

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
  //! Sets the back buffer and the depth buffer as render targets, the full viewport and scissor.
  void SetBackBufferTargets();

  //! Clears the back buffer / the depth buffer (in a frame).
  void ClearColor(const float rgba[4]);
  void ClearDepth(float fDepth, uint8 nStencil, bool bDepth, bool bStencil);

  //! Textures of the back buffer's size for CopyTargets: pColor in the back buffer's format, pDepth
  //! R24G8_TYPELESS (read as R24_UNORM_X8_TYPELESS); both in PIXEL_SHADER_RESOURCE state.
  bool CreateTargetCopies(ComPtr<ID3D12Resource> &pColor, ComPtr<ID3D12Resource> &pDepth);
  //! Copies the back buffer and the depth buffer as drawn so far in this frame into the textures
  //! of CreateTargetCopies (PIXEL_SHADER_RESOURCE before and after).
  void CopyTargets(ID3D12Resource *pColor, ID3D12Resource *pDepth);

  //! Copies the current back buffer (as drawn so far in this frame, or the last presented one
  //! between frames) to pDst: nWidth x nHeight pixels of B, G, R, A, rows from the top.
  //! Waits for the GPU.
  bool ReadBackBuffer(uint8 *pDst, int nWidth, int nHeight);

  //! The debug layer's messages to the engine's log.
  void LogDebugMessages();

private:
  bool CreateSwapChainBuffers();
  void ReleaseSwapChainBuffers();
  //! Resets the slot's allocator and the list and binds the heap, root signature and targets.
  void OpenCmdList();
  void WaitForFence(uint64 nValue);

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

  int m_nWidth = 0, m_nHeight = 0;
  int m_nFrameSlot = 0;
  bool m_bInFrame = false;

  struct SDeferred
  {
    ComPtr<IUnknown> pObject;
    uint64 nFence;
  };
  std::vector<SDeferred> m_Deferred;
};
