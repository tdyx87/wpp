# ============================================================================
#  Windsoul++ (wpp16) -- CMake build system
# ----------------------------------------------------------------------------
#  Windsoul++ is a 16-bit (RGB565) 2D game graphics library for Win32,
#  written by Cloud Wu in 1999-2001.  The original build system was
#  MSVC 6 (.dsp/.dsw) + Borland C++ Builder (.bpr/.bpg) + NASM (nasmw).
#
#  This CMake port reproduces the MSVC "Release|Win32" configuration and is
#  verified to build with Visual Studio 2008 (cl 15.00) against the Windows
#  SDK v6.0A, as well as with any newer MSVC toolset.
#
#  Quick start (Visual Studio 2008, from a shell where `nasm` is on PATH):
#      cmake -S . -B build/vs2008 -G "Visual Studio 9 2008"
#      cmake --build build/vs2008 --config Release
#      ctest --test-dir build/vs2008 -C Release --output-on-failure
#
#  Modern toolset (e.g. VS 2022):
#      cmake -S . -B build/vs2022 -G "Visual Studio 17 2022" -A Win32
#      cmake --build build/vs2022 --config Release
#
#  Ninja / NMake also work; the ASM is driven through a custom command so that
#  the Visual Studio generators (which do not implement ASM_NASM) work too.
#
#  See the per-directory CMakeLists.txt files for the mapping between this
#  build system and the original .dsp/.vcproj configurations.
# ============================================================================

cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# NASM must emit 32-bit Win32 COFF objects.  This variable is consumed while
# the ASM_NASM language is being enabled, so it has to be set before project().
# ---------------------------------------------------------------------------
set(CMAKE_ASM_NASM_OBJECT_FORMAT win32 CACHE STRING "NASM object format (Win32 COFF)")

project(wpp16
        VERSION 1.0.0
        DESCRIPTION "Windsoul++ 16-bit color 2D graphics library (Cloud Wu, 1999-2001)"
        LANGUAGES C CXX ASM_NASM)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(WPP16_BUILD_DLL      "Build windsoul.dll (the core library)"          ON)
option(WPP16_BUILD_TOOLS    "Build the wdfpck resource-pack tool"            ON)
option(WPP16_BUILD_ADDONS   "Build the wsaddon static library"               ON)
option(WPP16_BUILD_EXAMPLES "Build the example applications"                 ON)
option(WPP16_BUILD_GAME     "Build the Neon Barrage bullet-hell game"        ON)
option(WPP16_BUILD_TESTS    "Build the runtime smoke test (needs CTest)"     ON)

option(WPP16_STRICT_WARNINGS "Raise the warning level to /W4 (MSVC)"         OFF)
option(WPP16_SILENCE_CRT_DEPRECATION
       "Define _CRT_SECURE_NO_WARNINGS (the legacy code uses sprintf/strcpy)" ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ddraw.lib is not part of the modern Windows SDK core set, so it is looked up
# from the DirectX SDK / a Windows 10-11 SDK / the VS2008 Platform SDK.
find_package(DDraw REQUIRED)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
include(wpp16_compiler)

# All binaries land in one place so that windsoul.dll, its import library and
# the executables that load it can be found without extra configuration.
set(WPP16_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Runtime output directory")
set(WPP16_LIBRARY_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "DLL output directory")
set(WPP16_ARCHIVE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Static/import library output directory")

foreach(_cfg "" "_DEBUG" "_RELEASE" "_RELWITHDEBINFO" "_MINSIZEREL")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${_cfg} "${WPP16_RUNTIME_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY${_cfg} "${WPP16_LIBRARY_OUTPUT_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY${_cfg} "${WPP16_ARCHIVE_OUTPUT_DIR}")
endforeach()

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------
if(WPP16_BUILD_DLL)
    add_subdirectory(windsoul)
endif()

if(WPP16_BUILD_TOOLS)
    add_subdirectory(wdfpck)
endif()

if(WPP16_BUILD_ADDONS)
    add_subdirectory(wsaddon)
endif()

if(WPP16_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(WPP16_BUILD_GAME)
    add_subdirectory(games/neonbarrage)
    add_subdirectory(games/warfire)
endif()

if(WPP16_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
wpp16_print_summary()
