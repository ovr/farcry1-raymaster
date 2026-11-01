// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Compute.h
//	Description: The compute passes: their root signature and pipeline
//	states.
//
//	One root signature for every kernel:
//	  0: root CBV b0 (the pass's constants, CD3D12Upload::AllocFrame)
//	  1: descriptor table t0..t3 (COMPUTE_SRVS SRVs)
//	  2: descriptor table u0..u4 (COMPUTE_UAVS UAVs)
//	  static samplers s0 point clamp, s1 linear clamp, s2 the shadow maps'
//	  comparison (LESS_EQUAL, bilinear, white border: lit outside)
//	The tables are runs of CD3D12Descriptors::AllocRange of exactly that
//	many slots, the unused ones holding null views. The compute root
//	signature is separate from the draws' one, so a dispatch leaves the
//	draws' bindings as they are; the next draw sets its own pipeline state.
//
//	The kernels with rays (Shader Model 6.5, Shaders/RayShade.hlsli) have a
//	root signature of their own, the draws' with the pass's constants in
//	place of the draw's (DispatchRays):
//	  0: root CBV b0 (SFrameConstants)
//	  1: root CBV b2 (the pass's constants)
//	  2: the texture table (D3D12Pipeline.h: spaces 1, 2, 3, 4 and 6)
//	  3: root SRV t0 space5, the scene for rays (ROOT_RAY_SCENE)
//	  4: root SRV t1 space5, its instances (ROOT_RAY_INSTANCES)
//	  5: descriptor table u0..u4 (COMPUTE_UAVS UAVs)
//	  6: root SRV t2 space5, the path tracer's lights (ROOT_RAY_LIGHTS,
//	     SPTLight of Shaders/RayConstants.h)
//	  static samplers s0..s4 as the draws' (XR_SAMPLER_*, the shadow maps'
//	  comparison)
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <wrl/client.h>

//! The kernels (Shaders/Shaders.cmake).
enum ED3D12Kernel
{
  eD3D12K_GtaoPrefilterDepths = 0,  //!< XeGTAO: the view space depth and its MIPs (Shaders/XeGtao.hlsl)
  eD3D12K_GtaoNormals,              //!< XeGTAO: the view space normals from the depth
  eD3D12K_GtaoLow,                  //!< XeGTAO's main pass by quality (d3d12_AOQuality 0..3)
  eD3D12K_GtaoMedium,
  eD3D12K_GtaoHigh,
  eD3D12K_GtaoUltra,
  eD3D12K_GtaoDenoise,              //!< XeGTAO's denoise, a pass before the last
  eD3D12K_GtaoDenoiseLast,          //!< its last pass, into the output
  eD3D12K_AoResolve,                //!< the AO as the factor the scene is multiplied by (Shaders/AoResolve.hlsl)
  eD3D12K_Meter,                    //!< the HDR meter's first level (Shaders/Meter.hlsl)
  eD3D12K_Bloom,                    //!< a level of the bloom from one twice its size (Shaders/Bloom.hlsl)
  eD3D12K_DepthTiles,               //!< levels 0..4 of the depth tiles of the water's depth copy (Shaders/DepthTiles.hlsl)
  eD3D12K_DepthTilesUp,             //!< their levels 5 and 6 from level 4
  eD3D12K_VolumetricInject,         //!< the volumetric light's froxels (Shaders/Volumetric.hlsl)
  eD3D12K_VolumetricIntegrate,      //!< the light they scatter towards the camera, integrated
  eD3D12K_RaySunTrace,              //!< the sun's shadow by rays: the rays (Shaders/RaySun.hlsl; with rays)
  eD3D12K_RaySunTemporal,           //!< their visibility blended into the history
  eD3D12K_RaySunFilter,             //!< the history over the soft edges, into the mask
  eD3D12K_RayAOTrace,               //!< the ambient occlusion by rays: the rays (Shaders/RayAO.hlsl; with rays)
  eD3D12K_RayAOTemporal,            //!< their visibility blended into the history
  eD3D12K_RayAOFilter,              //!< the history filtered into the visibility
  eD3D12K_RayGITrace,               //!< the ambient light by rays (d3d12_RTGI): the rays, what they find shaded (RAY_GI)
  eD3D12K_RayGITemporal,            //!< their factor blended into the history
  eD3D12K_RayGIFilter,              //!< the history filtered into the factor
  eD3D12K_PathTrace,                //!< the path tracer (d3d12_PathTracing): its paths (Shaders/PathTrace.hlsl; with rays)
  eD3D12K_PathTemporal,             //!< their light blended into the history
  eD3D12K_PathFilter,               //!< the history filtered into the composite's light
  eD3D12K_PathSpecular,             //!< the highlights of the sun and the lamps (with rays)
  eD3D12K_PathNrdPack,              //!< NRD's inputs from the paths (Shaders/PathNrd.hlsl; with rays; only with NRD built in)
  eD3D12K_PathNrdResolve,           //!< NRD's light as a history for the filter (the same)
  eD3D12K_RaySunFfxInputs,          //!< the sun's shadow by rays with AMD FidelityFX Denoiser: the normals and the depth
                                    //!< (Shaders/RaySunFfx.hlsl; with rays; only with its submodule)
  eD3D12K_RaySunFfxPrepare,         //!< its bit masks of the rays
  eD3D12K_RaySunFfxClassify,        //!< its tile classification and history
  eD3D12K_RaySunFfxFilter,          //!< a pass of its filter
  eD3D12K_RaySunFfxPack,            //!< the denoised visibility into the mask
  eD3D12K_Num
};

class CD3D12Compute
{
public:
  static constexpr int COMPUTE_SRVS = 4;
  static constexpr int COMPUTE_UAVS = 5;
  //! The root parameter of the path tracer's lights in the root signature with rays.
  static constexpr UINT ROOT_RAY_LIGHTS = 6;

  CD3D12Compute();
  ~CD3D12Compute();

  bool Init(ID3D12Device *pDevice);
  void Shutdown();

  //! Records a dispatch of nGroupsX x nGroupsY x nGroupsZ groups into pList: the kernel, b0 at CB, the
  //! SRV table from slot nSRVs and the UAV table from slot nUAVs (runs of AllocRange; -1 leaves a table
  //! unset). The descriptor heap of CD3D12Descriptors must be bound.
  void Dispatch(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, D3D12_GPU_VIRTUAL_ADDRESS CB, int nSRVs, int nUAVs,
                UINT nGroupsX, UINT nGroupsY, UINT nGroupsZ = 1);
  //! The same for a kernel with rays: b0 at FrameCB, b2 at PassCB, the texture table, the scene for rays
  //! nRayScene and its instances nRayInstances (0: unset), the UAV table from slot nUAVs, the path tracer's lights
  //! nLights (0: unset).
  void DispatchRays(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, D3D12_GPU_VIRTUAL_ADDRESS FrameCB, D3D12_GPU_VIRTUAL_ADDRESS PassCB,
                    D3D12_GPU_VIRTUAL_ADDRESS nRayScene, D3D12_GPU_VIRTUAL_ADDRESS nRayInstances, int nUAVs, UINT nGroupsX, UINT nGroupsY,
                    D3D12_GPU_VIRTUAL_ADDRESS nLights = 0);
  //! Whether the kernel is built in and the device takes it (its pipeline state made).
  bool HasKernel(ED3D12Kernel eKernel) { return GetPSO(eKernel) != nullptr; }

private:
  bool CreateRootSignature();
  bool CreateRayRootSignature();
  //! nullptr when the device refuses it (logged once).
  ID3D12PipelineState *GetPSO(ED3D12Kernel eKernel);

  ID3D12Device *m_pDevice = nullptr;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRootSignature;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRayRootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pPSOs[eD3D12K_Num];
  bool m_bFailed[eD3D12K_Num] = {};
};
