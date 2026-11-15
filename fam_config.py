import os

MANIFEST_ROOTS = [
    "components",
    "applications",
    "applications_user",
]

APP_SOURCE_OVERRIDES = {
    "desktop": "applications",
    # components/desktop_settings is an ESP32 stub; use the complete PIN/settings app.
    "desktop_settings": "applications",
    "storage": "applications",
}

APPS = [
    "input",
    "notification",
    "gui",
    "dialogs",
    "locale",
    "cli",
    "cli_vcp",
    "storage",
    "storage_start",
    "power",
    "power_start",
    "power_settings",
    "loader",
    "loader_start",
    "namechanger_srv",
    "spoofing_settings",
    "notification_settings",
    "interface_settings",
    "system_settings",  # units, date/time format, log level, orientation, sleep
    "desktop_settings",
    "desktop",
    "archive",
    "about",
    "bt_settings",
    "example_apps_data",
    "example_apps_assets",
    "example_number_input",
    "clock",
    # "clock_settings" / "clock_settings_start" need an RTC-alarm HAL this
    # ESP32 port lacks (furi_hal_rtc_set_alarm / furi_hal_clock.h). Left out
    # until that HAL is implemented; the Clock stopwatch app above still builds.
    "bad_usb",
    "subghz",
    # "subghz_remote" bundles its own copy of helpers/txrx/subghz_txrx.c, which
    # collides with the main Sub-GHz app's (duplicate symbols) when built into
    # the image. It is a standalone FAP upstream. Left out until its txrx helper
    # is deduplicated against the main app or it is packaged as a FAP.
    "cli_subghz",
    "subghz_load_dangerous_settings",
    "passport",
    "nfc",
    "infrared",
    # "gpio" (USB-UART bridge, manual GPIO control, 5V/VBUS switch) removed:
    # not useful on the T-Embed hardware. Re-add here to build the app again.
    # "lfrfid" (125 kHz RFID) disabled: no currently supported board has the
    # LF-RFID hardware. Re-add here to build the app again.
    "wlan",
    "wifi_lab",  # passive Wi-Fi diagnostics; replaces "wlan" in the lab profile
    "ble_lab",  # passive BLE discovery; replaces "ble_spam" in the lab profile
    "wifi",
    "ota_updater",
    "streaming",
    "nrf24",
    "ble_spam",
    "js_app",
    "js_event_loop",
    "js_gui",
    "js_gui__loading",
    "js_gui__empty_screen",
    "js_gui__submenu",
    "js_gui__text_input",
    "js_gui__number_input",
    "js_gui__button_panel",
    "js_gui__popup",
    "js_gui__button_menu",
    "js_gui__menu",
    "js_gui__vi_list",
    "js_gui__byte_input",
    "js_gui__text_box",
    "js_gui__dialog",
    "js_gui__file_picker",
    "js_gui__widget",
    "js_gui__icon",
    "js_notification",
    "js_math",
    "js_storage",
    "js_subghz",
    "js_infrared",
    "js_blebeacon",
    # js_serial, js_gpio, js_i2c, js_spi excluded - need HAL porting
]

# Boards without NFC / IR hardware – exclude the corresponding apps
_board = os.environ.get("FLIPPER_BOARD", "")
# NOTE: furi_hal_usb_u2f.c is only built for esp32s3. "smoochie" was removed
# here because it is not a real board in this tree (no board_smoochie.h, no
# sdkconfig.defaults.smoochie) and therefore not an esp32s3 target.
_boards_with_u2f = {
    "esp32s3_generic",
    "lilygo_t_embed_cc1101",
    "paradoxt",
    "thormini",
}
_boards_without_nfc = {"waveshare_c6", "waveshare_c6_1.9", "waveshare_c6_1.47"}
_boards_without_ir = {"waveshare_c6", "waveshare_c6_1.9", "waveshare_c6_1.47"}

# Wolf3D shares Doom's requirements (PSRAM, ST7789 320xN, I2S speaker).
# Doom läuft ebenfalls nur auf T-Embed (PSRAM + 16 MB Flash) — wird aber als
# externer FAP gebaut (steht nicht in APPS), Block bleibt unten zur Klarheit.
_boards_without_wolf3d = {"waveshare_c6", "waveshare_c6_1.9", "waveshare_c6_1.47"}

if _board in _boards_without_nfc:
    APPS = [a for a in APPS if a != "nfc"]

if _board in _boards_with_u2f:
    APPS.append("u2f")

# waveshare_c6_1.9: external CC1101 module wired up (pins in board_waveshare_c6_1.9.h,
# BOARD_HAS_SUBGHZ=1) → SubGHz built in. 1.47 has no module → stays excluded.
_boards_without_subghz = {"waveshare_c6_1.47"}

# NRF24 plugs into the LORA slot (T-Embed CC1101). Boards without the slot
# don't have the required pin defines.
_boards_without_nrf24 = {"waveshare_c6", "waveshare_c6_1.9", "waveshare_c6_1.47"}

if _board in _boards_without_ir:
    APPS = [a for a in APPS if a not in ("infrared", "js_infrared")]

# GPIO app (USB-UART bridge, GPIO manual control, 5V/VBUS switch) is not built on
# any board. Non-S3 boards keep their 5V switch in Infrared -> GPIO Settings
# (see INFRARED_HAS_5V_SETTINGS in applications/main/infrared/infrared_app_i.h).

if _board in _boards_without_subghz:
    APPS = [a for a in APPS if a not in ("subghz", "cli_subghz", "subghz_load_dangerous_settings", "js_subghz")]

if _board in _boards_without_nrf24:
    APPS = [a for a in APPS if a != "nrf24"]

# NOTE: cli_vcp and dolphin look like easy RAM wins (~4KB stack each) but both
# are hard-wired into the desktop service: desktop.c calls cli_vcp_enable/disable
# directly (USB-Storage/qFlipper toggles) and lists "dolphin" in its `requires`
# while opening RECORD_DOLPHIN. Dropping either breaks the link / hangs the UI,
# so they stay. The real RAM wins are BLE-off (~68KB) + smaller WiFi buffers.

# qFlipper and USB-Storage are no longer standalone apps — they live in the
# desktop lock menu (applications/services/desktop). The menu gates them itself:
# qFlipper / USB-Storage behind a compile-time USB-OTG check (ESP32-S3/S2 only).
# So there is nothing to exclude here per board anymore.
# Note: nothing installs the TinyUSB composite at boot — doing so would switch
# the internal USB PHY to OTG and kill the USB-Serial-JTAG bridge that esptool
# uses, breaking the next `./buildAndFlash_T-Embed.sh` cycle. The composite is
# installed lazily, only when the user enables qFlipper / opens USB-Storage.

if _board in _boards_without_wolf3d:
    APPS = [a for a in APPS if a != "wolf3d"]
# (wolf3d und doom stehen nicht in APPS — externer FAP-Pfad. Block bleibt für Klarheit.)

# ---------------------------------------------------------------------------
# Lab profile (the only profile this fork builds; see CMakeLists.txt and
# docs/tembed-plus/FEATURE_MATRIX.md).
#
# LAB_EXCLUDED_APPS: built-in apps that are not compiled at all. The value is
# the reason shown in the feature matrix.
# LAB_EXCLUDED_SOURCES: for mixed apps that keep their benign parts, source
# globs (relative to the app directory) that are not compiled. The remaining
# files reference the dropped parts only inside #ifndef FURI_PROFILE_LAB.
# ---------------------------------------------------------------------------
LAB_EXCLUDED_APPS = {
    "wlan": "deauth, SAE flood, beacon spam, evil portal, MITM, printer hijack "
            "and unverified-TLS OTA live in this app; a passive scanner replaces it",
    "ble_spam": "BLE advertisement spam and writes to third-party devices",
    "bad_usb": "scripted keystroke injection over USB and BLE",
    "js_blebeacon": "lets scripts broadcast arbitrary BLE advertisements",
}

LAB_EXCLUDED_SOURCES = {
    # nRF24: only module detection and the receive-only spectrum view remain.
    "nrf24": [
        "scenes/scene_jam*.c",
        "scenes/scene_mj_*.c",
        "helpers/nrf24_jam_*.c",
        "helpers/nrf24_mj_*.c",
        "helpers/nrf24_channel_source.c",
        "views/nrf24_jam_view.c",
        "views/nrf24_mj_*.c",
        "views/nrf24_scan_view.c",
        "nrf24_wifi_scan.c",
    ],
    # NFC: read-only. The gated scenes (see nfc_scene_config.h) are replaced by
    # scenes/nfc_scene_lab_blocked.c and their sources are not compiled.
    "nfc": [
        "scenes/nfc_scene_emulate.c",
        "scenes/nfc_scene_write.c",
        "scenes/nfc_scene_rpc.c",
        "scenes/nfc_scene_mf_ultralight_c_dict_attack.c",
        "scenes/nfc_scene_mf_ultralight_unlock_menu.c",
        "scenes/nfc_scene_mf_ultralight_unlock_warn.c",
        "scenes/nfc_scene_mf_ultralight_key_input.c",
        "scenes/nfc_scene_mf_ultralight_capture_pass.c",
        "scenes/nfc_scene_des_auth_key_input.c",
        "scenes/nfc_scene_des_auth_unlock_warn.c",
        "scenes/nfc_scene_mf_classic_dict_attack.c",
        "scenes/nfc_scene_mf_classic_detect_reader.c",
        "scenes/nfc_scene_mf_classic_mfkey_nonces_info.c",
        "scenes/nfc_scene_mf_classic_mfkey_complete.c",
        "scenes/nfc_scene_mf_classic_update_initial.c",
        "scenes/nfc_scene_mf_classic_update_initial_success.c",
        "scenes/nfc_scene_mf_classic_update_initial_wrong_card.c",
        "scenes/nfc_scene_mf_classic_show_keys.c",
        "scenes/nfc_scene_mf_classic_keys.c",
        "scenes/nfc_scene_mf_classic_keys_list.c",
        "scenes/nfc_scene_mf_classic_keys_delete.c",
        "scenes/nfc_scene_mf_classic_keys_add.c",
        "scenes/nfc_scene_mf_classic_keys_warn_duplicate.c",
        "scenes/nfc_scene_mf_ultralight_c_keys.c",
        "scenes/nfc_scene_mf_ultralight_c_keys_list.c",
        "scenes/nfc_scene_mf_ultralight_c_keys_delete.c",
        "scenes/nfc_scene_mf_ultralight_c_keys_add.c",
        "scenes/nfc_scene_mf_ultralight_c_keys_warn_duplicate.c",
        "scenes/nfc_scene_set_type.c",
        "scenes/nfc_scene_set_sak.c",
        "scenes/nfc_scene_set_atqa.c",
        "scenes/nfc_scene_set_uid.c",
        "scenes/nfc_scene_slix_unlock_menu.c",
        "scenes/nfc_scene_slix_key_input.c",
        "scenes/nfc_scene_slix_unlock.c",
        "scenes/nfc_scene_slix_unlock_success.c",
        "scenes/nfc_scene_generate_info.c",
        "scenes/nfc_scene_chameleon_wipe.c",
        "scenes/nfc_scene_clone_place_blank.c",
    ],
    # Sub-GHz: no jammer, no code brute-forcer. (The "dangerous frequency"
    # opt-in is neutralised in furi_hal_subghz_set_dangerous_frequency(), which
    # also covers CLI, JS and external apps.)
    "subghz": [
        "scenes/subghz_scene_jammer.c",
        "scenes/subghz_scene_bf_*.c",
        "helpers/subbrute_*.c",
        "helpers/radio_device_loader.c",  # jammer-only radio selection
        "views/subbrute_*.c",
        "views/subghz_jammer.c",
    ],
}

if os.environ.get("FLIPPER_PROFILE", "lab") != "lab":
    raise RuntimeError("Only FLIPPER_PROFILE=lab is supported by this fork")

APPS = [a for a in APPS if a not in LAB_EXCLUDED_APPS]

EXTRA_EXT_APPS = []
TARGET_HW = 32
AUTORUN_APP = ""
