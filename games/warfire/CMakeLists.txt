# ============================================================================
#  warfire -- "Warfire", a top-down action RPG built on windsoul
# ----------------------------------------------------------------------------
#  Split in two targets on purpose:
#
#    warfire_core  the simulation (wf_core), the sprite atlas (wf_art) and the
#                  renderer (wf_render).  Nothing in here needs a window or
#                  DirectDraw, so the unit tests can link it and drive the
#                  whole thing headless.
#    warfire       the platform shell: window, message loop, DirectDraw
#                  surface, keyboard polling.
#
#  wf_text.h is GBK (code page 936) because WFont renders GB2312; every other
#  source here is pure ASCII.  VS2008 reads all of them with the system ANSI
#  code page, so both work.
# ============================================================================

set(WF_CORE_SOURCES
    wf_core.cpp
    wf_art.cpp
    wf_render.cpp
)

add_library(warfire_core STATIC ${WF_CORE_SOURCES})

target_include_directories(warfire_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(warfire_core
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(TARGET windsoul)
    target_link_libraries(warfire_core PUBLIC windsoul)
endif()

# the atlas needs user32/gdi32 for CreateFont, and wf_art uses WBitmap
target_link_libraries(warfire_core PUBLIC ${DDraw_LIBRARIES})

wpp16_apply_target_settings(warfire_core)

# ---------------------------------------------------------------------------

add_executable(warfire wf_game.cpp)

target_include_directories(warfire PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(warfire
    PRIVATE
        _MBCS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

set_target_properties(warfire PROPERTIES WIN32_EXECUTABLE TRUE)

if(TARGET windsoul)
    target_link_libraries(warfire PRIVATE warfire_core ${DDraw_LIBRARIES})
else()
    message(WARNING
        "WPP16_BUILD_GAME=ON but WPP16_BUILD_DLL=OFF: warfire cannot link "
        "without windsoul.dll and its import library.")
endif()

wpp16_apply_target_settings(warfire)

message(STATUS "game:   ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/warfire.exe")
