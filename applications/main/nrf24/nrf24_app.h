#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <esp_wifi.h>
#include <wifi.h>

#include "views/nrf24_spectrum_view.h"
/* Lab profile: only module detection and the receive-only spectrum view are
 * built. The jammer and MouseJack sources are not compiled (fam_config.py,
 * LAB_EXCLUDED_SOURCES) and nothing below references them. */
#ifndef FURI_PROFILE_LAB
#include "views/nrf24_jam_view.h"
#include "views/nrf24_scan_view.h"
#include "views/nrf24_mj_scan_view.h"
#include "views/nrf24_mj_attack_view.h"
#include "helpers/nrf24_mj_core.h"
#include "helpers/nrf24_channel_source.h"
#endif /* !FURI_PROFILE_LAB */
#include "scenes/scenes.h"

typedef enum {
    Nrf24ViewSubmenu,
    Nrf24ViewWidget,
    Nrf24ViewSpectrum,
#ifndef FURI_PROFILE_LAB
    Nrf24ViewJam,
    Nrf24ViewScan,
    Nrf24ViewJamConfig,
    Nrf24ViewMjScan,
    Nrf24ViewMjAttack,
#endif /* !FURI_PROFILE_LAB */
} Nrf24View;

#define NRF24_WIFI_SCAN_MAX 24

typedef struct Nrf24App {
    Gui* gui;
    SceneManager* scene_manager;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* widget;
    VariableItemList* var_item_list;
    DialogsApp* dialogs;
    Storage* storage;

    View* spectrum_view;
#ifndef FURI_PROFILE_LAB
    View* jam_view; /* unified jam engine */
    View* scan_view; /* activity-scan progress */
    View* mj_scan_view;
    View* mj_attack_view;

    /* Unified jammer selection / scan-cache state */
    Nrf24JamState jam;

    /* WiFi scan results — owned by the wifi-scan scene, consumed by the
     * WiFi channel source. */
    wifi_ap_record_t* wifi_aps;
    uint16_t wifi_ap_count;
    char selected_wifi_ssid[33];
    uint8_t selected_wifi_channel;

    /* MouseJacker shared state (owned by the mj scenes) */
    MjTarget mj_targets[MJ_MAX_TARGETS];
    uint8_t mj_target_count;
    int8_t mj_selected_target; /* -1 = none */
    FuriString* mj_script_path;
    bool mj_auto_mode;
#endif /* !FURI_PROFILE_LAB */

    /* WiFi war beim App-Start global aktiv → wir haben es pausiert (der
     * breitband 2.4-GHz-Jammer schießt sonst die eigene STA-Verbindung ab)
     * und reaktivieren es beim Verlassen. */
    bool wifi_was_paused;
} Nrf24App;
