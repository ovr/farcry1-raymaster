# XRenderCommon - renderer interface facts beyond the SDK headers that a compatible renderer needs,
# and what XRenderD3D9Traced records about how the engine calls its renderer.
# Header-only; shared by the renderers in mod/.

add_library(XRenderCommon INTERFACE
    XRenderCommon.h
    RetailRenderer.h
    Bin64Behaviour.h
    RendererAbiChecks.h
)
set_target_properties(XRenderCommon PROPERTIES FOLDER "Renderer")
target_include_directories(XRenderCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(XRenderCommon INTERFACE CryCommon)
