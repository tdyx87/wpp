# ============================================================================
#  FindDDraw.cmake -- locate the 32-bit DirectDraw import library
# ----------------------------------------------------------------------------
#  ddraw.lib is not part of the modern Windows SDK "core" set, and VS2008's
#  bundled Platform SDK does not ship it either, so it has to be picked up
#  from whichever SDK happens to be installed:
#
#     * the legacy DirectX SDK            ($DXSDK_DIR/Lib/x86)
#     * Windows 10/11 SDK                 (Lib/<ver>/um/x86)
#     * Windows SDK 6.0A / 7.x            (Microsoft SDKs/Windows/<ver>/Lib)
#     * VS2008 bundled Platform SDK       (VC/PlatformSDK/Lib)
#
#  Defines:
#     DDraw_FOUND
#     DDraw_LIBRARIES
#     DDraw_LIBRARY
# ============================================================================

set(_wpp16_ddraw_hints)

# --- legacy DirectX SDK ----------------------------------------------------
if(DEFINED ENV{DXSDK_DIR})
    list(APPEND _wpp16_ddraw_hints "$ENV{DXSDK_DIR}/Lib/x86")
endif()

# --- Windows 10/11 SDK: prefer the newest version directory ----------------
foreach(_root
        "C:/Program Files (x86)/Windows Kits/10/Lib"
        "C:/Program Files/Windows Kits/10/Lib")
    if(EXISTS "${_root}")
        file(GLOB _wpp16_sdk_vers LIST_DIRECTORIES true "${_root}/*")
        list(SORT _wpp16_sdk_vers ORDER DESCENDING)
        foreach(_v ${_wpp16_sdk_vers})
            list(APPEND _wpp16_ddraw_hints "${_v}/um/x86")
        endforeach()
    endif()
endforeach()

# --- Windows SDK 6.0A / 7.x ------------------------------------------------
file(GLOB _wpp16_old_sdks LIST_DIRECTORIES true
        "C:/Program Files/Microsoft SDKs/Windows/*/Lib"
        "C:/Program Files (x86)/Microsoft SDKs/Windows/*/Lib")
list(SORT _wpp16_old_sdks ORDER DESCENDING)
foreach(_s ${_wpp16_old_sdks})
    list(APPEND _wpp16_ddraw_hints "${_s}")
endforeach()

# --- VS2008 / VS2010 bundled Platform SDK ----------------------------------
list(APPEND _wpp16_ddraw_hints
        "C:/Program Files (x86)/Microsoft Visual Studio 9.0/VC/PlatformSDK/Lib"
        "C:/Program Files/Microsoft Visual Studio 9.0/VC/PlatformSDK/Lib"
        "C:/Program Files (x86)/Microsoft SDKs/Windows/v7.0A/Lib")

find_library(DDraw_LIBRARY
        NAMES ddraw
        PATHS ${_wpp16_ddraw_hints}
        NO_DEFAULT_PATH)

if(NOT DDraw_LIBRARY)
    # Last resort: let the linker search its own LIB path.
    find_library(DDraw_LIBRARY NAMES ddraw)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DDraw
        REQUIRED_VARS DDraw_LIBRARY
        FAIL_MESSAGE
        "Could not find the 32-bit ddraw.lib.
   Install a Windows SDK (the DirectX SDK or a Windows 10/11 SDK both work),
   or point CMake at the file with -DDDraw_LIBRARY=<path to ddraw.lib>.")

if(DDraw_FOUND)
    set(DDraw_LIBRARIES "${DDraw_LIBRARY}")
    mark_as_advanced(DDraw_LIBRARY)
endif()
