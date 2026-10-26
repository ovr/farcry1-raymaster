# Engine interface headers shared by CryGame and the SDK tools. Header-only.

set(CRYCOMMON_RENDERER
    ColorDefs.h
    CRE2DQuad.h
    CREDummy.h
    CREFlashBang.h
    CREGlare.h
    CREOcclusionQuery.h
    CREOcLeaf.h
    CREPolyMesh.h
    CREScreenProcess.h
    CREShadowMap.h
    CRESky.h
    CRETerrainSector.h
    CRETriMeshAdditionalShadow.h
    CRETriMeshShadow.h
    Cry_Color4.h
    Font.h
    LeafBuffer.h
    LMCompStructures.h
    MakMatInfoFromMAT_ENTITY.h
    MeshIdx.h
    Names.h
    RendElement.h
    SerializeBuffer.h
    VertexBufferSource.h
    VertexFormats.h
)
source_group("Renderer" FILES ${CRYCOMMON_RENDERER})

set(CRYCOMMON_MATH
    AABBSV.h
    Cry_Camera.h
    Cry_Geo.h
    Cry_GeoDistance.h
    Cry_GeoIntersect.h
    Cry_GeoOverlap.h
    Cry_Math.h
    Cry_Matrix.h
    Cry_Quat.h
    Cry_Vector2.h
    Cry_Vector3.h
    Cry_XOptimise.h
)
source_group("Math" FILES ${CRYCOMMON_MATH})

set(CRYCOMMON_ANIMATION
    CryAnimationScriptCommands.h
    CryCharAnimationParams.h
    CryCharFxTrailParams.h
    CryCharMorphParams.h
    CryCompiledFile.h
    CryEngineDecalInfo.h
    CryParticleSpawnInfo.h
)
source_group("Animation" FILES ${CRYCOMMON_ANIMATION})

set(CRYCOMMON_INTERFACES
    I3DEngine.h
    IAgent.h
    IAISystem.h
    IBindable.h
    IBitStream.h
    ICompressionHelper.h
    IConsole.h
    ICryAnimation.h
    ICryPak.h
    IDataProbe.h
    IEdgeConnectivityBuilder.h
    IEntityRenderState.h
    IEntitySystem.h
    IFont.h
    IGame.h
    IInput.h
    ILipSync.h
    ILMSerializationManager.h
    ILog.h
    IMarkers.h
    IMiniLog.h
    IMovieSystem.h
    INetwork.h
    IPAddress.h
    IPhysics.h
    IProcess.h
    IRenderer.h
    IScriptSystem.h
    IShader.h
    ISound.h
    IStatObj.h
    IStreamEngine.h
    IStreamPersist.h
    ISystem.h
    ITimer.h
    IValidator.h
    IXGame.h
    IXml.h
    IXMLDOM.h
)
source_group("INTERFACES" FILES ${CRYCOMMON_INTERFACES})

set(CRYCOMMON_PHYSICS
    CryPhysics.h
    matrixnm.h
    physinterface.h
    primitives.h
)
source_group("Physics" FILES ${CRYCOMMON_PHYSICS})

set(CRYCOMMON_SYSTEM
    CrtDebugStats.h
    CryFile.h
    CryHeaders.h
    CryLibrary.h
    CryMemoryManager.h
    CrySizer.h
    CryVersion.h
    FrameProfiler.h
    Linux32Specific.h
    Linux64Specific.h
    LinuxSpecific.h
    platform.h
    ProjectDefines.h
    StlDbgAlloc.h
    Stream.h
    StreamData.h
    Win32specific.h
    Win64specific.h
    XboxSpecific.h
)
source_group("System" FILES ${CRYCOMMON_SYSTEM})

set(CRYCOMMON_SOUND
    crysound.h
    crysound64.h
)
source_group("Sound" FILES ${CRYCOMMON_SOUND})

set(CRYCOMMON_UTILS
    list2.h
    Range.h
    smartptr.h
    StlUtils.h
    StringUtils.h
    TAlloc.h
    Tarray.h
    TArrays.h
    TimeValue.h
    TString.h
    XmlParser.h
    XmlParserFont.h
    XmlStream.h
)
source_group("Utils" FILES ${CRYCOMMON_UTILS})

set(CRYCOMMON_SCRIPT
    _ScriptableEx.h
    _TinyWindow.h
    ScriptObjectVector.h
)
source_group("Script" FILES ${CRYCOMMON_SCRIPT})

set(CRYCOMMON_MISC
    AgentParams.h
    AnimKey.h
    EntityDesc.h
)
source_group("Misc" FILES ${CRYCOMMON_MISC})

add_library(CryCommon INTERFACE
    ${CRYCOMMON_RENDERER}
    ${CRYCOMMON_MATH}
    ${CRYCOMMON_ANIMATION}
    ${CRYCOMMON_INTERFACES}
    ${CRYCOMMON_PHYSICS}
    ${CRYCOMMON_SYSTEM}
    ${CRYCOMMON_SOUND}
    ${CRYCOMMON_UTILS}
    ${CRYCOMMON_SCRIPT}
    ${CRYCOMMON_MISC}
)
set_target_properties(CryCommon PROPERTIES FOLDER "SDK")

target_include_directories(CryCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(CryCommon INTERFACE
    _MBCS
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
    _WINSOCK_DEPRECATED_NO_WARNINGS
    _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
)

if(FARCRY_X64)
    # CMake always passes /DWIN32; WIN64 is what selects the 64-bit code paths.
    target_compile_definitions(CryCommon INTERFACE WIN64 _AMD64_)
    # fastsincosf() used by Cry_Math.h on WIN64.
    target_link_libraries(CryCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/fSinCos64.lib")
else()
    # The engine was built with VC7.1, where time_t was 32 bits wide. ICryPak::FindFirst/FindNext
    # exchange _finddata_t with the engine, so its layout must match.
    target_compile_definitions(CryCommon INTERFACE _USE_32BIT_TIME_T)
endif()
