# Modules written for this project, built against the SDK headers in sdk/.

# Renderer interface knowledge beyond the SDK headers (header-only).
add_subdirectory(XRenderCommon)

# Tracing proxy of the retail D3D9 renderer. x86 is ABI compatible with the retail Far Cry 1.4
# engine (STLport); x64 builds with the MSVC STL, like CryGame, and is not verified.
add_subdirectory(XRenderD3D9Traced)

# A basic Direct3D 12 renderer (x64 only; XRENDER_OUTPUT_DIR comes from XRenderD3D9Traced).
add_subdirectory(XRenderD3D12)

# Demo scenes drawn by the game's engine, to compare the original renderer with ours (x64 only).
add_subdirectory(Demos)
