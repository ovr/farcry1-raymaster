// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: Upscaler.h
//	Description: The upscalers of the main view (d3d12_Upscaler). The
//	main view's 3D scene is drawn at a render size, jittered by a sub-pixel
//	offset that changes every frame, with its depth and motion vectors; the
//	upscaler makes the picture of the display's size from it and the
//	pictures before (Upscale.cpp). AMD FSR Upscaling is UpscalerFsr.cpp;
//	Intel XeSS and NVIDIA DLSS are to follow.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

//! d3d12_UpscaleMode: the render size over the display size (AMD's numbers, also XeSS's and DLSS's modes).
enum EUpscaleMode
{
  eUM_NativeAA = 0,             //!< 1x: anti-aliasing only
  eUM_Quality,                  //!< 1.5x
  eUM_Balanced,                 //!< 1.7x
  eUM_Performance,              //!< 2x
  eUM_UltraPerformance,         //!< 3x
  eUM_Num
};

//! What the upscaler gets for a frame. The inputs are in the state PIXEL_SHADER_RESOURCE |
//! NON_PIXEL_SHADER_RESOURCE, the output in UNORDERED_ACCESS; they are in the same states after.
struct SUpscaleInputs
{
  ID3D12GraphicsCommandList *pCmdList = nullptr;
  ID3D12Resource *pColor = nullptr;     //!< the scene at the render size, sRGB-ish LDR colours (D3D12_BACKBUFFER_FORMAT, or
                                        //!< with r_HDRRendering the HDR scene tone mapped, R8G8B8A8_UNORM)
  ID3D12Resource *pDepth = nullptr;     //!< its depth, R32_FLOAT (0 near, 1 far)
  ID3D12Resource *pVelocity = nullptr;  //!< R16G16_FLOAT: where each pixel was in the frame before minus where it is, in uv
  ID3D12Resource *pOutput = nullptr;    //!< the picture of the display's size (R8G8B8A8_UNORM)
  int nRenderWidth = 0, nRenderHeight = 0;
  float fJitterX = 0, fJitterY = 0;     //!< the frame's jitter (GetJitter), render pixels
  float fNear = 0, fFar = 0;            //!< the camera's planes (metres)
  float fFovY = 0;                      //!< its vertical field of view (radians)
  float fFrameMs = 0;                   //!< the time since the frame before
  float fSharpness = 0;                 //!< 0 (none) to 1
  bool bReset = false;                  //!< the frame before shows something else: no history
  bool bDebugView = false;              //!< the upscaler's debug view instead of the picture
};

class IUpscaler
{
public:
  virtual ~IUpscaler() = default;
  //! A context for a display of nWidth x nHeight; false when the upscaler is not there or refuses
  //! the device (logged). bDebug: the upscaler checks what it gets (d3d12_Debug). nVersion
  //! (d3d12_UpscaleVersion): the major version of the upscaler wanted, its newest of that major; 0
  //! the one the upscaler picks for the GPU; one it does not have acts as 0 (logged).
  virtual bool Create(ID3D12Device *pDevice, int nWidth, int nHeight, bool bDebug, int nVersion) = 0;
  //! The render size of a mode (EUpscaleMode) for the display of Create.
  virtual void GetRenderSize(int nMode, int &nWidth, int &nHeight) = 0;
  //! The jitter of the nFrame-th frame (counting up from 0) at the render width: x and y in render
  //! pixels, -0.5 to 0.5.
  virtual void GetJitter(int nFrame, int nRenderWidth, float &x, float &y) = 0;
  //! Records the upscale into In.pCmdList. It binds descriptor heaps and a root signature of its
  //! own; false when it failed (logged once).
  virtual bool Dispatch(const SUpscaleInputs &In) = 0;
  //! For the log: the upscaler and its version.
  virtual const char *GetName() const = 0;
};

//! AMD FSR Upscaling through the AMD FSR SDK's loader (amd_fidelityfx_loader_dx12.dll next to this
//! renderer).
std::unique_ptr<IUpscaler> CreateUpscalerFsr();
