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

#! xrender_d3d12_test_shader(<name> <file> <profile> <entry> [defines...]): compiles one kernel into Shaders/Test_<name>.h,
#! with the renderer's xrender_d3d12_dxc (../CMakeLists.txt): its dependencies are the files the kernel includes.
function(xrender_d3d12_test_shader name file profile entry)
    set(defines "")
    foreach(d ${ARGN})
        list(APPEND defines "-D" "${d}")
    endforeach()
    set(out "${XRENDERD3D12_TESTS_SHADER_DIR}/Test_${name}.h")
    xrender_d3d12_dxc("${out}" "g_Test_${name}" "${file}" "dxc (tests) ${entry} -> Test_${name}.h"
        -nologo -T ${profile} -E ${entry} -Zpr -O3 -Qstrip_reflect
        -I "${XRENDERD3D12_SHADER_SOURCE_DIR}" -I "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" -I "${XRENDERD3D12_XEGTAO_DIR}"
        -I "${XRENDERD3D12_SMAA_DIR}" -I "${XRENDERD3D12_CMAA2_DIR}" -I "${XRENDERD3D12_CAS_DIR}" ${defines})
    set(XRENDERD3D12_TESTS_SHADER_HEADERS ${XRENDERD3D12_TESTS_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()

# The headers that stand alone (Shader Model 6.0).
set(headers "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestHeaders.hlsl")
foreach(k Oct GBufferNormal Hdr HdrStats ToneMap HdrOutput SkyGlow FrameAverage Exposure CacheCell CacheLookup CacheAdd CacheRead CachePoke
          LightmapLight RenderMode Fxaa ArrayCoords BlendPictures PictureNoise WaterGlint WaterCap WaterCrest Foam FoamMix FoamLight FoamContact FoamSurf FoamClumps FoamShade CausticsRay CausticsDepth CausticsFar
          VolumeAttenuation VolumeInscatter VolumeSegments VolumeRay WakeShape Grid GridDisc UnderPath UnderPhase UnderSegments
          UnderInscatter UnderInscatterClosed UnderWindow UnderSurface UnderFog UnderClear CoverageAlpha FadeAlpha PbrSpecLobe PbrReflect PbrPrev PbrQuad PbrQuadShares PbrSpecOcclusion
          SkinSpecular SkinParts RayFace BarrierStep VolSun VolFroxel SkyFogBand)
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
# The draw's constants as the programs read them (Common.hlsli's g_Draw and g_DrawExt).
xrender_d3d12_test_shader(DrawLayout "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestDraw.hlsl" cs_6_0 CSDrawLayout)
# Parallax occlusion mapping (r_Parallax: Parallax.hlsli as General.hlsl includes it, its height the test's).
set(parallax "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestParallax.hlsl")
foreach(k UV Parts MarchGrad)
    xrender_d3d12_test_shader(Parallax${k} "${parallax}" cs_6_0 CSParallax${k})
endforeach()
# The path tracer's functions, PathTrace.hlsl included as it is (Shader Model 6.5, as Shaders.cmake compiles it).
set(pathtrace "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/TestPathTrace.hlsl")
foreach(k LeafSide Cosine Checkerboard Layout PixelSurface TerrainShare SurfaceNormal LampLight LampShadow LightmapFill)
    xrender_d3d12_test_shader(${k} "${pathtrace}" cs_6_5 CS${k})
endforeach()
# The FFT ocean's kernels, from the renderer's own source as Shaders.cmake compiles them (GpuHarness.h's RunOcean runs
# them as OceanFFT.cpp does).
foreach(k Rows Columns Waves Mips Foam)
    string(TOUPPER "${k}" kd)
    xrender_d3d12_test_shader(Ocean${k} "${XRENDERD3D12_SHADER_SOURCE_DIR}/OceanFFT.hlsl" cs_6_0 CSMain XR_OCEAN_${kd})
endforeach()
xrender_d3d12_test_shader(OceanFoamView "${XRENDERD3D12_SHADER_SOURCE_DIR}/OceanFFT.hlsl" cs_6_0 CSMain XR_OCEAN_FOAM_VIEW)
xrender_d3d12_test_shader(OceanCaustics "${XRENDERD3D12_SHADER_SOURCE_DIR}/OceanFFT.hlsl" cs_6_0 CSMain XR_OCEAN_CAUSTICS)
# The wakes' kernels, from the renderer's own source (GpuHarness.h's RunWake runs them as Wake.cpp does).
set(wake "${XRENDERD3D12_SHADER_SOURCE_DIR}/Wake.hlsl")
xrender_d3d12_test_shader(WakeRows "${wake}" cs_6_0 CSMain XR_WAKE_ROWS)
xrender_d3d12_test_shader(WakeColumns "${wake}" cs_6_0 CSMain XR_WAKE_COLUMNS)
xrender_d3d12_test_shader(WakeRowsInverse "${wake}" cs_6_0 CSMain XR_WAKE_ROWS_INVERSE)
xrender_d3d12_test_shader(WakeView "${wake}" cs_6_0 CSMain XR_WAKE_VIEW)
xrender_d3d12_test_shader(WakeHeight "${wake}" cs_6_0 CSMain XR_WAKE_HEIGHT)
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
    OceanTests.cpp
    FoamTests.cpp
    CausticsTests.cpp
    VolumeTests.cpp
    WakeTests.cpp
    GridTests.cpp
    UnderTests.cpp
    FadeTests.cpp
    SkyTests.cpp
    DisplayTests.cpp
    ParallaxTests.cpp
    PbrSpecularTests.cpp
    SkinTests.cpp
    RayFaceTests.cpp
    DrawDistanceTests.cpp
    DrawExtTests.cpp
    BarrierTests.cpp
    VolumetricTests.cpp
    ConfigTests.cpp
)
file(GLOB XRENDERD3D12_TESTS_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli")
set_source_files_properties(${XRENDERD3D12_TESTS_HLSL} PROPERTIES HEADER_FILE_ONLY ON)
source_group("Shaders" FILES ${XRENDERD3D12_TESTS_HLSL})
source_group("Shaders\\Generated" FILES ${XRENDERD3D12_TESTS_SHADER_HEADERS})

add_executable(XRenderD3D12Tests ${XRENDERD3D12_TESTS_SOURCES} ${XRENDERD3D12_TESTS_HLSL} ${XRENDERD3D12_TESTS_SHADER_HEADERS})
set_target_properties(XRenderD3D12Tests PROPERTIES FOLDER "Renderer"
    VS_PROJECT_IMPORT "${XRENDERD3D12_BATCH_PROPS}")  # one dxc command to a batch (../CMakeLists.txt)
target_include_directories(XRenderD3D12Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${XRENDERD3D12_TESTS_SHADER_DIR}" "${XRENDERD3D12_SMAA_DIR}" "${XRENDERD3D12_CAS_DIR}")
target_compile_definitions(XRenderD3D12Tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
    XR_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/..")  # Barrier_SourceLint (BarrierTests.cpp) and Config_Documented (ConfigTests.cpp) read the renderer's sources
target_compile_options(XRenderD3D12Tests PRIVATE /W4)
target_link_libraries(XRenderD3D12Tests PRIVATE d3d12 dxgi)

add_test(NAME XRenderD3D12Tests COMMAND XRenderD3D12Tests)
# The same on WARP: the results do not depend on the adapter (nor on a GPU being there at all).
add_test(NAME XRenderD3D12Tests.Warp COMMAND XRenderD3D12Tests --warp)
# The barriers' tests and the runs whose barriers the tracker gives (D3D12Barriers.h) under the debug layer: any of its
# warnings fails them (a transition's wrong before, a subresource twice in a call).
add_test(NAME XRenderD3D12Tests.Debug COMMAND XRenderD3D12Tests --debug Barrier Ocean_ Caustics_ Wake_ Cmaa2)
