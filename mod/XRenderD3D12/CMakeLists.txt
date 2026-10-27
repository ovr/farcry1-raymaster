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
    D3D12Device.h
    D3D12Device.cpp
    D3D12Upload.h
    D3D12Upload.cpp
    D3D12Descriptors.h
    D3D12Descriptors.cpp
    D3D12Pipeline.h
    D3D12Pipeline.cpp
    Texture.h
    Texture.cpp
    ImageLoaders.cpp
    RendererTextures.cpp
    Shader.h
    Shader.cpp
    RendererShaders.cpp
    Geometry.h
    LeafBuffer.cpp
    RendererBuffers.cpp
    RenderElements.cpp
    RenderObjects.cpp
    Scene.h
    Scene.cpp
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

#! xrender_d3d12_shader(<name> <file.hlsl> <profile> <entry> [defines...]): compiles one program
#! into Shaders/<name>.h (array g_<name>). Matrices are row major (-Zpr), as the engine's Matrix44.
function(xrender_d3d12_shader name file profile entry)
    set(defines "")
    foreach(d ${ARGN})
        list(APPEND defines "-D" "${d}")
    endforeach()
    set(out "${XRENDERD3D12_SHADER_DIR}/${name}.h")
    file(GLOB includes "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDERD3D12_SHADER_DIR}"
        COMMAND "${XRENDERD3D12_DXC}" -nologo -T ${profile} -E ${entry} -Zpr -O3 -Qstrip_reflect
                -I "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" ${defines}
                -Vn "g_${name}" -Fh "${out}" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${file}"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/${file}"
        DEPENDS ${includes}
        COMMENT "dxc ${file} ${entry} -> ${name}.h"
        VERBATIM
    )
    set(XRENDERD3D12_SHADER_HEADERS ${XRENDERD3D12_SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()

include(Shaders/Shaders.cmake)
file(GLOB XRENDERD3D12_HLSL "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.hlsli"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.cmake")
set_source_files_properties(${XRENDERD3D12_HLSL} PROPERTIES HEADER_FILE_ONLY ON)
source_group("Shaders" FILES ${XRENDERD3D12_HLSL})
source_group("Shaders\\Generated" FILES ${XRENDERD3D12_SHADER_HEADERS})

add_library(XRenderD3D12 SHARED ${XRENDERD3D12_SOURCES} ${XRENDERD3D12_HLSL} ${XRENDERD3D12_SHADER_HEADERS})
set_target_properties(XRenderD3D12 PROPERTIES
    FOLDER "Renderer"
    # Objects and arrays cross the renderer boundary in both directions, so the renderer uses
    # CrySystem's heap like every engine DLL (CryMemoryManager.h), which needs the release CRT in
    # every configuration (see XRenderD3D9Traced/CMakeLists.txt).
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

target_link_libraries(XRenderD3D12 PRIVATE CryCommon XRenderCommon d3d12 dxgi dxguid windowscodecs)
target_include_directories(XRenderD3D12 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${XRENDERD3D12_SHADER_DIR}")

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

# XRENDER_OUTPUT_DIR comes from XRenderD3D9Traced/CMakeLists.txt.
if(XRENDER_OUTPUT_DIR)
    add_custom_command(TARGET XRenderD3D12 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDER_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:XRenderD3D12>"
            "$<$<NOT:$<CONFIG:Release,MinSizeRel>>:$<TARGET_PDB_FILE:XRenderD3D12>>"
            "${XRENDER_OUTPUT_DIR}"
        COMMENT "Copying XRenderD3D12.dll to ${XRENDER_OUTPUT_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
