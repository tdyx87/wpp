# ============================================================================
#  tests -- runtime smoke test for windsoul.dll
# ----------------------------------------------------------------------------
#  test_windsoul.cpp links against the windsoul import library (implicit DLL
#  load) and exercises every subsystem that was touched while optimising the
#  library: WString/Format, WTimer, WFile+data.wdf, WBitmap/blit/ASCII text,
#  WAlphaBitmap/TGA (both RLE branches), WBox/WLine and WCanvas.
#
#  It prints one line per assertion and exits non-zero if anything fails, so
#  it drops straight into CTest.
# ============================================================================

if(NOT TARGET windsoul)
    message(FATAL_ERROR
        "WPP16_BUILD_TESTS=ON requires WPP16_BUILD_DLL=ON: the smoke test "
        "exercises windsoul.dll through its import library.")
endif()

add_executable(windsoul_smoke test_windsoul.cpp)

target_include_directories(windsoul_smoke PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(windsoul_smoke
    PRIVATE
        _CONSOLE
        _CRT_SECURE_NO_WARNINGS
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

target_link_libraries(windsoul_smoke PRIVATE windsoul)

wpp16_apply_target_settings(windsoul_smoke)

# ---------------------------------------------------------------------------
# The test reads dll/data.wdf.  It can locate the pack on its own, but pinning
# the working directory makes the CTest run independent of the build layout.
# ---------------------------------------------------------------------------
add_test(NAME windsoul.smoke
         COMMAND windsoul_smoke
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/dll")

set_tests_properties(windsoul.smoke PROPERTIES
    LABELS "smoke"
    TIMEOUT 60
)

message(STATUS "tests:  ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/windsoul_smoke.exe")

# ============================================================================
#  Neon Barrage -- headless unit test for the game prototype
# ----------------------------------------------------------------------------
#  Covers both layers of games/neonbarrage:
#    * nb_core   -- the simulation, driven directly (no window, no DirectDraw)
#    * nb_render -- driven into an off-screen WCanvas; the pixels are read back
#                   with Update() + BlitTo() so the drawing code is verified
#                   without ever creating a DirectDraw surface.
#
#  The renderer links against windsoul.dll, but nothing in this test opens a
#  window, so it runs unattended in CTest.
# ============================================================================
if(TARGET neonbarrage_core)

    add_executable(neonbarrage_test test_neonbarrage.cpp)

    target_include_directories(neonbarrage_test
        PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/games/neonbarrage"
    )

    target_compile_definitions(neonbarrage_test
        PRIVATE
            _CONSOLE
            _CRT_SECURE_NO_WARNINGS
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )

    target_link_libraries(neonbarrage_test PRIVATE neonbarrage_core)

    wpp16_apply_target_settings(neonbarrage_test)

    add_test(NAME neonbarrage.logic COMMAND neonbarrage_test)

    set_tests_properties(neonbarrage.logic PROPERTIES
        LABELS "game"
        TIMEOUT 60
    )

    message(STATUS "tests:  ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/neonbarrage_test.exe")

else()
    message(STATUS "tests:  neonbarrage_test skipped (WPP16_BUILD_GAME=OFF)")
endif()

# ============================================================================
#  Warfire -- headless unit test for the action RPG prototype
# ----------------------------------------------------------------------------
#  wf_core is driven directly; wf_render is driven into an off-screen WCanvas
#  and the pixels are read back.  The sprite atlas needs a GDI font, so the
#  renderer half of the test is skipped (not failed) if that is unavailable.
# ============================================================================
if(TARGET warfire_core)

    add_executable(warfire_test test_warfire.cpp)

    target_include_directories(warfire_test
        PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/games/warfire"
    )

    target_compile_definitions(warfire_test
        PRIVATE
            _CONSOLE
            _CRT_SECURE_NO_WARNINGS
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )

    target_link_libraries(warfire_test PRIVATE warfire_core)

    wpp16_apply_target_settings(warfire_test)

    add_test(NAME warfire.logic COMMAND warfire_test)

    set_tests_properties(warfire.logic PROPERTIES
        LABELS "game"
        TIMEOUT 90
    )

    message(STATUS "tests:  ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/warfire_test.exe")

else()
    message(STATUS "tests:  warfire_test skipped (WPP16_BUILD_GAME=OFF)")
endif()

# ============================================================================
#  Guofeng -- headless unit test for the isometric prototype
# ----------------------------------------------------------------------------
#  gf_core (the projection, the scene, the camera) is driven directly; the
#  atlas and gf_render are driven into an off-screen WCanvas and the pixels are
#  read back.  The atlas needs a GDI font for its labels, so the atlas and
#  render sections are skipped (not failed) if that is unavailable.
#
#  Baking the ground sheet is the slow part: 2048x1024 pixels of procedural
#  texture, plus a thumbnail pass.  Hence the longer timeout.
# ============================================================================
if(TARGET guofeng_core)

    add_executable(guofeng_test test_guofeng.cpp)

    target_include_directories(guofeng_test
        PRIVATE
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/games/guofeng"
    )

    target_compile_definitions(guofeng_test
        PRIVATE
            _CONSOLE
            _CRT_SECURE_NO_WARNINGS
            $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
    )

    target_link_libraries(guofeng_test PRIVATE guofeng_core)

    wpp16_apply_target_settings(guofeng_test)

    add_test(NAME guofeng.logic COMMAND guofeng_test)

    set_tests_properties(guofeng.logic PROPERTIES
        LABELS "game"
        TIMEOUT 120
    )

    message(STATUS "tests:  ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/guofeng_test.exe")

else()
    message(STATUS "tests:  guofeng_test skipped (WPP16_BUILD_GAME=OFF)")
endif()
