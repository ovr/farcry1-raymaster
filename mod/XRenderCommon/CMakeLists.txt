# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# XRenderCommon - renderer interface facts beyond the SDK headers that a compatible renderer needs.
# Header-only; shared by the renderers in mod/.

add_library(XRenderCommon INTERFACE
    XRenderCommon.h
    RetailRenderer.h
    RendererStructures.h
    RendererAbiChecks.h
    StructFields.inl
    VTables.inl
    StructLayouts.txt
)
set_target_properties(XRenderCommon PROPERTIES FOLDER "Renderer")
target_include_directories(XRenderCommon INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(XRenderCommon INTERFACE CryCommon)
