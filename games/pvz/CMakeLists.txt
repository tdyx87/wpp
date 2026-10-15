# ============================================================================
#  pvz -- "Lawn Defense", a bright cartoon garden-defense prototype on windsoul
# ----------------------------------------------------------------------------
#  Split in two targets on purpose, the same way warfire is:
#
#    pvz_core  the lawn simulation (pvz_core), the procedural cartoon atlas
#              (pvz_art) and the renderer (pvz_render).  Nothing in here needs
#              a window or DirectDraw, so the unit tests can link it and drive
#              the whole thing headless.
#    pvz       the platform shell: window, message loop, DirectDraw surface,
#              keyboard + mouse polling.
#
#  pvz_text.h is GBK (code page 936) because WFont rasterises GB2312; every
#  other source here is pure ASCII.  VS2008 reads all of them with the system
#  ANSI code page, so both work.
#
#  pvz_art.cpp bakes every sprite at start-up and needs gdi32 for CreateFont --
#  hence the explicit gdi32/user32 link below (CreateFont is a GDI call, not a
#  DirectX one, and the atlas must build even in the DLL-less test config).
# ============================================================================

set(PVZ_CORE_SOURCES
    pvz_core.cpp
    pvz_art.cpp
    pvz_render.cpp
)

add_library(pvz_core STATIC ${PVZ_CORE_SOURCES})

target_include_directories(pvz_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(pvz_core
    PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(TARGET windsoul)
    target_link_libraries(pvz_core PUBLIC windsoul)
endif()

# the atlas needs gdi32/user32 for CreateFont, and pvz_art uses WBitmap
target_link_libraries(pvz_core PUBLIC ${DDraw_LIBRARIES} gdi32 user32)

wpp16_apply_target_settings(pvz_core)

# ---------------------------------------------------------------------------

add_executable(pvz pvz_game.cpp)

target_include_directories(pvz PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(pvz
    PRIVATE
        _MBCS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

set_target_properties(pvz PROPERTIES WIN32_EXECUTABLE TRUE)

if(TARGET windsoul)
    target_link_libraries(pvz PRIVATE pvz_core ${DDraw_LIBRARIES} gdi32 user32)
else()
    message(WARNING
        "WPP16_BUILD_GAME=ON but WPP16_BUILD_DLL=OFF: pvz cannot link "
        "without windsoul.dll and its import library.")
endif()

wpp16_apply_target_settings(pvz)

message(STATUS "game:   ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/pvz.exe")
