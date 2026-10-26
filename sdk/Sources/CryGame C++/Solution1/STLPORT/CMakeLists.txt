# STLport 4.6 as used for the retail Far Cry 1.4 binaries. Header-only (_STLP_NO_OWN_IOSTREAMS),
# adapted to recent MSVC; see the "FC-SDK CMake port" notes in stlport/config/stl_msvc.h.

add_library(STLport INTERFACE)
target_include_directories(STLport BEFORE INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stlport")
