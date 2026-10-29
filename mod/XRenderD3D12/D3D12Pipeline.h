// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: D3D12Pipeline.h
//	Description: The root signature, the input layouts of the engine's
//	vertex formats and the cache of pipeline states.
//
//	There is one root signature for every draw (Shaders/Constants.h):
//	  0: root CBV b0 (SFrameConstants)
//	  1: root CBV b1 (SDrawConstants)
//	  2: descriptor table, the whole texture table (t0 space1 and t0 space2)
//	  static samplers s0..s3, s4 the comparison sampler of the shadow maps
//	The programs are general ("fixed function") shaders whose features are
//	switched by SDrawConstants::nFlags, so a pipeline state depends only on
//	the program, the vertex format and the fixed state below.
//
//	Vertex formats: every program reads POSITION, NORMAL, COLOR0, COLOR1,
//	TEXCOORD0, TEXCOORD1, TANGENT, BINORMAL and TNORMAL. What a format lacks
//	comes from input slot 1, a small buffer bound with stride 0 (normal
//	0,0,1; colours white; texture coordinates 0; tangent frame 1,0,0 0,1,0
//	0,0,1): BindDefaultStream sets it. Slot 0 is the engine's vertex buffer.
//	Vertex colours (COL4UB) are bytes B, G, R, A in memory.
//	VERTEX_FORMAT_TRP3F_COL4UB_TEX2F (pre-transformed): x, y in pixels; use
//	DF_SCREEN_POS. VERTEX_FORMAT_TEX2F and VERTEX_FORMAT_T3F_B3F_N3F are
//	not drawn (lightmap coordinates and tangents are extra streams).
//	Extra vertex streams (SPipelineKey::nStreams): TEXCOORD1 from slot 2
//	(lightmap coordinates, VERTEX_FORMAT_TEX2F), the tangent frame from
//	slot 3 (SPipTangents).
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <wrl/client.h>
#include <unordered_map>

enum ED3D12Program
{
  eD3D12P_General = 0,    //!< Shaders/General.hlsl: everything the DF_* flags describe
  eD3D12P_Water,          //!< Shaders/Water.hlsl: water surfaces (refraction, reflection, waves)
  eD3D12P_Post,           //!< Shaders/Post.hlsl: the screen effects (POST_* flags)
  eD3D12P_Hdr,            //!< Shaders/Hdr.hlsl: exposure, bloom and tone mapping of the HDR scene (HDR_* flags)
  eD3D12P_Num
};

//! SPipelineKey::nStreams: the extra vertex streams of a draw.
constexpr uint8 PIPELINE_STREAM_TEXCOORD1 = 1;    //!< TEXCOORD1 from slot 2 (VERTEX_FORMAT_TEX2F)
constexpr uint8 PIPELINE_STREAM_TANGENTS = 2;     //!< TANGENT, BINORMAL, TNORMAL from slot 3 (SPipTangents)

//! Everything a pipeline state depends on. Zero-initialise, then set the members.
struct SPipelineKey
{
  uint32 nState;          //!< GS_* bits used: GS_BLSRC_MASK, GS_BLDST_MASK, GS_DEPTHWRITE, GS_NODEPTHTEST,
                          //!< GS_DEPTHFUNC_EQUAL, GS_DEPTHFUNC_GREAT, GS_COLMASKONLYALPHA, GS_COLMASKONLYRGB,
                          //!< GS_NOCOLMASK (no colour writes); the rest is ignored. Depth func LESS_EQUAL by default.
  uint8 nProgram;         //!< ED3D12Program
  uint8 nVertexFormat;    //!< VERTEX_FORMAT_*
  uint8 nTopology;        //!< D3D12_PRIMITIVE_TOPOLOGY_TYPE (TRIANGLE or LINE)
  uint8 nCull;            //!< R_CULL_NONE, R_CULL_FRONT, R_CULL_BACK (the engine's front faces are counter-clockwise
                          //!< as the viewer sees them; one constant in D3D12Pipeline.cpp)
  uint8 bDepthBias;       //!< polygon offset (decals, EF_PolygonOffset)
  uint8 nStreams;         //!< PIPELINE_STREAM_* bits
  uint8 nTarget;          //!< 0 back buffer (D3D12_BACKBUFFER_FORMAT + D3D12_DEPTH_FORMAT), 1 R8G8B8A8 target without depth,
                          //!< 2 a shadow map (D3D12_SHADOW_MAP_FORMAT only; bDepthBias gives the shadow maps' bias),
                          //!< 3 an upscaler's scene (D3D12_BACKBUFFER_FORMAT + D3D12_VELOCITY_FORMAT + D3D12_UPSCALE_DEPTH_FORMAT
                          //!< + D3D12_SCENE_DEPTH_FORMAT; DrawGeometry makes 0 into 3 there): the general program's opaque
                          //!< draws write the motion vectors, its depth writes the upscaler's depth, the water both
  uint8 bWireframe;       //!< fill mode wireframe (SetPolygonMode R_WIREFRAME_MODE)
  uint8 nColorFormat;     //!< the format of colour target 0: PIPELINE_COLOR_* (DrawGeometry sets it for the main view's target)
  uint8 nPad[3];          //!< 0: no padding bytes in the key
};

//! SPipelineKey::nColorFormat.
constexpr uint8 PIPELINE_COLOR_DEFAULT = 0;       //!< nTarget 0 and 3: D3D12_BACKBUFFER_FORMAT, 1: R8G8B8A8_UNORM
constexpr uint8 PIPELINE_COLOR_HDR = 1;           //!< D3D12_HDR_FORMAT
constexpr uint8 PIPELINE_COLOR_R32F = 2;          //!< R32_FLOAT

class CD3D12Pipeline
{
public:
  CD3D12Pipeline();
  ~CD3D12Pipeline();

  bool Init(ID3D12Device *pDevice);
  void Shutdown();

  ID3D12RootSignature *GetRootSignature() const;
  //! The pipeline state for the key, created on first use; NULL for a vertex format that can't be drawn.
  ID3D12PipelineState *GetPSO(const SPipelineKey &Key);
  //! Binds input slot 1, the default values of the attributes a vertex format lacks.
  void BindDefaultStream(ID3D12GraphicsCommandList *pList);

  //! Size in bytes of a vertex of the format (m_VertexSize of VertexFormats.h, also for the
  //! formats gBufInfoTable leaves out).
  static int GetVertexSize(int nVertexFormat);

private:
  struct SKeyHash
  {
    size_t operator()(const SPipelineKey &Key) const;
  };
  struct SKeyEqual
  {
    bool operator()(const SPipelineKey &a, const SPipelineKey &b) const;
  };

  bool CreateRootSignature();
  bool CreateDefaultStream();
  //! nullptr when the key can't be drawn or the device refuses it.
  Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePSO(const SPipelineKey &Key);

  ID3D12Device *m_pDevice = nullptr;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRootSignature;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_pDefaultStream;   //!< input slot 1
  D3D12_VERTEX_BUFFER_VIEW m_DefaultStreamView = {};
  //! Every key asked for, with nullptr for the keys that failed (logged once).
  std::unordered_map<SPipelineKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, SKeyHash, SKeyEqual> m_PSOs;
};
