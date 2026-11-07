// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: NrdHost.h
//	Description: NVIDIA NRD (third_party/nrd, a git submodule; built in
//	with XRENDERD3D12_NRD), the path tracer's denoiser with
//	d3d12_PTDenoiser nrd: its REBLUR_DIFFUSE run by the renderer's device.
//
//	NRD hands out its passes as descriptions (nrd::DispatchDesc): the
//	pipeline (its DXIL), the textures it reads and writes in the order of
//	the pipeline's ranges (SRVs t0.., UAVs u0.. in its resources' space),
//	its constants (a root CBV b0 in the space of its samplers, the static
//	ones s0 nearest and s1 linear, both clamped). For each pipeline a
//	root signature of exactly those ranges and a pipeline state, made
//	once and kept; for each set of textures a pass names a descriptor
//	table (CD3D12Descriptors::AllocRange), written once and kept while the
//	textures stay; its constants in per frame memory. NRD's own textures
//	(its permanent and transient pools) are compute targets of the size
//	it names. Before a pass a UAV barrier and the transitions its
//	textures want; the caller's textures are in UNORDERED_ACCESS before
//	and after Denoise.
//	Without NRD (a build without the submodules) IsBuilt is false and Init
//	fails: the path tracer keeps its own denoiser.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

class CD3D12Texture;
class CD3D12Descriptors;
class CD3D12Upload;

//! A frame's view for NRD: the engine's matrices (row vectors, Matrix44's layout), without the jitter.
struct SNrdView
{
  float mWorldToView[16];
  float mViewToClip[16];
  float vJitter[2];           //!< this frame's jitter (pixels, -0.5..0.5)
  float fDenoisingRange;      //!< the view depth (m) beyond which NRD leaves the pixels be
  bool bRestart;              //!< the history is gone (a new instance, a cut, the frames before were not denoised)
};

//! The caller's textures (their size Init's; their states kept by the device, TrackState, left as NRD's last passes
//! want them).
struct SNrdTextures
{
  CD3D12Texture *pMotion;             //!< IN_MV: the uv where each pixel was, less its own (RG16F)
  CD3D12Texture *pNormalRoughness;    //!< IN_NORMAL_ROUGHNESS (NRD_FrontEnd_PackNormalAndRoughness; R10G10B10A2)
  CD3D12Texture *pViewZ;              //!< IN_VIEWZ: the view depth (R32F)
  CD3D12Texture *pDiffuse;            //!< IN_DIFF_RADIANCE_HITDIST (REBLUR_FrontEnd_PackRadianceAndNormHitDist; RGBA16F)
  CD3D12Texture *pOut;                //!< OUT_DIFF_RADIANCE_HITDIST (REBLUR_BackEnd_UnpackRadianceAndNormHitDist; RGBA16F)
};

//! REBLUR's settings the renderer changes.
struct SNrdSettings
{
  float fHitDistance[3];              //!< the hit distance's normalization (A, B, C: ReblurHitDistanceParameters)
  unsigned nMaxFrames;                //!< the history's frames at most (maxAccumulatedFrameNum)
};

class CD3D12Nrd
{
public:
  //! Whether this build has NRD in it.
  static bool IsBuilt();
  //! NRD's version ("4.18.0"), "" without it.
  static const char *GetVersion();
  //! REBLUR's defaults for SNrdSettings.
  static SNrdSettings GetDefaults();

  CD3D12Nrd();
  ~CD3D12Nrd();

  //! The instance and its textures for nWidth x nHeight (again for another size); false, logged, when NRD is not
  //! built in or the device refuses something.
  bool Init(ID3D12Device *pDevice, CD3D12Descriptors *pDescriptors, CD3D12Upload *pUpload, int nWidth, int nHeight);
  //! Gives the instance, its textures and its tables back (the GPU may still use them: deferred); the pipelines stay.
  void Release();
  bool IsReady() const;
  int GetWidth() const;
  int GetHeight() const;

  //! Records the passes of a frame into pList, the device's frame list (the descriptor heap of CD3D12Descriptors bound;
  //! the compute root signature and pipeline state are NRD's after it); false when it could not (the next frame starts
  //! NRD's history again).
  bool Denoise(ID3D12GraphicsCommandList *pList, const SNrdView &View, const SNrdSettings &Settings, const SNrdTextures &Textures);
  //! The passes of the last Denoise, the tables kept, the textures NRD has (for the frame dump).
  void GetStats(int &nPasses, int &nTables, int &nTextures) const;

private:
  struct SImpl;
  std::unique_ptr<SImpl> m_p;
};
