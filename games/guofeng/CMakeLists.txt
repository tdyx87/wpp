# ============================================================================
#  guofeng -- "Guofeng", an isometric Chinese-style village built on windsoul
# ----------------------------------------------------------------------------
#  Split in two targets on purpose, the same way warfire is:
#
#    guofeng_core  the projection and the scene (gf_core), the procedural
#                  atlas (gf_art) and the renderer (gf_render).  Nothing in
#                  here needs a window or DirectDraw, so the unit tests can
#                  link it and drive the whole thing headless.
#    guofeng       the platform shell: window, message loop, DirectDraw
#                  surface, keyboard polling.
#
#  gf_text.h is GBK (code page 936) because WFont rasterises GB2312; every
#  other source here is pure ASCII.  VS2008 reads all of them with the system
#  ANSI code page, so both work.
#
#  gf_art.cpp bakes a 2048x1024 ground sheet at start-up.  That is 4 MB of
#  RGB565 and roughly a second of arithmetic on a cold cache -- worth knowing
#  before wondering why the window takes a moment to appear.
# ============================================================================

set(GF_CORE_SOURCES
    gf_core.cpp
    gf_art.cpp
    gf_render.cpp
)

add_library(guofeng_core STATIC ${GF_CORE_SOURCES})

target_include_directories(guofeng_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(guofeng_core
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(TARGET windsoul)
    target_link_libraries(guofeng_core PUBLIC windsoul)
endif()

# the atlas needs user32/gdi32 for CreateFont, and gf_art uses WBitmap
target_link_libraries(guofeng_core PUBLIC ${DDraw_LIBRARIES})

wpp16_apply_target_settings(guofeng_core)

# ---------------------------------------------------------------------------

add_executable(guofeng gf_game.cpp)

target_include_directories(guofeng PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(guofeng
    PRIVATE
        _MBCS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

set_target_properties(guofeng PROPERTIES WIN32_EXECUTABLE TRUE)

if(TARGET windsoul)
    target_link_libraries(guofeng PRIVATE guofeng_core ${DDraw_LIBRARIES})
else()
    message(WARNING
        "WPP16_BUILD_GAME=ON but WPP16_BUILD_DLL=OFF: guofeng cannot link "
        "without windsoul.dll and its import library.")
endif()

wpp16_apply_target_settings(guofeng)

message(STATUS "game:   ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/guofeng.exe")
