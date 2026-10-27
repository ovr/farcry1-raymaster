//////////////////////////////////////////////////////////////////////
//
//	File: RendererStructures.h
//	Description: The structures shared by the engine and the renderer: who
//	creates them, who owns them and which members each side writes.
//
//	The layouts are those of the SDK headers: StructLayouts.txt lists the
//	members with their offsets and sizes on x86 and x64, StructFields.inl
//	the same as a table for code (both generated from the compiler's class
//	layout report of the SDK headers).
//	The engine accesses most of these structures directly (members and
//	inline methods), so a renderer has to declare them exactly as the SDK
//	does. Retail x86 sizes are in RetailRenderer.h.
//
//	The write protocol below is runtime behaviour of the 64-bit engine in
//	farcry1_testing1/Bin64 (AGENTS.md), recorded by XRenderD3D9Traced with
//	XRENDERTRACED_WATCH=1 on the Training level (level load and 1100
//	frames). "Engine" means code that runs outside the IRenderer and
//	CLeafBuffer calls (including calls the engine makes through the vtables
//	of renderer objects, such as CRendElement::Release); "renderer" means
//	code that runs inside them. A member that is not listed was not seen
//	being written by that side.
//
//////////////////////////////////////////////////////////////////////

#pragma once

namespace RendererStructures
{

//////////////////////////////////////////////////////////////////////////
// Sizes
//
// x64 sizes of the SDK classes as the 64-bit engine allocates them (Bin64, runtime), equal to
// sizeof() of the SDK headers built for x64:
//   CLeafBuffer 0x130, CRendElement 0x70 (also CRECommon and the classes of CRETerrainSector.h),
//   CRESky 0x290, CRE2DQuad 0xC0, CREScreenProcess 0x78, CREOcclusionQuery 0xA0.

//////////////////////////////////////////////////////////////////////////
// Objects the renderer creates and the engine accesses directly

// CLeafBuffer
//   Created by IRenderer::CreateLeafBuffer / CreateLeafBufferInitialized, deleted by
//   DeleteLeafBuffer. All methods are virtual and run in the renderer; the engine also reads and
//   writes members directly.
//   Engine writes:
//   - m_pMats. For "StatObj" buffers the engine allocates the material list and assigns it after
//     CreateLeafBuffer and before CreateBuffer(CIndexedMesh *), and sets it to NULL before
//     DeleteLeafBuffer: the list belongs to the engine. For CreateBuffer(VertexBufferSource *) the
//     renderer takes the list over (see VertexBufferSource).
//   - m_vBoxMin, m_vBoxMax of the "WaterVolume" buffers (made with CreateLeafBufferInitialized),
//     every frame: both hold the camera position, not the bounds.
//   - m_pVertexContainer of the "Dynamic" buffers (SetVertexContainer, inline in LeafBuffer.h): the
//     terrain sector whose vertices they use (Bin64Behaviour.h, "Terrain under a dynamic light").
//   Renderer writes:
//   - CreateBuffer(CIndexedMesh *): m_nVertexFormat, m_nPrimetiveType, m_pSecVertBuffer,
//     m_SecVertCount, m_pVertexBuffer, m_SecIndices, m_NumIndices, m_Indices,
//     m_pIndicesPreStrip, m_arrVertStripMap, m_TempColors, m_TempNormals, m_pLoadedColors,
//     m_fMinU, m_fMaxU, m_fMinV, m_fMaxV, m_vBoxMin, m_vBoxMax, m_UpdateVBufferMask,
//     m_UpdateFrame; the per-material ranges in m_pMats (see CMatInfo).
//   - CreateBuffer(VertexBufferSource *): m_pMats, m_NumIndices, m_SecIndices, m_SecVertCount,
//     m_vBoxMin, m_vBoxMax.
//   - CopyTo (character instances): the copy's m_pMats, m_pVertexBuffer, m_Indices, m_SecIndices,
//     m_NumIndices, m_SecVertCount, m_nVertexFormat, the flag bits.
//   - Every frame in EF_EndEf3D: m_Next / m_Prev (the renderer's list of buffers drawn in the
//     frame) and, when CheckUpdate creates or changes the video buffer, m_pVertexBuffer,
//     m_nVertexFormat, m_TempNormals, m_UpdateFrame.
//   - m_NextGlobal / m_PrevGlobal: the renderer's list of all leaf buffers (creation, deletion).
//   - UpdateCustomLighting and MakeSprite: m_pVertexBuffer.

// CMatInfo (the entries of CLeafBuffer::m_pMats)
//   Renderer writes in CreateBuffer: nFirstIndexId, nNumIndices, nFirstVertId, nNumVerts,
//   m_vCenter, m_fRadius and pRE (a CREOcLeaf per material with faces); m_dwNumSections and
//   m_pPrimitiveGroups for stripified buffers.
//   Engine writes: before DeleteLeafBuffer of a buffer with an engine-owned list it releases every
//   pRE (CRendElement::Release) and sets it to NULL; m_vCenter and m_fRadius of the water
//   volumes.

// CVertexBuffer
//   Created by IRenderer::CreateBuffer and by the leaf buffer (m_pVertexBuffer, m_pSecVertBuffer);
//   released by ReleaseBuffer. The renderer changes m_vertexformat and m_VS when CheckUpdate
//   re-creates a video buffer in another vertex format (during EF_EndEf3D).

// CCObject
//   Taken from the renderer's pool with EF_GetObject(true, -1); a temporary object is valid for
//   the current frame only (the pool is reused every frame).
//   Engine writes before CLeafBuffer::Render: m_Matrix, m_ObjFlags, m_SortId, m_AmbColor,
//   m_DynLMMask, m_fHeatFactor, m_Color, m_nLMId and m_pLMTCBufferO (lightmapped objects), the
//   union members m_nLod, m_nTemplId and m_pLightImage, m_ShaderParams.
//   Engine writes before CLeafBuffer::AddRenderElements: m_Matrix, m_ObjFlags, m_Color,
//   m_ShaderParams, the scissor rectangle m_nScissorX1..m_nScissorY2.
//   With shadow maps on, the engine also sets m_pShadowCasters (the casters whose shadow maps fall
//   on the object) before Render, AddRE and EF_AddEf; see "Shadow maps" below.
//   Engine writes for characters before EF_AddEf (most of it already before the EF_GetHeatVision
//   call in between): m_Matrix, m_ObjFlags, m_SortId, m_AmbColor, m_Color, m_DynLMMask,
//   m_fHeatFactor, m_ShaderParams, m_pCharInstance, m_nTemplId, the scissor rectangle.
//   Engine writes for particles before EF_AddSpriteToScene: m_Matrix (identity), m_SortId,
//   m_AmbColor, m_DynLMMask, m_NumCM (the particle texture's ID) and m_RenderState (GS_* blend and
//   alpha test bits from the particle's blend type); see Bin64Behaviour.h, "Particles".
//   Renderer writes:
//   - CLeafBuffer::Render and AddRenderElements: m_ObjFlags (can set FOB_HASALPHA); EF_AddEf:
//     m_ObjFlags (can clear FOB_ENVLIGHTING).
//   - EF_EndEf3D: m_VPMatrixId, m_InvMatrixId, m_VPMatrixFrame (matrices the renderer derives
//     from the object; the IDs are 0xFFFF before).
//   - MakeSprite: m_Counter, m_AmbColor, m_fHeatFactor.
//   CCObject has a vtable (AddWaves, SetShaderFloat, RemovePermanent; the SDK declares no virtual
//   destructor). The engine calls AddWaves once per frame for every vegetation instance it draws
//   with bending;
//   afterwards m_NumWFX and m_NumWFY (unions with m_nLod and m_nTemplId) hold two new indices into
//   the renderer's table of waves (CCObject::m_Waves, a static member only the renderer can see),
//   counted from 1 every frame (1/2, 3/4, ...). wf is an output: the renderer stores the two waves'
//   addresses in wf[0] and wf[1], and the engine fills the waves after the call (see
//   Bin64Behaviour.h, "Static objects"). RemovePermanent is called on permanent objects
//   (EF_GetObject(false, n)).
//   Shader inputs taken from the object (SDK "Shaders Specification"): ObjColor m_Color, ObjVal
//   m_TempVars, Bending m_fBending (a union with m_pLightImage), ObjWaveX/ObjWaveY the waves
//   m_NumWFX/m_NumWFY, ObjRefrFactor m_fRefract, BumpAmount m_fBump. In Training only the
//   vegetation sets them: m_fBending between 0 and 1.4 and the wave indices; m_TempVars, m_fRefract
//   and m_fBump stay 0.

// CRendElement and the CRE* classes
//   Created by EF_CreateRE (seen: Sky, Dummy, 2DQuad, ScreenProcess, OcclusionQuery, ShadowMapGen,
//   FarTreeSprites, TerrainParticles, TerrainDetailTextureLayers, ClearStencil) and by the leaf
//   buffer (a CREOcLeaf per material). Released with CRendElement::Release.
//   Renderer writes: m_NextGlobal / m_PrevGlobal (list of all render elements, on creation and
//   release); m_LastVP while drawing (EF_EndEf3D).
//   CREOcLeaf: m_pChunk is its CMatInfo entry, m_pBuffer its leaf buffer. m_pChunk changes only
//   when the engine releases the elements of a buffer before DeleteLeafBuffer: CRendElement::Release
//   (renderer code the engine calls through the vtable) destroys the element, and the destructor
//   in RendElement.h unlinks it from the global list, which also changes the neighbours'
//   m_NextGlobal / m_PrevGlobal. The engine does not write m_pChunk itself.
//   CREOcclusionQuery: the engine writes m_vBoxMin, m_vBoxMax before EF_AddEf; the renderer
//   writes m_nOcclusionID, m_nCheckFrame, m_nDrawFrame and the result m_nVisSamples in EF_EndEf3D.
//   CRESky: the engine writes m_fTerrainWaterLevel; the renderer allocates m_parrFogLayer and
//   m_parrFogLayer2 when it first draws the sky.
//   CRETriMeshShadow (stencil shadow volumes; off in farcry1_testing1's settings, seen with
//   e_stencil_shadows 1): created by the engine with EF_CreateRE. The engine sets m_pSvObj (its
//   ItShadowVolume) and m_nCurrInst = -1 before EF_AddEf, m_bAnimatedObject for characters, and
//   itself fills m_arrLBuffers and m_nRendIndices of some of them. The renderer fills the
//   m_arrLBuffers slots and m_nCurrInst in EF_EndEf3D, creating the volumes' leaf buffers there
//   with CreateLeafBufferInitialized. The SDK interfaces cover all of it.
//   CRECommon classes (CRETerrainSector.h): drawing them calls back into the 3D engine
//   (I3DEngine::DrawFarTrees, DrawTerrainDetailTextureLayers, DrawTerrainParticles), which calls
//   IRenderer methods from inside EF_EndEf3D (DrawObjSprites for the far trees). The
//   CRETerrainDetailTextureLayers element was never added (see "Virtual functions of the engine
//   the renderer calls" below).

// Virtual functions the engine calls
//   Besides IRenderer and CLeafBuffer, the engine calls these virtual functions of the objects the
//   renderer gives it (slots in VTables.inl; Bin64, Training: level load and 160 frames):
//   - IShader: AddRef, Release, GetName, GetSort, GetFlags, GetFlags2, GetLFlags, GetREs,
//     GetTemplate, GetPublicParams. GetTemplate and GetFlags2 are hot (about 180000 calls each
//     in that run, from Cry3DEngine and CryAnimation): SShaderItem::IsTransparent (IShader.h) calls
//     both for every material drawn. With particles on screen also GetID (for the shader ID
//     EF_AddSpriteToScene takes). The values they return: Bin64Behaviour.h, "Shaders".
//   - ITexPic: AddRef, Release, GetWidth, GetHeight, GetOriginalWidth, GetOriginalHeight,
//     GetTextureID, GetFlags, IsTextureLoaded (from Cry3DEngine, CrySystem, CryGame and
//     CryEntitySystem); with decals on also SetFilter and SetClamp (Bin64Behaviour.h, "Decals").
//   - SRenderShaderResources: Release, its only virtual function.
//   - CRendElement: Release (of the CREOcLeaf of the leaf buffer materials).
//   - CREScreenProcess: mfSetParameter, mfGetParameter (see "Screen effects" below).
//   - CCObject: AddWaves, RemovePermanent (see CCObject above); SetShaderFloat is not called.
//   CMatInfo's virtual functions are inline in IShader.h, so every module has its own copy.
//   All other virtual functions of these classes (mfPrepare, mfDraw, mfPreDraw, mfCheckUpdate,
//   mfGetMatInfo, mfReset, ...) were only called by the renderer itself, from inside EF_EndEf3D.
//   A renderer still has to fill every slot in the SDK order.

// Virtual functions of the engine the renderer calls (I3DEngine, the one ISystem::GetI3DEngine
// returns; slots in VTables.inl; Bin64, runtime: Training, River and Pier)
//   - While a level loads: GetSunColor in CLeafBuffer::UpdateCustomLighting, GetSunColor and
//     GetSunPosition in MakeSprite (once per call each).
//   - In EF_EndEf3D: GetMaxViewDist (in most frames on River, some on Training); DrawFarTrees for
//     the CREFarTreeSprites element, from inside which the engine calls DrawObjSprites back, and in
//     DrawObjSprites the renderer calls GetWorldColor once; DrawTerrainParticles(pShader) for the
//     CRETerrainParticles element (e_decals 1); GetWaterLevel() without arguments in some frames
//     on Training and Pier.
//   - For the water reflection (r_WaterReflections 1): GetDistanceToSectorWithWater,
//     GetWaterLevel(), GetMaxViewDistance, SetCamera(cam, false) and DrawLowDetail(DLD_TERRAIN |
//     DLD_TERRAIN_FULLRES), inside which the engine renders a frame of its own through IRenderer
//     (Bin64Behaviour.h, "Water reflections and refractions").
//   DrawTerrainDetailTextureLayers was never called: with e_detail_texture_quality 1 the engine
//   draws the detail layers as leaf buffers (Bin64Behaviour.h, "Terrain detail layers"). Nothing
//   else of I3DEngine was called by the renderer.

// Screen effects (CREScreenProcess)
//   The 3D engine creates one CREScreenProcess with EF_CreateRE(eDATA_ScreenProcess) and turns
//   I3DEngine::SetScreenFx, SetScreenFxParam, GetScreenFx and GetScreenFxParam (effect and
//   parameter names in the SDK's CScriptObjectSystem reference) into calls of
//   mfSetParameter(iProcess, iParam, pValue) and mfGetParameter(iProcess, iParam) with the codes
//   below (Bin64, runtime). pValue points to the engine's copy of the value, valid during the call;
//   mfGetParameter returns a pointer to the renderer's copy, which the engine reads right away.
//   mfSetParameter returned 0 for every call; mfActivate was never called.
//   The Active parameter is a bool (one byte; the bytes after it are not part of the value), the
//   others are floats.
static const int nBin64ScreenProcess_ScreenFade = 0;
static const int nBin64ScreenProcess_ScreenBlur = 1;
static const int nBin64ScreenProcess_NightVision = 6;
static const int nBin64ScreenProcess_FlashBang = 8;
static const int nBin64ScreenParam_Active = 30;                 // every process
static const int nBin64ScreenParam_ScreenFadeTime = 28;
static const int nBin64ScreenParam_ScreenPreFadeTime = 29;
static const int nBin64ScreenParam_ScreenBlurAmount = 1;
static const int nBin64ScreenParam_ScreenBlurColorRed = 2;
static const int nBin64ScreenParam_ScreenBlurColorGreen = 3;
static const int nBin64ScreenParam_ScreenBlurColorBlue = 4;
static const int nBin64ScreenParam_FlashBangTimeScale = 19;
static const int nBin64ScreenParam_FlashBangFlashPosX = 20;
static const int nBin64ScreenParam_FlashBangFlashPosY = 21;
static const int nBin64ScreenParam_FlashBangFlashSizeX = 22;
static const int nBin64ScreenParam_FlashBangFlashSizeY = 23;
static const int nBin64ScreenParam_FlashBangCurrent = 24;       // written by the renderer, see below
static const int nBin64ScreenParam_FlashBangForce = 25;
//   No other process codes were used; the SDK names only these four effects.
//   How the effects are drawn: Bin64Behaviour.h, "Screen effects" and "Heat vision and night
//   vision".
//   The engine reads NightVision's Active every frame. When a level starts it reads FlashBang's
//   Active once and, if it is set, parameters 19, 24 and 20 to 23.
//   FlashBang: parameter 24 is written only by the renderer. It is the part of the flash still to
//   come: 1 when FlashBang is switched on, then every frame (the first one included) it falls by
//   ITimer::GetFrameTime() / TimeScale. When it would drop to 0 or below, the renderer sets it back
//   to 1 and clears Active by itself. The flash thus lasts TimeScale seconds of game time
//   (TimeScale 2: twice as long). Force (25) changed nothing in the frame. The defaults read back
//   after FlashBang 1 alone: TimeScale 1, FlashPosX 200, FlashPosY 100, FlashSizeX 400, FlashSizeY
//   400.
//   At the start of a level the engine sets ScreenFade Active 1, ScreenFadeTime -2.5 and
//   ScreenPreFadeTime 5 (CryGame's XClient.cpp; a negative time fades in), then ScreenBlur Active 0
//   and ScreenBlurAmount 0. The fade in lasts |ScreenFadeTime| seconds of game time and starts about
//   0.09 s after Active is set. At its end the renderer clears Active and sets ScreenPreFadeTime
//   to 0.

// Lightmaps (Training: the static objects of the level with baked lighting)
//   The 3D engine loads the lightmap textures itself during the level load, with
//   EF_LoadTexture("Levels/<level>\xN.dds", FT_LM, FT2_NOANISO) (N from 0; 512x512 on Training),
//   and keeps their IDs in one RenderLMData per lightmap (LMCompStructures.h; created, owned and
//   reference counted by the engine). Only m_iColorLerpTex was set; the HDR, dominant direction
//   and occlusion IDs were 0.
//   For every lightmapped object instance the engine creates a buffer of lightmap texture
//   coordinates: CreateLeafBufferInitialized(pUVs, n, VERTEX_FORMAT_TEX2F, NULL, 0,
//   R_PRIMV_TRIANGLES, "LMapTexCoords", eBT_Static, 1) and SetChunk("default" shader, vertices and
//   indices 0..n, material 0). n is the vertex count of the object's leaf buffer (always
//   VERTEX_FORMAT_P3F_N_TEX2F here): one UV per vertex, in the order of the object's vertices.
//   When it draws the object, CLeafBuffer::Render gets SRendParams::pLightMapInfo (the
//   RenderLMData) and pLMTCBuffer (the "LMapTexCoords" buffer), and the engine has set
//   CCObject::m_nLMId (the colour texture's ID) and m_pLMTCBufferO (the same buffer);
//   m_nLMDirId and m_nOcclId stay 0. No object flag marks a lightmapped object.

// SShaderItem, SRenderShaderResources
//   From EF_LoadShaderItem; the shader resources are reference counted (m_nRefCounter).
//   Renderer: +1 per EF_LoadShaderItem that returns them, +1 per CLeafBuffer::CopyTo.
//   Engine: AddRef / Release (inline in IShader.h); before DeleteLeafBuffer it releases the
//   resources of the materials of an engine-owned list. The last Release deletes them.

//////////////////////////////////////////////////////////////////////////
// Structures the engine owns and passes to the renderer

// Read only (no writes by the renderer seen): SRendParams, CCamera, CIndexedMesh.
//
// CDLight: EF_ADDDlight assigns m_Id (the light's index in the frame, from 0); the renderer
//   stores its corona / flare objects in m_pObject.
// SInputShaderResources: EF_LoadShaderItem sets m_LMaterial when it is NULL (to a light material
//   owned by the renderer).
// VertexBufferSource: CreateBuffer(VertexBufferSource *) moves pMats into the leaf buffer and
//   sets pMats to NULL.

//////////////////////////////////////////////////////////////////////////
// Types the SDK only declares
//
// Owned by the renderer; the engine only holds pointers, so a renderer may define them freely:
//   CScreenVars (CREScreenProcess::m_pVars), STexPic (behind ITexPic), CVProgram
//   (CRendElement::m_LastVP), CPShader, SParamComp, SCGBind, SVrect.
// Never passed: CFBitmap (the CFBitmap overloads of the Font* functions are not called).
//
// Owned by the 3D engine and read by the renderer: the far vegetation sprites and the shadow maps.
// What is known of them is runtime behaviour of the 64-bit engine (Bin64); the offsets are those
// of Bin64 and say nothing about the retail x86 layout.

// Allocation sizes (Bin64).
static const int nBin64Size_CStatObjInst = 0x90;
static const int nBin64Size_CObjManager = 0x328;
static const int nBin64Size_CScreenVars = 0x288;
static const int nBin64Size_ShadowMapLightSource = 0x38;
static const int nBin64Size_ShadowMapLightSourceInstance = 0x30;  // element of a list2
static const int nBin64Size_ShadowMapFrustum = 0x148;             // element of a list2

// Far vegetation sprites
//   The renderer makes the sprite textures: during the level load the engine calls
//   MakeSprite(fScale, nTexSize, fAngle, pStatObj, NULL, 0) 24 times per vegetation object, with
//   fAngle from 90 to 435 degrees in steps of 15, and keeps the returned texture IDs. A renderer can
//   therefore look the sprites up by IStatObj and angle itself. pStatObj is the object's LOD 1
//   (IStatObj::GetLodObject(1)) when it has one and the object itself otherwise; fScale was 18 and
//   nTexSize 64 in every call, also for groups with nSpriteTexRes 128.
//   DrawObjSprites(pList, fMaxViewDist, pObjMan) is called every frame from inside EF_EndEf3D (see
//   CRECommon above) with the instances to draw as sprites. fMaxViewDist equals the camera's far
//   plane (350 to 1500 in the levels seen). The members of CStatObjInst that are enough to draw one
//   (Bin64 offsets; checked on 2962 instances of 62 vegetation groups):
static const int nBin64Offset_StatObjInst_MaxViewDist = 0x08;  // float: radius * group fMaxViewDistRatio * e_obj_view_dist_ratio (55)
static const int nBin64Offset_StatObjInst_Distance = 0x18;     // float: horizontal distance from the camera to the box centre
static const int nBin64Offset_StatObjInst_BoxMin = 0x2C;       // Vec3, world space
static const int nBin64Offset_StatObjInst_BoxMax = 0x38;       // Vec3, world space
static const int nBin64Offset_StatObjInst_Radius = 0x44;       // float: IStatObj::GetRadius() of the group * scale
static const int nBin64Offset_StatObjInst_Pos = 0x68;          // Vec3, world space: the object's origin
static const int nBin64Offset_StatObjInst_Scale = 0x74;        // float
static const int nBin64Offset_StatObjInst_Group = 0x88;        // byte: vegetation group ID
static const int nBin64Offset_StatObjInst_Brightness = 0x89;   // byte: 100 when fully lit, lower in shadow
//   The box is the object's box times the scale, moved to Pos. The group ID gives the IStatObj and
//   the group settings (fBrightness, fSpriteDistRatio, fMaxViewDistRatio, bUseSprites) through
//   I3DEngine::GetStatInstGroup. The instance starts with a vtable; it also holds a frame ID
//   (GetFrameID values) at 0x10 and pointers to other engine objects.
//   GetStatInstGroup does not fill in bFadeSize and m_dwRndFlags (they keep what the caller's
//   structure held), but the renderer needs bFadeSize (see Bin64Behaviour.h). CObjManager starts
//   with the engine's array of groups, which have the layout of IStatInstGroup:
static const int nBin64Offset_ObjManager_Groups = 0x00;        // pointer to the groups, indexed by group ID
static const int nBin64Offset_ObjManager_GroupCount = 0x08;    // int: 1024
static const int nBin64Size_StatInstGroup = 0x48;              // sizeof(IStatInstGroup) on x64
static const int nBin64Offset_StatInstGroup_FadeSize = 0x32;   // bool bFadeSize

// Shadow maps (off in farcry1_testing1's settings; seen with e_shadow_maps 1 on Training)
//   MakeShadowMapFrustum(pFrustum, pLightSource, vObjPos, pStatObjects, 0): the engine passes a
//   zeroed frustum it owns (a local variable) and gets the same pointer back. The renderer fills
//   in (Bin64 offsets): the light source pointer (0x88), the field of view (0x94, float), the near
//   and far plane (0x98, 0x9C: the distance from the light to vObjPos minus and plus about the
//   object's radius) and vObjPos (0xA0, Vec3). The engine keeps the frustums in a list2<ShadowMapFrustum> of the light
//   source.
//   ShadowMapLightSource: the light's position in the space of vObjPos (0x00, Vec3), a float
//     500000 (0x18) and the list2 of frustums (0x20).
//   ShadowMapLightSourceInstance (the elements of CCObject::m_pShadowCasters and
//     SRendParams::pShadowMapCasters): the light source (0x00, pointer), the caster's world
//     position (0x08, Vec3) and a float scale (0x14), sometimes an entity pointer (0x20).
//   When it first draws a frustum's shadow map (in EF_EndEf3D) the renderer writes into it: the
//     projection matrix (0x00, 4x4 floats: a right-handed perspective projection from the field of
//     view at 0x94 in degrees, aspect 1, and the near and far plane), the light's view matrix
//     (0x40, 4x4 floats: looking from the light's position at vObjPos, rows as in D3D with the
//     translation in the last row), the ID of the depth texture (0x80, int) and 1 into the ints at
//     0xD0 (-1 before) and 0xD4 (0 before). For the static caster seen, nothing changed in the
//     frames after that. Not written by the renderer: 0xB0 (-1), 0xB4 (512), 0xC8 (a pointer to an
//     engine object) and 0xE8 (a pointer to a list2 of pointers).
//   The receivers seen are "EntityShadowOnTerrain" leaf buffers (shader "terrainshadowpass") added
//   with AddRE and a CCObject whose m_pShadowCasters lists the casters.

} // namespace RendererStructures
