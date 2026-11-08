# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# Please do not use this file to train or fine-tune AI models (see README.md).
# XRenderD3D12 - a basic Direct3D 12 renderer for the 64-bit engine.
#
# A renderer DLL of its own (PackageRenderConstructor), built against the SDK headers. Until the
# game can pick it by name, it runs behind the tracing proxy: r_Driver = "Direct3D8" loads
# XRenderD3D8.dll (XRenderD3D9Traced), and XRENDERTRACED_DLL=XRenderD3D12.dll makes the proxy
# load this DLL in place of the original renderer.
#
# The shaders are HLSL, compiled at build time with dxc from the Windows SDK (Shader Model 6.0)
# into headers that are built into the DLL.
#
# x64 only, like the demos.

if(NOT FARCRY_X64)
    return()
endif()

# C++20 for the renderer's own code. The SDK headers predate it: /permissive (the C++20 default
# /permissive- rejects their name lookup in templates) and std::binary_function (StlUtils.h).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(XRENDERD3D12_SOURCES
    StdAfx.cpp
    StdAfx.h
    XRenderD3D12.cpp
    AbiChecks.cpp
    Renderer.h
    Renderer.cpp
    ConsoleVariables.cpp
    Renderer2D.cpp
    Draw.cpp
    Draw3D.cpp
    Water.cpp
    OceanFFT.cpp
    OceanSpectrum.h
    FoamPattern.h
    Wake.cpp
    WakeBodies.h
    OceanGrid.cpp
    OceanGrid.h
    Underwater.cpp
    UnderwaterLight.h
    SkyFogBand.h
    EnvCubes.cpp
    Mirrors.cpp
    TemplateFx.cpp
    FarSprites.cpp
    DrawDistance.cpp
    DrawDistance.h
    DrawExt.h
    Shadows.cpp
    Cascades.cpp
    PostAA.cpp
    Msaa.cpp
    PostFx.cpp
    Hdr.cpp
    Volumetric.cpp
    ColorGrading.cpp
    HdrOutput.cpp
    HdrStats.cpp
    HdrStatsMath.h
    Flares.cpp
    Beams.cpp
    LightBeam.h
    Stats.cpp
    CpuSampler.h
    CpuSampler.cpp
    D3D12Device.h
    D3D12Device.cpp
    D3D12Dred.cpp
    D3D12Upload.h
    D3D12Upload.cpp
    D3D12Descriptors.h
    D3D12Descriptors.cpp
    D3D12Pipeline.h
    D3D12Pipeline.cpp
    Texture.h
    Texture.cpp
    ImageLoaders.cpp
    NormalMapHeight.h
    LightImage.h
    FarSpriteAngles.h
    RendererTextures.cpp
    Shader.h
    Shader.cpp
    RendererShaders.cpp
    Geometry.h
    TangentFrame.h
    LeafBuffer.cpp
    RendererBuffers.cpp
    RenderElements.cpp
    RenderObjects.cpp
    Scene.h
    Scene.cpp
    Upscaler.h
    Upscale.cpp
    UpscalerFsr.cpp
    UpscalerTaa.cpp
    D3D12Compute.h
    D3D12Compute.cpp
    D3D12Barriers.h
    AmbientOcclusion.h
    AmbientOcclusion.cpp
    AOXeGtao.cpp
    AOCacao.cpp
    RayTracing.h
    RayTracing.cpp
    RaySun.cpp
    RayAO.cpp
    PathTrace.cpp
    NrdHost.h
    NrdHost.cpp
    Cacao/base/UserMarkers.h
)
source_group("Source Files" FILES ${XRENDERD3D12_SOURCES})

# HLSL: Shaders/Shaders.cmake lists the programs (xrender_d3d12_shader calls).
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    set(XRENDERD3D12_SDK_VERSION "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
elseif(DEFINED ENV{WindowsSDKVersion})
    string(REGEX REPLACE "[\\\\/]+$" "" XRENDERD3D12_SDK_VERSION "$ENV{WindowsSDKVersion}")
endif()
find_program(XRENDERD3D12_DXC dxc
    HINTS
        "$ENV{WindowsSdkDir}bin/${XRENDERD3D12_SDK_VERSION}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/${XRENDERD3D12_SDK_VERSION}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
    NO_DEFAULT_PATH
    REQUIRED
    DOC "dxc.exe of the Windows SDK")

set(XRENDERD3D12_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(XRENDERD3D12_SHADER_HEADERS "")
# The third_party shader sources that ours include: Intel XeGTAO (third_party/xegtao).
set(XRENDERD3D12_XEGTAO_DIR "${CMAKE_SOURCE_DIR}/third_party/xegtao/Source/Rendering/Shaders")
# SMAA (third_party/smaa): its shader for the anti-aliasing's kernels, its precomputed textures for PostAA.cpp.
set(XRENDERD3D12_SMAA_DIR "${CMAKE_SOURCE_DIR}/third_party/smaa")
# CMAA2 (third_party/cmaa2): its shader for the anti-aliasing's kernels.
set(XRENDERD3D12_CMAA2_DIR "${CMAKE_SOURCE_DIR}/third_party/cmaa2/Projects/CMAA2/CMAA2")
# AMD FidelityFX CAS (third_party/fidelityfx-cas): TAA's sharpening, its kernel in Taa.hlsl, its constants in UpscalerTaa.cpp.
set(XRENDERD3D12_CAS_DIR "${CMAKE_SOURCE_DIR}/third_party/fidelityfx-cas/ffx-cas")
# More directories to include from, set around the xrender_d3d12_shader calls of the programs that want them (NRD.hlsli).
set(XRENDERD3D12_SHADER_INCLUDES "")

# NVIDIA NRD v4.18.0 (NVIDIA Real-time Denoisers, under the NVIDIA RTX SDKs License): the path
# tracer's denoiser (d3d12_PTDenoiser nrd, NrdHost.cpp), with ShaderMake (the build tool of its
# shaders) and MathLib (both MIT). The three are git submodules of NVIDIA's repositories:
# third_party/nrd at the tag v4.18.0, third_party/shadermake and third_party/mathlib at the commits
# that tag's CMake names. This repository has their links, not their files. NRD is built as a static
# library with its shaders as DXIL, compiled by the Windows SDK's dxc; nothing is downloaded. Without
# the submodules (a clone without --recurse-submodules) or with a CMake older than NRD's 3.30,
# XRenderD3D12 is built without NRD.
# AMD FidelityFX Denoiser v1.2 (MIT): its shadows denoiser for the sun's shadow by rays
# (d3d12_RTSunDenoise 2, Shaders/RaySunFfx.hlsl), shader headers alone. A git submodule of AMD's
# repository at the tag v1.2 (third_party/fidelityfx-denoiser); without it XRenderD3D12 is built
# without that denoiser.
set(XRENDERD3D12_FFX_DENOISER_DIR "${CMAKE_SOURCE_DIR}/third_party/fidelityfx-denoiser/ffx-shadows-dnsr")
set(XRENDERD3D12_FFX_DENOISER OFF)
if(EXISTS "${XRENDERD3D12_FFX_DENOISER_DIR}/ffx_denoiser_shadows_filter.h")
    set(XRENDERD3D12_FFX_DENOISER ON)
else()
    message(WARNING "third_party/fidelityfx-denoiser is empty (git submodule update --init): "
        "XRenderD3D12 is built without AMD FidelityFX Denoiser.")
endif()

set(XRENDERD3D12_NRD_DIR "${CMAKE_SOURCE_DIR}/third_party/nrd")
set(XRENDERD3D12_SHADERMAKE_DIR "${CMAKE_SOURCE_DIR}/third_party/shadermake")
set(XRENDERD3D12_MATHLIB_DIR "${CMAKE_SOURCE_DIR}/third_party/mathlib")
set(XRENDERD3D12_NRD OFF)
if(NOT EXISTS "${XRENDERD3D12_NRD_DIR}/Include/NRD.h" OR NOT EXISTS "${XRENDERD3D12_SHADERMAKE_DIR}/CMakeLists.txt"
        OR NOT EXISTS "${XRENDERD3D12_MATHLIB_DIR}/ml.h")
    message(WARNING "third_party/nrd, third_party/shadermake or third_party/mathlib is empty "
        "(git submodule update --init): XRenderD3D12 is built without NVIDIA NRD.")
elseif(CMAKE_VERSION VERSION_LESS 3.30)
    message(WARNING "NVIDIA NRD needs CMake 3.30 or later (this one is ${CMAKE_VERSION}): "
        "XRenderD3D12 is built without it.")
else()
    set(XRENDERD3D12_NRD ON)
endif()

#! xrender_d3d12_dxc(<out.h> <array> <source> <comment> <dxc options...>): the command that compiles <source> with dxc
#! into the header <out.h> (array <array>), also for the tests (Tests/CMakeLists.txt). It depends on the files the
#! source really includes with these options: dxc writes them into <out.h>.d first (-MF; dxc 1.8 only preprocesses
#! then, ~40 ms, and takes no -Vn), which DEPFILE reads (Visual Studio since CMake 3.21), so a header recompiles only
#! the programs that include it and a new one is followed without a configure. No MAIN_DEPENDENCY: Visual Studio
#! hangs only the first command of a source on it and the others on .rule files, which it never builds in parallel
#! (CMP0147, ../../CMakeLists.txt).
function(xrender_d3d12_dxc out array source comment)
    get_filename_component(dir "${out}" DIRECTORY)
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${dir}"
        COMMAND "${XRENDERD3D12_DXC}" ${ARGN} -MF "${out}.d" "${source}"
        COMMAND "${XRENDERD3D12_DXC}" ${ARGN} -Vn "${array}" -Fh "${out}" "${source}"
        DEPENDS "${source}"
        DEPFILE "${out}.d"
        COMMENT "${comment}"
        VERBATIM
    )
endfunction()

#! xrender_d3d12_shader(<name> <file.hlsl> <profile> <entry> [defines...]): compiles one program
#! into Shaders/<name>.h (array g_<name>). Matrices are row major (-Zpr), as the engine's Matrix44.
function(xrender_d3d12_shader name file profile entry)
    set(defines "")
    foreach(d ${ARGN})
        list(APPEND defines "-D" "${d}")
    endforeach()
    set(more "")
    foreach(dir ${XRENDERD3D12_SHADER_INCLUDES})
        list(APPEND more "-I" "${dir}")
    endforeach()
    set(out "${XRENDERD3D12_SHADER_DIR}/${name}.h")
    xrender_d3d12_dxc("${out}" "g_${name}" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${file}" "dxc ${file} ${entry} -> ${name}.h"
        -nologo -T ${profile} -E ${entry} -Zpr -O3 -Qstrip_reflect
        -I "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" -I "${XRENDERD3D12_XEGTAO_DIR}" ${more} ${defines})
    set(XRENDERD3D12_SHADER_HEADERS ${XRENDERD3D12_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()

include(Shaders/Shaders.cmake)

# AMD FidelityFX CACAO (third_party/fidelityfx-cacao, tag v1.2): its Direct3D 12 host is built as it
# is, its shaders as its build_shaders_dxil.bat compiles them (cs_6_2, column major) into the
# PrecompiledShadersDXIL/CACAO<pass>.h headers (array CS<pass>DXIL) that the host includes.
set(XRENDERD3D12_CACAO_DIR "${CMAKE_SOURCE_DIR}/third_party/fidelityfx-cacao/ffx-cacao")
set(XRENDERD3D12_CACAO_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/cacao")
set(XRENDERD3D12_CACAO_PASSES
    ClearLoadCounter
    PrepareDownsampledDepths PrepareNativeDepths PrepareDownsampledDepthsAndMips PrepareNativeDepthsAndMips
    PrepareDownsampledNormals PrepareNativeNormals PrepareDownsampledNormalsFromInputNormals
    PrepareNativeNormalsFromInputNormals PrepareDownsampledDepthsHalf PrepareNativeDepthsHalf
    GenerateQ0 GenerateQ1 GenerateQ2 GenerateQ3 GenerateQ3Base
    GenerateImportanceMap PostprocessImportanceMapA PostprocessImportanceMapB
    EdgeSensitiveBlur1 EdgeSensitiveBlur2 EdgeSensitiveBlur3 EdgeSensitiveBlur4
    EdgeSensitiveBlur5 EdgeSensitiveBlur6 EdgeSensitiveBlur7 EdgeSensitiveBlur8
    Apply NonSmartApply NonSmartHalfApply
    UpscaleBilateral5x5NonSmart UpscaleBilateral5x5Smart UpscaleBilateral5x5Half
)
set(XRENDERD3D12_CACAO_HEADERS "")
foreach(pass ${XRENDERD3D12_CACAO_PASSES})
    set(out "${XRENDERD3D12_CACAO_SHADER_DIR}/PrecompiledShadersDXIL/CACAO${pass}.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDERD3D12_CACAO_SHADER_DIR}/PrecompiledShadersDXIL"
        COMMAND "${XRENDERD3D12_DXC}" -nologo -T cs_6_2 -E FFX_CACAO_${pass} -Vn CS${pass}DXIL -Fh "${out}"
                "${XRENDERD3D12_CACAO_DIR}/src/ffx_cacao.hlsl"
        DEPENDS "${XRENDERD3D12_CACAO_DIR}/src/ffx_cacao.hlsl" "${XRENDERD3D12_CACAO_DIR}/src/ffx_cacao_bindings.hlsl"
                "${XRENDERD3D12_CACAO_DIR}/src/ffx_cacao_defines.h"
        COMMENT "dxc ffx_cacao.hlsl FFX_CACAO_${pass} -> CACAO${pass}.h"
        VERBATIM
    )
    list(APPEND XRENDERD3D12_CACAO_HEADERS "${out}")
endforeach()
# The host: with FFX_CACAO_ENABLE_D3D12, d3dx12.h (third_party/directx-headers) and an empty stub for
# the Cauldron markers it includes (Cacao/base/UserMarkers.h); without the renderer's precompiled
# header.
set(XRENDERD3D12_CACAO_SOURCES "${XRENDERD3D12_CACAO_DIR}/src/ffx_cacao.cpp" "${XRENDERD3D12_CACAO_DIR}/src/ffx_cacao_impl.cpp")
set_source_files_properties(${XRENDERD3D12_CACAO_SOURCES} PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/directx-headers/include/directx;${CMAKE_CURRENT_SOURCE_DIR}/Cacao;${XRENDERD3D12_CACAO_SHADER_DIR}"
)
set_source_files_properties(${XRENDERD3D12_CACAO_SOURCES} PROPERTIES COMPILE_DEFINITIONS FFX_CACAO_ENABLE_D3D12)
source_group("Third Party\\FidelityFX CACAO" FILES ${XRENDERD3D12_CACAO_SOURCES})
source_group("Shaders\\Generated\\CACAO" FILES ${XRENDERD3D12_CACAO_HEADERS})

file(GLOB XRENDERD3D12_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.cmake")
set_source_files_properties(${XRENDERD3D12_HLSL} PROPERTIES HEADER_FILE_ONLY ON)
source_group("Shaders" FILES ${XRENDERD3D12_HLSL})
source_group("Shaders\\Generated" FILES ${XRENDERD3D12_SHADER_HEADERS})

add_library(XRenderD3D12 SHARED ${XRENDERD3D12_SOURCES} ${XRENDERD3D12_HLSL} ${XRENDERD3D12_SHADER_HEADERS}
    ${XRENDERD3D12_CACAO_SOURCES} ${XRENDERD3D12_CACAO_HEADERS})
set_target_properties(XRenderD3D12 PROPERTIES
    FOLDER "Renderer"
    # Objects and arrays cross the renderer boundary in both directions, so the renderer uses
    # CrySystem's heap like every engine DLL (CryMemoryManager.h), which needs the release CRT in
    # every configuration (see XRenderD3D9Traced/CMakeLists.txt).
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

# MSBuild runs the parallel custom commands in batches, one batch after another in a process, by default as many
# commands to a batch as spread them evenly over the processes (MaxItemsInBatch 0): General.hlsl's heavy programs,
# listed together, then fill a few batches that the build waits for. One command to a batch hands each free process
# the next program (also for XRenderD3D12Tests, Tests/CMakeLists.txt).
set(XRENDERD3D12_BATCH_PROPS "${CMAKE_CURRENT_BINARY_DIR}/CustomBuildBatch.props")
file(CONFIGURE OUTPUT "${XRENDERD3D12_BATCH_PROPS}" CONTENT [[<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup>
    <CustomBuild>
      <MaxItemsInBatch>1</MaxItemsInBatch>
    </CustomBuild>
  </ItemDefinitionGroup>
</Project>
]])
set_property(TARGET XRenderD3D12 PROPERTY VS_PROJECT_IMPORT "${XRENDERD3D12_BATCH_PROPS}")

target_link_libraries(XRenderD3D12 PRIVATE CryCommon XRenderCommon d3d12 dxgi dxguid windowscodecs)
target_include_directories(XRenderD3D12 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${XRENDERD3D12_SHADER_DIR}" "${XRENDERD3D12_XEGTAO_DIR}"
    "${XRENDERD3D12_CACAO_DIR}/inc" "${XRENDERD3D12_SMAA_DIR}" "${XRENDERD3D12_CAS_DIR}")

# AMD FSR Upscaling (third_party/amd-fsr-sdk, tag v2.4.0): the headers and the loader are in the
# repository; the upscaler (57 MB) is downloaded from the same tag, or is the file that
# XRENDER_FSR_UPSCALER_DLL names. Both go next to XRenderD3D12.dll, which loads them at run time;
# without them the renderer runs without FSR.
set(XRENDERD3D12_FSR_DIR "${CMAKE_SOURCE_DIR}/third_party/amd-fsr-sdk")
set(XRENDERD3D12_FSR_URL "https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/v2.4.0/Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_dx12.dll")
set(XRENDERD3D12_FSR_SHA256 "aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace")
set(XRENDER_FSR_UPSCALER_DLL "" CACHE FILEPATH
    "amd_fidelityfx_upscaler_dx12.dll of the AMD FSR SDK v2.4.0 (empty: download it)")
target_include_directories(XRenderD3D12 PRIVATE "${XRENDERD3D12_FSR_DIR}/Kits/FidelityFX")

if(XRENDER_FSR_UPSCALER_DLL)
    set(XRENDERD3D12_FSR_UPSCALER "${XRENDER_FSR_UPSCALER_DLL}")
else()
    set(XRENDERD3D12_FSR_UPSCALER "${CMAKE_CURRENT_BINARY_DIR}/fsr/amd_fidelityfx_upscaler_dx12.dll")
endif()
set(fsr_hash "")
if(EXISTS "${XRENDERD3D12_FSR_UPSCALER}")
    file(SHA256 "${XRENDERD3D12_FSR_UPSCALER}" fsr_hash)
endif()
if(NOT fsr_hash STREQUAL XRENDERD3D12_FSR_SHA256 AND NOT XRENDER_FSR_UPSCALER_DLL)
    message(STATUS "Downloading amd_fidelityfx_upscaler_dx12.dll (57 MB) of the AMD FSR SDK v2.4.0")
    file(DOWNLOAD "${XRENDERD3D12_FSR_URL}" "${XRENDERD3D12_FSR_UPSCALER}.part" STATUS fsr_status TLS_VERIFY ON)
    list(GET fsr_status 0 fsr_code)
    set(fsr_hash "")
    if(fsr_code EQUAL 0)
        file(SHA256 "${XRENDERD3D12_FSR_UPSCALER}.part" fsr_hash)
    endif()
    if(fsr_hash STREQUAL XRENDERD3D12_FSR_SHA256)
        file(RENAME "${XRENDERD3D12_FSR_UPSCALER}.part" "${XRENDERD3D12_FSR_UPSCALER}")
    else()
        file(REMOVE "${XRENDERD3D12_FSR_UPSCALER}.part")
        message(WARNING "amd_fidelityfx_upscaler_dx12.dll could not be downloaded (${fsr_status}, "
            "SHA-256 '${fsr_hash}'): XRenderD3D12 is built without it and runs without FSR. "
            "XRENDER_FSR_UPSCALER_DLL can name a copy of it.")
    endif()
elseif(NOT fsr_hash STREQUAL XRENDERD3D12_FSR_SHA256)
    message(WARNING "${XRENDER_FSR_UPSCALER_DLL} is not the upscaler of the AMD FSR SDK v2.4.0 "
        "(SHA-256 '${fsr_hash}'); it is used all the same.")
endif()
set(XRENDERD3D12_FSR_FILES "${XRENDERD3D12_FSR_DIR}/Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll")
if(fsr_hash)
    list(APPEND XRENDERD3D12_FSR_FILES "${XRENDERD3D12_FSR_UPSCALER}")
endif()

target_compile_definitions(XRenderD3D12 PRIVATE
    _USRDLL
    XRENDERD3D12_EXPORTS
    _HAS_AUTO_PTR_ETC=1
    $<$<CONFIG:Release>:_RELEASE>
)

target_precompile_headers(XRenderD3D12 PRIVATE StdAfx.h)

target_compile_options(XRenderD3D12 PRIVATE
    /W3
    /permissive
    # Global operator new/delete are replaced inline by CryMemoryManager.h.
    /wd4595
    # Every delete goes through the replaced unsized operator delete.
    /Zc:sizedDealloc-
    # The offset table of VertexFormats.h (SDK) casts pointers to int.
    /wd4302 /wd4311
)

target_link_options(XRenderD3D12 PRIVATE /MAP)

#! NRD (above) as its own CMake builds it, its options as variables of this function's scope: a static
#! library with the DXIL shaders alone (the Windows SDK's dxc, not one ShaderMake would download), the
#! submodules in place of the ShaderMake and MathLib it would fetch, the renderer's C runtime
#! (MultiThreaded), its own C++ standard, its shaders' headers in the build directory. NRD writes
#! Shaders/NRDConfig.hlsli into its folder, which its .gitignore leaves out.
function(xrender_d3d12_add_nrd)
    set(NRD_STATIC_LIBRARY ON)
    set(NRD_EMBEDS_DXIL_SHADERS ON)
    set(NRD_EMBEDS_SPIRV_SHADERS OFF)
    set(NRD_EMBEDS_DXBC_SHADERS OFF)
    set(NRD_SHADERS_PATH "${CMAKE_CURRENT_BINARY_DIR}/nrd-shaders")
    set(SHADERMAKE_FIND_COMPILERS OFF)
    set(SHADERMAKE_DXC_PATH "${XRENDERD3D12_DXC}")
    set(FETCHCONTENT_SOURCE_DIR_SHADERMAKE "${XRENDERD3D12_SHADERMAKE_DIR}")
    set(FETCHCONTENT_SOURCE_DIR_MATHLIB "${XRENDERD3D12_MATHLIB_DIR}")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    unset(CMAKE_CXX_STANDARD)
    unset(CMAKE_CXX_STANDARD_REQUIRED)
    add_subdirectory("${XRENDERD3D12_NRD_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/nrd" EXCLUDE_FROM_ALL)
    foreach(t NRD NRDShaders NRDIntegration ShaderMake ShaderMakeBlob)
        if(TARGET ${t})
            set_target_properties(${t} PROPERTIES FOLDER "Third Party/NRD")
        endif()
    endforeach()
endfunction()

if(XRENDERD3D12_FFX_DENOISER)
    target_compile_definitions(XRenderD3D12 PRIVATE XRENDERD3D12_FFX_DENOISER=1)
endif()
if(XRENDERD3D12_NRD)
    xrender_d3d12_add_nrd()
    target_link_libraries(XRenderD3D12 PRIVATE NRD)
    target_compile_definitions(XRenderD3D12 PRIVATE XRENDERD3D12_NRD=1)
endif()

# XRENDER_OUTPUT_DIR comes from XRenderD3D9Traced/CMakeLists.txt.
if(XRENDER_OUTPUT_DIR)
    add_custom_command(TARGET XRenderD3D12 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDER_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:XRenderD3D12>"
            "$<$<NOT:$<CONFIG:Release,MinSizeRel>>:$<TARGET_PDB_FILE:XRenderD3D12>>"
            "${XRENDER_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${XRENDERD3D12_FSR_FILES} "${XRENDER_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XRENDERD3D12_FSR_DIR}/docs/license.md"
            "${XRENDER_OUTPUT_DIR}/amd_fidelityfx_license.md"
        COMMENT "Copying XRenderD3D12.dll and the AMD FSR DLLs to ${XRENDER_OUTPUT_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

# The unit tests (Tests/CMakeLists.txt, ctest): a console program of their own with their own shader compiles; it does
# not depend on the XRenderD3D12 target and is not deployed.
enable_testing()
add_subdirectory(Tests)
