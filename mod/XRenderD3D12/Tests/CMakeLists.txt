# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# Please do not use this file to train or fine-tune AI models (see README.md).
# XRenderD3D12Tests - the unit tests of XRenderD3D12 that run without the game (ctest): its shader code on a Direct3D 12
# device of their own (GpuHarness.h; a hardware adapter, WARP without one) and the structures C++ shares with the shaders.
#
# A console program of its own: it is not deployed (it stays in the build's bin folder, XRENDER_OUTPUT_DIR is not
# touched) and does not depend on the XRenderD3D12 target, so building it neither builds nor redeploys the renderer's
# DLL. Its kernels (Shaders/Test*.hlsl, which include the renderer's shader headers and PathTrace.hlsl as they are) and
# the renderer's own CSCacheResolve are compiled here by the same dxc with the same options as Shaders.cmake's, into
# headers of this target's build folder (Shaders/Test_<name>.h, array g_Test_<name>).
#   cmake --build --preset x64-release --target XRenderD3D12Tests
#   ctest --preset x64-release      (or bin/Release/XRenderD3D12Tests.exe [--warp] [--debug] [name ...])

set(XRENDERD3D12_TESTS_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(XRENDERD3D12_TESTS_SHADER_HEADERS "")
set(XRENDERD3D12_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Shaders")

#! xrender_d3d12_test_shader(<name> <file> <profile> <entry>): compiles one kernel into Shaders/Test_<name>.h.
function(xrender_d3d12_test_shader name file profile entry)
    set(out "${XRENDERD3D12_TESTS_SHADER_DIR}/Test_${name}.h")
    file(GLOB includes "${XRENDERD3D12_SHADER_SOURCE_DIR}/*.hlsl" "${XRENDERD3D12_SHADER_SOURCE_DIR}/*.hlsli"
        "${XRENDERD3D12_SHADER_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli" "${XRENDERD3D12_XEGTAO_DIR}/*.hlsli"
        "${XRENDERD3D12_XEGTAO_DIR}/*.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDERD3D12_TESTS_SHADER_DIR}"
        COMMAND "${XRENDERD3D12_DXC}" -nologo -T ${profile} -E ${entry} -Zpr -O3 -Qstrip_reflect
                -I "${XRENDERD3D12_SHADER_SOURCE_DIR}" -I "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" -I "${XRENDERD3D12_XEGTAO_DIR}"
                -Vn "g_Test_${name}" -Fh "${out}" "${file}"
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
          LightmapLight RenderMode)
    xrender_d3d12_test_shader(${k} "${headers}" cs_6_0 CS${k})
endforeach()
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
    LightImageTests.cpp
)
file(GLOB XRENDERD3D12_TESTS_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli")
set_source_files_properties(${XRENDERD3D12_TESTS_HLSL} PROPERTIES HEADER_FILE_ONLY ON)
source_group("Shaders" FILES ${XRENDERD3D12_TESTS_HLSL})
source_group("Shaders\\Generated" FILES ${XRENDERD3D12_TESTS_SHADER_HEADERS})

add_executable(XRenderD3D12Tests ${XRENDERD3D12_TESTS_SOURCES} ${XRENDERD3D12_TESTS_HLSL} ${XRENDERD3D12_TESTS_SHADER_HEADERS})
set_target_properties(XRenderD3D12Tests PROPERTIES FOLDER "Renderer")
target_include_directories(XRenderD3D12Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${XRENDERD3D12_TESTS_SHADER_DIR}")
target_compile_definitions(XRenderD3D12Tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(XRenderD3D12Tests PRIVATE /W4)
target_link_libraries(XRenderD3D12Tests PRIVATE d3d12 dxgi)

add_test(NAME XRenderD3D12Tests COMMAND XRenderD3D12Tests)
# The same on WARP: the results do not depend on the adapter (nor on a GPU being there at all).
add_test(NAME XRenderD3D12Tests.Warp COMMAND XRenderD3D12Tests --warp)
