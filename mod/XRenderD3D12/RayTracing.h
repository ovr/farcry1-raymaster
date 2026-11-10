// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: RayTracing.h
//	Description: The scene for rays (DXR 1.1, inline RayQuery in the
//	shaders): the main view's opaque draws as acceleration structures.
//
//	- The device: DXR tier 1.1 and Shader Model 6.5 (Init), not with
//	  XRENDERD3D12_NORT=1. Without them there are no rays, the cvars that
//	  want them change nothing and d3d12_RTReflections is unregistered
//	  (the menu greys out its list).
//	- Capture: before the main view's opaque list is drawn (CScene::EndEf3D,
//	  CD3D12Renderer::BuildRayScene, BeginCapture .. EndCapture), its items
//	  are set up as the list will draw them, without their lights, and
//	  DrawGeometry hands their draws to CaptureDraw in place of drawing
//	  them. It keeps those of the general program that write the depth, do
//	  not blend and are triangle lists in world or object space. A draw
//	  whose mWorld is not the one of its mWorldViewProj is left out.
//	  A draw with a depth bias (a decal on a surface) is left out, and of
//	  draws with the same geometry and transform (passes over one another)
//	  the first is kept.
//	  The water's planar reflection (UpdateWaterReflection) sees what is
//	  out of the main view: its static draws (BeginReflectionCapture ..
//	  EndReflectionCapture) are kept until it is drawn again and join every
//	  Build, those whose buffers or textures are gone or changed and those
//	  the main view has too (the same geometry and transform) left out.
//	  Out of the main view (d3d12_RTOffscreen): the parts of the sun's
//	  cascades (the scene of the camera towards the sun at the view's
//	  position, whose detail the engine picks as for the view, and the live
//	  casters; CD3D12Renderer::NoteRayOffscreenItems) are walked after the
//	  main view's in the same way, those it has too left out.
//	- One scene a frame: the first main view's (another top level view in
//	  the same frame draws with it). Without a cvar that wants rays, its
//	  memory goes (Release).
//	- BLAS: one per draw's geometry, the vertex positions (at offset 0, three
//	  floats) and the indices as DrawGeometry has them, untransformed.
//	  Geometry in static buffers (CD3D12Upload::FindBuffer) keeps its BLAS
//	  from frame to frame, by the buffers' ids, versions and ranges; those
//	  not used for a while, or the least recently used beyond d3d12_RTMemory,
//	  go. They live in pools of 64 MB. Geometry in per frame memory gets a
//	  BLAS of the frame (built fast) in the frame slot's memory. The builds
//	  of a frame are limited (MAX_*_TRIANGLES); what is over waits for the
//	  next frames.
//	- TLAS: built before the opaque list (Build) in a frame that traces rays
//	  (a water surface in the main view's lists, or d3d12_RTDebug), one
//	  instance per draw whose kinds d3d12_RTMask has all of, the instance's
//	  transform its mWorld, its mask and InstanceContributionToHitGroupIndex
//	  its kind (Shaders/RayConstants.h; with XR_RAY_LIVE for a live caster
//	  of the sun's), culling off. In another frame only
//	  the missing static BLAS are built (for when the water shows), not the
//	  frame's nor the TLAS: away from the water 0.2 ms of the GPU and 0.1 ms
//	  of the CPU less (the time demos of Volcano 340 -> 361 FPS, Research
//	  446 -> 485).
//	- Queues: with d3d12_RTAsync the builds run on the compute queue once
//	  the uploads so far are done, beside the frame's work on the GPU; the
//	  frame's list waits for them at the first draw or pass that traces rays
//	  (CD3D12Renderer::UseRayScene), else at its end.
//	- Instances: an SRayInstance per instance of the TLAS (its InstanceID),
//	  in per frame memory: the raw views of its vertices, indices and second
//	  stream (CD3D12Upload::GetRawSRV) and the part of its draw's constants
//	  the hits are shaded with (Shaders/RayShade.hlsli), and the path
//	  tracer's material (SetCaptureMaterial, DrawLeafItem; Shaders/
//	  PathShade.hlsli). A draw whose buffers have no view is shaded grey.
//	- Binding: root SRVs ROOT_RAY_SCENE and ROOT_RAY_INSTANCES
//	  (D3D12Pipeline.h), for the programs with rays, from GetTLAS and
//	  GetInstances in the same frame.
//	- Alpha tests' shares (d3d12_RTAlphaCoverage): for each static alpha
//	  tested mesh and material (SCoverageKey) a byte a triangle, the share
//	  of it its alpha test passes on, made once by RayCoverage.hlsl into one
//	  buffer (SRayInstance::nPath.w, GetCoverageView). The rays whose light
//	  is averaged take a leaf by it at random in place of the test.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <wrl/client.h>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include "D3D12Device.h"
#include "Shaders/RayConstants.h"

struct SPipelineKey;
struct SDrawConstants;

class CD3D12RayScene
{
public:
  CD3D12RayScene();
  ~CD3D12RayScene();
  CD3D12RayScene(const CD3D12RayScene &) = delete;
  CD3D12RayScene &operator=(const CD3D12RayScene &) = delete;

  //! False when the device has no rays (logged why, once); the scene then stays empty.
  bool Init(ID3D12Device *pDevice);
  //! Releases everything at once (the GPU is idle).
  void Shutdown();
  //! Releases the memory of the scene once the GPU is done with it (nothing wants rays); the device stays.
  void Release();
  bool IsSupported() const { return m_pDevice5 != nullptr; }
  //! The renderer's m_nFrames of the last Build (-1: none).
  int GetBuiltFrame() const { return m_nBuiltFrame; }
  //! Bytes of the static geometry's acceleration structures (d3d12_RTMemory).
  uint64 GetStaticBytes() const { return m_nStaticBytes; }

  //! Where the draws captured from here on come from (CaptureDraw).
  enum ESource : uint8
  {
    SOURCE_VIEW,          //!< the main view's opaque list
    SOURCE_OFFSCREEN,     //!< out of the main view: the parts of the sun's cascades it does not have
    SOURCE_REFLECTION,    //!< the water's planar reflection (BeginReflectionCapture)
  };

  //! The main view's opaque list starts: its draws until EndCapture make this frame's scene.
  //! ViewProj: its camera's world to clip (the draws' mWorld times it is their mWorldViewProj).
  void BeginCapture(const Matrix44 &ViewProj, const Vec3 &vCamera);
  //! Where the draws captured from here on come from.
  void SetCaptureSource(ESource eSource) { m_eCaptureSource = eSource; }
  //! The main view's draws captured from here on are of a live caster (XR_RAY_LIVE in their masks).
  void SetCaptureLive(bool bLive) { m_bCaptureLive = bLive; }
  //! The path tracer's material of the draws captured until ClearCaptureMaterial (SRayInstance's vAlbedo,
  //! vSpecular, nPath; XR_PT_MAT_TERRAIN also XR_RAY_TERRAIN in the masks; vAmbient the game's ambient light, rgb,
  //! PathPackAmbient). Without one CaptureDraw takes it from the draw's flags (DefaultMaterial). bSky: the draws are
  //! pieces of the sky (XR_RAY_SKY: a texture of the sky boxes, CTexture::m_bSky).
  void SetCaptureMaterial(uint32 nMaterial, const float vAlbedo[4], const float vSpecular[4], const float vAmbient[3], bool bSky = false);
  //! The game's ambient light on a draw (rgb, the game's colour: what its ambient colour adds to the light 1 its
  //! albedo has) as SRayInstance::nPath.y keeps it: / 2, 8 bits a channel from the lowest.
  static uint32 PathPackAmbient(const float vAmbient[3]);
  //! That light of a draw from its constants as the raster lights it: the ambient colour (under a light map the
  //! material's colour in it), times 2 / its scale where DF_LIGHTING lights it (the albedo is scale / 2 of its colour).
  static void PathAmbient(const SDrawConstants &dc, float vAmbient[3]);
  void ClearCaptureMaterial() { m_bCaptureMaterial = false; }
  void EndCapture() { m_bCapturing = false; }
  bool IsCapturing() const { return m_bCapturing; }
  //! The water's planar reflection is drawn: its static draws until EndReflectionCapture replace those
  //! kept of the last one. ViewProj: its camera's.
  void BeginReflectionCapture(const Matrix44 &ViewProj);
  void EndReflectionCapture() { m_bCapturingReflection = false; }
  bool IsCapturingReflection() const { return m_bCapturingReflection; }
  //! Forgets the reflection's draws (a new level: their textures may be gone).
  void ClearReflection() { m_ReflectionDraws.clear(); }
  //! A draw of DrawGeometry while capturing; it picks those for the scene.
  void CaptureDraw(const SPipelineKey &Key, const SDrawConstants &dc, D3D_PRIMITIVE_TOPOLOGY eTopology, const D3D12_VERTEX_BUFFER_VIEW &VB,
    const D3D12_INDEX_BUFFER_VIEW *pIB, uint32 nCount, uint32 nFirst, int nBaseVertex, const D3D12_VERTEX_BUFFER_VIEW *pTexCoord1);
  //! Records the builds of the missing BLAS and of the TLAS of the captured draws into the frame's
  //! command list; without bTop (nothing in the frame traces rays) only those of the missing static
  //! BLAS. pDump: a line about them (d3d12_DumpFrame).
  void Build(FILE *pDump, bool bTop);
  //! The TLAS built in this frame, 0 when there is none.
  D3D12_GPU_VIRTUAL_ADDRESS GetTLAS() const;
  //! Its instances (SRayInstance, by InstanceID), with GetTLAS.
  D3D12_GPU_VIRTUAL_ADDRESS GetInstances() const { return m_nInstances; }
  //! d3d12_RTAlphaCoverage: the raw view of the alpha tests' shares (SFrameConstants::nRayCoverage; 0: none).
  uint32 GetCoverageView() const { return m_nCoverageSRV > 0 ? (uint32)m_nCoverageSRV : 0; }
  //! Its meshes, bytes in use and the triangles the last Build made (the frame log, r_Stats).
  uint32 GetCoverageMeshes() const { return (uint32)m_Coverage.size(); }
  uint32 GetCoverageBytes() const { return m_nCoverageBytes; }
  uint32 GetCoverageMade() const { return m_nCoverageMade; }
  uint32 GetFrameBlasBuilt() const { return m_nFrameBlasBuilt; }
  uint32 GetStaticBlasBuilt() const { return m_nStaticBlasBuilt; }
  //! Of them those of the live casters (XR_RAY_LIVE), with GetTLAS.
  uint32 GetLiveInstances() const { return m_nLiveInstances; }
  //! The last Build's TLAS: its instances (0: none built) and their triangles (r_DisplayInfo).
  uint32 GetSceneInstances() const { return m_nSceneInstances; }
  uint32 GetSceneTriangles() const { return m_nSceneTriangles; }
  //! The camera of the capture (Build's frame).
  const Matrix44 &GetViewProj() const { return m_ViewProj; }
  const Vec3 &GetCamera() const { return m_vCamera; }

private:
  //! The geometry of a static BLAS: the buffers by id and version, the ranges in them.
  struct SBlasKey
  {
    uint64 nVBId, nIBId;            //!< CD3D12Buffer::m_nId (nIBId 0: not indexed)
    uint32 nVBVersion, nIBVersion;
    uint32 nVBOffset, nIBOffset;    //!< bytes from the buffers' start: the first vertex, the first index
    uint32 nVertices, nIndices, nStride;
    uint32 nFlags;                  //!< the geometry's flags (opaque or not)

    bool operator==(const SBlasKey &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
  };
  struct SBlasKeyHash
  {
    size_t operator()(const SBlasKey &Key) const;
  };
  //! A part of a pool.
  struct SPoolRange
  {
    int nPool = -1;
    uint64 nOffset = 0, nSize = 0;
  };
  struct SPool
  {
    Microsoft::WRL::ComPtr<ID3D12Resource> pBuffer;   //!< RAYTRACING_ACCELERATION_STRUCTURE
    uint64 nSize = 0, nUsed = 0;
    std::map<uint64, uint64> Free;                      //!< offset -> size, merged with their neighbours
  };
  struct SStaticBlas
  {
    SPoolRange Mem;
    uint32 nTriangles = 0;
    int nLastFrame = 0;             //!< m_nFrame of its last use
    bool bBuilt = false;
  };
  //! A draw for the scene.
  struct SDraw
  {
    D3D12_RAYTRACING_GEOMETRY_DESC Geom;
    float Transform[3][4];
    uint8 nKind;                    //!< XR_RAY_*
    SBlasKey Key;                   //!< XR_RAY_STATIC
    D3D12_GPU_VIRTUAL_ADDRESS nBlas;
    SRayInstance Inst;
    D3D12_GPU_VIRTUAL_ADDRESS nTexCoord1;   //!< the second stream's first vertex (0: none)
    uint64 nTexCoord1Id;            //!< its static buffer's CD3D12Buffer::m_nId (0: per frame memory)
    uint32 nTexGeneration[4];       //!< CD3D12Descriptors::GetGeneration of Inst's textures (GetTextureSlots)
    uint64 nHash;                   //!< DrawHash
    ESource eSource;
    bool bLive;                     //!< SetCaptureLive
    bool bTerrain;                  //!< a sector of the terrain (XR_RAY_TERRAIN)
    bool bSky;                      //!< a piece of the sky (XR_RAY_SKY; SetCaptureMaterial)
  };
  //! A buffer of a frame slot that grows (RAYTRACING_ACCELERATION_STRUCTURE or UNORDERED_ACCESS).
  struct SSlotBuffer
  {
    Microsoft::WRL::ComPtr<ID3D12Resource> pBuffer;
    uint64 nSize = 0;
  };
  struct SFrameSlot
  {
    SSlotBuffer Scratch, Blas, Tlas;
  };
  //! What an alpha tested instance's shares depend on: its geometry, its texture 0 and how its alpha is read.
  struct SCoverageKey
  {
    SBlasKey Blas;
    uint32 nFlags, nTex0, nTexGeneration, nSamplers;
    uint32 nAlphaRef, nColorAlpha, nLayout, nPad;
    float TexMat[8];

    bool operator==(const SCoverageKey &o) const { return memcmp(this, &o, sizeof(*this)) == 0; }
  };
  struct SCoverageKeyHash
  {
    size_t operator()(const SCoverageKey &Key) const;
  };
  struct SCoverage
  {
    uint32 nOffset = 0, nBytes = 0;   //!< in m_pCoverage
    int nLastFrame = 0;
    bool bMade = false;               //!< its kernel recorded
  };
  //! A job of RayCoverage.hlsl (SRayCoverageConstants::nJobs).
  struct SCoverageJob
  {
    uint32 nInstance, nOffset, nTriangles;
  };

  //! The texture slots of an instance (SRayInstance: nTex0, nTex1, nTex4, nTex2).
  static void GetTextureSlots(const SRayInstance &Inst, uint32 Slots[4]);
  //! The draw's buffers and textures are still those it was captured with.
  static bool IsAlive(const SDraw &d);
  //! The draw's geometry and transform (the same in the main view and in the reflection, in all passes
  //! over it).
  static uint64 DrawHash(const SDraw &d);
  //! Adds a DrawHash to m_Seen; false when it is there already.
  bool InsertSeen(uint64 nHash);
  //! The instance's data of a draw (its geometry Tri; nTexCoord1 the second stream's first vertex, 0 for
  //! none).
  static void FillInstance(SRayInstance &Inst, const SPipelineKey &Key, const SDrawConstants &dc, const D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC &Tri,
    D3D12_GPU_VIRTUAL_ADDRESS nTexCoord1);
  //! The path tracer's material of a draw from its flags: the vegetation (DF_VCOLOR_X2), lit (DF_LIGHTING, a light map)
  //! with its colour as the albedo, else unlit.
  static void DefaultMaterial(SRayInstance &Inst, const SDrawConstants &dc);
  //! A buffer of nSize bytes with unordered access in the default heap; NULL when refused (logged).
  Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(uint64 nSize, D3D12_RESOURCE_STATES State, const wchar_t *szName);
  //! At least nSize bytes in Buf (a new buffer of the next power of two, the old one released when the GPU
  //! is done with it).
  bool Reserve(SSlotBuffer &Buf, uint64 nSize, D3D12_RESOURCE_STATES State, const wchar_t *szName);
  bool AllocPool(uint64 nSize, SPoolRange &Out);
  //! Back to the pool once the GPU finished the current frame.
  void FreePool(const SPoolRange &Range);
  void CollectPools();
  //! The static BLAS not used in this frame, least recently used first, until the pools hold at most
  //! nBudget bytes; and those not used for a while.
  void Evict(uint64 nBudget);
  //! The shares' buffer, its raw view and its UAV table; false (logged once) leaves the exact tests.
  bool InitCoverage();
  void ReleaseCoverage();
  //! The key of a draw's shares; false: it has none (not a static alpha tested draw, a texture read by the world or a cube).
  static bool CoverageKey(const SDraw &d, SCoverageKey &Key);
  //! The byte of the draw's triangle 0 in the shares (XR_RAY_NONE: none this frame), its job queued in Jobs where not
  //! made (nInstance its InstanceID; at most MAX_COVERAGE_TRIANGLES a frame).
  uint32 UseCoverage(const SDraw &d, uint32 nInstance, std::vector<SCoverageJob> &Jobs, uint32 &nJobTriangles);
  //! The jobs' kernels into pList (the compute queue's: bAsync), the shares readable after.
  void MakeCoverage(ID3D12GraphicsCommandList *pList, bool bAsync, const std::vector<SCoverageJob> &Jobs, D3D12_GPU_VIRTUAL_ADDRESS nInstances);
  //! The shares not used for a while go; then the least recently used until nBytes fit.
  bool AllocCoverage(uint32 nBytes, uint32 &nOffset);
  void CollectCoverage();

  Microsoft::WRL::ComPtr<ID3D12Device5> m_pDevice5;
  bool m_bCapturing = false;
  bool m_bCapturingReflection = false;
  Matrix44 m_ViewProj;
  Matrix44 m_ReflectionViewProj;
  std::vector<SDraw> m_ReflectionDraws;   //!< the static draws of the last water reflection
  ESource m_eCaptureSource = SOURCE_VIEW;
  bool m_bCaptureLive = false;
  bool m_bCaptureMaterial = false;  //!< SetCaptureMaterial
  bool m_bCaptureSky = false;       //!< SetCaptureMaterial's bSky
  uint32 m_nCaptureMaterial = XR_PT_MAT_LIT;
  float m_vCaptureAlbedo[4] = {}, m_vCaptureSpecular[4] = {};
  uint32 m_nCaptureAmbient = 0;     //!< PathPackAmbient
  Vec3 m_vCamera = Vec3(0, 0, 0);
  int m_nFrame = 0;                 //!< counts BeginCapture
  int m_nBuiltFrame = -1;           //!< the renderer's m_nFrames of the last Build (GetTLAS)
  int m_nBuiltSlot = 0;
  D3D12_GPU_VIRTUAL_ADDRESS m_nInstances = 0;   //!< the last Build's SRayInstance array
  uint32 m_nLiveInstances = 0;                  //!< of them the live casters'
  uint32 m_nSceneInstances = 0;                 //!< the last Build's instances in its TLAS (0: no TLAS)
  uint32 m_nSceneTriangles = 0;                 //!< and their triangles
  std::vector<SDraw> m_Draws;
  //! Build's DrawHash of the draws taken: open addressing, at most half full, 0 empty (a hash of 0
  //! counts as 1).
  std::vector<uint64> m_Seen;
  int m_nSkippedTransform = 0;      //!< draws left out by their transforms in this capture

  std::unordered_map<SBlasKey, SStaticBlas, SBlasKeyHash> m_Static;
  uint64 m_nStaticBytes = 0;        //!< the pool memory of m_Static
  std::vector<std::unique_ptr<SPool>> m_Pools;   //!< the same index for good (an empty pool's buffer goes)
  std::vector<std::pair<SPoolRange, uint64>> m_PendingFree;   //!< with the fence after which they are free
  // d3d12_RTAlphaCoverage
  std::unordered_map<SCoverageKey, SCoverage, SCoverageKeyHash> m_Coverage;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_pCoverage;  //!< the shares, a byte a triangle (COVERAGE_BYTES)
  int m_nCoverageSRV = -1;                            //!< its raw view in the texture table
  int m_nCoverageTable = -1;                          //!< its UAV table (u0) for RayCoverage.hlsl
  bool m_bCoverageFailed = false;
  std::map<uint32, uint32> m_CoverageFree;            //!< offset -> bytes, merged with their neighbours
  std::vector<std::pair<std::pair<uint32, uint32>, uint64>> m_CoveragePending;   //!< freed: offset, bytes, the fence
  uint32 m_nCoverageBytes = 0;                        //!< in use
  uint32 m_nCoverageMade = 0;                         //!< the last Build's triangles made (the frame dump)
  uint32 m_nFrameBlasBuilt = 0, m_nStaticBlasBuilt = 0;   //!< the last Build's BLAS of the frame's geometry and new static ones
  SFrameSlot m_Slots[D3D12_FRAMES];
};
