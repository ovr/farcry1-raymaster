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
//	  2: descriptor table, the whole texture table (t0 space1, space2 and
//	     space3; raw buffers in space4 for the rays; volumes in space6)
//	  3: root SRV t0 space5, the scene for rays (ROOT_RAY_SCENE)
//	  4: root SRV t1 space5, its instances (ROOT_RAY_INSTANCES)
//	  static samplers s0..s3, s4 the comparison sampler of the shadow maps
//	Only the programs with rays (Shader Model 6.5, RayTracing.h) read 3 and
//	4; DrawGeometry binds them for those alone.
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
//	The keys of the pipeline states made are kept in a file of the working
//	directory (XRenderD3D12_pipelines.bin), and Init makes those of the
//	runs before on several threads: a pipeline state made in the frame that
//	first draws with it holds that frame up (a tenth of a millisecond and
//	more each, tens of them where a level shows something new).
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
  eD3D12P_RayDebug,       //!< Shaders/RayDebug.hlsl: the scene for rays seen from the main view's camera (d3d12_RTDebug)
  eD3D12P_Num
};

//! The root parameters the programs with rays read (above).
constexpr UINT ROOT_RAY_SCENE = 3;
constexpr UINT ROOT_RAY_INSTANCES = 4;

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
  uint8 bDepthBias;       //!< polygon offset (decals, EF_PolygonOffset); 2 in a shadow map: the sun's cascades (no depth clip)
  uint8 nStreams;         //!< PIPELINE_STREAM_* bits
  uint8 nTarget;          //!< 0 back buffer (D3D12_BACKBUFFER_FORMAT + D3D12_DEPTH_FORMAT), 1 R8G8B8A8 target without depth,
                          //!< 2 a shadow map (D3D12_SHADOW_MAP_FORMAT only; bDepthBias gives the shadow maps' bias),
                          //!< 3 an upscaler's scene (D3D12_BACKBUFFER_FORMAT + D3D12_VELOCITY_FORMAT + D3D12_UPSCALE_DEPTH_FORMAT
                          //!< + D3D12_SCENE_DEPTH_FORMAT; DrawGeometry makes 0 into 3 there): the general program's opaque
                          //!< draws write the motion vectors, its depth writes the upscaler's depth, the water both,
                          //!< 4 a colour target (D3D12_BACKBUFFER_FORMAT, or nColorFormat PIPELINE_COLOR_RGBA8) and the highlight
                          //!< layer (D3D12_HIGHLIGHT_FORMAT, HdrOutput.cpp) without depth: the HDR and post programs' passes that write both
  uint8 bWireframe;       //!< fill mode wireframe (SetPolygonMode R_WIREFRAME_MODE)
  uint8 nColorFormat;     //!< the format of colour target 0: PIPELINE_COLOR_* (DrawGeometry sets it for the main view's target)
  uint8 bCascades;        //!< the general program's pixel shader with the sun's cascades (XR_CASCADES; DrawGeometry sets it
                          //!< for the draws with DF2_SUN_CASCADES or DF2_CASCADE_CASTER); 2 with the sun through the leaves
                          //!< (XR_FOLIAGE, DF2_FOLIAGE)
  uint8 bAmbient;         //!< nTarget 0 and 3 with the ambient occlusion's target bound (D3D12_AMBIENT_FORMAT, the fourth;
                          //!< without an upscaler after the null second and third): the general program writes the ambient
                          //!< light's part of its colour there (XR_AMBIENT), blended as the colour; DrawGeometry sets it
  uint8 nBlendOp;         //!< 0 the colour blends add, 1 subtract the source from the target (REV_SUBTRACT)
  uint8 bRayTrace;        //!< the water program with rays (XR_RAYTRACE, Shader Model 6.5; DrawGeometry clears it without a scene
                          //!< for rays in the frame)
  uint8 bCascadeInstances;  //!< nTarget 2: the general program's vertex shader of the full set of the sun's cascades, a slice
                            //!< per instance (XR_CASCADE_INSTANCES: mShadow[i] into slice nShadowTex[i]; Cascades.cpp)
  uint8 bDepthOnly;       //!< no pixel shader and no colour writes (DrawGeometry sets it for a shadow map's caster that
                          //!< discards nothing and for the main view's depth prepass)
  uint8 nPad[1];
};

//! SPipelineKey::nColorFormat.
constexpr uint8 PIPELINE_COLOR_DEFAULT = 0;       //!< nTarget 0, 3 and 4: D3D12_BACKBUFFER_FORMAT, 1: R8G8B8A8_UNORM
constexpr uint8 PIPELINE_COLOR_HDR = 1;           //!< D3D12_HDR_FORMAT (and D3D12_SCRGB_FORMAT, the same)
constexpr uint8 PIPELINE_COLOR_R32F = 2;          //!< R32_FLOAT
constexpr uint8 PIPELINE_COLOR_RGBA8 = 3;         //!< R8G8B8A8_UNORM
constexpr uint8 PIPELINE_COLOR_HDR10 = 4;         //!< D3D12_HDR10_FORMAT
constexpr uint8 PIPELINE_COLOR_BGRA8 = 5;         //!< D3D12_BACKBUFFER_FORMAT (for nTarget 1)

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
  //! The keys asked for so far (each made once).
  size_t GetPSOCount() const { return m_PSOs.size(); }
  //! After each frame: appends the keys made since the last write to the file of keys, once a frame
  //! made none (the game may be ended without a shutdown).
  void SaveKeys();

  //! Size in bytes of a vertex of the format (m_VertexSize of VertexFormats.h, also for the
  //! formats gBufInfoTable leaves out).
  static int GetVertexSize(int nVertexFormat);
  //! Where a vertex of the format keeps its normal, first colour and texture coordinates 0 and 1
  //! (bytes from its start), -1 for those it lacks (the default stream's then); false for a format
  //! not drawn on its own.
  static bool GetVertexAttributes(int nVertexFormat, int &nNormal, int &nColor0, int &nTex0, int &nTex1);

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
  //! Makes the pipeline states of the file's keys, then writes it again without those the device
  //! refused and the repeats.
  void LoadKeys();
  //! nullptr when the key can't be drawn or the device refuses it; bLog false: no error logged (the
  //! file's keys, made on other threads).
  Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePSO(const SPipelineKey &Key, bool bLog);

  ID3D12Device *m_pDevice = nullptr;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRootSignature;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_pDefaultStream;   //!< input slot 1
  D3D12_VERTEX_BUFFER_VIEW m_DefaultStreamView = {};
  //! Every key asked for, with nullptr for the keys that failed (logged once).
  std::unordered_map<SPipelineKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, SKeyHash, SKeyEqual> m_PSOs;
  std::vector<SPipelineKey> m_NewKeys;  //!< made and not yet in the file
  size_t m_nNewKeysBefore = 0;          //!< m_NewKeys.size() at the last SaveKeys
};
