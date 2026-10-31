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
  eD3D12K_Num
};

class CD3D12Compute
{
public:
  static constexpr int COMPUTE_SRVS = 4;
  static constexpr int COMPUTE_UAVS = 5;

  CD3D12Compute();
  ~CD3D12Compute();

  bool Init(ID3D12Device *pDevice);
  void Shutdown();

  //! Records a dispatch of nGroupsX x nGroupsY x nGroupsZ groups into pList: the kernel, b0 at CB, the
  //! SRV table from slot nSRVs and the UAV table from slot nUAVs (runs of AllocRange; -1 leaves a table
  //! unset). The descriptor heap of CD3D12Descriptors must be bound.
  void Dispatch(ID3D12GraphicsCommandList *pList, ED3D12Kernel eKernel, D3D12_GPU_VIRTUAL_ADDRESS CB, int nSRVs, int nUAVs,
                UINT nGroupsX, UINT nGroupsY, UINT nGroupsZ = 1);

private:
  bool CreateRootSignature();
  //! nullptr when the device refuses it (logged once).
  ID3D12PipelineState *GetPSO(ED3D12Kernel eKernel);

  ID3D12Device *m_pDevice = nullptr;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pPSOs[eD3D12K_Num];
  bool m_bFailed[eD3D12K_Num] = {};
};
