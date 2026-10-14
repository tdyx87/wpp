# ============================================================================
#  wpp16_compiler.cmake -- shared toolchain / target configuration
# ----------------------------------------------------------------------------
#  Centralises everything that is specific to building this 1999-2001 era
#  code base with a modern build driver:
#
#    * the legacy preprocessor defines the original .dsp files relied on
#    * the static CRT (/MT) used by every original configuration
#    * GBK source encoding (the sources carry GB2312 Chinese comments)
#    * a single helper, wpp16_apply_target_settings(), used by all targets
# ============================================================================

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Toolchain sanity
# ---------------------------------------------------------------------------
if(NOT WIN32)
    message(FATAL_ERROR
        "Windsoul++ targets Win32 only: the library links DirectDraw/DirectSound "
        "and contains x86 inline assembly (timer.cpp) plus 32-bit NASM objects.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "Windsoul++ is 32-bit only (RGB565 bitfields, x86 NASM blitters, "
        "MSVC inline __asm).  Configure with -A Win32 (or a 32-bit toolchain).")
endif()

set(WPP16_IS_MSVC FALSE)
if(MSVC)
    set(WPP16_IS_MSVC TRUE)
else()
    message(WARNING
        "Windsoul++ is written for MSVC: timer.cpp uses MSVC/BCB inline assembly "
        "and the sources are compiled with the MSVC-compatible `_MSVC` macro. "
        "Building with '${CMAKE_CXX_COMPILER_ID}' is untested and may fail.")
endif()

# ---------------------------------------------------------------------------
# Source character set
# ---------------------------------------------------------------------------
# Every .cpp/.h in this project is encoded in GBK (code page 936).  MSVC 2015+
# defaults to UTF-8 detection and therefore needs to be told explicitly; older
# toolsets always use the process ANSI code page.
if(WPP16_IS_MSVC)
    if(MSVC_VERSION GREATER_EQUAL 1900)
        add_compile_options("/source-charset:gbk")
    else()
        # VS2008 and older have no /source-charset switch.  They interpret the
        # sources with the system ANSI code page, which must be 936.
        if(NOT CMAKE_HOST_SYSTEM_VERSION STREQUAL "")
            execute_process(
                COMMAND cmd /c chcp
                OUTPUT_VARIABLE _wpp16_chcp
                ERROR_QUIET)
            string(REGEX MATCH "([0-9]+)" _wpp16_acp "${_wpp16_chcp}")
            if(_wpp16_acp AND NOT _wpp16_acp STREQUAL "936")
                message(WARNING
                    "Active code page is ${_wpp16_acp}, not 936.  The GBK-encoded "
                    "sources may be misread by MSVC ${MSVC_VERSION}.  Consider "
                    "running in a code page 936 console (chcp 936).")
            endif()
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Legacy preprocessor defines
# ---------------------------------------------------------------------------
#   NDEBUG/_DEBUG      -- release vs debug
#   WIN32/_WINDOWS     -- Win32 GUI subsystem
#   _MSVC              -- the code base branches on this to select MSVC idioms
#   _USRDLL            -- building a DLL that uses the static CRT
#   _MBCS              -- single-byte ANSI build, as in the original .dsp
add_compile_definitions(_MSVC WIN32 _WINDOWS)

if(WPP16_SILENCE_CRT_DEPRECATION)
    # The legacy code uses sprintf/strcpy/fopen and friends on purpose.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

# ---------------------------------------------------------------------------
# Static CRT: /MT (Release) and /MTd (Debug), matching RuntimeLibrary="0"/"1"
# ---------------------------------------------------------------------------
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Warning level
# ---------------------------------------------------------------------------
if(WPP16_IS_MSVC)
    if(WPP16_STRICT_WARNINGS)
        add_compile_options(/W4)
    else()
        add_compile_options(/W3)
    endif()
    # C4819: "file contains a character that cannot be represented in the
    # current code page" -- only fires for UTF-8 files, harmless here.
    add_compile_options(/wd4819)

    if(MSVC_VERSION GREATER_EQUAL 1910)
        # VS2017 (19.16) and later front-ends emit two warnings the VC9-era
        # sources never triggered.  Both are benign compiler-version artifacts:
        #   C4595 - global operator new/delete declared inline (include/wsmemory.h)
        #   C4251 - dll-export of a class with a non-exported std-container member
        # They do not affect correctness (all unit tests pass), so silence them
        # to keep the VS2017+ build at the project's 0-warning bar.  VS2008
        # (MSVC 15.00) is excluded by the version guard.
        add_compile_options(/wd4251 /wd4595)
    endif()
endif()

# ---------------------------------------------------------------------------
# wpp16_apply_target_settings(<target>)
#   Common post-creation settings for every target in the project.
# ---------------------------------------------------------------------------
function(wpp16_apply_target_settings target)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set_target_properties(${target} PROPERTIES
        # Keep the source tree clean: everything goes into the build tree.
        FOLDER "wpp16"
    )

    # Every artifact lands in one shared directory, so a Debug and a Release
    # build of the same target would overwrite each other.  Visual Studio then
    # decides the surviving file is newer than the objects of the configuration
    # you are building and skips it, and the linker picks up something built
    # against the other CRT -- LNK1104, or worse, a silent mismatch.  A stale
    # executable is quieter still: `cmake --build --preset ...-release` followed
    # by `./game.exe` can hand you the Debug build.  Suffix the Debug artifact
    # the way windsoul.dll becomes wsdebug.dll.  (Shared libraries name their
    # own import library.)
    get_target_property(_wpp16_type ${target} TYPE)
    if(_wpp16_type STREQUAL "STATIC_LIBRARY")
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME_DEBUG         "${target}d"
            ARCHIVE_OUTPUT_NAME_DEBUG "${target}d"
        )
    elseif(_wpp16_type STREQUAL "EXECUTABLE")
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME_DEBUG         "${target}d"
            RUNTIME_OUTPUT_NAME_DEBUG "${target}d"
        )
    endif()

    if(WPP16_IS_MSVC)
        # Match the original Release configuration: fast code, string pooling,
        # COMDAT folding, and no incremental linking.
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/GF>)

        if(NOT MSVC_VERSION GREATER_EQUAL 1900)
            # VS2008 and earlier have no /MP; keep builds serial.
            target_compile_options(${target} PRIVATE /nologo)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# wpp16_add_nasm(<out_var>
#                SOURCES      <file.asm> ...
#                INCLUDE_DIRS <dir> ...
#                [DEPENDS     <c32.mac> ...]
#                [WORKING_DIR <dir>])
#
#   Assembles each source with NASM (-f win32) into a per-configuration .obj
#   and returns the list of objects in <out_var> so the caller can add them to
#   a target.
#
#   This exists because the Visual Studio generators do NOT implement CMake's
#   ASM_NASM language support -- only the Makefile and Ninja generators do.
#   Driving NASM from add_custom_command() works with every generator.
# ---------------------------------------------------------------------------
function(wpp16_add_nasm out_var)
    cmake_parse_arguments(ARG "" "WORKING_DIR" "SOURCES;INCLUDE_DIRS;DEPENDS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "wpp16_add_nasm() called without SOURCES")
    endif()

    set(_include_flags "")
    foreach(_dir ${ARG_INCLUDE_DIRS})
        # NASM only searches a -I directory when its name ends with a path
        # separator.  Forward slashes are used because a trailing backslash
        # would escape the closing quote once cmd.exe sees the command line.
        file(TO_CMAKE_PATH "${_dir}" _dir_fwd)
        if(NOT _dir_fwd MATCHES "/$")
            set(_dir_fwd "${_dir_fwd}/")
        endif()
        list(APPEND _include_flags "-I${_dir_fwd}")
    endforeach()

    set(_workdir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(ARG_WORKING_DIR)
        set(_workdir "${ARG_WORKING_DIR}")
    endif()

    set(_objects "")
    foreach(_asm ${ARG_SOURCES})
        get_filename_component(_name "${_asm}" NAME_WE)
        get_filename_component(_abs "${_asm}" ABSOLUTE)

        set(_obj "${CMAKE_CURRENT_BINARY_DIR}/nasm/$<CONFIG>/${_name}.obj")

        add_custom_command(
            OUTPUT  "${_obj}"
            COMMAND "${CMAKE_ASM_NASM_COMPILER}"
                    -f ${CMAKE_ASM_NASM_OBJECT_FORMAT}
                    ${_include_flags}
                    -o "${_obj}"
                    "${_abs}"
            # NASM resolves %include against the working directory, so running
            # from the assembly source folder makes the plain `%include
            # "c32.mac"` lines resolve without any quoting gymnastics.
            WORKING_DIRECTORY "${_workdir}"
            DEPENDS "${_abs}" ${ARG_DEPENDS}
            COMMENT "NASM ${_name}.asm"
            VERBATIM)

        set_source_files_properties("${_obj}" PROPERTIES GENERATED TRUE)
        list(APPEND _objects "${_obj}")
    endforeach()

    set(${out_var} "${_objects}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# wpp16_print_summary()
# ---------------------------------------------------------------------------
function(wpp16_print_summary)
    set(_gen "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        string(APPEND _gen " / ${CMAKE_GENERATOR_PLATFORM}")
    endif()

    set(_cfg "${CMAKE_BUILD_TYPE}")
    if(NOT _cfg)
        set(_cfg "multi-config")
    endif()

    set(_targets "")
    if(WPP16_BUILD_DLL)
        list(APPEND _targets "windsoul.dll")
    endif()
    if(WPP16_BUILD_TOOLS)
        list(APPEND _targets "wdfpck.exe")
    endif()
    if(WPP16_BUILD_ADDONS)
        list(APPEND _targets "wsaddon.lib")
    endif()
    if(WPP16_BUILD_EXAMPLES)
        list(APPEND _targets "Ex01.exe")
    endif()
    if(WPP16_BUILD_GAME)
        list(APPEND _targets "neonbarrage.exe" "warfire.exe")
    endif()
    if(WPP16_BUILD_TESTS)
        list(APPEND _targets "windsoul_smoke.exe" "neonbarrage_test.exe" "warfire_test.exe")
    endif()
    string(REPLACE ";" " " _targets "${_targets}")

    message(STATUS "")
    message(STATUS "================ Windsoul++ (wpp16) ================")
    message(STATUS "  Compiler      : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "  Compiler path : ${CMAKE_CXX_COMPILER}")
    if(MSVC)
        message(STATUS "  MSVC toolset  : ${MSVC_VERSION}")
    endif()
    message(STATUS "  Generator     : ${_gen}")
    message(STATUS "  Build type    : ${_cfg}")
    message(STATUS "  NASM          : ${CMAKE_ASM_NASM_COMPILER} (${CMAKE_ASM_NASM_OBJECT_FORMAT})")
    message(STATUS "  Runtime dir   : ${WPP16_RUNTIME_OUTPUT_DIR}")
    message(STATUS "  Library dir   : ${WPP16_ARCHIVE_OUTPUT_DIR}")
    message(STATUS "  Targets       : ${_targets}")
    message(STATUS "====================================================")
    message(STATUS "")
endfunction()
