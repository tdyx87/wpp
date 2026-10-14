# ============================================================================
#  windsoul -- the core library (windsoul.dll)
# ----------------------------------------------------------------------------
#  Mirrors the original "Release|Win32" configuration of windsoul.vcproj:
#
#    17 C++ translation units in windsoul/  (wsapi.cpp is compiled too, but
#        everything in it is behind `#ifdef _API`, so it contributes nothing)
#     3 C++ translation units in wsapp/     (the WApplication layer)
#     7 NASM blitters                       (16-bit colour, win32 COFF)
#
#  base address 0x11000000, static CRT, 311 exported symbols.
#
#  wsapi.cpp belongs to the separate "API" configuration (wsapi.dll), which is
#  what `_API` selects.  It is kept in the source list so the object stays in
#  sync with the other sources; building the C API variant is a matter of
#  adding `_API` to the defines (see WPP16_BUILD_WSAPI below).
# ============================================================================

set(WINDSOUL_SOURCES
    CANVAS.CPP
    WSTRING.CPP
    achannel.cpp
    bitmap.cpp
    blit.cpp
    datafile.cpp
    font.cpp
    gdi.cpp
    geometry.cpp
    memory.cpp
    object.cpp
    readbmp.cpp
    readtga.cpp
    text.cpp
    timer.cpp
    wsapi.cpp
    wsurface.cpp
)

# The application layer (WApplication / message map) lives in wsapp/ but is
# compiled into windsoul.dll, exactly as the original project does.
set(WINDSOUL_APP_SOURCES
    "${CMAKE_SOURCE_DIR}/wsapp/wsapp.cpp"
    "${CMAKE_SOURCE_DIR}/wsapp/wsmap.cpp"
    "${CMAKE_SOURCE_DIR}/wsapp/wswnd.cpp"
)

# 16-bit colour blitters, hand-written in NASM.  NASM resolves %include
# against its search path, so the folder holding c32.mac / wpp.inc / *clip.mac
# has to be handed over with -I.
set(WINDSOUL_ASM_SOURCES
    alpha16.asm
    blit16.asm
    box16.asm
    font16.asm
    string.asm
    surface.asm
    text16.asm
)

set(WINDSOUL_ASM_HEADERS
    c32.mac
    wpp.inc
    bmpclip.mac
    boxclip.mac
    fontclip.mac
)

wpp16_add_nasm(WINDSOUL_ASM_OBJECTS
    SOURCES      ${WINDSOUL_ASM_SOURCES}
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS      ${WINDSOUL_ASM_HEADERS})

add_library(windsoul SHARED
    ${WINDSOUL_SOURCES}
    ${WINDSOUL_APP_SOURCES}
    ${WINDSOUL_ASM_OBJECTS}
)

target_include_directories(windsoul
    PUBLIC  "${CMAKE_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_definitions(windsoul
    PRIVATE
        _USRDLL
        _MBCS
        WINDSOUL_EXPORTS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
target_link_libraries(windsoul PRIVATE ${DDraw_LIBRARIES})

# The original project fixes the image base so that the DLL and the import
# library stay reproducible across builds.
target_link_options(windsoul PRIVATE "/base:0x11000000")

# Naming follows the original project, which ships a differently named binary
# per configuration:
#     Release -> windsoul.dll / windsoul.lib
#     Debug   -> wsdebug.dll  / wsdebug.lib
# include/windsoul.h emits `#pragma comment(lib,"wsdebug.lib")` for _DEBUG
# consumers, so the Debug import library really has to carry that name.
set_target_properties(windsoul PROPERTIES
    OUTPUT_NAME          "windsoul"
    OUTPUT_NAME_DEBUG    "wsdebug"
    ARCHIVE_OUTPUT_NAME  "windsoul"
    ARCHIVE_OUTPUT_NAME_DEBUG "wsdebug"
)

# A MAP file is handy when inspecting the exported symbol table.
if(MSVC)
    target_link_options(windsoul PRIVATE "/MAP:${WPP16_ARCHIVE_OUTPUT_DIR}/windsoul_$<CONFIG>.map")
endif()

wpp16_apply_target_settings(windsoul)

# ---------------------------------------------------------------------------
# Optional Windows resource script (version info).  Off by default because
# windsoul.rc pulls in afxres.h, which is only present with MFC installed.
# ---------------------------------------------------------------------------
option(WPP16_WINDSOUL_RESOURCE "Compile windsoul.rc into the DLL (needs MFC headers)" OFF)
if(WPP16_WINDSOUL_RESOURCE)
    target_sources(windsoul PRIVATE windsoul.rc)
endif()

message(STATUS "windsoul: ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_LIBRARY_OUTPUT_DIR}/windsoul.dll")
