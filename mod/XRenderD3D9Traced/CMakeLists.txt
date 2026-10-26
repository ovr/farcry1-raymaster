# XRenderD3D9Traced - tracing proxy of the retail Far Cry 1.4 Direct3D 9 renderer.
#
# The DLL loads the original XRenderD3D9.dll, forwards every IRenderer call to it and writes the
# calls, their arguments, the structures behind them and the objects the original renderer returns
# to XRenderD3D9Traced.log in the game's working directory. It is a tool for learning how the
# engine uses its renderer; what is learned goes to mod/XRenderCommon.
#
# Tracing is done only in farcry1_testing1/ (see AGENTS.md): the build deploys the DLL to
# farcry1_testing1/Bin32 (x86) or farcry1_testing1/Bin64 (x64), and the log is written to
# farcry1_testing1/XRenderD3D9Traced.log.
#
# CrySystem.dll picks the renderer DLL by the r_Driver console variable and hard-codes its name:
#   "OpenGL" -> XRenderOGL.dll, "Direct3D8" -> XRenderD3D8.dll, "Direct3D9" -> XRenderD3D9.dll,
#   "NULL" -> XRenderNULL.dll.
# Far Cry 1.4 ships no XRenderD3D8.dll, so the proxy is built under that name by default and is
# selected with r_Driver = "Direct3D8", without touching any of the retail files.
#
# The x86 build is ABI compatible with the retail 32-bit engine (STLport 4.6). The x64 build uses
# the MSVC STL and compiles, but there is no 64-bit engine in the SDK to test it against.

set(XRENDERD3D9TRACED_DLL_NAME "XRenderD3D8" CACHE STRING
    "File name (without .dll) of the tracing renderer. CrySystem only loads XRenderD3D8, XRenderD3D9, XRenderOGL and XRenderNULL.")
if(FARCRY_X64)
    set(XRENDER_DEFAULT_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/farcry1_testing1/Bin64")
else()
    set(XRENDER_DEFAULT_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/farcry1_testing1/Bin32")
endif()
if(NOT EXISTS "${XRENDER_DEFAULT_OUTPUT_DIR}")
    set(XRENDER_DEFAULT_OUTPUT_DIR "")
endif()
set(XRENDER_OUTPUT_DIR "${XRENDER_DEFAULT_OUTPUT_DIR}" CACHE PATH
    "Directory to copy the tracing renderer into after each build (farcry1_testing1/Bin32 or Bin64, empty to skip)")

set(XRENDERD3D9TRACED_SOURCES
    StdAfx.cpp
    StdAfx.h
    XRenderD3D9Traced.cpp
    RendererProxy.h
    RendererProxy.cpp
    RendererProxyShaders.cpp
    RendererProxyBuffers.cpp
    RenderTrace.cpp
    RenderTrace.h
    LeafBufferHooks.cpp
    LeafBufferHooks.h
)
source_group("Source Files" FILES ${XRENDERD3D9TRACED_SOURCES})

add_library(XRenderD3D9Traced SHARED ${XRENDERD3D9TRACED_SOURCES})
set_target_properties(XRenderD3D9Traced PROPERTIES
    FOLDER "Renderer"
    OUTPUT_NAME "${XRENDERD3D9TRACED_DLL_NAME}"
    # The retail engine DLLs are release builds that share CrySystem's heap (CryMemoryManager.h).
    # Objects and arrays cross the renderer boundary in both directions, so the proxy must use that
    # heap too, which CryMemoryManager.h only does without _DEBUG. The release CRT is therefore used
    # in every configuration; Debug only turns off the optimizations.
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

if(CRYGAME_USE_STLPORT)
    # Linked first so that STLport precedes everything else on the include path.
    target_link_libraries(XRenderD3D9Traced PRIVATE STLport)
endif()
target_link_libraries(XRenderD3D9Traced PRIVATE CryCommon XRenderCommon)

target_compile_definitions(XRenderD3D9Traced PRIVATE
    _USRDLL
    XRENDERD3D9TRACED_EXPORTS
    $<$<CONFIG:Release>:_RELEASE>
)

target_precompile_headers(XRenderD3D9Traced PRIVATE StdAfx.h)

target_compile_options(XRenderD3D9Traced PRIVATE
    /W3
    # Global operator new/delete are replaced inline by CryMemoryManager.h.
    /wd4595
    # Keep VC7.1 semantics: every delete goes through the replaced unsized operator delete.
    /Zc:sizedDealloc-
)

target_link_options(XRenderD3D9Traced PRIVATE /MAP)

if(XRENDER_OUTPUT_DIR)
    set(XRENDERD3D9TRACED_DEPLOY_FILES
        "$<TARGET_FILE:XRenderD3D9Traced>"
        "$<$<NOT:$<CONFIG:Release,MinSizeRel>>:$<TARGET_PDB_FILE:XRenderD3D9Traced>>"
    )
    add_custom_command(TARGET XRenderD3D9Traced POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDER_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${XRENDERD3D9TRACED_DEPLOY_FILES}" "${XRENDER_OUTPUT_DIR}"
        COMMENT "Copying ${XRENDERD3D9TRACED_DLL_NAME}.dll to ${XRENDER_OUTPUT_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()
