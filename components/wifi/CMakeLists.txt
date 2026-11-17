idf_component_register(
    SRCS
        "wlan_hal.c"
        "wlan_passwords.c"
        "wifi_settings.c"
        "wifi_service.c"
        "wifite/core.c"
        "wifite/pixie.c"
        "wifite/radio.c"
        "wifite/link.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES wpa_supplicant
    REQUIRES
        furi
        furi_hal
        toolbox
        storage
        btshim
        gui
        esp_wifi
        esp_netif
        esp_event
        lwip
        mbedtls
        notification
)

idf_component_get_property(_wps_dir wpa_supplicant COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${_wps_dir}/src"
    "${_wps_dir}/esp_supplicant/src"
)
