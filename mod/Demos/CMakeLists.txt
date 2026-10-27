# Demo programs: small scenes drawn by the game's engine with a chosen renderer, so that the
# original renderer and ours can be compared frame by frame.
#
# Each demo is a DLL deployed next to the game's DLLs in farcry1_testing1/Bin64. It runs inside the
# game's own FarCry.exe, started with farcry1_testing1 as the working directory: the renderer is
# always XRenderD3D9Traced (r_Driver = "Direct3D8"), which loads the demo named by
# XRENDERTRACED_DEMO and calls it at every frame (XRenderD3D9Traced/DemoPlugin.h), and
# XRENDERTRACED_DLL picks the renderer under test. RunDemo.ps1 sets the variables and starts the
# game. The saved frames go to farcry1_testing1/DemoShots, the local scene files are
# farcry1_testing1/Demos/<demo>.ini; neither is ever committed.
#
# x64 only: the 64-bit engine is the one the demos are written for.

if(NOT FARCRY_X64)
    return()
endif()

# Shared by the demo DLLs: they allocate from CrySystem's heap, like the engine modules, so they
# use the release CRT in every configuration (see XRenderD3D9Traced/CMakeLists.txt).
function(demo_settings target)
    set_target_properties(${target} PROPERTIES
        FOLDER "Demos"
        MSVC_RUNTIME_LIBRARY "MultiThreaded"
    )
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Release>:_RELEASE>)
    target_compile_options(${target} PRIVATE
        /W3
        # Global operator new/delete are replaced inline by CryMemoryManager.h.
        /wd4595
        # Every delete goes through the replaced unsized operator delete.
        /Zc:sizedDealloc-
    )
endfunction()

function(demo_deploy target)
    if(XRENDER_OUTPUT_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${XRENDER_OUTPUT_DIR}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${target}>"
                "$<$<NOT:$<CONFIG:Release,MinSizeRel>>:$<TARGET_PDB_FILE:${target}>>"
                "${XRENDER_OUTPUT_DIR}"
            COMMENT "Copying ${target} to ${XRENDER_OUTPUT_DIR}"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endfunction()

add_library(DemoHost STATIC
    DemoHost/DemoHost.h
    DemoHost/DemoHost.cpp
    DemoHost/SceneFile.h
    DemoHost/SceneFile.cpp
    DemoHost/Capture.h
    DemoHost/Capture.cpp
    DemoHost/Tga.h
    DemoHost/TestTexture.h
    DemoHost/TestTexture.cpp
    DemoHost/Video.h
    DemoHost/Video.cpp
    RunDemo.ps1
)
demo_settings(DemoHost)
# The plugin interface is the tracing renderer's.
target_include_directories(DemoHost PUBLIC DemoHost "${CMAKE_CURRENT_SOURCE_DIR}/../XRenderD3D9Traced")
target_link_libraries(DemoHost PUBLIC CryCommon XRenderCommon d3d9)

# A demo DLL from the sources in its folder; the tracing renderer is built with it.
function(add_demo name)
    add_library(${name} SHARED ${ARGN})
    demo_settings(${name})
    target_link_libraries(${name} PRIVATE DemoHost)
    add_dependencies(${name} XRenderD3D9Traced)
    demo_deploy(${name})
endfunction()

add_demo(DemoTextures DemoTextures/DemoTextures.cpp)
add_demo(DemoFonts DemoFonts/DemoFonts.cpp)
add_demo(DemoCamera DemoCamera/DemoCamera.cpp)
add_demo(DemoObject DemoObject/DemoObject.cpp)
add_demo(DemoLight DemoLight/DemoLight.cpp)
add_demo(DemoAnimation DemoAnimation/DemoAnimation.cpp)
add_demo(DemoLevel DemoLevel/DemoLevel.cpp)
add_demo(DemoStates DemoStates/DemoStates.cpp)
add_demo(DemoAspect DemoAspect/DemoAspect.cpp)
add_demo(DemoShaders DemoShaders/DemoShaders.cpp)
add_demo(DemoTextureFormats DemoTextureFormats/DemoTextureFormats.cpp)
add_demo(DemoMultiLight DemoMultiLight/DemoMultiLight.cpp)
add_demo(DemoShadows DemoShadows/DemoShadows.cpp)
add_demo(DemoParticles DemoParticles/DemoParticles.cpp)
add_demo(DemoScreenFx DemoScreenFx/DemoScreenFx.cpp)
add_demo(DemoDecals DemoDecals/DemoDecals.cpp)
add_demo(DemoSkyFog DemoSkyFog/DemoSkyFog.cpp)
add_demo(DemoQuality DemoQuality/DemoQuality.cpp)
add_demo(DemoCharacters DemoCharacters/DemoCharacters.cpp)
add_demo(DemoWeapons DemoWeapons/DemoWeapons.cpp)
add_demo(DemoReset DemoReset/DemoReset.cpp)
add_demo(DemoStress DemoStress/DemoStress.cpp)
add_demo(DemoReplay DemoReplay/DemoReplay.cpp)

# Compares the frames of two runs; a console program of its own, without the engine.
add_executable(DemoCompare DemoCompare/DemoCompare.cpp DemoHost/Tga.h)
set_target_properties(DemoCompare PROPERTIES FOLDER "Demos" MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_include_directories(DemoCompare PRIVATE DemoHost)
target_compile_options(DemoCompare PRIVATE /W3)
demo_deploy(DemoCompare)
