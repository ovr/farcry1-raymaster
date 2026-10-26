# CgfDump - prints the chunk structure of .cgf/.cga/.caf files.

add_executable(CgfDump
    CgfDump.cpp
    CgfUtils.cpp
    CgfUtils.h
    ChunkFileReader.cpp
    ChunkFileReader.h
    crc32.cpp
    crc32.h
    CryHeaders.h
    FileMapping.cpp
    FileMapping.h
    stdafx.cpp
    stdafx.h
)
set_target_properties(CgfDump PROPERTIES FOLDER "Tools")

target_link_libraries(CgfDump PRIVATE CryCommon)
# Standalone tool: use the CRT heap instead of the CrySystem.dll pool.
target_compile_definitions(CgfDump PRIVATE DONT_USE_FILE_MAPPING DONT_USE_CRY_MEMORY_MANAGER)
target_precompile_headers(CgfDump PRIVATE stdafx.h)
target_compile_options(CgfDump PRIVATE /W3)
