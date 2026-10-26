//////////////////////////////////////////////////////////////////////
//
//	File: RetailRenderer.h
//	Description: The renderer interface contract of the retail Far Cry 1.4
//	engine (x86) beyond what the SDK headers say: what a compatible
//	renderer DLL has to provide and how the engine calls it.
//
//////////////////////////////////////////////////////////////////////

#pragma once

namespace RetailRenderer
{

//////////////////////////////////////////////////////////////////////////
// Loading
//
// The r_Driver console variable selects the renderer type, and the type selects a fixed DLL name:
//   "OpenGL"    -> R_GL_RENDERER   (0) -> XRenderOGL.dll
//   "Direct3D8" -> R_DX8_RENDERER  (1) -> XRenderD3D8.dll   (not shipped with Far Cry 1.4)
//   "Direct3D9" -> R_DX9_RENDERER  (2) -> XRenderD3D9.dll
//   "NULL"      -> R_NULL_RENDERER (3) -> XRenderNULL.dll
// CrySystem then calls the export PackageRenderConstructor (cdecl) as (0, NULL, &sp) and, if it
// returned non-NULL, IRenderer::SetType(type).
//
// SCryRenderInterface as filled by CrySystem: ipLog, ipConsole, ipTimer, ipSystem, ipTest_int
// and pIPhysicalWorld are set; igcpMalloc is NOT initialized and must not be used.

typedef IRenderer *(*PFN_PackageRenderConstructor)(int argc, char *argv[], SCryRenderInterface *sp);

static const char szPackageRenderConstructor[] = "PackageRenderConstructor";

struct SDriver
{
  const char *szDriver;   //!< r_Driver value
  int nType;              //!< passed to IRenderer::SetType
  const char *szDll;
};

static const SDriver Drivers[] =
{
  { "OpenGL",    R_GL_RENDERER,   "XRenderOGL.dll" },
  { "Direct3D8", R_DX8_RENDERER,  "XRenderD3D8.dll" },
  { "Direct3D9", R_DX9_RENDERER,  "XRenderD3D9.dll" },
  { "NULL",      R_NULL_RENDERER, "XRenderNULL.dll" },
};

//////////////////////////////////////////////////////////////////////////
// Initialization
//
// IRenderer::Init is called as
//   Init(0, 0, r_Width, r_Height, r_ColorBits, r_DepthBits, r_StencilBits, r_Fullscreen != 0,
//        hInstance, hWndAttach, 0, 0, false)
// hWndAttach is the window to render into when the engine is hosted (editor), 0 for the game.
// The returned HWND is kept by CrySystem (input, focus); 0 means failure.
//
// FarCry.exe registers the window class "CryENGINE"; the renderer creates the game window with it,
// and the class name is also used to detect a running instance. CrySystem pumps the window
// messages.

static const char szWindowClass[] = "CryENGINE";

//////////////////////////////////////////////////////////////////////////
// IRenderer vtable
//
// 216 virtual functions. MSVC groups overloaded virtual functions at the position of the first
// declaration, in reverse declaration order (e.g. DrawDynVB(pBuf, pInds, ...) comes before
// DrawDynVB(nOffs, Pool, nVerts)). The SDK header produces the retail order.

enum EVTableOffset
{
  VT_Init                 = 0x000,
  VT_BeginFrame           = 0x038,
  VT_Update               = 0x03C,
  VT_EF_LoadShader        = 0x160,
  VT_EF_LoadShaderItem    = 0x164,
  VT_EF_CreateRE          = 0x1AC,
  VT_EF_GetObject         = 0x1B4,
  VT_EF_AddEf             = 0x1B8,
  VT_EF_EndEf3D           = 0x1BC,
  VT_EF_Query             = 0x1F0,
  VT_CreateLeafBuffer     = 0x20C,
  VT_CreateLeafBufferInitialized = 0x210,
  VT_SetType              = 0x31C,
  VT_EF_GetWaterZElevation = 0x35C,   //!< last slot
  VT_NumSlots             = 216,
};

//! CLeafBuffer vtable (32 slots): CreateBuffer(VertexBufferSource*) is slot 7 (+0x1C),
//! CreateBuffer(CIndexedMesh*, ...) slot 8 (+0x20).

//////////////////////////////////////////////////////////////////////////
// EF_Query
//
//   EFQ_LightSource (18)     value: CDLight* of the current light list at index Param, or NULL.
//                            The 3D engine checks m_Flags & DLF_SUN on it.
//   EFQ_RecurseLevel (19)    value: recursion level of the scene being drawn, 1 for the main view.
//                            CryGame (XPlayer.cpp) draws the first person weapon only when
//                            (int)EF_Query(EFQ_RecurseLevel) - 1 == 0.
//   EFQ_Pointer2FrameID (20) int*: the frame counter returned by GetFrameID(); the 3D engine
//                            keeps the pointer.
//   EFQ_NumRenderItems (22)  value: number of render items.
//   EFQ_DeviceLost (23)      int*: CrySystem waits while *ptr != 0.
//   EFQ_D3DDevice (25), EFQ_glReadPixels (26): may be NULL.
// The NULL renderer returns NULL for every other query.

//////////////////////////////////////////////////////////////////////////
// Object sizes (x86). The engine never allocates these; it gets them from the renderer and then
// accesses the members directly, so the renderer must use exactly these layouts.

enum
{
  SIZEOF_CLeafBuffer      = 0xC0,
  SIZEOF_CRendElement     = 0x58,   //!< also the size of the render elements that add no members
};

//! Size of the render element EF_CreateRE creates for a type, 0 if it returns NULL for that type.
//! The classes with an SDK header have exactly this size (see RendererAbiChecks.h); Beam, Prefab,
//! Flare and Ocean have no public class.
inline int RESize(int eType)
{
  switch (eType)
  {
  case eDATA_Dummy:                       return 0x58;    // CREDummy
  case eDATA_Sky:                         return 0x270;   // CRESky
  case eDATA_Beam:                        return 0xB0;    // no SDK class
  case eDATA_Poly:                        return 0x88;    // CREPolyMesh
  case eDATA_TriMeshShadow:               return 0x3E8;   // CRETriMeshShadow
  case eDATA_Prefab:                      return 0x5C;    // no SDK class
  case eDATA_Flare:                       return 0xC0;    // no SDK class
  case eDATA_OcLeaf:                      return 0xB0;    // CREOcLeaf
  case eDATA_TerrainSector:               return 0x58;    // CRECommon
  case eDATA_2DQuad:                      return 0xA8;    // CRE2DQuad
  case eDATA_FarTreeSprites:              return 0x58;
  case eDATA_ShadowMapGen:                return 0x58;
  case eDATA_TerrainDetailTextureLayers:  return 0x58;
  case eDATA_TerrainParticles:            return 0x58;
  case eDATA_Ocean:                       return 0x25D8C; // no SDK class
  case eDATA_Glare:                       return 0x64;    // CREGlare
  case eDATA_OcclusionQuery:              return 0x80;    // CREOcclusionQuery
  case eDATA_ClearStencil:                return 0x58;
  case eDATA_FlashBang:                   return 0x74;    // CREFlashBang
  case eDATA_ScreenProcess:               return 0x5C;    // CREScreenProcess
  case eDATA_HDRProcess:                  return 0x58;
  default:                                return 0;
  }
}

//////////////////////////////////////////////////////////////////////////
// CreateLeafBuffer contract
//
// Returns a new CLeafBuffer for szSource with m_bDynamic and m_Indices.m_bDynamic set to bDynamic
// and m_nVertexFormat VERTEX_FORMAT_P3F. With a CIndexedMesh, the buffer also gets a material list
// (m_pMats, at least one default CMatInfo) and its buffers are built from the mesh
// (CLeafBuffer::CreateBuffer).

//////////////////////////////////////////////////////////////////////////
// Minimal renderer (what the engine accepts from the NULL renderer)
//
//   EF_LoadShader / EF_LoadShaderItem may return one shared default shader (item).
//   FontCreateTexture may return -1, GetHWND 0, EnumDisplayFormats/EnumAAFormats 0,
//   CreateRenderTarget 0; GetVertexProfile and GetPixelProfile return a valid string.

//////////////////////////////////////////////////////////////////////////
// CREScreenProcess::m_pVars points to a CScreenVars, a class that is not in the SDK. Its size is
// not known yet; the renderer must still provide a valid block (CREScreenProcess::GetVars() is
// inline and may be used by the engine).

} // namespace RetailRenderer
