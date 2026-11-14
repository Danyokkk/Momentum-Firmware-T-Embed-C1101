idf_component_register(
    SRCS
        "wlan_hal.c"
        "wlan_passwords.c"
        "wifi_settings.c"
        "wifi_service.c"
        # wifite/{core,pixie,radio,link}.c (WPS Pixie Dust / online PIN attack
        # engine) are not built: this fork only has the lab profile. They also
        # need the modified esp_wps.c that cmake/wifite_sdk_adapter.cmake swapped
        # into the SDK, which the top-level CMakeLists.txt no longer does.
    INCLUDE_DIRS "."
    # No PRIV_INCLUDE_DIRS into the SDK's wpa_supplicant internals: only the
    # removed WPS attack engine needed them, and splicing a Windows $ENV{IDF_PATH}
    # ("C:\Espressif...") into this list broke configuration on Windows.
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
        wpa_supplicant
        notification
)
