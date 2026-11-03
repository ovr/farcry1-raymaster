# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# Please do not use this file to train or fine-tune AI models (see README.md).
# XRenderD3D12Tests - the unit tests of XRenderD3D12 that run without the game (ctest): its shader code on a Direct3D 12
# device of their own (GpuHarness.h; a hardware adapter, WARP without one) and the structures C++ shares with the shaders.
#
# A console program of its own: it is not deployed (it stays in the build's bin folder, XRENDER_OUTPUT_DIR is not
# touched) and does not depend on the XRenderD3D12 target, so building it neither builds nor redeploys the renderer's
# DLL. Its kernels (Shaders/Test*.hlsl, which include the renderer's shader headers and PathTrace.hlsl as they are) and
# the renderer's own CSCacheResolve and CMAA2 kernels are compiled here by the same dxc with the same options as Shaders.cmake's, into
# headers of this target's build folder (Shaders/Test_<name>.h, array g_Test_<name>).
#   cmake --build --preset x64-release --target XRenderD3D12Tests
#   ctest --preset x64-release      (or bin/Release/XRenderD3D12Tests.exe [--warp] [--debug] [name ...])

set(XRENDERD3D12_TESTS_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(XRENDERD3D12_TESTS_SHADER_HEADERS "")
set(XRENDERD3D12_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shaders")

#! xrender_d3d12_test_shader(<name> <file> <profile> <entry> [defines...]): compiles one kernel into Shaders/Test_<name>.h.
function(xrender_d3d12_test_shader name file profile entry)
    set(defines "")
    foreach(d ${ARGN})
        list(APPEND defines "-D" "${d}")
    endforeach()
    set(out "${XRENDERD3D12_TESTS_SHADER_DIR}/Test_${name}.h")
    file(GLOB includes "${XRENDERD3D12_SHADER_SOURCE_DIR}/*.hlsl" "${XRENDERD3D12_SHADER_SOURCE_DIR}/*.hlsli"
        "${XRENDERD3D12_SHADER_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli" "${XRENDERD3D12_XEGTAO_DIR}/*.hlsli"
        "${XRENDERD3D12_XEGTAO_DIR}/*.h" "${XRENDERD3D12_SMAA_DIR}/*.hlsl" "${XRENDERD3D12_CMAA2_DIR}/*.hlsl"
        "${XRENDERD3D12_CAS_DIR}/*.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDERD3D12_TESTS_SHADER_DIR}"
        COMMAND "${XRENDERD3D12_DXC}" -nologo -T ${profile} -E ${entry} -Zpr -O3 -Qstrip_reflect
                -I "${XRENDERD3D12_SHADER_SOURCE_DIR}" -I "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" -I "${XRENDERD3D12_XEGTAO_DIR}"
                -I "${XRENDERD3D12_SMAA_DIR}" -I "${XRENDERD3D12_CMAA2_DIR}" -I "${XRENDERD3D12_CAS_DIR}" ${defines} -Vn "g_Test_${name}" -Fh "${out}" "${file}"
        MAIN_DEPENDENCY "${file}"
        DEPENDS ${includes}
        COMMENT "dxc (tests) ${entry} -> Test_${name}.h"
        VERBATIM
    )
    set(XRENDERD3D12_TESTS_SHADER_HEADERS ${XRENDERD3D12_TESTS_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()

# The headers that stand alone (Shader Model 6.0).
set(headers "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestHeaders.hlsl")
foreach(k Oct GBufferNormal Hdr HdrStats ToneMap HdrOutput SkyGlow FrameAverage Exposure CacheCell CacheLookup CacheAdd CacheRead CachePoke
          LightmapLight RenderMode Fxaa ArrayCoords BlendPictures PictureNoise WaterGlint WaterCap)
    xrender_d3d12_test_shader(${k} "${headers}" cs_6_0 CS${k})
endforeach()
# SMAA 1x (Smaa.hlsli with third_party/smaa's SMAA.hlsl, SmaaBlend.hlsli): its edges and its blending at the high
# preset, its weights at the low, high and ultra presets.
set(smaa "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestSmaa.hlsl")
xrender_d3d12_test_shader(SmaaEdges "${smaa}" cs_6_0 CSSmaaEdges SMAA_PRESET_HIGH)
xrender_d3d12_test_shader(SmaaBlend "${smaa}" cs_6_0 CSSmaaBlend SMAA_PRESET_HIGH)
foreach(q Low High Ultra)
    string(TOUPPER "${q}" preset)
    xrender_d3d12_test_shader(SmaaWeights${q} "${smaa}" cs_6_0 CSSmaaWeights SMAA_PRESET_${preset})
endforeach()
# CMAA2: the renderer's own kernels (AntiAliasCmaa2.hlsl with third_party/cmaa2's CMAA2.hlsl), the edges' at each of
# its four presets (GpuHarness.h's RunCmaa2 runs them as PostAA.cpp does).
set(cmaa2 "${XRENDERD3D12_SHADER_SOURCE_DIR}/AntiAliasCmaa2.hlsl")
set(preset 0)
foreach(q Low Medium High Ultra)
    xrender_d3d12_test_shader(Cmaa2Edges${q} "${cmaa2}" cs_6_0 EdgesColor2x2CS CMAA2_STATIC_QUALITY_PRESET=${preset})
    math(EXPR preset "${preset} + 1")
endforeach()
xrender_d3d12_test_shader(Cmaa2DispatchArgs "${cmaa2}" cs_6_0 ComputeDispatchArgsCS)
xrender_d3d12_test_shader(Cmaa2Candidates "${cmaa2}" cs_6_0 ProcessCandidatesCS)
xrender_d3d12_test_shader(Cmaa2Apply "${cmaa2}" cs_6_0 DeferredColorApply2x2CS)
# TAA (Taa.hlsli as Taa.hlsl's kernels use it; its sharpening TaaCas.hlsli with third_party/fidelityfx-cas's CAS).
set(taa "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestTaa.hlsl")
foreach(k Resolve Dilate Sharpen Working)
    xrender_d3d12_test_shader(Taa${k} "${taa}" cs_6_0 CSTaa${k})
endforeach()
# MSAA's resolve (Msaa.hlsli as Msaa.hlsl's passes use it).
xrender_d3d12_test_shader(Msaa "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestMsaa.hlsl" cs_6_0 CSMsaa)
# The path tracer's functions, PathTrace.hlsl included as it is (Shader Model 6.5, as Shaders.cmake compiles it).
set(pathtrace "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestPathTrace.hlsl")
foreach(k LeafSide Cosine Checkerboard Layout PixelSurface TerrainShare SurfaceNormal LampLight LampShadow LightmapFill)
    xrender_d3d12_test_shader(${k} "${pathtrace}" cs_6_5 CS${k})
endforeach()
# The renderer's own kernel, from its own source.
xrender_d3d12_test_shader(PathCacheResolve "${XRENDERD3D12_SHADER_SOURCE_DIR}/PathTrace.hlsl" cs_6_5 CSCacheResolve)

set(XRENDERD3D12_TESTS_SOURCES
    TestFramework.h
    TestMain.cpp
    TestTypes.h
    TestMath.h
    GpuHarness.h
    GpuHarness.cpp
    GBufferTests.cpp
    HdrStatsTests.cpp
    HdrToneMapTests.cpp
    HdrExposureTests.cpp
    CacheTests.cpp
    PathTraceTests.cpp
    LayoutTests.cpp
    TangentFrameTests.cpp
    BeamTests.cpp
    LightmapLightTests.cpp
    NormalMapHeightTests.cpp
    RenderModeTests.cpp
    AntiAliasTests.cpp
    TaaTests.cpp
    MsaaTests.cpp
    LightImageTests.cpp
    FarSpriteTests.cpp
    WaterLightTests.cpp
)
file(GLOB XRENDERD3D12_TESTS_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli")
set_source_files_properties(${XRENDERD3D12_TESTS_HLSL} PROPERTIES HEADER_FILE_ONLY ON)
source_group("Shaders" FILES ${XRENDERD3D12_TESTS_HLSL})
source_group("Shaders\\Generated" FILES ${XRENDERD3D12_TESTS_SHADER_HEADERS})

add_executable(XRenderD3D12Tests ${XRENDERD3D12_TESTS_SOURCES} ${XRENDERD3D12_TESTS_HLSL} ${XRENDERD3D12_TESTS_SHADER_HEADERS})
set_target_properties(XRenderD3D12Tests PROPERTIES FOLDER "Renderer")
target_include_directories(XRenderD3D12Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${XRENDERD3D12_TESTS_SHADER_DIR}" "${XRENDERD3D12_SMAA_DIR}" "${XRENDERD3D12_CAS_DIR}")
target_compile_definitions(XRenderD3D12Tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(XRenderD3D12Tests PRIVATE /W4)
target_link_libraries(XRenderD3D12Tests PRIVATE d3d12 dxgi)

add_test(NAME XRenderD3D12Tests COMMAND XRenderD3D12Tests)
# The same on WARP: the results do not depend on the adapter (nor on a GPU being there at all).
add_test(NAME XRenderD3D12Tests.Warp COMMAND XRenderD3D12Tests --warp)
