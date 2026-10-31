// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: AmbientOcclusion.h
//	Description: The screen space ambient occlusion of the main view
//	(d3d12_AO). After the opaque list a backend computes from the depth
//	buffer how much of the hemisphere above each pixel is open (the
//	visibility); AmbientOcclusion.cpp makes it the factor the scene drawn
//	so far is multiplied by, or (d3d12_AOApply 1) the part of the ambient
//	light it takes from the scene. Intel XeGTAO is AOXeGtao.cpp, AMD
//	FidelityFX CACAO AOCacao.cpp. Both make the normals from the depth
//	themselves.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

//! What a backend works on, fixed while it lives (made again when it changes).
struct SAOTargets
{
  ID3D12Resource *pDepth = nullptr;     //!< the main view's depth buffer (0 near, 1 far), NON_PIXEL_SHADER_RESOURCE | DEPTH_READ (with d3d12_AOApply 1 also PIXEL_SHADER_RESOURCE) in Dispatch
  DXGI_FORMAT DepthFormat = DXGI_FORMAT_UNKNOWN;  //!< the format it is read in (R24_UNORM_X8_TYPELESS, R32_FLOAT)
  ID3D12Resource *pOutput = nullptr;    //!< the visibility, R8_TYPELESS of the depth's size (1: open); UNORDERED_ACCESS before and after Dispatch
  int nWidth = 0, nHeight = 0;
};

//! What changes from frame to frame.
struct SAOFrame
{
  ID3D12GraphicsCommandList *pCmdList = nullptr;
  float Proj[16] = {};                  //!< the projection the depth was drawn with (row major, as Matrix44), without an upscaler's jitter
  float fRadius = 1;                    //!< how far a surface occludes (m)
  int nQuality = 2;                     //!< 0 to 3
  int nDenoise = 2;                     //!< XeGTAO's denoise passes (0 to 3), CACAO's blur passes (1 to 8)
  int nNoiseIndex = 0;                  //!< the frame's noise: 0, or counting up while an upscaler gathers the frames
  float fFadeStart = 0, fFadeEnd = 0;   //!< the distance (m) over which it fades out (CACAO; the resolve fades it with the fog)
};

class IAmbientOcclusion
{
public:
  virtual ~IAmbientOcclusion() = default;
  //! The resources for Targets; false when the backend refuses them (logged). bDebug: d3d12_Debug.
  virtual bool Create(ID3D12Device *pDevice, const SAOTargets &Targets, bool bDebug) = 0;
  //! Records the passes into Frame.pCmdList. It may bind descriptor heaps, root signatures and
  //! pipeline states of its own; false when it failed (logged once).
  virtual bool Dispatch(const SAOFrame &Frame) = 0;
  //! For the log: the backend and its version.
  virtual const char *GetName() const = 0;
};

//! Intel XeGTAO 1.30 (third_party/xegtao) on the renderer's compute passes (D3D12Compute.h).
std::unique_ptr<IAmbientOcclusion> CreateAOXeGtao();
//! AMD FidelityFX CACAO 1.2 (third_party/fidelityfx-cacao) through its own Direct3D 12 host.
std::unique_ptr<IAmbientOcclusion> CreateAOCacao();
