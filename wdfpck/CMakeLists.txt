# ============================================================================
#  wdfpck -- resource-pack (.wdf) packer / unpacker, a small console tool
# ----------------------------------------------------------------------------
#  Self-contained: it only uses the C++ standard library, no windsoul headers.
# ============================================================================

add_executable(wdfpck wdfpck.cpp)

target_compile_definitions(wdfpck
    PRIVATE
        _CONSOLE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

wpp16_apply_target_settings(wdfpck)

message(STATUS "wdfpck: ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_RUNTIME_OUTPUT_DIR}/wdfpck.exe")
