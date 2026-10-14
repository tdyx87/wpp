# ============================================================================
#  wsaddon -- optional add-on layer (static library)
# ----------------------------------------------------------------------------
#  NOTE ON SCOPE
#  -------------
#  The original wsaddons project also contains lozenge.cpp, mask.cpp,
#  richtext.cpp, sptani.cpp, sptcache.cpp, sptdraw.cpp, switch.cpp, terra.cpp,
#  wsprite.cpp and zoombmp.cpp, plus the Sprite/ and addon/ header folders.
#  Those files are NOT part of this source snapshot, so only the two add-ons
#  that are present here are built.  Turn the target off with
#  -DWPP16_BUILD_ADDONS=OFF if you only need the core library.
#
#  The original project also expected pre-built sprite blitters
#  (Release\sptblit.obj, Release\lozblit.obj) produced by NASM from
#  sptblit.asm / lozblit.asm -- also absent from this snapshot.
# ============================================================================

set(WSADDON_SOURCES
    rand.cpp
    tips.cpp
)

add_library(wsaddon STATIC ${WSADDON_SOURCES})

target_include_directories(wsaddon
    PUBLIC "${CMAKE_SOURCE_DIR}/include"
)

target_compile_definitions(wsaddon
    PUBLIC  WINDSOULADD_STATICLIB
    PRIVATE
        _LIB
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

wpp16_apply_target_settings(wsaddon)

message(STATUS "wsaddon: ${CMAKE_CURRENT_SOURCE_DIR} -> ${WPP16_ARCHIVE_OUTPUT_DIR}/wsaddon.lib")
