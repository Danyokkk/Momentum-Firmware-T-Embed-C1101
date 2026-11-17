# Patch the Espressif Wi-Fi archive in the build directory. Never change the
# library in IDF_PATH: other projects may share that ESP-IDF checkout.
idf_build_get_property(_wifi_target IDF_TARGET)
if(_wifi_target STREQUAL "esp32s3")
    idf_component_get_property(_esp_wifi_lib esp_wifi COMPONENT_LIB)
    idf_component_get_property(_esp_wifi_dir esp_wifi COMPONENT_DIR)
    set(_net80211_original "${_esp_wifi_dir}/lib/esp32s3/libnet80211.a")
    set(_net80211_patched "${CMAKE_BINARY_DIR}/wifi_blob/libnet80211.a")
    if(NOT EXISTS "${_net80211_original}")
        message(FATAL_ERROR "Missing ESP-IDF Wi-Fi archive: ${_net80211_original}")
    endif()
    if(NOT TARGET esp_wifi_net80211)
        message(FATAL_ERROR "ESP-IDF did not define the esp_wifi_net80211 target")
    endif()
    if(NOT CMAKE_OBJCOPY OR NOT CMAKE_AR)
        message(FATAL_ERROR "The Xtensa objcopy and ar tools are required for the Wi-Fi adapter")
    endif()

    add_custom_command(
        OUTPUT "${_net80211_patched}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/wifi_blob"
        COMMAND "${PYTHON}" "${CMAKE_SOURCE_DIR}/tools/patch_wifi_lib.py"
                "${_net80211_original}" "${CMAKE_OBJCOPY}" "${CMAKE_AR}"
                "${_net80211_patched}"
        DEPENDS "${_net80211_original}" "${CMAKE_SOURCE_DIR}/tools/patch_wifi_lib.py"
        VERBATIM
        COMMENT "Preparing build-local Wi-Fi library for raw management-frame TX"
    )
    add_custom_target(prepare_wifi_blob DEPENDS "${_net80211_patched}")
    add_dependencies(${_esp_wifi_lib} prepare_wifi_blob)
    set_property(TARGET esp_wifi_net80211 PROPERTY IMPORTED_LOCATION "${_net80211_patched}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_net80211_original}" "${CMAKE_SOURCE_DIR}/tools/patch_wifi_lib.py")
    message(STATUS "ESP32-S3 Wi-Fi library will be adapted in the build directory")
endif()
