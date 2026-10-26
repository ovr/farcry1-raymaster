# CcgDump - prints the contents of compiled character (.ccg) files. x86 only.

add_executable(CcgDump
    BoneLightBindInfo.cpp
    BoneLightBindInfo.h
    CcgAnimScript.cpp
    CcgAnimScript.h
    CcgBoneDescArray.h
    CcgBoneGeometry.cpp
    CcgBoneGeometry.h
    CcgDump.cpp
    CcgDump.h
    CcgGeometryInfo.cpp
    CcgGeometryInfo.h
    CcgMorphTargetSet.cpp
    CcgMorphTargetSet.h
    CcgPrintChunk.cpp
    CcgPrintChunk.h
    CcgUtils.h
    CgfUtils.cpp
    CgfUtils.h
    Controller.h
    CryAnimationInfo.h
    CryBoneDesc.cpp
    CryBoneDesc.h
    CryHeaders.h
    CrySkinBase.cpp
    CrySkinBase.h
    CrySkinBasisBuilder.cpp
    CrySkinBasisBuilder.h
    CrySkinBuilderBase.cpp
    CrySkinBuilderBase.h
    CrySkinFull.cpp
    CrySkinFull.h
    CrySkinMorph.cpp
    CrySkinMorph.h
    CrySkinMorphBuilder.cpp
    CrySkinMorphBuilder.h
    CrySkinRigidBasis.cpp
    CrySkinRigidBasis.h
    CrySkinTypes.h
    CryVertexBinding.cpp
    CryVertexBinding.h
    MathUtils.h
    QuaternionExponentX87.h
    SSEUtils.cpp
    SSEUtils.h
    StencilShadowConnectivity.h
    stdafx.cpp
    stdafx.h
)
set_target_properties(CcgDump PROPERTIES FOLDER "Tools")

target_link_libraries(CcgDump PRIVATE CryCommon)
# Standalone tool: use the CRT heap instead of the CrySystem.dll pool.
target_compile_definitions(CcgDump PRIVATE _CONSOLE DONT_USE_CRY_MEMORY_MANAGER)
target_precompile_headers(CcgDump PRIVATE stdafx.h)
target_compile_options(CcgDump PRIVATE /W3)
