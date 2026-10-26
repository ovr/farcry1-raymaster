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
//	1024x768 windowed, log farcry1_testing1/XRenderD3D9Traced.log.
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
// The scene geometry does not go through IRenderer::EF_AddEf. The 3D engine takes render objects
// from EF_GetObject and hands them to CLeafBuffer methods, which run inside the renderer DLL and
// add their render elements internally. A replacement renderer has to implement CLeafBuffer in
// full, and tracing the geometry needs a hook on the CLeafBuffer vtable.
//
// GetFrameID starts at 65535 in the first frame and counts up by one per frame.

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
