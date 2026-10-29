# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
cmake_minimum_required(VERSION 3.21)

project(FarCrySDK LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "The Far Cry SDK can only be built with MSVC (Visual Studio 2022 or newer).")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FARCRY_X64 ON)
else()
    set(FARCRY_X64 OFF)
endif()

# The code base predates C++17 (std::binary_function, dynamic exception specs, ...).
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT: a mod DLL should not require the VC++ redistributable. In Release the
# game routes all heap traffic to CrySystem.dll (see CryMemoryManager.h) anyway.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# The retail 32-bit engine was built with STLport 4.6 and passes STL containers
# through its interfaces, so CryGame.dll must use the very same STL to stay ABI
# compatible with Far Cry 1.4.
if(FARCRY_X64)
    set(CRYGAME_USE_STLPORT OFF CACHE BOOL "Build CryGame against the bundled STLport 4.6 (x86 only)" FORCE)
else()
    option(CRYGAME_USE_STLPORT "Build CryGame against the bundled STLport 4.6 (required for compatibility with the retail game)" ON)
    if(NOT CRYGAME_USE_STLPORT)
        message(WARNING "CRYGAME_USE_STLPORT is OFF: CryGame.dll will not be ABI compatible with the retail Far Cry 1.4 binaries.")
    endif()
endif()

if(FARCRY_X64)
    message(STATUS "x64 build: CryGame.dll is compile-only and is not verified against the Far Cry 64-bit engine.")
endif()

set(CRYGAME_OUTPUT_DIR "" CACHE PATH "Optional directory (e.g. <FarCry>/Mods/MyMod/Bin32) to copy CryGame.dll into after each build")

set(FARCRY_SOLUTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk/Sources/CryGame C++/Solution1")

if(NOT FARCRY_X64)
    add_subdirectory("${FARCRY_SOLUTION_DIR}/STLPORT" STLport)
endif()
add_subdirectory("${FARCRY_SOLUTION_DIR}/CryCommon" CryCommon)
add_subdirectory("${FARCRY_SOLUTION_DIR}/CryGame" CryGame)
add_subdirectory(sdk/Sources/CgfDump)
if(NOT FARCRY_X64)
    # The x64 skinning path needs Amd64Skinner from CrySkinAMD64.ASM, which is not part of the SDK.
    add_subdirectory(sdk/Sources/CcgDump)
endif()

# Our own modules (not part of the Crytek SDK).
add_subdirectory(mod)
