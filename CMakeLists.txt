cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

# Keep board-specific sdkconfig values isolated from the shared workspace
# sdkconfig. Paradoxt and T-Embed use the shared reboot helper, which needs the
# dual-core and ESP Timer ISR settings from their board defaults. Explicit
# per-build SDKCONFIG paths are preserved; board defaults are appended if needed.
if(NOT DEFINED FLIPPER_BOARD AND DEFINED ENV{FLIPPER_BOARD})
    set(FLIPPER_BOARD "$ENV{FLIPPER_BOARD}")
endif()

if(("${FLIPPER_BOARD}" STREQUAL "paradoxt" OR
    "${FLIPPER_BOARD}" STREQUAL "lilygo_t_embed_cc1101") AND
   EXISTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults.${FLIPPER_BOARD}")
        if(DEFINED SDKCONFIG AND NOT "${SDKCONFIG}" STREQUAL "")
            get_filename_component(_requested_sdkconfig "${SDKCONFIG}" ABSOLUTE
                BASE_DIR "${CMAKE_SOURCE_DIR}")
        else()
            set(_requested_sdkconfig "")
        endif()
        get_filename_component(_shared_sdkconfig "${CMAKE_SOURCE_DIR}/sdkconfig" ABSOLUTE)

        # If this build directory was previously configured by this block for
        # another board, switch to a separate config file instead of reusing
        # that board's generated sdkconfig. A user-supplied path remains intact.
        if(NOT _requested_sdkconfig OR
           "${_requested_sdkconfig}" STREQUAL "${_shared_sdkconfig}" OR
           (DEFINED FURI_AUTO_SDKCONFIG AND
            "${_requested_sdkconfig}" STREQUAL "${FURI_AUTO_SDKCONFIG}"))
            set(_managed_sdkconfig "${CMAKE_BINARY_DIR}/sdkconfig.${FLIPPER_BOARD}")
            set(SDKCONFIG "${_managed_sdkconfig}" CACHE FILEPATH
                "Board-specific ESP-IDF sdkconfig" FORCE)
            set(FURI_AUTO_SDKCONFIG "${_managed_sdkconfig}" CACHE INTERNAL
                "SDKCONFIG path managed by the selected Flipper board" FORCE)
        endif()

        # Keep the original defaults separate from defaults auto-appended by a
        # previous configure, so changing boards never loads both board files.
        if(DEFINED FURI_AUTO_SDKCONFIG_DEFAULTS AND
           "${SDKCONFIG_DEFAULTS}" STREQUAL "${FURI_AUTO_SDKCONFIG_DEFAULTS}")
            set(_base_sdkconfig_defaults "${FURI_BASE_SDKCONFIG_DEFAULTS}")
        elseif(NOT DEFINED SDKCONFIG_DEFAULTS OR "${SDKCONFIG_DEFAULTS}" STREQUAL "")
            set(_base_sdkconfig_defaults "sdkconfig.defaults")
        else()
            set(_base_sdkconfig_defaults "${SDKCONFIG_DEFAULTS}")
        endif()

        set(_board_sdkconfig_defaults_list "${_base_sdkconfig_defaults}")
        list(FIND _board_sdkconfig_defaults_list "sdkconfig.defaults.${FLIPPER_BOARD}"
            _board_defaults_index)
        if(_board_defaults_index EQUAL -1)
            list(APPEND _board_sdkconfig_defaults_list "sdkconfig.defaults.${FLIPPER_BOARD}")
        endif()

        set(SDKCONFIG_DEFAULTS "${_board_sdkconfig_defaults_list}" CACHE STRING
            "ESP-IDF sdkconfig defaults for the selected board" FORCE)
        set(FURI_BASE_SDKCONFIG_DEFAULTS "${_base_sdkconfig_defaults}" CACHE INTERNAL
            "SDKCONFIG_DEFAULTS before board-specific defaults were appended" FORCE)
        set(FURI_AUTO_SDKCONFIG_DEFAULTS "${_board_sdkconfig_defaults_list}" CACHE INTERNAL
            "SDKCONFIG_DEFAULTS managed by the selected Flipper board" FORCE)
    else()
        # Restore the shared project config/defaults when switching this build
        # directory back to a board without an isolated defaults file.
        get_filename_component(_shared_sdkconfig "${CMAKE_SOURCE_DIR}/sdkconfig" ABSOLUTE)
        if(DEFINED SDKCONFIG AND DEFINED FURI_AUTO_SDKCONFIG AND
           NOT "${FURI_AUTO_SDKCONFIG}" STREQUAL "")
            get_filename_component(_requested_sdkconfig "${SDKCONFIG}" ABSOLUTE
                BASE_DIR "${CMAKE_SOURCE_DIR}")
            if("${_requested_sdkconfig}" STREQUAL "${FURI_AUTO_SDKCONFIG}")
                set(SDKCONFIG "${_shared_sdkconfig}" CACHE FILEPATH
                    "ESP-IDF sdkconfig for the selected board" FORCE)
                unset(FURI_AUTO_SDKCONFIG CACHE)
            endif()
        endif()

        if(DEFINED SDKCONFIG_DEFAULTS AND DEFINED FURI_AUTO_SDKCONFIG_DEFAULTS AND
           "${SDKCONFIG_DEFAULTS}" STREQUAL "${FURI_AUTO_SDKCONFIG_DEFAULTS}")
            set(SDKCONFIG_DEFAULTS "${FURI_BASE_SDKCONFIG_DEFAULTS}" CACHE STRING
                "ESP-IDF sdkconfig defaults without board-specific additions" FORCE)
            unset(FURI_AUTO_SDKCONFIG_DEFAULTS CACHE)
            unset(FURI_BASE_SDKCONFIG_DEFAULTS CACHE)
        endif()
endif()

# ---------------------------------------------------------------------------
# Build profile label. No profile-based app or source exclusions are applied.
# ---------------------------------------------------------------------------
set(FLIPPER_PROFILE "full" CACHE STRING "Build profile label")
set(ENV{FLIPPER_PROFILE} "${FLIPPER_PROFILE}")

# ---------------------------------------------------------------------------
# App-Descriptor-Version (esp_app_desc_t.version) aus der zentralen Port-Version
# in components/toolbox/fw_version.h. Damit traegt jede furi_esp32.bin ihre
# Versionsnummer im Image-Header (Offset 0x30) — qT-Embed liest sie vor dem
# Flashen, der OTA-Updater schreibt sie nach dem Flashen in /ext/.fw_version.
# Ohne PROJECT_VER wuerde ESP-IDF "git describe" eintragen.
# ---------------------------------------------------------------------------
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/components/toolbox/fw_version.h")
file(STRINGS "${CMAKE_SOURCE_DIR}/components/toolbox/fw_version.h" _furi_ver_line
    REGEX "^#define[ \t]+FURI_ESP32_VERSION[ \t]+\"")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PROJECT_VER "${_furi_ver_line}")
if(NOT PROJECT_VER)
    message(FATAL_ERROR "FURI_ESP32_VERSION not found in components/toolbox/fw_version.h")
endif()
# Build label from CI (-DFLIPPER_BUILD_LABEL=lab.1a2b3c4 or lab.v0.1.0; empty for
# local builds), appended as semver build metadata. The image header, the web
# flasher's build list and the About screen then all name the same build.
# esp_app_desc_t.version holds 31 characters.
if(DEFINED FLIPPER_BUILD_LABEL AND NOT "${FLIPPER_BUILD_LABEL}" STREQUAL "")
    string(REGEX REPLACE "[^A-Za-z0-9.-]" "-" _flipper_label "${FLIPPER_BUILD_LABEL}")
    set(PROJECT_VER "${PROJECT_VER}+${_flipper_label}")
    string(LENGTH "${PROJECT_VER}" _flipper_version_length)
    if(_flipper_version_length GREATER 31)
        string(SUBSTRING "${PROJECT_VER}" 0 31 PROJECT_VER)
    endif()
endif()
message(STATUS "Firmware version (PROJECT_VER): ${PROJECT_VER}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# dependencies.lock pins the managed components for the ESP32-S3 boards. The
# component manager rewrites that file when another IDF target is built
# from the same tree (it re-solves and picks newer versions), which silently
# un-pins every later build. Builds for other boards therefore pass their own
# lock path (tools/tembed_plus/cross_build.py) and leave this one alone.
if(DEFINED FLIPPER_DEPENDENCIES_LOCK)
    idf_build_set_property(DEPENDENCIES_LOCK "${FLIPPER_DEPENDENCIES_LOCK}")
endif()
project(furi_esp32)
# Keep all SDK adaptations in the build tree; the installed ESP-IDF checkout is
# used as a read-only source for the pinned Wi-Fi libraries and WPS source.
include(${CMAKE_SOURCE_DIR}/cmake/wifite_sdk_adapter.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/wifi_blob_adapter.cmake)
