cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "components")

# ---------------------------------------------------------------------------
# Build profile.
#
# "lab" is the only profile this fork builds. It is a passive-by-default,
# authorised-lab firmware: the features listed as "excluded for safety" in
# docs/tembed-plus/FEATURE_MATRIX.md are not compiled, and the build never
# modifies the installed ESP-IDF. The policy lives in three places:
#   - here            : no SDK patching, FURI_PROFILE_LAB=1 for all C/C++ code
#   - fam_config.py   : apps and per-app sources that are left out
#   - #ifndef FURI_PROFILE_LAB guards in the remaining mixed apps
# tools/tembed_plus/check_lab_exclusions.py verifies the result on the linked
# image, so a regression fails the build check instead of shipping.
# ---------------------------------------------------------------------------
set(FLIPPER_PROFILE "lab" CACHE STRING "Build profile (only \"lab\" is supported by this fork)")
if(NOT FLIPPER_PROFILE STREQUAL "lab")
    message(FATAL_ERROR
        "FLIPPER_PROFILE='${FLIPPER_PROFILE}' is not supported. This fork only builds the "
        "\"lab\" profile; see docs/tembed-plus/FEATURE_MATRIX.md.")
endif()
set(ENV{FLIPPER_PROFILE} "${FLIPPER_PROFILE}")

# ---------------------------------------------------------------------------
# Upstream patched $IDF_PATH/components/esp_wifi/lib/esp32s3/libnet80211.a in
# place here (tools/patch_wifi_lib.py) so the Wi-Fi blob would transmit raw
# deauth/disassoc frames. That rewrote the developer's ESP-IDF installation for
# every project using it and is the enabling step for the deauth features, so
# this fork does not do it. tools/tembed_plus/check_idf_pristine.py confirms the
# installed blob still matches Espressif's release.
# ---------------------------------------------------------------------------

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
# Visible to every component and every generated app target.
idf_build_set_property(COMPILE_DEFINITIONS "FURI_PROFILE_LAB=1" APPEND)
project(furi_esp32)
# cmake/wifite_sdk_adapter.cmake (swaps the SDK's esp_wps.c for a modified copy
# used by the WPS PIN attack engine) is intentionally not included.
