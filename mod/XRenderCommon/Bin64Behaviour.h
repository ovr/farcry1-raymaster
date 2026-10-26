//////////////////////////////////////////////////////////////////////
//
//	File: Bin64Behaviour.h
//	Description: How the 64-bit engine in farcry1_testing1/Bin64 uses its
//	renderer, as recorded by XRenderD3D9Traced.
//
//	Bin64 is the 64-bit build of the Cumulative Patch (see AGENTS.md).
//	Everything here is runtime behaviour observed in the trace log of that
//	build; nothing comes from its code. Facts known for the retail x86
//	engine are in RetailRenderer.h.
//
//	Trace: Training level ("map training" at frame 200), r_Driver
//	"Direct3D8" (the proxy) over farcry1_testing1/Bin64/XRenderD3D9.dll,
//	1024x768 windowed, log farcry1_testing1/XRenderD3D9Traced.log, with the
//	CLeafBuffer methods traced and the 300th frame of the level captured in
//	full (the player at the start position, inside the tunnel).
//
//////////////////////////////////////////////////////////////////////

#pragma once

namespace Bin64Renderer
{

//////////////////////////////////////////////////////////////////////////
// Loading
//
// Same contract as the retail x86 engine (RetailRenderer.h): r_Driver "Direct3D8" makes CrySystem
// load XRenderD3D8.dll, call PackageRenderConstructor(0, NULL, &sp) and then
// SetType(R_DX8_RENDERER). CrySystem.dll exports CryMalloc, CryFree and the rest, so
// CryMemoryManager.h works unchanged.
//
// Init is called as Init(0, 0, 1024, 768, 32, 32, 8, false, hInstance, 0, 0, 0, false). The D3D9
// renderer creates the window itself and reports a 24-bit depth buffer although 32 was requested.

//////////////////////////////////////////////////////////////////////////
// Memory
//
// Blocks from CrySystem's heap are only 4-byte aligned on x64: object addresses in the trace end
// in 0x4 or 0xC more often than in 0x0. Objects shared with the engine (render objects, shaders,
// textures, leaf buffers, render elements) must not rely on 8- or 16-byte alignment: no aligned
// SSE loads or stores on them and no __declspec(align) members.

static const int nHeapAlignment = 4;

//////////////////////////////////////////////////////////////////////////
// Frame
//
// IRenderer calls of one game frame in the level (frame 5000):
//   BeginFrame
//   EF_Query(EFQ_Pointer2FrameID), GetFrameID (15x), EF_GetHeatVision (68x)
//   SetCamera (2x), EF_StartEf, EF_SetWorldColor, SetClearColor
//   EF_ADDDlight (3x), EF_UpdateDLight (4x), EF_IsFakeDLight (3x)
//   EF_GetObject (45x)
//   EF_AddEf (1x, the ScreenProcess render element)
//   EF_EndEf3D(3), EF_ClearLightsList
//   2D: Set2DMode, Draw2dImage, DrawDynVB, SetState, ...
//   Update
//
// Most of the scene geometry does not go through IRenderer::EF_AddEf. The 3D engine takes render
// objects from EF_GetObject and hands them to CLeafBuffer methods, which run inside the renderer
// DLL and add their render elements internally (see Geometry). A replacement renderer has to
// implement CLeafBuffer in full, and tracing the geometry needs a hook on the CLeafBuffer vtable.
//
// GetFrameID starts at 65535 in the first frame and counts up by one per frame.

//////////////////////////////////////////////////////////////////////////
// Geometry
//
// All leaf buffers, including the ones the renderer creates for itself, share one CLeafBuffer
// vtable, and the CLeafBuffer methods call each other through it (CreateBuffer calls
// UpdateSysIndices, CheckUpdate calls CreateVidVertices and UpdateVidIndices, ...). CheckUpdate
// returned false in every traced call. m_sSource tells what a leaf buffer is for.
//
// Static objects ("StatObj", the geometry of IStatObj)
//   Created with CreateLeafBuffer(false, "StatObj", NULL) followed by
//   CLeafBuffer::CreateBuffer(pMesh, true, true, false). The result is an R_PRIMV_MULTI_GROUPS
//   buffer with a system copy of the vertices (m_pSecVertBuffer, m_SecVertCount) and of the
//   indices (m_SecIndices, 3 per face), and one m_pMats entry per mesh material with its vertex and
//   index range and its own render element (pRE); materials without faces get empty ranges and no
//   render element. Vertices are split by texture coordinates (a mesh with 495 positions and 540
//   texture coordinates gives 521 vertices). The vertex format depends on the object:
//   P3F_COL4UB_TEX2F (vegetation, colours lit by UpdateCustomLighting) or P3F_N_TEX2F.
//   There is no video buffer until the first CheckUpdate.
//
//   Drawn once per visible instance and frame:
//     CCObject *pObj = EF_GetObject(true, -1);   // filled by the 3D engine before the call below
//     pLB->Render(rParams, pObj, ShaderTemplates, 0, false, pMaterial, false);
//   When Render is called, the object already holds the instance matrix (= *rParams.pMatrix),
//   m_ObjFlags (= rParams.dwFObjFlags, 0x70000800), m_SortId (= rParams.fCustomSortOffset, -1e6),
//   the ambient colour, m_DynLMMask (= rParams.nDLightMask) and, for lightmapped objects, the
//   lightmap texture ID in m_nLMId and m_pLMTCBufferO (= rParams.pLMTCBuffer). ShaderTemplates has
//   one entry per material (-1: no template override); pMaterial is the instance's material or
//   NULL. Render makes no IRenderer calls: its render elements go straight to the renderer's own
//   lists.
//
//   In EF_EndEf3D the renderer calls CheckUpdate(nVertexFormat, nFlags, false) on every leaf buffer
//   it draws, with the vertex format the shader needs (P3F_N_TEX2F) and VSM_TANGENTS (0x2) in
//   nFlags when the shader also needs the tangent stream. The first call creates the video buffer
//   (CreateVidVertices, UpdateVidIndices).
//
// Characters (CryAnimation)
//   The model goes into CreateLeafBuffer(true, "ModelState", NULL) with
//   CreateBuffer(VertexBufferSource *); that buffer keeps no system vertex buffer (m_pSecVertBuffer
//   NULL, m_SecVertCount set). Instances get copies ("CryModelArray": CopyTo, CreateTangBuffer).
//   Every frame the character's per-material CREOcLeaf render elements are submitted with
//   IRenderer::EF_AddEf, all with one CCObject (m_pCharInstance set); in the tunnel they are
//   submitted a second time with a fog volume ID and sort group 27. These render elements point to
//   a leaf buffer with the source name "Unknown"; during EF_EndEf3D the renderer calls CheckUpdate
//   on it with nFlags 0x10, after which the engine uploads the skinned vertices (P3F_N_TEX2F) with
//   IRenderer::UpdateBuffer.
//
// Terrain ("TerrainSector")
//   One leaf buffer per 64 m x 64 m sector, created with
//   CreateLeafBufferInitialized(pVerts, 1089, VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, pInds, nInds,
//   R_PRIMV_MULTI_STRIPS, "TerrainSector", eBT_Static, 80, nTexId, NULL, NULL, false, false):
//   a 33 x 33 vertex grid with 2 m spacing, one triangle strip per row. SetChunk is called once per
//   strip (all vertices, the strip's index range, the same shader); only m_pMats[0] gets the shader
//   and a render element, the other entries only describe strips. SetShader(pShader, nTexId) sets
//   the sector texture (128x128 eTF_DXT1, created with DownLoadToVideoMemory just before). The
//   sector is drawn with AddRenderElements(pObj, 0, -1, nFogID, 0x30000000, NULL), pObj with an
//   identity matrix.
//
// Water
//   "WaterVolume" (P3F_N_COL4UB_TEX2F triangles, shader "terrainlake") and the rings around the
//   camera, "OutdoorWaterCircle" (P3F_COL4UB strips, new vertices every frame with
//   UpdateSysVertices) and "OceanBottomCircle" (P3F strips, scaled by the CCObject matrix), are
//   drawn with AddRenderElements; the water surface uses sort group 14.

//////////////////////////////////////////////////////////////////////////
// Camera
//
// SetCamera: CCamera::GetVCMatrixD3D9() equals the matrix returned by GetModelViewMatrix. The
// projection returned by GetProjectionMatrix for ProjRatio 0.75, ZMin 0.25, ZMax 700:
//   [1.013 0 0 0] [0 1.35 0 0] [0 0 -1.000 -1] [0 0 -0.250 0]
// i.e. a right-handed D3D projection (m23 = -1) with m00 = m11 * ProjRatio.

//////////////////////////////////////////////////////////////////////////
// Lights
//
// The sun is added every frame with EF_ADDDlight: flags DLF_DIRECTIONAL | DLF_CASTSHADOW_MAPS |
// DLF_LIGHTSOURCE | DLF_SUN | DLF_COPY | DLF_LM | DLF_THIS_AREA_ONLY (0x6d0012), m_Origin far
// away (447, 8530, 10200 in Training), m_fRadius 1e8, debug name "Sun". EF_ADDDlight assigns
// m_Id (0 for the first light of the frame).

//////////////////////////////////////////////////////////////////////////
// Textures
//
// Texture IDs start at 4096. A texture that is not found ('black.tga', 'Textures/fl') comes back
// as a placeholder ITexPic with ID 4096, IsTextureLoaded() == false and flags2 0x10000000.
// Loading an already loaded texture again returns the same ITexPic with FT2_WASFOUND set in its
// flags2.

} // namespace Bin64Renderer
