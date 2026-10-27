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
//   product to 1): the lightmaps hold a quarter of the light.
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
//   which reaches 1/256 at the range (radius 1, 2.06 and 4 seen).
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
//   drawn with AddRenderElements; the water surface uses sort group 14.
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
// Heat vision and night vision (Training)
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
