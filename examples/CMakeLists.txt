# ============================================================================
#  examples -- sample applications
# ----------------------------------------------------------------------------
#  Ex01 is the plain Win32 + DirectDraw sample.  Its two translation units
#  (ex01.cpp, main.cpp) define MyApp : WApplication and WinMain; the
#  WApplication implementation itself lives inside windsoul.dll.
#
#  mfcex01 (the MFC document/view variant) is not ported here: it needs the
#  MFC toolset and a wizard-generated .rc, and adds nothing to the library
#  build.  Its original project file is kept under examples/mfcex01/.
# ============================================================================

add_executable(Ex01
    ex01.cpp
    main.cpp
)

target_include_directories(Ex01 PRIVATE "${CMAKE_SOURCE_DIR}/include")

target_compile_definitions(Ex01
    PRIVATE
        _MBCS
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# GUI subsystem: the entry point is WinMain (examples/main.cpp).
set_target_properties(Ex01 PROPERTIES WIN32_EXECUTABLE TRUE)

if(TARGET windsoul)
    target_link_libraries(Ex01 PRIVATE windsoul ${DDraw_LIBRARIES})
else()
    message(WARNING
        "WPP16_BUILD_EXAMPLES=ON but WPP16_BUILD_DLL=OFF: Ex01 will not link, "
        "because it needs windsoul.dll and its import library.")
endif()

wpp16_apply_target_settings(Ex01)

message(STATUS "Ex01:   ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/Ex01.exe")
