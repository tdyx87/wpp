# ============================================================================
#  neonbarrage -- "Neon Barrage", a vertical bullet-hell built on windsoul
# ----------------------------------------------------------------------------
#  Split in two targets on purpose:
#
#    neonbarrage_core  the simulation (nb_core) plus the renderer (nb_render),
#                      which only ever touches a WCanvas.  Because nothing in
#                      here needs a window or DirectDraw, the unit tests can
#                      link it and drive the whole thing headless.
#    neonbarrage       the platform shell: window, message loop, DirectDraw
#                      surface, keyboard polling.
#
#  All sources are pure ASCII so they compile under any source code page.
# ============================================================================

set(NB_CORE_SOURCES
    nb_core.cpp
    nb_render.cpp
)

add_library(neonbarrage_core STATIC ${NB_CORE_SOURCES})

target_include_directories(neonbarrage_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(neonbarrage_core
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(TARGET windsoul)
    target_link_libraries(neonbarrage_core PUBLIC windsoul)
endif()

wpp16_apply_target_settings(neonbarrage_core)

# ---------------------------------------------------------------------------

add_executable(neonbarrage nb_game.cpp)

target_include_directories(neonbarrage PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(neonbarrage
    PRIVATE
        _MBCS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

set_target_properties(neonbarrage PROPERTIES WIN32_EXECUTABLE TRUE)

if(TARGET windsoul)
    target_link_libraries(neonbarrage PRIVATE neonbarrage_core ${DDraw_LIBRARIES})
else()
    message(WARNING
        "WPP16_BUILD_GAME=ON but WPP16_BUILD_DLL=OFF: neonbarrage cannot link "
        "without windsoul.dll and its import library.")
endif()

wpp16_apply_target_settings(neonbarrage)

message(STATUS "game:   ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/neonbarrage.exe")
