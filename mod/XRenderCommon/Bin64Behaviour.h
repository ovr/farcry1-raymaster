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
// Shutdown (Training, the console command "quit" 100 frames after the load)
//
// The engine draws 62 more frames after "quit". After the last one it calls EF_ClearLightsList,
// EnableSwapBuffers(true), RemoveTexture for textures of the game (the same ID up to 28 times in
// a row), GetFrameID(true), GetHWND, DownLoadToVideoMemory of an 800 x 600 eTF_0888 picture
// without mip maps (it gets the ID of a texture removed just before) and ScaleCoordX /
// ScaleCoordY. Then it frees the level: OnEntityDeleted 577 times with DeleteLeafBuffer in
// between (491 calls: "StatObj", the characters' "Unknown", "CryModelArray" and "ModelState", and
// 35 times NULL), RemoveTexture(0xFFFFFFFF), RemoveTexture of the 800 x 600 picture, and last
// IRenderer::Release(). ShutDown and FreeResources are not called, and the terrain buffers
// ("TerrainSector", "LowResTerrain") are not deleted: Release has to free what is left, and
// RemoveTexture and DeleteLeafBuffer have to accept the same ID several times, -1 and NULL.
// While the D3D9 renderer's Release runs, GetType is called once on the IRenderer the engine
// holds (it returns R_DX9_RENDERER), so the object has to answer until Release returns.

//////////////////////////////////////////////////////////////////////////
// Level change (Training, then the console command "map River")
//
// Before PreLoad of the new level the engine frees the old one, in this order (the load itself:
// "Level load" below):
// - EF_LoadTexture of the new level's loading screen picture; OnEntityDeleted for the game's
//   entities with DeleteLeafBuffer of their buffers; one frame (BeginFrame, Draw2dImage of the
//   loading screen, FlushTextMessages, Update); the game's particle textures again (EF_LoadTexture,
//   SetTexture, SetTexClampMode, LoadAnimatedTexture; these stay loaded).
// - Then the level: every leaf buffer the engine created for it, the terrain sectors (each
//   followed by RemoveTexture of its texture), "LMapTexCoords", "MergedLB", "WaterVolume",
//   "LowResTerrain", "OutdoorWaterCircle" and "OceanBottomCircle" included (4275 DeleteLeafBuffer
//   calls, 1133 of them with NULL), RemoveTexture (2007 calls, 13 of them with 0, one ID up to 40
//   times), RemoveAnimatedTexture (16),
//   CRendElement::Release of the CRETerrainDetailTextureLayers element and of both
//   CREOcclusionQuery elements, and the Release of the shaders and their resources (more than
//   4000 calls of IShader::Release and of SRenderShaderResources::Release, from Cry3DEngine and
//   CryAnimation) and of the CREOcLeaf elements (2291). OnEntityDeleted comes 102768 times in all.
// While loading the new level the engine creates a new CRETerrainDetailTextureLayers element; the
// other elements of EF_CreateRE (Sky, Dummy, 2DQuad, ScreenProcess, ShadowMapGen, FarTreeSprites,
// TerrainParticles, ClearStencil) are made once at start-up and kept. A renderer must survive any
// number of these cycles: every object the engine got from it can be released at a level change.

//////////////////////////////////////////////////////////////////////////
// Level load (Training, the console command "map training" during the start-up videos)
//
// The engine removes the video texture (RemoveTexture) and makes an 800 x 600 texture of zeros in
// its place (see "2D and text", "Start-up videos"), then calls PreLoad. Every frame of the load
// (942 here) draws only the loading screen ("2D and text"). After PostLoad it calls
// EnableSwapBuffers(false) and renders 162 frames of the level (in two runs) that are not meant
// to be seen: complete 3D frames with Clear, EF_EndEf3D and their draws, but the D3D9 renderer
// calls no Present in them, so the last loading screen stays on the screen. In these frames the
// camera (SetCamera once per frame, fov 1.7) stands at a series of points of the level (47 on
// Archive: 282 frames) and turns in six directions, one frame each, angles (90, -90, 0),
// (90, 90, 0), (180, 180, 0), (0, 180, 0), (90, 180, 0), (90, 0, 0); what is seen from there is
// rendered once (the leaf buffers get their video buffers) before the game starts. Then
// EnableSwapBuffers(true), RemoveTexture for a few textures (one ID 28 times), 5 frames of the
// loading screen with the full progress bar, and the game. Update has to skip Present while
// swapping is off, and the frame count (GetFrameID) goes on in these frames.

//////////////////////////////////////////////////////////////////////////
// Console variables (Training)
//
// The D3D9 renderer registers its console variables (254: 223 "r_...", 28 "d3d9_..." and 3
// "CV_ind_...") inside PackageRenderConstructor, before SetType and Init: integers and floats with
// IConsole::Register, bound to variables of its own, and 9 strings with CreateVariable. CrySystem
// reads system.cfg before it loads the renderer, and a variable set there gets that value at
// registration: Register returns it and stores it in the bound variable instead of the default
// (r_WaterReflections: default 1, 0 from farcry1_testing1's system.cfg).
// Other modules look some of them up by name (IConsole::GetCVar, which ignores case; seen from the
// renderer's construction to the first frames of a level): Cry3DEngine reads r_Quality_BumpMapping
// right after Init and while a level loads (911 lookups on Training), r_WaterReflections and
// r_WaterRefractions while a level loads, and r_DisableSfx and r_ResetScreenFx in the first frame
// of a level. No other module looked up a renderer variable. A replacement renderer has to
// register these five; what the engine does without them was not tried.
// The renderer itself looks up CrySystem's r_Width, r_Height, r_Fullscreen and r_ColorBits and
// Cry3DEngine's e_fog, e_shadow_maps, e_stencil_shadows, e_shadow_maps_debug and
// e_hw_occlusion_culling_objects; "GPU", "hud_fadeamount" and "r_ScreenColorTransfer" are
// registered by no one (GetCVar returns NULL). It calls Register for r_LogTextureUsage again for
// every texture it loads (2931 times on Training); the console keeps the one variable.

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
// returned false in every traced call except for the detail grass (below). m_sSource tells what a
// leaf buffer is for.
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
//   Vegetation instances also get m_fBending (SRendParams::fBending, 0 to 1.4 in Training), and
//   after that one call per frame of CCObject::AddWaves (the first virtual function of CCObject)
//   with the SDK's parameter, AddWaves(SWaveForm2 **wf). wf points to two pointers of the engine's
//   that the call has to set (they hold stale values when it starts):
//   - the renderer adds two waves to its table (CCObject::m_Waves, 20-byte SWaveForm2 entries),
//     stores their indices in m_NumWFX and m_NumWFY (1/2, 3/4, ... from the start of every frame;
//     index 0 is never handed out) and their addresses in wf[0] and wf[1];
//   - right after the call the engine fills both waves through these pointers (if wf is left as
//     it was, the engine writes through a stale pointer and crashes). Seen on Training and River:
//     type eWF_Sin, level 0, amplitude 0.002, frequency 0.21 to 0.51, phase 29 to 94 for X and 54
//     to 134 for Y (different per instance, constant while the instance stays in view).
//   The other registers of the call are left over from the engine (xmm2 equals m_fBending).
//   With r_Quality_BumpMapping 2 the D3D9 renderer draws vegetation with vertex shaders and gives
//   them (ObjWaveX, ObjWaveY, m_fBending, 1) in constant c7 at every draw of the instance, each
//   wave evaluated as level + amp * sin(2 * pi * (t * freq + phase)) with t = ITimer::GetCurrTime()
//   of the frame (to within 1.3e-5 at amplitude 0.002: 1163 draws on Training with the time
//   standing still, 3239 on River over 178 frames with the time running, five vertex shaders).
//   With r_Quality_BumpMapping 0 vegetation is drawn by the fixed function pipeline from its static
//   buffers and does not bend. On Training the engine made these calls only while vegetation with
//   bending was drawn (3695 calls in the first 150 frames after the load).
//
//   In EF_EndEf3D the renderer calls CheckUpdate(nVertexFormat, nFlags, false) on every leaf buffer
//   it draws, with the vertex format the shader needs (P3F_N_TEX2F) and VSM_TANGENTS (0x2) in
//   nFlags when the shader also needs the tangent stream. The first call creates the video buffer
//   (CreateVidVertices, UpdateVidIndices).
//
//   Lightmapped objects (River, every lightmapped draw of the frame, the same for all their
//   materials): the D3D9 renderer takes the lightmap UVs from the m_pLMTCBufferO buffer as a
//   second texture coordinate set and combines in two fixed function stages:
//     stage 0: lightmap texture (m_nLMId) + texture factor, alpha lightmap alpha * factor alpha;
//     stage 1: diffuse texture * result * 4.
//   The texture factor is the object's ambient colour divided by 4 (ambient (0.380, 0.410, 0.375)
//   gives the bytes 24, 26, 23; its alpha is 255 for most objects, lower for a few). So a texel is
//   diffuse * (4 * lightmap + ambient), clamped after each stage (lightmap + ambient / 4 to 1, the
//   product to 1): the lightmaps hold a quarter of the light. When a dynamic light reaches the
//   object (Training, Carrier: 73 draws, each with a point light set), Direct3D lighting is on and
//   stage 0 adds the vertex lighting instead of the texture factor.
//   The D3D point light of a static object has the attenuation (a0, a1, 0) and the range
//   (256 - a0) / max(a1, 0.01), 0 when a0 > 256 (the distance where the attenuation reaches 1/256).
//   Unlike for characters (below), a0 and a1 are fitted per object and light, so that the Direct3D
//   attenuation 1 / (a0 + a1 x) follows the falloff
//     f(x) = 2 * (1 - 3 v^2 + 2 v^3),  v = min(x / (0.9 * R), 1)      (R = the light's m_fRadius)
//   over the object: 2 at the light, 0 from 0.9 R on. The object counts as a sphere. Its centre C is
//   the instance position plus the centre of the leaf buffer's box (m_vBoxMin, m_vBoxMax, in the
//   object's own space; it can be larger than the chunks that are drawn), added without the
//   instance rotation; its radius r is half the diagonal of that box. With
//   d = max(|light origin - C|, 0.1):
//     x1 = max(d - r, 0.1 R)                   (the nearest point, never nearer than 0.1 R)
//     x2 = min(max(d + r, 0.1 R), 0.99 R)      (the farthest point)
//     g1 = 1 / f(x1),  g2 = 1 / max(f(x2), 0.1)
//     s  = (g2 - g1) / (x2 - x1)               (0 when x2 = x1)
//     a0 = g1 - s * x1;  when a0 < 0.01: a0 = 0.01 and s = (g1 - 0.01) / x1
//     a1 = max(s, 0)
//   The line goes through the falloff at the nearest point, and also at the farthest one unless a0
//   had to be raised. All chunks of the object get the same values. While the whole object is
//   within 0.1 R of the light (d + r <= 0.1 R) the result is (1 / f(0.1 R), 0) = (0.517756, 0), a
//   constant 1.931. When the light is inside the sphere (d - r <= 0.1 R) and the far point gets
//   clamped, the result is (0.01, (1 / f(0.1 R) - 0.01) / (0.1 R)) = (0.01, 5.07756 / R): the
//   attenuation every character gets (below). When even the nearest point is beyond 0.99 R the
//   result is (10, 0). Seen on Bunker (box (-0.060, -0.161, 0.109)-(1.002, 0.156, 2.109), so
//   r = 1.143; the instance is rotated by 180 degrees about z, which puts C outside the rotated box)
//   for radii 3.5 to 100, the light from inside the object to just outside the radius, in several
//   directions: all values within 0.1 %; and on the objects of Steam under the projected light
//   (below). The renderer adds no cut-off of its own: the object is lit as long as the engine sets
//   the light's bit in nDLightMask (a0 up to 186 seen, with the light 7.01 from C for R 7). Other
//   levels give a0 from 0.01 to 464 and a1 from 0 to 2.2 (Carrier, Steam, River).
//   Vegetation (River): texture * vertex colour * 2 * m_Color * the colour of EF_SetWorldColor
//   (m_Color 0.75 and the world colour (0.843, 0.780, 0.659) give the factor bytes 161, 149, 126).
//   For grass the factor's alpha is the square of m_Color's alpha (0.84 gives 179, 0.12 gives 3),
//   which fades it out (alpha blended, no depth writes).
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
//   The D3D9 renderer lights them with fixed function lighting (Steam): the world matrix stays
//   identity and the object's matrix is folded into the view matrix, so the lights are given in the
//   object's space. Global ambient white; material ambient = the object's m_AmbColor, halved for
//   the draws that modulate the texture by 2 (the draws with specular lighting modulate by 1 and
//   keep it); material diffuse 1 or the material's diffuse colour. Light 0 is the sun, a
//   directional light with the CDLight colour as diffuse. A dynamic point light becomes a D3D point
//   light with diffuse = m_Color (m_fDirectFactor not applied), specular = m_SpecColor in the draws
//   with specular lighting, range 50.416 * m_fRadius and attenuation (0.01, 5.07756 / m_fRadius, 0),
//   which reaches 1/256 at the range (radius 1, 2.06 and 4 seen). These are the static objects'
//   values for a light inside the object ("Static objects": the falloff 2 (1 - 3 v^2 + 2 v^3) met
//   at 0.1 m_fRadius), whatever the distance.
//   Cloaked characters (the stealth mutants, Archive): CryAnimation adds the template
//   "MutantStealth" to the shaders of the character's materials, for each LOD of the model, with
//   IShader::AddTemplate(pRes, nId, "MutantStealth") (bSetPreferred false, nMaskGen 0): the first
//   material with nId -1 and the others with the id that call gave back. A call with -1 gets a new
//   id from EFT_USER_FIRST (30) up, a new one every time (30, 31, 32, 33 for two mutants of two
//   LODs each, all on the same shader); a call with an id keeps it. Both return true. Every frame
//   the 3D engine adds the same template, with the id the character uses, to the shader of the
//   object the character carries (its weapon, a static object). While the character is cloaked its
//   render elements go into EF_AddEf with that id as nTempl and m_nTemplId, and the weapon into
//   CLeafBuffer::Render with the id in SRendParams::nShaderTemplate and in the template list; the
//   renderer draws both with that template only, none of the material's own passes. The
//   character's CCObject::m_ShaderParams holds one float, "refraction" (0.049 and 0.01 seen; the
//   weapon has none), read when the template is drawn ("mutantstealth_fp" under "Shaders").
//   First person weapon (the player's AG36 and hands; Archive, Bunker, Regulator, River): a
//   character like the others, its render elements submitted with nSort eS_Nearest (26) and one
//   CCObject with FOB_NEAREST in m_ObjFlags (0x70000804 or 0x70100804), whose matrix puts the model
//   at the camera position. The D3D9 renderer draws them after everything else of the sorted list
//   (the opaque world, the characters, eS_Decal), before the blended elements sorted by distance
//   and the screen effects, with:
//   - the same view as every object (the object's matrix folded into the camera's view);
//   - a projection of its own: near 0.01, far 40, m11 = 1 / tan(fov * ratio / 3) and m00 = m11 *
//     ratio, where the world gets near and far from the camera and m11 = 1 / tan(fov * ratio / 2)
//     (fov and ratio of the CCamera given to SetCamera; the console command "fov" changes both).
//     With fov pi/2 and ratio 0.75 (1024 x 768): [1.81086 0 0 0] [0 2.41448 0 0]
//     [0 0 -1.00025 -1] [0 0 -0.0100025 0] against the world's [1.12245 0 0 0] [0 1.49661 0 0];
//   - the viewport depth range 0..0.1 (the world 0..1) and no clear of the depth buffer between
//     them, so the weapon is in front of all world geometry further than about 0.28 m (camera
//     near 0.25, far 305) and depth tested against itself (LESSEQUAL with depth writes; the
//     additional light passes EQUAL);
//   - the lighting of the other characters.
//   FOB_NEAREST alone selects the projection and the depth range, the sort only the place: with
//   nSort eS_Opaque and the flag the weapon is drawn among the opaque objects with its own
//   projection and 0..0.1; with eS_Nearest and without the flag it keeps its place after the rest
//   of the sorted list but gets the world's projection and 0..1.
//   The scope glass ("s_ag36scope", template RLScopeRefraction, eS_Refractive) is submitted in the
//   same way and not drawn with r_ScopeLens_fx 0 (see "aw50scoperefraction").
//
// Terrain ("TerrainSector")
//   One leaf buffer per 64 m x 64 m sector, created with
//   CreateLeafBufferInitialized(pVerts, 1089, VERTEX_FORMAT_P3F_N_COL4UB_COL4UB, pInds, nInds,
//   R_PRIMV_MULTI_STRIPS, "TerrainSector", eBT_Static, 80, nTexId, NULL, NULL, false, false):
//   a 33 x 33 vertex grid with 2 m spacing, one triangle strip per row. Sectors further away get
//   coarser grids of the same size: 17 x 17 (289 vertices), 9 x 9 (81) or 5 x 5 (25) (Steam: 7,
//   3, 3 and 15 sectors), with indices from the engine that also stitch them to their neighbours
//   (UpdateSysIndices when they change). SetChunk is called once per
//   strip (all vertices, the strip's index range, the same shader); only m_pMats[0] gets the shader
//   and a render element, the other entries only describe strips. SetShader(pShader, nTexId) sets
//   the sector texture (128x128 eTF_DXT1, created with DownLoadToVideoMemory just before). The
//   sector is drawn with AddRenderElements(pObj, 0, -1, nFogID, 0x30000000, NULL), pObj with an
//   identity matrix.
//   The vertices carry no texture coordinates; the D3D9 renderer makes them from the world
//   position: the sector texture at u = (y - y0) / 64, v = (x - x0) / 64 for the sector's corner
//   (x0, y0) (clamped), the detail texture at u = y / 8, v = x / 8 (repeated). Its pixel shader
//   also gets the colour of EF_SetWorldColor and the fog colour of SetFog.
//
// Water
//   "WaterVolume" (P3F_N_COL4UB_TEX2F triangles, shader "terrainlake") and the rings around the
//   camera, "OutdoorWaterCircle" (P3F_COL4UB strips, new vertices every frame with
//   UpdateSysVertices) and "OceanBottomCircle" (P3F strips, scaled by the CCObject matrix), are
//   drawn with AddRenderElements; the water surface uses sort group 14. How the D3D9 renderer
//   draws the ocean: "Water reflections and refractions".
//
// Low detail terrain ("LowResTerrain")
//   CreateLeafBufferInitialized(pVerts, 16641, VERTEX_FORMAT_P3F, NULL, 0, R_PRIMV_TRIANGLES,
//   "LowResTerrain", eBT_Dynamic, 1, nTexId, NULL, NULL, false, true): a 129 x 129 grid with 16 m
//   spacing. Every frame: UpdateSysIndices(NULL, 0), InvalidateVideoBuffer,
//   SetChunk(pShader, 0, 16641, 0, 0, 0, false) with the shader "terrainlowlod" and no indices,
//   SetRECustomData(pData, 1, 1) and AddRenderElements(pObj, 0, -1, 0, 0, NULL). pData holds
//   (1, 1, 1/2048, 0), (0, 0, 0, 0), four floats that are not set, and the camera position
//   (x, y, z, 0).
//   As long as UpdateSysIndices gets no indices the buffer has no material: m_pMats is empty after
//   the creation and a SetChunk without indices adds none, so AddRenderElements adds no render
//   element and nothing is drawn (Training at the start, Steam, River). When the engine has far
//   terrain for it (Training, frame 152 after the load) it calls UpdateSysIndices(pInds, 96) and
//   SetChunk(pShader, 0, 16641, 0, 96, 0, false), which adds the material; the D3D9 renderer then
//   draws an indexed triangle list over the 129 x 129 grid (32 triangles) with vs_1_1 / ps_2_0, the
//   texture nTexId (256 x 256 DXT1, clamped) in stage 0, depth test and writes, fog.
//
// Terrain under a dynamic light ("Dynamic", Steam)
//   When a dynamic point light reaches the terrain (here a light of radius 2 next to the player),
//   the engine creates CreateLeafBufferInitialized(pVerts, 1, VERTEX_FORMAT_P3F_N_COL4UB_COL4UB,
//   NULL, 0, R_PRIMV_TRIANGLES, "Dynamic", eBT_Static, 1, nTexId, NULL, NULL, false, false) with one
//   zeroed vertex and the texture ID of the terrain sector the light is in, and points its
//   m_pVertexContainer to that sector's leaf buffer (CLeafBuffer::SetVertexContainer, inline in
//   LeafBuffer.h). Every frame it then calls UpdateSysIndices with the triangles of the sector
//   near the light (a triangle list into the sector's 33 x 33 grid, 96 indices here),
//   InvalidateVideoBuffer, SetChunk(pShader, 0, 1089, 0, nIndices, 0, false) and SetShader(pShader,
//   nTexId) with the shader "terrainlightpass", SetRECustomData(pData, 0, 1) and AddRE(pObj, NULL,
//   0x30000000, NULL). pData holds (-y0 / 64, -x0 / 64, 1 / 64, 0) for the sector's corner
//   (x0, y0): (-14, -17, 0.015625, 0) for the sector at (1088, 896). pObj has an identity matrix
//   and the light's bit in m_DynLMMask. The D3D9 renderer draws it as one indexed triangle list
//   over the container's video vertex buffer, with the sector texture in the first stage, the
//   texture coordinates from pData as for the sector, added to the picture (ONE, ONE) without
//   depth writes, with the light's origin, radius, 1 / radius and colour as shader constants.
//
// Terrain detail layers ("TerrainDetailTex"; River with e_detail_texture_quality 1, which is 0 in
//   farcry1_testing1's settings)
//   At start-up the engine loads EF_LoadShader("TerrainLayer", eSH_World, EF_SYSTEM) and creates a
//   CRETerrainDetailTextureLayers element; the element was never added and
//   I3DEngine::DrawTerrainDetailTextureLayers never called (also with r_DetailTextures 1,
//   r_DetailNumLayers 2, r_DetailDistance 8). The layers are leaf buffers instead: in the first
//   frames after the load, for the terrain layers around the camera (six on River),
//   DeleteLeafBuffer(NULL), CreateLeafBufferInitialized(pVerts, n, VERTEX_FORMAT_P3F_N_COL4UB,
//   pInds, m, R_PRIMV_TRIANGLES, "TerrainDetailTex", eBT_Static, 1, nTexId, NULL, NULL, false,
//   true) (23 to 2684 vertices), SetShader(the "TerrainLayer" shader, nTexId) and SetChunk(that
//   shader, 0, n, 0, m, 0, false). The vertices are world positions with the terrain
//   normal and the colour (128, 128, 128) with alpha 0 or 255, the layer's weight; there are no
//   texture coordinates. Every frame: AddRenderElements(NULL, 0, -1, 0, 0, pMaterial), with no
//   object and pMaterial the layer's material ("Level.Terrain.Layer0", an IMatInfo of the engine
//   whose shader item came from EF_LoadShaderItem("MaterialContainer", ...)) or NULL.
//   The D3D9 renderer draws a layer without a material with "terrainlayer(0)": a vertex and a
//   pixel shader, stage 0 the texture nTexId (512 x 512 DXT1 or DXT5, wrap, linear with mip
//   maps), blending (DESTCOLOR, SRCCOLOR) = 2 * source * destination, depth test LESSEQUAL without
//   depth writes, culling D3DCULL_CW, fog. A layer with a material is drawn with the material's
//   shader item instead (two passes with its textures: a 128 x 128 DXT1 and a 128 x 128
//   D3DFMT_A8R8G8B8 normal map, the second pass also a 256 x 256 cube map).
//
// Fog volume caps ("VolFogTopCircle", Carrier)
//   Like the water rings: P3F, R_PRIMV_MULTI_STRIPS, eBT_Dynamic, 16 chunks of 26 vertices (one
//   strip each) with the shader "foglayer", drawn with AddRenderElements and the fog volume's ID.
//
// Merged static objects ("MergedLB", Carrier)
//   The engine merges static objects into buffers in world space:
//   CreateLeafBufferInitialized(pVerts, n, VERTEX_FORMAT_P3F_N_COL4UB_TEX2F, pInds, n,
//   R_PRIMV_TRIANGLES, "MergedLB", eBT_Static, nMats, 0, NULL, NULL, false, true) with as many
//   indices as vertices, then UpdateTangBuffer, SetChunk(pShader, 0, n, nFirstIndex, nIndices, i,
//   true) for every material (the shaders of the objects' materials), and an "LMapTexCoords"
//   buffer with one lightmap UV per vertex. None of them was drawn from the start position on
//   Carrier; on River they are drawn like the other lightmapped objects, one draw per material,
//   from a video buffer in the format they were created with (the vertex colours stay unused).
//
// Detail grass ("Grass"; Cooler, the only one of the 20 levels that makes it)
//   CreateLeafBufferInitialized(NULL, 50000, VERTEX_FORMAT_P3F_COL4UB_TEX2F, pInds, 150000,
//   R_PRIMV_TRIANGLES, "Grass", eBT_Dynamic, 1, 0, PrepareBufferCallback, pCustomData, false, true)
//   with all indices 0, then SetRECustomData(pData, 0, 1) and SetChunk(pShader, 0, 50000, 0,
//   150000, 0) with the shader "TerrainDetailObjects". The renderer calls the callback once during
//   the creation, after CreateVidVertices; it returns false then.
//   In every frame with grass near the camera the engine calls InvalidateVideoBuffer(-1) and
//   AddRenderElements(pObj, nLightMask, -1, nFogVolume, 21, NULL) (sort group 21); m_vBoxMin and
//   m_vBoxMax both hold the camera position. In EF_EndEf3D the renderer calls CheckUpdate on the
//   buffer, and CheckUpdate calls PrepareBufferCallback(pLB, false) before the buffer is drawn;
//   the callback and CheckUpdate return true. Inside the callback the engine
//   - when the camera has moved (6 m here), rebuilds the grass around it:
//     UpdateBuffer(pLB->m_pVertexBuffer, NULL, 0, false, 0, 0) locks the video buffer and has to
//     leave writable memory for the whole buffer in m_VS[VSF_GENERAL].m_VData (m_bLocked true);
//     the engine writes the vertices there (copies of a small vegetation object, 36 vertices each,
//     in world space, colour 0xff999999 here); UpdateBuffer(pLB->m_pVertexBuffer, NULL, 0, true,
//     0, 1) unlocks it; then UpdateVidIndices(pInds, n) with the list 0, 1, 2, ..., n - 1,
//     SetChunk(pShader, 0, n, 0, n, 0) and SetShader(pShader, nTexId) with the grass texture;
//   - in every call, SetRECustomData(pData, 0, 1) with pData = (0, 0, 1/36, -(y - 18) / 36,
//     -(x - 18) / 36, 0, ...) for the camera at (x, y): a 36 m square around the camera.
//   The D3D9 renderer draws the chunk as one indexed triangle list (21024 and 22680 vertices seen)
//   with depth test, no depth writes, alpha test (> 0), alpha blending (SRCALPHA, INVSRCALPHA), no
//   culling, fog, and a pixel shader with two textures.
//
// Far vegetation sprites ("FarTreeSprites"; River, Research, Pier)
//   DrawObjSprites (RendererStructures.h) gets the instances to draw as sprites; the D3D9 renderer
//   draws one upright quad (two triangles) per instance, one draw per sprite texture, with
//   texture * vertex colour * 2, alpha test (reference 128), depth writes, fog and no culling.
//   For an instance at position P with scale s, group g and the object O that MakeSprite got:
//   - Size: k = s * f, where the fade factor f is 1, or, when g's bFadeSize is set,
//     min(1, (E - d) / (E / 8)) with d the instance's distance and E = min(the instance's
//     MaxViewDist, 0.8 * fMaxViewDist): the quad shrinks to nothing over the last eighth of E
//     (River: E = 400, the quads shrink from 350 m on).
//   - Placement: the bottom edge at P.z + k * O's box min z, the height k * O's box height, the
//     horizontal centre at P + k * the horizontal centre of O's box; so a fading quad shrinks
//     towards P. The horizontal edge is perpendicular to the horizontal direction from the camera
//     to the instance.
//   - Width: 2 * k * r0, with r0 the largest horizontal distance of a corner of O's box from O's
//     vertical axis (x = y = 0 in O's space): the largest sqrt(x * x + y * y) over the box's four
//     corners. That is the room O needs when it turns about its axis, so the picture of every angle
//     fits. Median width / (2 * k * r0) 1.000 for 60 objects on River, Pier and Research, all
//     within 0.02 but one (1.05).
//   - Texture: with a the angle of the horizontal direction from the camera to the instance
//     (atan2(dy, dx) in degrees, 0 to 360), the sprite made with fAngle = 165 - 15 * floor(a / 15),
//     brought into 90 to 435 by adding or subtracting 360 (a from 75 to 90 uses fAngle 90, from 90
//     to 105 fAngle 435).
//   - Texture coordinates: u from 0 at the left edge to 1 at the right edge and v from 0 at the
//     bottom to 1 at the top (in the sprite texture the picture's bottom is in row 0), plus half a
//     texel in u and minus half a texel in v (1/128 for the 64 x 64 sprites), with wrap addressing.
//   - Vertex colour: the colour of EF_SetWorldColor times min(1, g's fBrightness * the instance's
//     brightness byte / 100), alpha 255.
//
// Sky box (CRESky; River, Steam, Research, Pier)
//   The engine adds the sky element with EF_AddEf every frame, after writing m_fTerrainWaterLevel,
//   m_fSkyBoxStretching (from the level: 1 on River and Steam, 0.5 on Pier, 0 on Research) and
//   m_fAlpha (I3DEngine::SetSkyBoxAlpha). The D3D9 renderer draws a box around the camera: the view
//   matrix without its translation, no depth test or writes, no fog, clamped textures, texture *
//   texture factor; the factor is white with alpha m_fAlpha, and below alpha 1 the box is blended
//   (SRCALPHA, INVSRCALPHA). Five strips, in this order:
//   - the top, z = 32, x and y from -32 to 32, u = (x + 32) / 64, v = (y + 32) / 64;
//   - the sides y = -32, x = -32, y = 32, x = 32, each from z = 32 down to z1 (v from the top
//     value to 0.5) and on to z2 (v stays 0.5), u 1 to 0 along the side (y = -32: x from -32 to 32;
//     x = -32: y from 32 to -32; y = 32: x from 32 to -32; x = 32: y from -32 to 32). The sides
//     y = -32 and y = 32 run from v 0 at the top, x = -32 and x = 32 from v 1 at the top: two sides
//     share a texture, one in its upper half, the other upside down in its lower half.
//   - Three textures: the top, the sides y = -32 and x = -32, the sides y = 32 and x = 32.
//   With h = camera z - m_fTerrainWaterLevel, s = m_fSkyBoxStretching and F the view distance (the
//   camera's far plane):
//     z1 = -s * h * (30.72 / F + 1 / 128)
//     z2 = -8 + h * (30.72 / F - 0.018)
//   and z1 = z2 = 0 when the camera is below the water level. Both are not clamped: z1 can lie
//   below z2. Checked for F from 350 to 1500, h from 1.6 to 114 and s 0, 0.5, 1 and 2: within 0.001
//   of the heights drawn.

//////////////////////////////////////////////////////////////////////////
// Shaders (Training: 636 shaders and what their getters return)
//
// The engine loads its own shaders by name with EF_LoadShader(szName, eSH_World, EF_SYSTEM, 0):
// "Default", "<Stencil>", "StencilState", "StencilStateInv", "ClearStencil", "ShadowMapGen",
// "OcclusionTest", "CryLight", "ParticleLight", "FogLayer", "RainMap", "OutSpace", "FarTreeSprites",
// "BumpSunGlow", "Decal_VP", "Decal_2D_VP", "ScreenProcess", "ScreenTexMap", "ScreenDistort",
// "BinocularDistortMask", "SniperDistortMask", "Terrain", "TerrainWithFog", "TerrainLowLOD",
// "TerrainWithDefaultDetailTexture", "TerrainDetailLayers", "TerrainDetailTextureLayers",
// "TerrainLayer", "TerrainLightPass", "TerrainShadowPass", "TerrainParticles", "TerrainLake",
// "TerrainWaterBeach", "TerrainWaterBottomSimple", "TerrainWater_OnlySky". Other names come from the
// level and its objects (the sky, flares, lights), some with eSH_Misc and flags 0. Names are not
// case sensitive (the templates "NoDraw" and "nodraw" are the same shader); GetName returns them in
// lower case.
//
// Most shaders are materials: EF_LoadShaderItem(szMaterial, eSH_World, true, szTemplate, 0, pRes,
// 0), with the template the material names ("TemplBumpDiffuse", "TemplModelCommon",
// "TemplBumpSpec_HP_GlossAlpha", "TemplPlants1", "TemplDecal", ..., "NoDraw" for invisible
// surfaces). The shader returned has EF_NOTFOUND set (there is no shader of the material's name);
// GetTemplate(-1) returns the template that is actually used, which the renderer picks (with
// farcry1_testing1's settings mostly "templdiffuse_fp"; "nodraw" for "NoDraw").
// GetName of a material's shader is the material name in lower case. A name loaded again with a
// template that resolves to another GetTemplate(-1) gives a separate shader named "<name>_<n>"
// (n counts the shaders made for that name: "materialcontainer_1" ... "materialcontainer_17" on
// Archive); templates that resolve to the same one share a shader. Names are not unique (two
// shaders "20 - default_1" with different ids on Archive); GetID is.
//
// The values the engine reads:
//   GetTemplate(-1)->GetFlags2() & EF2_OPAQUE: SShaderItem::IsTransparent (IShader.h), called for
//     every drawn material. Opaque templates return flags2 EF2_OPAQUE | EF2_FOGOVERLAY1 (0x408),
//     decals EF2_DONTSORTBYDIST, alpha-blended ones 0.
//   GetSort: eS_Opaque for most, eS_Trees (vegetation), eS_Decal, eS_SeeThrough, eS_Banner (alpha
//     blended), eS_Sky, eS_Terrain, eS_TerrainShadowPass, eS_TerrainDetailTextures,
//     eS_TerrainLightPass, eS_Stencil, eS_Water, eS_WaterBeach, eS_Particles, eS_OcclusionTest,
//     eS_MuzzleFlash, eS_Nearest (lights and flares), eS_FogShader, eS_Refractive (scopes),
//     eS_Glare (screen effects).
//   GetFlags: EF_COMPILEDLAYERS for nearly all, EF_NEEDNORMALS and EF_NOTFOUND for the materials,
//     EF_SYSTEM for the engine's own shaders, EF_SKY for the sky, EF_FOGSHADER for "foglayer",
//     EF_HASCULL, EF_HASVSHADER, EF_USELIGHTS, EF_HASDIFFUSEMAP, EF_OVERLAY, EF_NOMIPMAPS on a few.
//   GetFlags3 (read by the renderer): EF3_NODRAW for the "NoDraw" materials, EF3_HASLM for the
//     lightmapped ones, EF3_DEPTHWRITE for the vegetation template, EF3_HASAMBPASSES,
//     EF3_HASRGBGEN, EF3_HASPSHADER.
//   GetLFlags: 0 except LMF_BUMPMATERIAL (terrain, water) and LMF_DISABLE (decals, some lights).
//   GetREs: empty except for light shaders (one Flare render element: "crylight", the flares) and
//     light beams (one Beam).
//   GetPublicParams: empty except for water and glass shaders (waveamplitude, bumpamountx,
//     bumpamounty, reflectamount, refrbumpamount, opacity, ...).
//
// The templates the materials of the 20 levels name (case varies, the renderer ignores it), with
// the template GetTemplate(-1) returns under farcry1_testing1's settings (r_Quality_BumpMapping 0),
// its sort and its flags2:
//   TemplBumpDiffuse, TemplBumpDiffuse_AlphaGlow, TemplBumpDiffuse_GlossAlphaSpecHigh,
//   TemplBumpDiffuse_GlossSpecHigh, TemplBumpDiffuse_SpecHigh, TemplBumpSpec,
//   TemplBumpSpec_EnvCMAmb, TemplBumpSpec_EnvCMAmb_PS20, TemplBumpSpec_GlossAlpha,
//   TemplBumpSpec_GlossAlpha_PS20, TemplBumpSpec_HP, TemplBumpSpec_HP_GlossAlpha,
//   TemplBumpSpec_PS20, TemplModelBumpSpec_HP, TemplModelBumpSpec_HP_GlossAlpha,
//   TemplModelCommon, TemplModelCommon_Offset_PS20, TemplLightDiffuse_AlphaGlow and "TBD"
//     -> "templdiffuse_fp", eS_Opaque, EF2_OPAQUE | EF2_FOGOVERLAY1;
//   TemplPlants, TemplPlants1 -> "templdecalalphatest_vcolors", eS_Trees, EF2_OPAQUE;
//   TemplPlantsBark -> "templdecal_vcolors", eS_Trees, EF2_OPAQUE;
//   TemplBumpReflCM, TemplBumpReflCM_Light -> "templreflcm", eS_Opaque, EF2_OPAQUE |
//     EF2_FOGOVERLAY1;
//   TemplDecalModulate -> "templdecalmodulate_fp", eS_Decal, EF2_DONTSORTBYDIST;
//   TemplGlassCM -> "templglasscm_fp", eS_Opaque, EF2_OPAQUE | EF2_FOGOVERLAY1;
//   TemplMuzzleFlash_Auto -> "templmuzzleflash_auto_fp", eS_MuzzleFlash, 0;
//   TemplHologram -> "templhologram_fp", TemplDecalOpacityShift2 -> "templdecalopacityshift2_fp",
//     TemplDecalOpacityShift3 -> "templdecalopacityshift3_fp", MutantStealth ->
//     "mutantstealth_fp" (eS_Banner, 0);
//   NoDraw (also "no_draw") -> "nodraw", eS_Opaque, EF2_OPAQUE | EF2_FOGOVERLAY1;
//   TerrainLayer -> "terrainlayer(<maskGen in hex>)" ("terrainlayer(7)", "terrainlayer(27)"),
//     eS_TerrainDetailTextures, EF2_FOGOVERLAY1 | EF2_DONTSORTBYDIST;
//   kept as named:
//     eS_Opaque, EF2_OPAQUE | EF2_FOGOVERLAY1: TemplDecal, TemplDecalAlphaTest,
//       TemplDecalAlphaTestForShadows, TemplDecalGlow, TemplDecalGlowSelfIllum,
//       TemplDecalGlowSelfIllumAnimated, TemplReflCM, TemplReflCMDecal,
//       TemplReflCMDecal_ReflAmountOpacity, TemplReflCMDecalAddSig, TemplTextureShiftT02,
//       TemplTextureShiftT05, LavaVolume;
//     eS_Opaque, EF2_FOGOVERLAY1 | EF2_DONTSORTBYDIST (not opaque): TemplInvLight;
//     eS_Decal, EF2_DONTSORTBYDIST: TemplDecalAlphaBlend;
//     eS_Banner, 0: TemplAlphaBlend, TemplDecalAdd, TemplDecalAdd_VLight, TemplDecalAlphaAdd,
//       TemplDecalOpacity, TemplTextureV22Rotate, S_ObjectColor, FlareProc and FlareProc with a
//       number ("FlareProc 0.025", "FlareProc 0.25", "FlareProc 0.5", "FlareProc 0.75": a template
//       of its own for each, named with the number);
//     eS_Banner, EF2_IGNORERESOURCESTATES: TemplBeamProc;
//     eS_Water, 0: WaterVolume, WaterVolumeBumpReflCM;
//     eS_HeatVision, 0: ParticleMinHeatHaze, ParticleMedHeatHaze, ParticleMaxHeatHaze;
//     eS_MuzzleFlash, 0: TemplMuzzleFlash;
//     eS_Refractive, 0: AG36ScopeRefraction, AW50ScopeRefraction, OICWScopeRefraction,
//       RLScopeRefraction.
//   A template that does not exist ("FlareProc_warmyellow", Swamp) gives a shader with EF_NOTFOUND
//   and EF3_NOTEMPLATE whose GetTemplate(-1) is the shader itself: eS_Opaque, EF2_OPAQUE |
//   EF2_FOGOVERLAY1.
//   With r_Quality_BumpMapping 2 the bump and model templates keep their own names (eS_Opaque,
//   EF2_OPAQUE | EF2_FOGOVERLAY1, vertex and pixel shaders) and TemplPlants1 stays "templplants1"
//   (eS_Trees, EF2_OPAQUE | EF2_NOCASTSHADOWS).
// How the D3D9 renderer draws them with the default settings (the frames at the start positions
// of the seven levels; all fixed function, with fog):
//   "templdiffuse_fp": with lightmap coordinates the two stages of the lightmapped objects (see
//     "Static objects"); without them lighting is on: texture * vertex lighting * 2.
//     Opaque, depth writes, culling D3DCULL_CW. Some materials add a texture transform of the
//     first coordinate set (scale and offset).
//   "templdecalalphatest_vcolors": texture * vertex colour, then * texture factor * 2 (the
//     vegetation of "Static objects"); no lighting, no culling. Most draws are blended (SRCALPHA,
//     INVSRCALPHA; alpha = texture alpha * factor alpha) without depth writes, the others alpha
//     tested (texture alpha >= 128) with depth writes.
//   "templdecal_vcolors": the same colour, opaque, depth writes, culling D3DCULL_CW.
//   "templdecalglowselfillum": two passes: texture * vertex lighting, then the texture again
//     added (ONE, ONE) without lighting or depth writes.
//   "templdecalglow": texture * vertex lighting plus a second texture added in stage 1.
//   "templreflcmdecal": a cube map looked up with the camera space reflection vector (turned back
//     to world space by a texture transform) * vertex lighting, plus the texture; opaque.
//   "templtexturev22rotate": the texture, alpha from the texture, blended (SRCALPHA, INVSRCALPHA)
//     with an alpha test (>= 2), no depth writes, no culling, a texture transform.
//   The other templates were not in view. With every material of River given one of them (the
//   frame at the start position; XRENDERTRACED_TEMPLATE):
//   "templdecal": texture * texture factor (the object's ambient colour), or texture * vertex
//     lighting for objects that are lit; no lightmap stage; opaque, depth writes.
//   "templdecalalphatest": the same with an alpha test (>= 128).
//   "templalphablend": the same blended (SRCALPHA, INVSRCALPHA) without depth writes.
//   "templdecaladd", "templmuzzleflash", "templmuzzleflash_auto_fp": the texture alone, added
//     (ONE, ONE), no depth writes; the vertices carry only positions and texture coordinates.
//   "templdecalalphaadd": texture * vertex colour, blended (SRCALPHA, ONE), no depth writes.
//   "templglasscm_fp": texture * texture factor (grey 25) plus a cube map looked up with the
//     camera space reflection vector (texture transform as for "templreflcmdecal"); opaque,
//     depth writes.
//   "templdecalmodulate_fp" and "templdecalalphablend" (eS_Decal): with every material of the
//     frame given one of them nothing at all is drawn (the first also not with
//     r_Quality_BumpMapping 2), although the engine renders the objects as usual (the same
//     CLeafBuffer::Render calls). Given to some materials only (Training, the first frames;
//     both templates alike):
//       lightmapped objects: drawn with the two stages of the lightmapped objects, opaque,
//       depth writes, as if the template were "templdiffuse_fp";
//       static objects without lightmap: no draw, also with the material's opacity set below 1
//       (the same material with "templdiffuse_fp" and opacity 0.5 is drawn blended);
//       decal entities (SRendParams::pMaterial a material of the entity, loaded as
//       "MaterialContainer" with TemplDecalModulate, opacity 0.99, a 2 triangle quad): drawn
//       with lighting on, stage 0 colour texture * vertex lighting * 2 (MODULATE2X), alpha
//       texture * material alpha (the opacity), blended (SRCALPHA, INVSRCALPHA), depth test
//       LESSEQUAL without depth writes, culling D3DCULL_CW, fog; among the blended objects of
//       the frame. With the opacity set to 1 the same decals get no draw.
//   "aw50scoperefraction" (eS_Refractive): nothing is drawn with r_ScopeLens_fx 0 (the default).
//     With r_ScopeLens_fx 1 (River) the renderer first copies the whole back buffer
//     (IDirect3DDevice9::StretchRect, no filter) into a 1024 x 768 D3DFMT_A8R8G8B8 render target
//     texture, once per frame, right before the first element it draws with this template. It
//     then draws the objects with a vertex and a pixel shader: stage 0 a 64 x 64 D3DFMT_X8L8V8U8
//     bump map (wrap, linear with mip maps), stage 1 the copy (clamp, point filtering), stage 2 a
//     16 x 16 D3DFMT_A8R8G8B8 texture; blending (SRCALPHA, INVSRCALPHA), depth test (LESSEQUAL)
//     without depth writes, culling D3DCULL_CW, fog. r_ScreenRefract 1 alone makes the copy but no
//     draws. With r_Quality_BumpMapping 1 or 2 the same copy is made once per frame before the
//     ocean, and no draw of the frame reads it (r_WaterRefractions 0).
// The templates that only the other 13 levels name, given in the same way to every material of
// River (t is ITimer::GetCurrTime(); "texture" is the material's diffuse texture, "factor" the
// texture factor, which holds the object's ambient colour unless said otherwise; all with depth
// test LESSEQUAL and fog):
//   "templreflcm" (TemplReflCM, TemplBumpReflCM): a 64 texel D3DFMT_X8R8G8B8 cube map (clamp)
//     looked up with the camera space reflection vector and a COUNT3 texture transform back to
//     world space (as for "templreflcmdecal"), * factor or * vertex lighting; no diffuse texture,
//     the vertices carry positions and normals only; opaque, depth writes.
//   "templreflcmdecaladdsig": stage 0 the same cube map lookup, stage 1 texture ADDSIGNED result
//     (alpha texture * result); opaque, depth writes.
//   "templreflcmdecal_reflamountopacity": a pixel shader (ps_2_0) with sampler 0 the texture,
//     sampler 1 a 128 x 128 DXT1 texture that is the same for every material, sampler 2 the cube
//     map; opaque, depth writes.
//   "templtextureshiftt02", "templtextureshiftt05": like "templdecal" (texture * factor or *
//     vertex lighting, opaque, depth writes) with a COUNT2 texture transform that moves v by
//     0.2 * t and 0.5 * t (the offset stands in the third and fourth rows of the matrix).
//   "templdecalopacityshift2_fp", "templdecalopacityshift3_fp": colour the texture, alpha texture
//     alpha * factor alpha, blended (SRCALPHA, INVSRCALPHA) without depth writes or culling, v moved
//     by -0.35 * t. The first takes positions and texture coordinates; the second asks for
//     positions and normals only (no texture coordinates in the stream).
//   "templdecalopacity": a pixel shader (ps_2_0), sampler 0 the texture, sampler 1 a 128 x 128
//     DXT1 texture that is the same for every material; blended (SRCALPHA, INVSRCALPHA), no depth
//     writes.
//   "templdecalglowselfillumanimated": the two passes of "templdecalglowselfillum", the second
//     (depth test EQUAL, ONE, ONE, no depth writes) texture * factor with factor = (g, g, g) and
//     g = 0.75 + 0.75 * sin(2 * pi * 0.5 * t) clamped to 0..1 (ten frames fitted to 1/255).
//   "templdecaladd_vlight": texture * factor, added (ONE, ONE), no depth writes.
//   "templhologram_fp": stage 0 the texture, stage 1 result * factor with the alpha of the texture
//     at the second coordinate set (which the stream does not have), added (ONE, ONE), no depth
//     writes, no culling.
//   "templdecalalphatestforshadows": texture * factor * 2, alpha test (>= 128), depth writes, no
//     culling; positions and texture coordinates only.
//   "s_objectcolor": a 16 x 16 DXT1 texture of its own (clamp) * factor, the factor being the
//     object's colour (0xffbfbfbf, 0xffffffff seen); blended (SRCALPHA, INVSRCALPHA), no depth
//     writes; positions only.
//   "templinvlight": drawn once for each element of every object that has no lightmap
//     (SRendParams::pLightMapInfo NULL) and a dynamic light in SRendParams::nDLightMask (grass,
//     trees, weapons lying about, rocks; one draw whether the mask holds one light or more);
//     objects without dynamic lights and lightmapped objects (lit or not, MergedLB as well) get no
//     draw at all. After CheckUpdate with nFlags 0x9; a vertex and a pixel shader
//     (vs_1_1, ps_2_0), the position from stream 0 and three FLOAT3 (TEXCOORD2, TEXCOORD3,
//     BLENDWEIGHT0) from stream 1, sampler 0 a 16 x 16 DXT1 and sampler 1 a 32 x 32
//     D3DFMT_X8R8G8B8 texture (clamp), both the same for every material; mostly blended
//     (DESTCOLOR, ZERO), i.e. multiplied into the frame, else (SRCALPHA, INVSRCALPHA); no depth
//     writes.
//   "lavavolume": vs_1_1 and ps_2_0, sampler 0 the texture, sampler 1 a 512 x 512
//     D3DFMT_X8L8V8U8 bump map, sampler 2 a 32 x 32 DXT1 texture; opaque, depth writes.
//   "mutantstealth_fp": one fixed function pass, colour = texture * texture factor * 2
//     (D3DTOP_MODULATE2X), alpha = texture * factor; the texture is a 32 x 32 DXT1 grey noise
//     texture of its own (mean about 0.54, 6 levels, repeated, the mesh's own texture coordinates,
//     no texture transform); added (ONE, ONE), depth test LESSEQUAL without depth writes, back
//     faces culled, fog on, no lighting. The factor is grey: (int)(refraction * 255) in R, G and B,
//     alpha 255, from the float "refraction" in the object's m_ShaderParams (0.01 gives 2, 0.5 gives
//     127); without it the template's own value, which gives 12 (Archive's weapons and cloaked
//     mutants with the params taken away). Drawn among the blended objects, after the opaque ones.
//     With a value of 0.01 the character is all but invisible; 0.5 shows a light, noisy ghost of
//     it. Named as a material's own template (forced on meshes with no params) the factor was
//     black (0xff000000): nothing visible.
//   "templbeamproc": a vertex shader (vs_1_1) with fixed function stages, texture * diffuse,
//     blended (SRCALPHA, INVSRCALPHA), no depth writes, no culling. The same for a light beam
//     object of its own at Treehouse's start (369 vertices, P3F_N_COL4UB_TEX2F, a 16 x 16
//     D3DFMT_X8R8G8B8 texture).
//   "flareproc", "particlemedheathaze": no draw at all for the objects.

//////////////////////////////////////////////////////////////////////////
// Water reflections and refractions (Pier, r_Quality_BumpMapping 0)
//
// The ocean ring ("OutdoorWaterCircle", "terrainwater_onlysky") with r_WaterReflections and
// r_WaterRefractions (0 in farcry1_testing1's settings; set before the level load). All passes
// have a vertex and a pixel shader, blending (SRCALPHA, INVSRCALPHA), depth test LESSEQUAL without
// depth writes, no culling and fog:
// - Both 0, or only r_WaterRefractions 1: one pass, stage 0 a 64 x 64 D3DFMT_X8L8V8U8 bump map
//   (wrap, linear with mip maps; another texture every few frames, an animation), stage 1 a
//   128 x 128 D3DFMT_X8R8G8B8 texture (wrap, linear with mip maps).
// - r_WaterReflections 1: the reflection (below) in a 512 x 512 D3DFMT_X8R8G8B8 render target
//   texture, and one pass with other shaders: stage 0 the bump map, stage 1 the reflection (clamp,
//   linear without mip maps), stage 2 a 64 x 4 and stage 3 a 64 x 64 DXT1 texture. Right before
//   the ocean the renderer copies the whole back buffer (StretchRect) into a 1024 x 768
//   D3DFMT_A8R8G8B8 render target texture, which no draw reads.
// - Both 1: the same, with one more pass before it: stage 0 a 256 x 256 D3DFMT_X8L8V8U8 bump map
//   (wrap), stage 1 the copy of the back buffer (clamp, linear without mip maps).
//
// The reflection. In EF_EndEf3D, before it draws anything of the frame, the renderer calls
// I3DEngine::GetDistanceToSectorWithWater() (D: 134.3 and 186.8 in Pier's intro, 5.67 at the
// start position, 111.4 on River), GetWaterLevel() (no arguments: W = 16 on Pier, 28 on River)
// and GetMaxViewDistance() (1500). It does this in every frame; when the reflection is due (below)
// it then sets the render target texture with
// a 1024 x 768 D24S8 depth surface of its own (not the back buffer's) and a 512 x 512 viewport,
// clears colour, depth and stencil (the colour of SetClearColor), and calls
//   I3DEngine::SetCamera(cam, false);
//   I3DEngine::DrawLowDetail(DLD_TERRAIN | DLD_TERRAIN_FULLRES);   // 0x1040
// with cam the frame's camera mirrored in the water plane: position (x, y, 2 * W - z), the pitch
// negated, no roll, the same field of view and far plane, the near plane at D but at most 16 (5.67
// at Pier's start position, 16 in its intro and on River). Inside DrawLowDetail the engine
// renders a frame of its own: GetWidth, GetHeight, IRenderer::SetCamera(cam), GetFrameID(true),
// EF_Query(EFQ_RecurseLevel) (1 here), EF_StartEf, EF_ClearLightsList, the sun (EF_ADDDlight),
// the sky and its flare (EF_AddEf), the terrain sectors and "LowResTerrain"
// (AddRenderElements), SetFog, EnableFog(true), EF_EndEf3D(3), EnableFog(false); no objects,
// characters, water, particles or sprites. The renderer draws the sky (no depth test, no fog) and
// the terrain (depth test and writes, culling D3DCULL_CW, fog) into the texture, then sets the back
// buffer and its depth surface again, clears them and draws the frame. The camera is an ordinary
// one (its view matrix is a rotation, not a mirror), and the culling is the frame's.
// The reflection is not drawn every frame, and the ocean reads the last one in between. It is
// drawn again (timer time of ITimer::GetCurrTime, camera of the frame against the one of the last
// reflection) when
//   - the time since the last one exceeds r_WaterUpdateFactor * D, at most 0.3 s (default factor
//     0.02: 0.114 s on Pier with D = 5.67; factor 0.04: 0.228 s; on River 0.002 * 111.4 = 0.223 s,
//     and 0.3 s with the default factor);
//   - or the camera moved more than r_WaterUpdateDistance (2 m; with 0.1 every 0.1 m of a slowly
//     moving camera);
//   - or it turned more than r_WaterUpdateDeltaAngle degrees (5: a turn of 6.4 degrees redraws it
//     in the same frame, 3.2 degrees does not).

//////////////////////////////////////////////////////////////////////////
// Particles
//
// Particles (I3DEngine::SpawnParticles with the default texture, and a light effect on Carrier)
// are added one by one every frame with EF_AddSpriteToScene(nShaderID, 4, pVerts, pObj, NULL, 0, 0):
// a quad facing the camera in world space, texture coordinates (1,0) (1,1) (0,1) (0,0), the
// particle's colour in SColorVert::color (bytes R, G, B, A), the ID of the shader
// ("particlelight_fp"), which the engine gets with IShader::GetID, and an object from EF_GetObject
// with an identity matrix and m_SortId -1e6 or 1e6 (some with the ambient colour of the place in
// m_AmbColor). EF_AddSpriteToScene returns pObj.
// The object also carries the texture and the blending:
// - m_NumCM is the texture ID. The default particle texture is the one the engine made at start-up
//   with GenerateAlphaGlowTexture(1): 256 x 256 D3DFMT_A8 with all 9 mip levels, alpha =
//   clamp(256 * (1 - r / 128), 0, 255) for r the distance of the texel (x, y) from (128, 128).
// - m_RenderState comes from ParticleParams::eBlendType: AlphaBased 0x10000065 (GS_BLSRC_SRCALPHA |
//   GS_BLDST_ONEMINUSSRCALPHA | GS_ALPHATEST_GREATER0), ColorBased 0x42 (GS_BLSRC_ONE |
//   GS_BLDST_ONEMINUSSRCCOL), Additive 0x22 (GS_BLSRC_ONE | GS_BLDST_ONE), None 0.
// How the D3D9 renderer draws them (Training): world space (identity world matrix), indexed
// triangle lists from a dynamic P3F_COL4UB_TEX2F buffer, two triangles per quad (0 1 2, 0 2 3),
// sprites that share a CCObject in one draw; depth test without depth writes, fog, no culling,
// linear filtering with mip maps, wrap addressing. Blending and alpha test as m_RenderState says;
// 0 is drawn like AlphaBased. Texture stages: colour = (vertex colour + m_AmbColor, as the texture
// factor) * texture colour, alpha = vertex alpha * texture alpha. The sprites are drawn within the
// scene, not after it, and not in the order of the calls.
// Heat haze (Rebellion, a fire near the start): sprites in the same form with the ID of
// "ParticleMinHeatHaze" (loaded with EF_LoadShader(name, eSH_Misc, 0); "ParticleMedHeatHaze" and
// "ParticleMaxHeatHaze" on other levels; eS_HeatVision), vertex colour (0, 0, 0, 250),
// m_RenderState 0x10000065 and a texture ID in m_NumCM. The D3D9 renderer makes no draw for them
// with farcry1_testing1's settings: the frame has the same draws with and without them (checked by
// holding the sprites back), while the "particlelight_fp" sprite next to them is drawn.

//////////////////////////////////////////////////////////////////////////
// Decals (River and Pier with e_decals 1; farcry1_testing1's settings have e_decals 0)
//
// With e_decals 0 there are no decals: I3DEngine::CreateDecal does nothing and the TerrainParticles
// element below is not added. With e_decals 1:
// - CreateDecal looks the texture up with EF_GetTextureByID(nTid) and sets ITexPic::SetFilter
//   (FILTER_LINEAR) and ITexPic::SetClamp(true) on it. With an owner (a static object) it also
//   calls CLeafBuffer::GetIndices(NULL) twice on the owner's leaf buffer, with
//   DeleteLeafBuffer(NULL) in between.
// - Every frame the engine adds its CRETerrainParticles element (EF_AddEf, shader
//   "terrainparticles", template 0). Drawing it calls I3DEngine::DrawTerrainParticles, and the
//   engine draws the decals on the terrain from inside that call (inside EF_EndEf3D):
//   SetState(GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA), SetColorOp(eCO_MODULATE, eCO_MODULATE,
//   eCA_Texture | (eCA_Constant << 3), the same for alpha), SetCullMode(R_CULL_NONE), then for each
//   decal SetMaterialColor(the world colour EF_SetWorldColor got) and for each strip
//   SetTexture(nTid, eTT_Base), SetTexClampMode(true) and DrawTriStrip(pBuffer, nVerts).
// - pBuffer is the engine's CVertexBuffer: VERTEX_FORMAT_P3F_TEX2F, m_VS[VSF_GENERAL].m_VData in
//   system memory, no hardware buffer, m_NumVerts 0; nVerts vertices form a triangle strip.
// - The strips are the columns of the terrain grid (2 m cells, heights from the height map) under
//   a square of side 2 * fSize around the decal, all moved towards the camera by 1% of the
//   distance. Texture coordinates map the square to 0..1 (u along +x, v along -y); the vertices
//   outside it get coordinates outside 0..1, which the clamping turns into the texture's edge.
// How the D3D9 renderer draws DrawTriStrip here: DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, nVerts - 2)
// with stride 20 and an identity world matrix; depth test (LESSEQUAL) without depth writes,
// blending (SRCALPHA, INVSRCALPHA), no culling, fog; stage 0 colour and alpha = texture * texture
// factor (the material colour), clamp addressing, linear filtering without mip maps.
// Decals with an owner (walls and floors of buildings, crates, trees and bushes; the engine's own
// bullet hits too; River with e_decals 1):
// - While the level loads (after PreLoad) the engine calls EF_LoadShader("Decal_VP") and
//   EF_LoadShader("Decal_2D_VP"). The D3D9 renderer returns "decal_vp" and "decal_2d_vp", or, with
//   r_Quality_BumpMapping 0 at that time, the same "decal_fp" for both (sort 15, flags2 0x100,
//   LMF_DISABLE).
// - CreateDecal goes on with CreateLeafBufferInitialized(one zeroed vertex,
//   VERTEX_FORMAT_P3F_COL4UB, the indices of the owner's triangles the decal covers (114 to 135
//   of a wall's 180, all 6 of a floor tile's), R_PRIMV_TRIANGLES, "BigDecal", eBT_Static, 1
//   material, the decal's texture ID, no callback, no custom data, bOnlyVideoBuffer false,
//   bPrecache true), then CLeafBuffer::SetChunk(the "Decal_2D_VP" shader, first vertex 0, the
//   owner's vertex count, first index 0, the index count, material 0) and
//   CLeafBuffer::SetShader(the same shader, the decal's texture ID). The engine sets the new
//   buffer's m_pVertexContainer to the owner's leaf buffer (the inline SetVertexContainer; nothing
//   reaches the renderer): the indices address the owner's vertices, and the buffer's own single
//   vertex is never used.
// - Every frame while r_Quality_BumpMapping is not 0 (its value at that moment: switching it while
//   the level runs turns these decals on and off; with 0 they are made but never added), for each
//   such decal in view: EF_GetObject(true, -1), with the owner's matrix,
//   m_ObjFlags FOB_TRANS_MASK (with FOB_BENDED on a bending plant), m_SortId -1e6;
//   CLeafBuffer::SetRECustomData(16 floats, fog scale 0, alpha 1) and
//   CLeafBuffer::AddRenderElements(object, no lights, template -1, fog 0, sort 0, no material).
//   The 16 floats, in the owner's object space: the plane of the texture's u, the plane of v (each
//   (a, b, c, d) with u = a * x + b * y + c * z + d, of length 1 / (2 * size), 0.5 at the centre),
//   then the decal's centre and its size (fSize over the owner's scale), then four zeros. The
//   texture covers a square of side 2 * size around the centre, as on the terrain.
// How the D3D9 renderer draws a "BigDecal": an indexed triangle list from its own index buffer
// over the owner's vertex buffer, after its owner; alpha test (GREATER 0), blending (SRCALPHA,
// INVSRCALPHA), depth test EQUAL (the owner's own depths) without depth writes, no culling, fog;
// stage 0 the decal's texture (clamp, linear without mip maps).
// - "decal_2d_vp" (owner buffers VERTEX_FORMAT_P3F_TEX2F, stride 20, when r_Quality_BumpMapping is
//   not 0 at load): a vertex and a pixel shader; the first three rows of the custom data are
//   vertex shader constants c4 to c6; stage 1 a 32 x 32 D3DFMT_X8R8G8B8 texture (clamp, linear
//   with mip maps).
// - "decal_fp" (owner buffers VERTEX_FORMAT_P3F_N_TEX2F, stride 32): fixed function. The texture
//   coordinates are generated from the camera space position (D3DTSS_TCI_CAMERASPACEPOSITION,
//   D3DTTFF_COUNT2) with a texture matrix that takes it back to the owner's object space and
//   applies the u and v planes; colour and alpha = texture * texture factor (white).

//////////////////////////////////////////////////////////////////////////
// Shadows (Training with e_shadow_maps 1 and e_stencil_shadows 1)
//
// Shadow maps (frustum layout in RendererStructures.h, "Shadow maps"). The D3D9 renderer renders
// a frustum's map inside EF_EndEf3D of a frame that draws a receiver of it: a static caster's map
// once (frames 7, 8, 10, 38, 67, ... after the load), the maps of characters every frame:
// - into a D3DFMT_A8R8G8B8 render target of the map's size (512 x 512 and 128 x 128 for static
//   objects, 32 x 32 for the characters seen), cleared first to colour 0x00FFE000, depth 1 and
//   stencil 0; viewport of the map's size, no culling, no fog;
// - from the light: for the sun the eye is 10000 units from the caster along the sun direction,
//   with a perspective projection whose near and far plane lie around that distance (9989.6 and
//   10005.9 for the caster seen) and a field of view of about 0.08 degrees, so practically
//   orthographic;
// - each opaque chunk of the caster in two passes: alpha only without a depth test, then colour
//   only with depth test and writes, a vs_1_1, a second stream of 36-byte vertices (three FLOAT3
//   declared TEXCOORD2, TEXCOORD3, BLENDWEIGHT) and a 2048 x 1 D3DFMT_A8R8G8B8 texture in stage 0
//   (clamped); an alpha-tested chunk in one pass with its own texture (alpha test >= 128) in
//   stage 0 and the 2048 x 1 texture in stage 1.
// Receivers: the terrain under a caster gets "EntityShadowOnTerrain" leaf buffers (shader
// "terrainshadowpass", indices into the sector's 33 x 33 grid as for "Dynamic"), drawn with
// vs_2_0 / ps_2_0: the sector texture in stage 0 and the shadow map in stage 1 (clamped,
// bilinear), blended (SRCALPHA, INVSRCALPHA) with alpha test > 0, depth test without depth
// writes, fog; vertex constants c0-c3 the camera's view projection, c10 and c11 the sector
// texture coordinates (as c4 and c5 of the terrain).
//
// Stencil shadows: characters get a "Character ShadowVolume" leaf buffer (VERTEX_FORMAT_P3F, new
// indices every frame with UpdateVidIndices) drawn through a TriMeshShadow element with the shader
// "<stencil>" (EF_AddEf, sort value 0x30000000), between two ClearStencil elements with the same
// sort value. ClearStencil is Clear(D3DCLEAR_STENCIL, stencil 0). The volume: an indexed triangle
// list with a vs_1_1, depth test without depth writes, no colour writes, no culling, two-sided
// stencil with func ALWAYS, zfail DECR for the front (clockwise) faces and INCR for the back
// faces, fail and pass KEEP (depth fail). In the frames seen no later draw tested the stencil, so
// the volumes changed nothing on the screen.

//////////////////////////////////////////////////////////////////////////
// Fog volumes (Carrier)
//
// EF_RegisterFogVolume(fMaxFogDist, fFogLayerZ, color, -1, bCaustics) returns 1, 2, 3, ... in
// the order of the calls. On an outdoor level the first is the ocean (layer z = the water level,
// bCaustics true); the volumes inside buildings have bCaustics false. The ID of the volume an
// object is in reaches the renderer as SRendParams::nFogVolumeID (CLeafBuffer::Render), the
// nFogID of AddRenderElements and the NumFog of EF_AddEf (see also Characters, Geometry).
// The D3D9 renderer draws volume fog only with r_VolumetricFog 1 (0 in farcry1_testing1's
// settings; then nothing on the screen depends on the IDs and the caps are not drawn). With it on:
// - after the opaque geometry, every element drawn with a volume ID is drawn again: the same
//   geometry with depth test EQUAL, no depth writes, no fog, blended (SRCALPHA, INVSRCALPHA);
// - fixed function, both stages with the camera space position as texture coordinates (through a
//   texture matrix): stage 0 a 16 x 16 DXT5 texture whose v grows by 0.1 per unit of height,
//   stage 1 a 128 x 128 A8R8G8B8 texture whose u grows by 1 / (4 * fMaxFogDist) per unit of
//   distance from the camera; colour and alpha = texture 0 * texture factor * texture 1, the
//   texture factor being the volume's colour (alpha 255);
// - then the caps ("VolFogTopCircle", see Geometry) with the same stages, depth test LESSEQUAL
//   without depth writes, no culling, blended the same way.
//
// Camera under water (Pier; the ocean is volume 1: fMaxFogDist 40, fFogLayerZ 16, colour (0.188,
// 0.306, 0.573); the player moved below the surface with the console command goto, looking ahead
// and looking down). The engine changes the frame's fog and the ocean's elements:
// - Every frame on land: SetFog(0, 600, 900, the level's fog colour (0.384, 0.549, 0.780),
//   R_FOGMODE_LINEAR), EnableFog(true), SetClearColor(the same colour). Under water: SetFog(0, 0.4,
//   40, the volume's colour, R_FOGMODE_LINEAR) (the end is fMaxFogDist), EnableFog(true), then
//   SetClearColor twice, first with the level's fog colour, then with the volume's; the frame is
//   cleared with the last one (0x304e92).
// - The ocean ring ("OutdoorWaterCircle") is added with AddRenderElements nFogID 1 and sort 17
//   (eS_Water); on land with nFogID 0 and sort 14 (eS_SeeThrough). The ocean bottom
//   ("OceanBottomCircle") has nFogID 1 and sort 0 in both cases.
// The D3D9 renderer draws the same elements in the same order as on land (the sky first, without
// fog, then terrain, the ocean bottom, the weapon, then the ring with its usual shader and states);
// the ring is seen from below (no culling) and fades into the fog colour, and it hides the sky. One
// difference with r_WaterReflections 0: right before the ring the renderer copies the whole back
// buffer (StretchRect into a 1024 x 768 render target texture that no draw of the frame reads),
// which on land it does only with r_WaterReflections 1. Air bubbles are particles
// (EF_AddSpriteToScene, "particlelight_fp"), drawn after the ring. There is no full screen pass
// for the water: the blue comes from the fog and the clear colour, and ScreenProcess draws nothing
// unless a screen effect is active (the ScreenFade fade in after the level start shows as a black
// quad).

//////////////////////////////////////////////////////////////////////////
// Screen effects (Training)
//
// The ScreenProcess element (EF_AddEf every frame, RendererStructures.h, "Screen effects") draws
// the active effects at the end of the 3D frame, before the 2D calls. FlashBang and ScreenFade
// each draw one quad over the whole screen, FlashBang first. Positions go from -0.5 pixel to the
// last pixel minus 0.5 (0..1 with an orthographic projection, y down), uv 0..1. The texture is a
// 16 x 16 DXT1 texture of the renderer (the same one for both), clamped, with linear filtering.
// There is no depth test or depth write and no culling. Colour and alpha = texture * texture
// factor:
// - FlashBang: added (ONE, ONE); the texture factor is grey, c * 255 with alpha 255, where c falls
//   with the flash value v (parameter 24): c = 1.08 * v - 0.07 within 2 / 255 for v from 0.22 to
//   0.92. With the defaults nothing else is drawn at the flash position.
// - ScreenFade fading in: blended (SRCALPHA, INVSRCALPHA); the texture factor is black, with an
//   alpha that falls linearly from 255 to 0 over the fade time (102 per second for -2.5).
// ScreenBlur Active 1 with ScreenBlurAmount 0.5 and a colour drew nothing and rendered into no
// texture, also with r_MotionBlur 1 and r_Glare 1 set before the level load.

//////////////////////////////////////////////////////////////////////////
// Heat vision and night vision (Training, River, Pier, Steam)
//
// I3DEngine::EnableHeatVision(b) calls EF_EnableHeatVision(b) right away; EF_GetHeatVision must
// return the stored value, the engine asks it for every object it draws (about 730 times per frame
// on Training). While it returns true the engine changes what it hands over:
// - every CLeafBuffer::Render gets SRendParams::nTempl EFT_HEATVISION (19) instead of -2;
// - characters are added with EF_AddEf(nTempl 19) and their CCObject has m_nTemplId 19 and
//   FOB_HOTAMBIENT in m_ObjFlags (m_fHeatFactor is 1, as without heat vision);
// - the terrain sector objects get FOB_HEATVISION in m_ObjFlags;
// - before an object a character carries (its weapon) is drawn, the engine adds a heat source with
//   EF_ADDDlight: flags DLF_POINT | DLF_HEATSOURCE | DLF_LOCAL (0x108020), name "local_hs_weapon",
//   white, radius 0.15 to 0.42, direct factor 0, at the object; that object's SRendParams has no
//   lights and its CCObject::m_DynLMMask holds only the bit of this light;
// - SetFog gets the colour (0, 0, 0, 0).
// The D3D9 renderer then draws every static object, plant and character with pixel shaders and a
// vertex colour (CheckUpdate to VERTEX_FORMAT_P3F_COL4UB_TEX2F; the characters' dynamic buffers
// become P3F_N_COL4UB_TEX2F), without fixed function lighting; terrain, water, sky and sprites
// are drawn as without heat vision. It renders into no texture and adds no full screen pass.
// The objects' pixel shader is the same for every template, with the pixel constants c0 (0.85,
// 0.85, 0.85, 1), c1 (0.1, 0.1, 0.1, 0.4) and c2 (0.5, 0.7, 1, 1).
// None of these objects (static objects, plants, characters with the player's hands and weapon)
// gets its own transform: where the D3D9 renderer normally folds the object's matrix into the view
// matrix, every one of them is drawn with the view matrix the draw before it left. After the
// terrain that is the camera's, so the vertices are placed as if object space were world space;
// after the ocean bottom it is the ocean bottom's (the camera's rotation with world x and y scaled
// by far plane distance / 16 and the eye at (0, 0, camera height + 0.1); 31.25 on River, 43.75
// on Training, 25 on Steam), which puts them beyond the far plane. In the frames of River, Pier,
// Training and Steam none of them could be seen: where they should be, what is behind them shows
// (the clear colour, black, inside buildings). Terrain, water, sky and the far tree sprites keep
// the camera and stay in place; in the back buffer water comes out dark blue (about (30, 44, 66)),
// the far tree sprites dark green, and the sky is dimmed.
// At start-up this renderer reports (ILog::LogWarning and ISystem::Warning) that it cannot compile
// two of its own programs: the vertex program of the characters in heat vision and a pixel shader
// for the screen. The missing transform of the characters at least may come from that.
//
// Night vision is the screen effect "NightVision" (Active 1, see RendererStructures.h, "Screen
// effects"); r_Cryvision 1 has the same effect. The ScreenProcess element then draws two quads
// over the whole screen at the end of the 3D frame: positions 0..1 with an orthographic
// projection (y down), u 0..2, v from an offset that changes with time to the offset + 2; no depth
// test or writes, no culling, linear filtering with wrapping; colour and alpha = texture * texture
// factor:
//   1. a 16 x 16 DXT1 texture, texture factor (0, 51, 76), added (ONE, ONE);
//   2. a 64 x 64 DXT1 texture, texture factor (102, 153, 204), blended (DESTCOLOR, SRCCOLOR),
//      i.e. the screen times twice the quad colour.
// Night vision changes nothing else in the frame; heat vision alone draws no such quads.

//////////////////////////////////////////////////////////////////////////
// 2D and text
//
// Draw2dImage takes coordinates of an 800 x 600 virtual screen: the engine clears the screen and
// draws the loading screen with Draw2dImage(0, 0, 800, 600, ...), also outside Set2DMode.
// ScaleCoordX(x) = x * width / 800 and ScaleCoordY(y) = y * height / 600 (12 -> 15.36 at
// 1024 x 768). Set2DMode is called with (true, 1024, 768), (true, 800, 600), (false, 0, 0) and
// (false, 800, 600).
//
// How the D3D9 renderer draws the 2D calls (River, 1024 x 768, HUD and console):
//   Draw2dImage(x, y, w, h, nTexId, s0, t0, s1, t1, fAngle, r, g, b, a, z): one quad (a triangle
//     fan) in back buffer pixels whatever Set2DMode says: the corners are x * W / 800 and
//     y * H / 600 (W x H the viewport), minus half a pixel when fAngle is 0. Texture coordinates:
//     top left (s0, 1 - t0), top right (s1, 1 - t0), bottom right (s1, 1 - t1), bottom left
//     (s0, 1 - t1), so the engine's usual (0, 1, 1, 0) shows the image upright; values outside
//     0..1 repeat the texture (the console passes (4, 2, 1, 1)). fAngle is in degrees and turns the
//     quad about its centre: -90 turns it a quarter turn counter-clockwise on the screen (the top
//     left corner goes to the bottom left). The vertex colour is (r, g, b, a); SetMaterialColor
//     does not change it. Orthographic projection of the viewport with the origin top left; depth,
//     blending and colour writes follow SetState.
//   DrawLineColor(v0, c0, v1, c1) and DrawDynVB(pVerts, pInds, nVerts, nInds, R_PRIMV_TRIANGLES)
//     come inside Set2DMode(true, 800, 600): an orthographic projection of 800 x 600 with the
//     origin top left, positions as passed (no half pixel). A line is one line list primitive with
//     the colours as vertex colours; DrawDynVB draws an indexed triangle list of the vertices as
//     passed with the texture of SetTexture.
//   Text (DrawDynVB(nOffs, 0, nVerts) after the font calls below) keeps its positions too.
//   nTexId -1 draws without a texture: the vertex colour alone.
//   TransformTextureMatrix(x, y, fAngle, fScale) ... ResetTextureMatrix around a Draw2dImage moves
//     its texture coordinates (seen with fAngle 0): fScale 1 gives (u + x, v + y), fScale -1 gives
//     (1 + x - u, 1 + y - v). The console background is two such images of 800 x 300 at the top of
//     the screen, the second with fScale -1 and blended, with x growing each frame.
//   SetState: the GS_BLSRC_* / GS_BLDST_* pairs seen in 2D are (ONE, ZERO), (SRCALPHA,
//     ONEMINUSSRCALPHA) and (ONEMINUSDSTALPHA, DSTALPHA), drawn with the same blend factors, and no
//     blending without them. GS_COLMASKONLYALPHA writes only alpha: the HUD writes two images into
//     the back buffer's alpha (GS_COLMASKONLYALPHA with ONE, ZERO) and then draws two more over
//     the same place with (ONEMINUSDSTALPHA, DSTALPHA), so the back buffer needs an alpha channel.
//
// Start-up videos and the main menu (the frames before a level):
//   The first video: DownLoadToVideoMemory(pData, 640, 480, eTF_0888, eTF_0888, 0, false, 0, 0,
//   "$VideoPanel", FT_DYNAMIC) makes the video texture (the D3D9 renderer makes a 640 x 480
//   X8R8G8B8 texture with one level in D3DPOOL_MANAGED, without usage flags). Each following
//   video: RemoveTexture(nTexId) and DownLoadToVideoMemory(pData, w, h, eTF_0888, eTF_0888, 0,
//   false, 0, 0, NULL, FT_DYNAMIC) with pData all zeros, which gets the same ID back: 800 x 600,
//   1024 x 768 (the D3D9 renderer makes it at that size, not a power of two), then the main menu's
//   background, 800 x 600 (the menu was up at frame 15000 here).
//   Whenever the video has a new picture (not every frame): UpdateTextureInVideoMemory(nTexId,
//   pData, 0, 0, w, h, eTF_8888) with the whole picture, always in the same pData: 4 bytes per
//   pixel in the order B, G, R, A (A always 255), rows from the top. The D3D9 renderer shows these
//   bytes as they are (X8R8G8B8).
//   Every frame, without Clear: GetHWND, Set2DMode(true, 1024, 768), SetState(GS_NODEPTHTEST |
//   GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA), the UpdateTextureInVideoMemory if any, the same
//   SetState again, Draw2dImage(0, 0, 800, 600, -1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0) (an opaque black
//   quad over the whole screen), Draw2dImage(0, 0, 800, 600, nTexId, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0),
//   SetState(GS_DEPTHWRITE), Set2DMode(false, 0, 0), SetState(GS_DEPTHWRITE). The D3D9 renderer
//   samples the video clamped, with linear filtering and no mip maps.
//   The main menu draws over its video only with Draw2dImage (its frame images, DXT3 textures in
//   the D3D9 renderer, the mouse pointer, and quads with nTexId -1 and colour (0, 0, 0, 0)) and
//   text (each string twice: a black shadow one pixel lower right, then the string); its frames
//   call nothing else of IRenderer.
//   At "map" the engine calls RemoveTexture(nTexId) and makes an 800 x 600 texture of zeros the
//   same way, which is never updated; the last loading screen frames still draw the black quad and
//   this texture first.
// Loading screen: every frame of a level load draws, twice, SetState(GS_NODEPTHTEST |
//   GS_BLSRC_SRCALPHA | GS_BLDST_ONEMINUSSRCALPHA), Draw2dImage(0, 0, 800, 600, nPicture, 0, 1, 1,
//   0, 0, 1, 1, 1, 1, 1) with the level's picture (EF_LoadTexture) and the progress bar
//   Draw2dImage(40, 480, w, 13, ...), w growing to 720 as the level loads.
//
// Text (CryFont) uses only the texture ID functions; the CFBitmap overloads are never called.
//   FontCreateTexture(512, 512, pData, eTF_8888) creates a font texture (4 at start-up) and
//   FontUpdateTexture(nTexId, 0, 0, 512, 512, pData) uploads the whole bitmap again, with the same
//   pData, whenever glyphs are added (dozens of times in the first frames). The bitmap has 4 bytes
//   per pixel, B, G, R, A: 255, 255, 255 inside the glyph cells and the glyph's coverage in A; the
//   D3D9 renderer keeps only A (a D3DFMT_A8 texture).
//   Every block of text: FontSetTexture(nTexId, 2), FontSetRenderingState(0, 0),
//   FontSetBlending(4, 5), GetDynVBPtr(nVerts, nOffs, 0), DrawDynVB(nOffs, 0, nVerts),
//   FontRestoreRenderingState(). Pool 0 of the dynamic vertex buffer holds P3F_COL4UB_TEX2F
//   vertices; text is a triangle list, 6 vertices per glyph, positions in pixels of the back buffer
//   (origin top left, z 1), texture coordinates in the font texture.
//   The D3D9 renderer draws it with an orthographic projection of the back buffer, no depth test or
//   depth writes, no culling, the vertex colour, alpha = texture alpha * vertex alpha, alpha test
//   (alpha > 0), alpha blending (SRCALPHA, INVSRCALPHA) and bilinear filtering without mip maps;
//   the arguments of FontSetBlending make no difference to it.

//////////////////////////////////////////////////////////////////////////
// Camera
//
// SetCamera: CCamera::GetVCMatrixD3D9() equals the matrix returned by GetModelViewMatrix. The
// projection returned by GetProjectionMatrix for ProjRatio 0.75, ZMin 0.25, ZMax 700:
//   [1.013 0 0 0] [0 1.35 0 0] [0 0 -1.000 -1] [0 0 -0.250 0]
// i.e. a right-handed D3D projection (m23 = -1) with m00 = m11 * ProjRatio. The D3D9 renderer draws
// the scene with m11 = 1 / tan(fov * ratio / 2) (CCamera's fov and ratio; ratio = height / width
// of the render surface: 0.75 at 1024 x 768, 0.559 in a 1270 x 710 window) and the first person
// weapon with a narrower one of its own ("Characters").

//////////////////////////////////////////////////////////////////////////
// Resolution change (Archive, windowed, the console commands r_Width and r_Height)
//
// At start-up the window gets the outer size r_Width x r_Height (1024 x 768: client area 1014 x 758)
// and the back buffer r_Width x r_Height; GetWidth and GetHeight return r_Width and r_Height, and
// ChangeViewport is not called.
// When r_Width or r_Height change the engine calls no IRenderer method for it (no
// ChangeResolution). The D3D9 renderer applies the new values itself within the next frame: it
// resets its device (the same device) with a back buffer of the new size and gives the window the
// outer size r_Width x r_Height; meanwhile CLeafBuffer::Unload is called on the leaf buffers with
// dynamic vertices ("Unknown", "ModelState", the water and fog circles, "LowResTerrain"), whose
// video buffers are made again when they are drawn. FarCry.exe then calls
// ChangeViewport(0, 0, client width, client height) (1270 x 710 for 1280 x 720, 1014 x 758 for
// 1024 x 768); from then on GetWidth and GetHeight return that size, the 3D viewport has it and the
// camera's ratio is its height / width (0.559). Both commands in one frame give one change. The
// engine reloads nothing (textures, shaders and leaf buffers stay).

//////////////////////////////////////////////////////////////////////////
// Lights
//
// The sun is added every frame with EF_ADDDlight: flags DLF_DIRECTIONAL | DLF_CASTSHADOW_MAPS |
// DLF_LIGHTSOURCE | DLF_SUN | DLF_COPY | DLF_LM | DLF_THIS_AREA_ONLY (0x6d0012), m_Origin far
// away (447, 8530, 10200 in Training), m_fRadius 1e8, debug name "Sun". EF_ADDDlight assigns
// m_Id (0 for the first light of the frame).
//
// Order in a frame (Steam): while the entities update, EF_UpdateDLight on the engine's own lights;
// at the start of the scene EF_StartEf, EF_ClearLightsList, EF_UpdateDLight on a copy of each
// light (DLF_COPY added), then for each copy EF_IsFakeDLight and EF_ADDDlight. EF_ADDDlight numbers
// the lights from 0, but a light with DLF_FAKE keeps m_Id -1, and EF_IsFakeDLight is true only for
// it (sun 0, a point light 1, a projector 2, a fake point light -1).
//
// Projected lights (Steam, a lamp hanging in a hut)
//   The light: DLF_PROJECT | DLF_HEATSOURCE | DLF_LIGHTSOURCE | DLF_THIS_AREA_ONLY |
//   DLF_IGNORE_TERRAIN | DLF_SPECULAR_ONLY_FOR_HIGHSPEC | DLF_LMDOT3 (0x1a418040), m_fRadius 7,
//   m_fLightFrustumAngle 60, m_ProjAngles (0, 90, 288) in degrees, changing a little every frame
//   (the lamp swings: x up to about 2, y up to 90.3), m_pShader and m_pLightImage set.
//   The image comes from EF_LoadTexture(name, 0, FT2_FORCECUBEMAP, eTT_Cubemap) (called several
//   times, the same ITexPic each time): a 256 x 256 picture becomes a 64 x 64 texture
//   (GetOriginalWidth 256) with the flags FT_HASMIPS | FT_DXT1 and the flags2 FT2_FORCECUBEMAP |
//   FT2_WASLOADED | FT2_CUBEASSINGLETEXTURE | FT2_WASFOUND. In Direct3D it is a DXT1 cube texture
//   of 64 texels with 7 levels (managed pool). The picture is on the +X face; the other five faces
//   are transparent black.
//   EF_UpdateDLight fills m_Orientation and m_TextureMatrix of a DLF_PROJECT light (of no other
//   light). The engine's light has the default basis of the CDLight constructor again at every
//   call, so a renderer computes both in every call:
//     R = Rz(m_ProjAngles.z) * Ry(m_ProjAngles.y) * Rx(m_ProjAngles.x) (degrees, right-handed
//     rotations about the world axes, column vectors);
//     m_vForward F = R (1,0,0), m_vUp U = R (0,1,0), m_vRight V = R (0,0,1) = F x U; the angles
//     (0, 90, 288) point it straight down;
//     m_TextureMatrix (Direct3D style, p * M) takes a world point p to the cube lookup vector
//     (F.d, s U.d, s V.d) with d = p - m_Origin and s = 1 / tan(m_fLightFrustumAngle): its
//     columns are F, s U, s V and (0, 0, 0, 1), its last row is -(F.o, s U.o, s V.o) for
//     o = m_Origin (frustum 15 gives s = 3.73, 45 gives 1, 60 gives 0.577, 90 gives 0).
//   So the picture covers a square pyramid around F whose sides are m_fLightFrustumAngle off the
//   axis. For F.d > 0 inside it, the +X face gives u = (1 - s V.d / F.d) / 2 and
//   v = (1 - s U.d / F.d) / 2, where u = 0 is the left column and v = 0 the top row of the picture
//   file. So the picture is cast transposed: seen along F with
//   V up (U to the left), its top row lies along the left edge and its left column along the top
//   edge; its columns run towards -V and its rows towards -U.
//   How the D3D9 renderer draws it: after the normal draws of each object the light reaches
//   (static objects and characters; 27 draws here), it draws the object's geometry once more:
//     depth test EQUAL, no depth writes, the same culling, fog on; blending ONE, ONE (SRCALPHA,
//     INVSRCALPHA for the alpha-blended materials);
//     stage 0: diffuse texture * lit vertex colour, alpha = texture alpha;
//     stage 1: cube texture * result * 2 (alpha texture * current); the texture coordinates are
//     the camera-space position (D3DTSS_TCI_CAMERASPACEPOSITION) with the texture transform
//     inverse(camera view) * m_TextureMatrix (COUNT3), the same for every object (the
//     camera's view matrix, not the view of the draw, which holds the object's matrix);
//     fixed function lighting with light 0 only: a point light at m_Origin with diffuse m_Color,
//     material diffuse 1, no ambient, and the attenuation and range a point light of that radius
//     gets on the object ("Static objects" above; for characters (0.01, 5.07756 / m_fRadius)).
//     Seen: (0.01, 0.30 to 0.73) on the objects around the lamp (range 50.4 to 121 * m_fRadius)
//     and (10, 0) on objects farther away than the radius.

//////////////////////////////////////////////////////////////////////////
// Flares and light coronas (the sun: Factory, Boat, Bunker, Training, Pier; a lamp in Bunker)
//
// Shaders whose GetREs hold one Flare render element (eDATA_Flare, no SDK class): the sun flares
// ("sun_evening" in Factory, Boat and Bunker with the colour (1, 0.70, 0.46); "flare_training" in
// Training and Pier with (0.5, 0.4, 0.3)), "crylight", and "flare_from_light", which is m_pShader of
// the lights with a corona (light entities, CDLight::m_fCoronaScale 1). The engine loads "CryLight"
// at start-up (EF_LoadShader with flags 0x20000000) and never hands it back to the renderer in any
// of the traces (no light, EF_AddEf or sprite uses it); it only has to load.
//
// The engine adds the sun's flare every frame in which the sun is near the view (not when it is
// behind the camera), whatever the settings: EF_AddEf(0, the Flare element, the shader, NULL, obj,
// -1, NULL, EFSLIST_DISTSORT). obj comes from EF_GetObject(false, ...) and is the same object every
// frame:
//   m_ObjFlags FOB_DRSUN | FOB_TRANS_TRANSLATE, an identity matrix with the translation
//   camera + sun direction * 0.9 * far plane (180 for a far plane of 200, 540 for 600);
//   m_SortId -1e6; m_TempVars[2] = 1 at every add (the renderer sets it back to 0 when it tests).
// A new object has m_Color (1, 1, 1, 1) and m_AmbColor (1, 1, 1).
// A light's corona is not added by the engine: the renderer handles it in EF_ADDDlight (below).
//
// The D3D9 renderer tests and draws flares and coronas only with r_Coronas 1 (0 in
// farcry1_testing1's settings); r_Flares, r_ProcFlares and r_checkSunVis change nothing here.
// The same steps for both, with h the half side of the drawn square:
//   the sun: h = 0.2 * r_CoronaSizeScale * distance (20 at 100, 36 at 180, 60 at 300 for 1;
//   18 at 180 for 0.5, 72 at 180 for 2);
//   a corona: h = 0.3 * m_fCoronaScale * r_CoronaSizeScale * distance^0.7 (0.245 at 0.75, 0.398
//   at 1.5, 1.05 at 6, 1.71 at 12, for 1 and 1), whatever the light's radius;
//   distance = from the camera to the flare's position (the sun's object, the light's m_Origin).
//   Visibility test. At the start of EF_EndEf3D, before the Clear of the frame (so against the
//   depth buffer of the frame before), it draws a white square around the flare's position,
//   facing the camera, with the side 0.2 * h (7.2 for the sun at 180): a D3DPT_TRIANGLEFAN of 2
//   triangles, P3F_COL4UB_TEX2F with the texture coordinates (0,0), (0,1), (1,1), (1,0), world
//   space (identity world matrix), the camera's view and projection, depth test LESSEQUAL, no
//   depth writes, no colour writes, fog on, a 16 x 16 DXT1 texture bound. It does this only while
//   the flare's centre projects into the viewport, and only when ITimer::GetCurrTime() is at
//   least r_CoronaFade / 8 seconds past the last test (r_CoronaFade 0, as in farcry1_testing1's
//   settings: every frame; 1: every 0.125 s). The result arrives 2 frames later as a sample: the
//   number of pixels that passed divided by the area of the projected square in pixels (about 1
//   when nothing covers it, a few percent either way; 0.5 when half of it is off the screen; 0
//   when hidden). The renderer reads back no surface for this and makes no ray tests in the 3D
//   engine or the physics.
//   State in the object. The renderer keeps the last 8 samples in the flare's object, oldest
//   first: m_AmbColor.x, .y, .z, m_TempVars[3], m_Angs2[0], [1], [2], m_TempVars[4] (each test
//   shifts them by one). m_Color is the flare's colour (the sun: the shader's flare; a corona: the
//   light's m_Color) with alpha = the mean of the 8 samples (it can exceed 1 a little); when the
//   mean is 0 the alpha keeps its last value. m_Trans2 is the projected position: screen x, y in
//   pixels and depth (0..1). m_fLightFadeTime is ITimer::GetCurrTime() of the last test, m_RE a
//   value of the renderer's own from the first test on, and FOB_HASALPHA is set in m_ObjFlags.
//   While the centre is off the screen the samples and the alpha stay as they are.
//   Draw. Nothing while the mean is 0. Otherwise the flare is drawn after everything else of the
//   3D scene (before the screen effects): a D3DPT_TRIANGLEFAN of 8 triangles, 10 vertices
//   (P3F_COL4UB_TEX2F, world space): the centre (texture coordinates 0.5, 0.5), then the corners
//   and edge midpoints of a square facing the camera, with the first of them repeated at the end.
//   The size does not depend on the alpha. With R and U the camera's right and up vectors, the
//   point with the texture coordinates (u, v) is centre + h * ((1 - 2v) R + (1 - 2u) U): (0,0) is
//   the top right corner on the screen, (0,1) the top left, (1,1) the bottom left, (1,0) the
//   bottom right; the order is (0,0), (0,0.5), (0,1), (0.5,1), (1,1), (1,0.5), (1,0), (0.5,0),
//   (0,0).
//   Vertex colour: rgb = the flare's colour * min(alpha, 1), a = min(alpha, 1), each converted to a
//   byte by truncation (alpha 0.9936 with (1, 0.70, 0.46) gives 0xfdfdb174, 1.0127 gives
//   0xffffb275; a lamp of (0.527, 0.313, 0.154) at alpha 0.9555 gives 0xf3804c25).
//   Texture: 128 x 128 X8R8G8B8 (the sun: one level; the corona: 8 levels). Stage 0 colour and
//   alpha = texture * diffuse; blending ONE, ONE; no depth test and no depth writes; no fog; no
//   culling; fixed function.
// Light coronas: the first time a light with m_fCoronaScale > 0 and m_pShader "flare_from_light"
// reaches EF_ADDDlight, the renderer makes an object for its corona and stores it in
// m_pObject[1][0]; the engine passes the same CDLight every frame, so the object stays.
// When the light drops out of the engine's list (or the list is rebuilt after a level load), the
// engine calls RemovePermanent() on that object before it copies another light into the entry
// (RendererStructures.h, CDLight). A removed object can come back in the same entry's
// m_pObject[1][0] (Bunker, after the load: another lamp with another colour), with m_RE NULL and
// the fade time 0: the renderer starts its test and fade anew. A light added without a corona
// keeps all m_pObject entries NULL. The engine adds lights with coronas only in the main view
// (EF_Query(EFQ_RecurseLevel) is 0 before EF_StartEf and 1 after it); the water reflection's
// frame adds only the sun.
// Its m_ObjFlags are 0x70a00000 (FOB_TRANS_ROTATE | FOB_TRANS_SCALE | FOB_TRANS_TRANSLATE |
// FOB_HASALPHA | FOB_BENDED); the corona is tested and drawn at m_Origin in every frame in which
// the light is added.
// The projected light on Steam (m_fCoronaScale 1) has instead a light beam shader as m_pShader
// ("LightBeamHangingYellow02": sort 14, flags 0x2000000, one element of type eDATA_Beam). The
// renderer stores an object for it in m_pObject[1][0] as well, with m_ObjFlags 0x70200000 (no
// FOB_HASALPHA) and m_pRE set; it makes no visibility test for it and draws no Beam element,
// with r_Beams 0 (farcry1_testing1's settings), with r_Beams 1 set before the level loads, and
// with r_Quality_BumpMapping 2 as well. The light beams that Steam does show are ordinary static
// objects of their own (material "s_testBeam", template TemplBeamProc, see the templates), placed
// apart from the lights.
// The renderer also calls I3DEngine::GetSunPosition and GetSunColor about once a frame.

//////////////////////////////////////////////////////////////////////////
// Textures
//
// Texture IDs start at 4096. A texture that is not found ('black.tga', 'Textures/fl') comes back
// as a placeholder ITexPic with ID 4096, IsTextureLoaded() == false and flags2 0x10000000.
// Loading an already loaded texture again returns the same ITexPic with FT2_WASFOUND set in its
// flags2.
//
// Files (Training). The renderer reads every file through ICryPak (ISystem::GetIPak): FOpen ("rb",
// "r" for its own text files), FRead, FSeek, FTell, FEof, FGets, FClose, and FindFirst, FindNext
// and GetModificationTime for its own shader folders. A texture is read inside the call that names
// it (EF_LoadTexture, LoadTexture, EF_LoadShaderItem, EF_LoadShader, LoadAnimatedTexture), not
// later and not on another thread.
// How a texture name becomes a file:
// - Back slashes become slashes, the name becomes lower case and its extension (from the last dot
//   of the file name on, so "a/00.dds, 24" gives "a/00") is replaced by ".dds", ".tga" and ".jpg",
//   in this order; the first file that opens is loaded, whatever extension the name had
//   ("x.tga" loads x.dds when it exists).
// - EF_LoadTexture and LoadTexture: a name without a folder is tried as "textures/<name>", then as
//   "<name>"; a name in another folder as "<name>", then as "textures/<name>"; a name in
//   "textures/" only as it is.
// - The textures of a material (EF_LoadShaderItem, SInputShaderResources::m_Textures): almost all
//   are found as they are named. The others (names with the folders of the machine the material
//   was made on) are found as m_TexturePath (the folder of the object) + the file name; on the way
//   the renderer also tries "textures/<name>" and m_TexturePath + the whole name.
// - A cube map, EF_LoadTexture with eTT_Cubemap or a material's EFTT_CUBEMAP texture named
//   "<base>_posx.<ext>", is six files <base>_posx, _negx, _posy, _negy, _posz and _negz. When
//   <base>_posx is not found (the light images), the file <base> itself is loaded as one picture
//   (FT2_CUBEASSINGLETEXTURE, see "Projected lights").
// - Animated material textures: "<a>##<first>_<last>(<t>)<b>.<ext>" (as many '#' as digits) is
//   the frames <a><n><b> for n from <first> to <last> (e.g. "w##01_04(.9).dds" is w01 to w04,
//   "c###001_011(1.00).dds" c001 to c011); the material's texture becomes the first frame. <t> is
//   0.02 to 1 in the levels seen; how it times the frames was not measured.
// The renderer fills EFTT_ATTENUATION2D (and EFTT_ATTENUATION1D) of every SRenderShaderResources
// with its own default attenuation textures.
//
// Animated textures of particles (LoadAnimatedTexture, Training)
//   LoadAnimatedTexture(format, nCount) returns the ID of an AnimTexInfo, 1, 2, ... in the order
//   they are made; for its particles the engine calls GetAnimTexInfoFromId(ID) right after. An
//   AnimTexInfo with the same sName and nFramesCount == nCount is returned again, with nRefCounter
//   one higher. Otherwise a new one is made: sName = format, nRefCounter 1, frame i (0, 1, ...) is the texture sprintf(format, i)
//   loaded as by EF_LoadTexture (frames already loaded, by other AnimTexInfos too, are shared), and
//   the loading stops at the first frame that is not found. nFramesCount is the number of frames
//   loaded and pBindIds holds their texture IDs. So nCount 103 for 24 files gives 24 frames, and
//   each further call with nCount 103 makes another AnimTexInfo. A format without '%' ("a/00.dds")
//   counts up the digits in front of the extension (00, 01, ...).
//   At a level change (Training, then "map River") the engine calls RemoveAnimatedTexture(pInfo)
//   once for every LoadAnimatedTexture whose AnimTexInfo it asked for (16 calls), each time with
//   the pointer GetAnimTexInfoFromId returned and with nRefCounter one lower than the time before
//   (down to 1 at the last call). The animated texture it never asked for (ID 1, loaded twice per
//   level) is not removed and stays; the next level's ones get the IDs from 2 again.
//
// The engine's services the renderer calls (Training; the renderer column of the call summary)
//   ISystem: GetIPak (about 28000 times), GetI3DEngine, GetITimer, GetIProfileSystem,
//   CheckLogVerbosity, GetCPUFlags, GetSecondsPerCycle, Warning, StartProfilerSection and
//   EndProfilerSection. ILog: Log, LogPlus, LogToFile, LogToFilePlus, LogWarning. ITimer:
//   GetCurrTime and GetFrameTime. ICryPak: as above. IStreamEngine, IPhysicalWorld and ICryFont are
//   not called, and neither is ISystem::GetViewCamera: the renderer gets everything about the
//   frame through IRenderer.

} // namespace Bin64Renderer
