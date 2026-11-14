/**
 * WiFi Lab - passive Wi-Fi diagnostics for the lab profile.
 *
 * What it does:
 *   - passive scan (listens for beacons, transmits nothing), sorted by signal
 *   - per-network detail: BSSID, channel, RSSI, security
 *   - join a network you own, after an explicit on-device confirmation
 *   - connection status (IP, gateway, RSSI)
 *   - save the scan list to the SD card, only when asked
 *
 * What it deliberately does not do: inject frames, deauthenticate, capture
 * handshakes, spoof access points, or touch any client but this device.
 *
 * Replaces applications/main/wlan_app, which is not built in this fork.
 */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>
#include <gui/modules/loading.h>
#include <storage/storage.h>

#include <wifi.h>
#include <wlan_hal.h>
#include <wlan_passwords.h>

#define TAG "WifiLab"

#define WIFI_LAB_MAX_APS            48
#define WIFI_LAB_PASSWORD_MAX       64
#define WIFI_LAB_CONNECT_TIMEOUT_MS 15000
#define WIFI_LAB_EXPORT_DIR         EXT_PATH("apps_data/wifi_lab")

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
} WifiLabAp;

typedef enum {
    WifiLabViewSubmenu,
    WifiLabViewWidget,
    WifiLabViewDialog,
    WifiLabViewTextInput,
    WifiLabViewLoading,
} WifiLabView;

typedef enum {
    WifiLabSceneStart,
    WifiLabSceneScan,
    WifiLabSceneList,
    WifiLabSceneDetail,
    WifiLabSceneConfirmJoin,
    WifiLabScenePassword,
    WifiLabSceneConnecting,
    WifiLabSceneSavePassword,
    WifiLabSceneMessage,
    WifiLabSceneStatus,
    WifiLabSceneCount,
} WifiLabScene;

typedef enum {
    WifiLabEventScanDone = 0x1000,
    WifiLabEventJoinPressed,
    WifiLabEventPasswordEntered,
} WifiLabEvent;

enum {
    WifiLabStartScan,
    WifiLabStartStatus,
    WifiLabStartDisconnect,
};

#define WIFI_LAB_LIST_SAVE 0xFFFFu

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    Widget* widget;
    DialogEx* dialog;
    TextInput* text_input;
    Loading* loading;

    WifiLabAp aps[WIFI_LAB_MAX_APS];
    uint16_t ap_count;
    uint16_t selected;
    bool scan_ok;

    FuriThread* scan_thread;
    volatile bool scan_cancel;

    char password[WIFI_LAB_PASSWORD_MAX + 1];
    bool password_typed; /* entered now, as opposed to read from the SD card */
    uint32_t connect_started;

    FuriString* message;
    bool started_wifi; /* this app brought the radio up */
    bool joined; /* a join succeeded: leave the radio to the WiFi service */
} WifiLab;

/* ------------------------------------------------------------- helpers --- */

static const char* wifi_lab_auth_name(wifi_auth_mode_t mode) {
    switch(mode) {
    case WIFI_AUTH_OPEN:
        return "Open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_OWE:
        return "OWE";
    default:
        return "Other";
    }
}

static const char* wifi_lab_display_ssid(const WifiLabAp* ap) {
    return ap->ssid[0] ? ap->ssid : "(hidden)";
}

static bool wifi_lab_ensure_radio(WifiLab* app) {
    if(wlan_hal_is_started()) return true;
    if(!wlan_hal_start()) return false;
    app->started_wifi = true;
    return true;
}

static void wifi_lab_show_message(WifiLab* app, const char* text) {
    furi_string_set(app->message, text);
    scene_manager_next_scene(app->scene_manager, WifiLabSceneMessage);
}

static int wifi_lab_sort_by_rssi(const void* a, const void* b) {
    return (int)((const WifiLabAp*)b)->rssi - (int)((const WifiLabAp*)a)->rssi;
}

/* CSV field: quoted, quotes doubled, control characters replaced. */
static void wifi_lab_csv_field(FuriString* out, const char* text) {
    furi_string_push_back(out, '"');
    for(const char* c = text; *c; c++) {
        if(*c == '"') {
            furi_string_cat(out, "\"\"");
        } else if((unsigned char)*c < 0x20) {
            furi_string_push_back(out, ' ');
        } else {
            furi_string_push_back(out, *c);
        }
    }
    furi_string_push_back(out, '"');
}

static bool wifi_lab_export(WifiLab* app, FuriString* path_out) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    storage_simply_mkdir(storage, WIFI_LAB_EXPORT_DIR);
    furi_string_printf(
        path_out, "%s/scan_%lu.csv", WIFI_LAB_EXPORT_DIR, (unsigned long)furi_hal_rtc_get_timestamp());

    if(storage_file_open(file, furi_string_get_cstr(path_out), FSAM_WRITE, FSOM_CREATE_NEW)) {
        FuriString* line = furi_string_alloc_set("ssid,bssid,channel,rssi_dbm,security\n");
        ok = storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line)) ==
             furi_string_size(line);
        for(uint16_t i = 0; ok && i < app->ap_count; i++) {
            const WifiLabAp* ap = &app->aps[i];
            furi_string_reset(line);
            wifi_lab_csv_field(line, ap->ssid);
            furi_string_cat_printf(
                line,
                ",%02X:%02X:%02X:%02X:%02X:%02X,%u,%d,%s\n",
                ap->bssid[0],
                ap->bssid[1],
                ap->bssid[2],
                ap->bssid[3],
                ap->bssid[4],
                ap->bssid[5],
                ap->channel,
                ap->rssi,
                wifi_lab_auth_name(ap->authmode));
            ok = storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line)) ==
                 furi_string_size(line);
        }
        furi_string_free(line);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* ---------------------------------------------------------------- scan --- */

static int32_t wifi_lab_scan_worker(void* context) {
    WifiLab* app = context;
    wifi_ap_record_t* records = NULL;
    uint16_t found = 0;

    app->ap_count = 0;
    app->scan_ok = false;
    if(wifi_lab_ensure_radio(app) && !app->scan_cancel) {
        /* Passive in this profile: see wlan_hal_scan_cancellable(). */
        wlan_hal_scan_cancellable(&records, &found, WIFI_LAB_MAX_APS, &app->scan_cancel);
        if(!app->scan_cancel) {
            for(uint16_t i = 0; i < found && app->ap_count < WIFI_LAB_MAX_APS; i++) {
                WifiLabAp* ap = &app->aps[app->ap_count++];
                memset(ap, 0, sizeof(*ap));
                strlcpy(ap->ssid, (const char*)records[i].ssid, sizeof(ap->ssid));
                memcpy(ap->bssid, records[i].bssid, sizeof(ap->bssid));
                ap->rssi = records[i].rssi;
                ap->channel = records[i].primary;
                ap->authmode = records[i].authmode;
            }
            qsort(app->aps, app->ap_count, sizeof(WifiLabAp), wifi_lab_sort_by_rssi);
            app->scan_ok = true;
        }
    }
    if(records) free(records);
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiLabEventScanDone);
    return 0;
}

static void wifi_lab_scan_join(WifiLab* app) {
    if(!app->scan_thread) return;
    app->scan_cancel = true;
    furi_thread_join(app->scan_thread);
    furi_thread_free(app->scan_thread);
    app->scan_thread = NULL;
}

/* -------------------------------------------------------------- scenes --- */

static void wifi_lab_submenu_callback(void* context, uint32_t index) {
    WifiLab* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void wifi_lab_dialog_callback(DialogExResult result, void* context) {
    WifiLab* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

/* Start */
static void wifi_lab_scene_start_on_enter(void* context) {
    WifiLab* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "WiFi Lab");
    submenu_add_item(
        app->submenu, "Scan (passive)", WifiLabStartScan, wifi_lab_submenu_callback, app);
    submenu_add_item(app->submenu, "Status", WifiLabStartStatus, wifi_lab_submenu_callback, app);
    if(wlan_hal_is_started() && wlan_hal_is_connected()) {
        submenu_add_item(
            app->submenu, "Disconnect", WifiLabStartDisconnect, wifi_lab_submenu_callback, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewSubmenu);
}

static bool wifi_lab_scene_start_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case WifiLabStartScan:
        scene_manager_next_scene(app->scene_manager, WifiLabSceneScan);
        return true;
    case WifiLabStartStatus:
        scene_manager_next_scene(app->scene_manager, WifiLabSceneStatus);
        return true;
    case WifiLabStartDisconnect:
        wlan_hal_disconnect();
        wifi_lab_show_message(app, "Disconnected.");
        return true;
    default:
        return false;
    }
}

static void wifi_lab_scene_start_on_exit(void* context) {
    WifiLab* app = context;
    submenu_reset(app->submenu);
}

/* Scan */
static void wifi_lab_scene_scan_on_enter(void* context) {
    WifiLab* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewLoading);
    app->scan_cancel = false;
    app->scan_thread = furi_thread_alloc_ex("WifiLabScan", 4096, wifi_lab_scan_worker, app);
    furi_thread_start(app->scan_thread);
}

static bool wifi_lab_scene_scan_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WifiLabEventScanDone) {
        wifi_lab_scan_join(app);
        if(app->scan_ok) {
            scene_manager_next_scene(app->scene_manager, WifiLabSceneList);
        } else {
            wifi_lab_show_message(app, "Scan failed.\nIs WiFi available?");
        }
        return true;
    }
    return false;
}

static void wifi_lab_scene_scan_on_exit(void* context) {
    WifiLab* app = context;
    wifi_lab_scan_join(app);
}

/* List */
static void wifi_lab_scene_list_on_enter(void* context) {
    WifiLab* app = context;
    char label[64];
    submenu_reset(app->submenu);
    snprintf(label, sizeof(label), "%u network%s", app->ap_count, app->ap_count == 1 ? "" : "s");
    submenu_set_header(app->submenu, label);
    for(uint16_t i = 0; i < app->ap_count; i++) {
        const WifiLabAp* ap = &app->aps[i];
        snprintf(
            label,
            sizeof(label),
            "%d ch%u %s",
            ap->rssi,
            ap->channel,
            wifi_lab_display_ssid(ap));
        submenu_add_item(app->submenu, label, i, wifi_lab_submenu_callback, app);
    }
    if(app->ap_count) {
        submenu_add_item(
            app->submenu, "Save list to SD", WIFI_LAB_LIST_SAVE, wifi_lab_submenu_callback, app);
    }
    submenu_set_selected_item(app->submenu, app->selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewSubmenu);
}

static bool wifi_lab_scene_list_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WIFI_LAB_LIST_SAVE) {
            FuriString* path = furi_string_alloc();
            if(wifi_lab_export(app, path)) {
                furi_string_printf(app->message, "Saved:\n%s", furi_string_get_cstr(path));
            } else {
                furi_string_set(app->message, "Could not write to the SD card.");
            }
            furi_string_free(path);
            scene_manager_next_scene(app->scene_manager, WifiLabSceneMessage);
            return true;
        }
        if(event.event < app->ap_count) {
            app->selected = (uint16_t)event.event;
            scene_manager_next_scene(app->scene_manager, WifiLabSceneDetail);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        /* Skip the finished scan scene on the way back. */
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, WifiLabSceneStart);
        return true;
    }
    return false;
}

static void wifi_lab_scene_list_on_exit(void* context) {
    WifiLab* app = context;
    submenu_reset(app->submenu);
}

/* Detail */
static void wifi_lab_detail_button_callback(GuiButtonType result, InputType type, void* context) {
    WifiLab* app = context;
    if(result == GuiButtonTypeRight && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiLabEventJoinPressed);
    }
}

static void wifi_lab_scene_detail_on_enter(void* context) {
    WifiLab* app = context;
    const WifiLabAp* ap = &app->aps[app->selected];
    FuriString* text = furi_string_alloc();
    furi_string_printf(
        text,
        "%s\nBSSID %02X:%02X:%02X:%02X:%02X:%02X\nChannel %u   %d dBm\nSecurity %s",
        wifi_lab_display_ssid(ap),
        ap->bssid[0],
        ap->bssid[1],
        ap->bssid[2],
        ap->bssid[3],
        ap->bssid[4],
        ap->bssid[5],
        ap->channel,
        ap->rssi,
        wifi_lab_auth_name(ap->authmode));
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 50, furi_string_get_cstr(text));
    if(ap->ssid[0]) {
        widget_add_button_element(
            app->widget, GuiButtonTypeRight, "Join", wifi_lab_detail_button_callback, app);
    }
    furi_string_free(text);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewWidget);
}

static bool wifi_lab_scene_detail_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WifiLabEventJoinPressed) {
        scene_manager_next_scene(app->scene_manager, WifiLabSceneConfirmJoin);
        return true;
    }
    return false;
}

static void wifi_lab_scene_detail_on_exit(void* context) {
    WifiLab* app = context;
    widget_reset(app->widget);
}

/* Confirm join */
static void wifi_lab_scene_confirm_join_on_enter(void* context) {
    WifiLab* app = context;
    const WifiLabAp* ap = &app->aps[app->selected];
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, wifi_lab_dialog_callback);
    dialog_ex_set_header(app->dialog, ap->ssid, 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog,
        "Join this network?\nOnly join networks you\nown or may use.",
        64,
        16,
        AlignCenter,
        AlignTop);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Join");
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewDialog);
}

static bool wifi_lab_scene_confirm_join_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultRight) {
        const WifiLabAp* ap = &app->aps[app->selected];
        memset(app->password, 0, sizeof(app->password));
        app->password_typed = false;
        if(ap->authmode == WIFI_AUTH_OPEN) {
            scene_manager_next_scene(app->scene_manager, WifiLabSceneConnecting);
        } else if(wlan_password_read(ap->ssid, app->password, sizeof(app->password))) {
            scene_manager_next_scene(app->scene_manager, WifiLabSceneConnecting);
        } else {
            scene_manager_next_scene(app->scene_manager, WifiLabScenePassword);
        }
        return true;
    }
    if(event.event == DialogExResultLeft) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

static void wifi_lab_scene_confirm_join_on_exit(void* context) {
    WifiLab* app = context;
    dialog_ex_reset(app->dialog);
}

/* Password */
static void wifi_lab_password_callback(void* context) {
    WifiLab* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiLabEventPasswordEntered);
}

static void wifi_lab_scene_password_on_enter(void* context) {
    WifiLab* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "WiFi password");
    text_input_set_result_callback(
        app->text_input,
        wifi_lab_password_callback,
        app,
        app->password,
        sizeof(app->password),
        true);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewTextInput);
}

static bool wifi_lab_scene_password_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WifiLabEventPasswordEntered) {
        app->password_typed = true;
        scene_manager_next_scene(app->scene_manager, WifiLabSceneConnecting);
        return true;
    }
    return false;
}

static void wifi_lab_scene_password_on_exit(void* context) {
    WifiLab* app = context;
    text_input_reset(app->text_input);
}

/* Connecting */
static void wifi_lab_scene_connecting_on_enter(void* context) {
    WifiLab* app = context;
    const WifiLabAp* ap = &app->aps[app->selected];
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewLoading);
    app->connect_started = furi_get_tick();
    if(!wifi_lab_ensure_radio(app) ||
       !wlan_hal_connect(ap->ssid, app->password, ap->bssid, ap->channel)) {
        app->connect_started = 0; /* fail on the first tick */
    }
}

static void wifi_lab_connect_failed(WifiLab* app, const char* text) {
    wlan_hal_disconnect();
    memset(app->password, 0, sizeof(app->password));
    scene_manager_search_and_switch_to_previous_scene(app->scene_manager, WifiLabSceneDetail);
    wifi_lab_show_message(app, text);
}

static bool wifi_lab_scene_connecting_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        if(app->connect_started == 0) {
            wifi_lab_connect_failed(app, "Could not start\nthe connection.");
        } else if(wlan_hal_is_connected()) {
            Wifi* wifi = furi_record_open(RECORD_WIFI);
            wifi_mark_connected(wifi, app->aps[app->selected].ssid);
            furi_record_close(RECORD_WIFI);
            app->joined = true;
            if(app->password_typed) {
                scene_manager_next_scene(app->scene_manager, WifiLabSceneSavePassword);
            } else {
                memset(app->password, 0, sizeof(app->password));
                scene_manager_search_and_switch_to_previous_scene(
                    app->scene_manager, WifiLabSceneStart);
                scene_manager_next_scene(app->scene_manager, WifiLabSceneStatus);
            }
        } else if(furi_get_tick() - app->connect_started >
                  furi_ms_to_ticks(WIFI_LAB_CONNECT_TIMEOUT_MS)) {
            wifi_lab_connect_failed(
                app,
                wlan_hal_last_fail_is_auth() ? "Wrong password." : "Could not connect\n(timed out).");
        }
        return true;
    }
    if(event.type == SceneManagerEventTypeBack) {
        wifi_lab_connect_failed(app, "Cancelled.");
        return true;
    }
    return false;
}

static void wifi_lab_scene_connecting_on_exit(void* context) {
    UNUSED(context);
}

/* Offer to remember the password. It is stored in plain text, so ask. */
static void wifi_lab_scene_save_password_on_enter(void* context) {
    WifiLab* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, wifi_lab_dialog_callback);
    dialog_ex_set_header(app->dialog, "Connected", 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(
        app->dialog,
        "Remember the password?\nIt is stored on the SD\ncard as plain text.",
        64,
        16,
        AlignCenter,
        AlignTop);
    dialog_ex_set_left_button_text(app->dialog, "No");
    dialog_ex_set_right_button_text(app->dialog, "Save");
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewDialog);
}

static bool wifi_lab_scene_save_password_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    bool leave = event.type == SceneManagerEventTypeBack;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DialogExResultRight) {
            wlan_password_save(app->aps[app->selected].ssid, app->password);
            leave = true;
        } else if(event.event == DialogExResultLeft) {
            leave = true;
        }
    }
    if(leave) {
        memset(app->password, 0, sizeof(app->password));
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, WifiLabSceneStart);
        scene_manager_next_scene(app->scene_manager, WifiLabSceneStatus);
        return true;
    }
    return false;
}

static void wifi_lab_scene_save_password_on_exit(void* context) {
    WifiLab* app = context;
    dialog_ex_reset(app->dialog);
}

/* Message */
static void wifi_lab_scene_message_on_enter(void* context) {
    WifiLab* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->message));
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewWidget);
}

static bool wifi_lab_scene_message_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void wifi_lab_scene_message_on_exit(void* context) {
    WifiLab* app = context;
    widget_reset(app->widget);
}

/* Status */
static void wifi_lab_scene_status_on_enter(void* context) {
    WifiLab* app = context;
    FuriString* text = furi_string_alloc();
    wifi_ap_record_t ap = {0};
    if(wlan_hal_is_started() && wlan_hal_is_connected() && wlan_hal_get_connected_ap(&ap)) {
        const uint32_t ip = wlan_hal_get_own_ip();
        const uint32_t gw = wlan_hal_get_gw_ip();
        furi_string_printf(
            text,
            "Connected\n%s\nChannel %u   %d dBm\nIP %u.%u.%u.%u\nGW %u.%u.%u.%u",
            (const char*)ap.ssid,
            ap.primary,
            ap.rssi,
            (unsigned)(ip & 0xFF),
            (unsigned)((ip >> 8) & 0xFF),
            (unsigned)((ip >> 16) & 0xFF),
            (unsigned)((ip >> 24) & 0xFF),
            (unsigned)(gw & 0xFF),
            (unsigned)((gw >> 8) & 0xFF),
            (unsigned)((gw >> 16) & 0xFF),
            (unsigned)((gw >> 24) & 0xFF));
    } else {
        furi_string_set(
            text, wlan_hal_is_started() ? "WiFi is on,\nnot connected." : "WiFi is off.");
    }
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewWidget);
}

static bool wifi_lab_scene_status_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void wifi_lab_scene_status_on_exit(void* context) {
    WifiLab* app = context;
    widget_reset(app->widget);
}

static void (*const wifi_lab_on_enter[])(void*) = {
    [WifiLabSceneStart] = wifi_lab_scene_start_on_enter,
    [WifiLabSceneScan] = wifi_lab_scene_scan_on_enter,
    [WifiLabSceneList] = wifi_lab_scene_list_on_enter,
    [WifiLabSceneDetail] = wifi_lab_scene_detail_on_enter,
    [WifiLabSceneConfirmJoin] = wifi_lab_scene_confirm_join_on_enter,
    [WifiLabScenePassword] = wifi_lab_scene_password_on_enter,
    [WifiLabSceneConnecting] = wifi_lab_scene_connecting_on_enter,
    [WifiLabSceneSavePassword] = wifi_lab_scene_save_password_on_enter,
    [WifiLabSceneMessage] = wifi_lab_scene_message_on_enter,
    [WifiLabSceneStatus] = wifi_lab_scene_status_on_enter,
};

static bool (*const wifi_lab_on_event[])(void*, SceneManagerEvent) = {
    [WifiLabSceneStart] = wifi_lab_scene_start_on_event,
    [WifiLabSceneScan] = wifi_lab_scene_scan_on_event,
    [WifiLabSceneList] = wifi_lab_scene_list_on_event,
    [WifiLabSceneDetail] = wifi_lab_scene_detail_on_event,
    [WifiLabSceneConfirmJoin] = wifi_lab_scene_confirm_join_on_event,
    [WifiLabScenePassword] = wifi_lab_scene_password_on_event,
    [WifiLabSceneConnecting] = wifi_lab_scene_connecting_on_event,
    [WifiLabSceneSavePassword] = wifi_lab_scene_save_password_on_event,
    [WifiLabSceneMessage] = wifi_lab_scene_message_on_event,
    [WifiLabSceneStatus] = wifi_lab_scene_status_on_event,
};

static void (*const wifi_lab_on_exit[])(void*) = {
    [WifiLabSceneStart] = wifi_lab_scene_start_on_exit,
    [WifiLabSceneScan] = wifi_lab_scene_scan_on_exit,
    [WifiLabSceneList] = wifi_lab_scene_list_on_exit,
    [WifiLabSceneDetail] = wifi_lab_scene_detail_on_exit,
    [WifiLabSceneConfirmJoin] = wifi_lab_scene_confirm_join_on_exit,
    [WifiLabScenePassword] = wifi_lab_scene_password_on_exit,
    [WifiLabSceneConnecting] = wifi_lab_scene_connecting_on_exit,
    [WifiLabSceneSavePassword] = wifi_lab_scene_save_password_on_exit,
    [WifiLabSceneMessage] = wifi_lab_scene_message_on_exit,
    [WifiLabSceneStatus] = wifi_lab_scene_status_on_exit,
};

static const SceneManagerHandlers wifi_lab_scene_handlers = {
    .on_enter_handlers = wifi_lab_on_enter,
    .on_event_handlers = wifi_lab_on_event,
    .on_exit_handlers = wifi_lab_on_exit,
    .scene_num = WifiLabSceneCount,
};

/* ----------------------------------------------------------------- app --- */

static bool wifi_lab_custom_event_callback(void* context, uint32_t event) {
    WifiLab* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool wifi_lab_back_event_callback(void* context) {
    WifiLab* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void wifi_lab_tick_event_callback(void* context) {
    WifiLab* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

int32_t wifi_lab_app(void* p) {
    UNUSED(p);
    WifiLab* app = malloc(sizeof(WifiLab));
    app->message = furi_string_alloc();
    app->gui = furi_record_open(RECORD_GUI);
    app->scene_manager = scene_manager_alloc(&wifi_lab_scene_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, wifi_lab_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, wifi_lab_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, wifi_lab_tick_event_callback, 250);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiLabViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WifiLabViewWidget, widget_get_view(app->widget));
    app->dialog = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiLabViewDialog, dialog_ex_get_view(app->dialog));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiLabViewTextInput, text_input_get_view(app->text_input));
    app->loading = loading_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiLabViewLoading, loading_get_view(app->loading));

    scene_manager_next_scene(app->scene_manager, WifiLabSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    wifi_lab_scan_join(app);
    /* If this app switched the radio on only to look around, switch it off again
     * (which also hands the shared radio back to Bluetooth). After a join the
     * WiFi service owns it. */
    if(app->started_wifi && !app->joined) wlan_hal_stop();

    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewDialog);
    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewLoading);
    submenu_free(app->submenu);
    widget_free(app->widget);
    dialog_ex_free(app->dialog);
    text_input_free(app->text_input);
    loading_free(app->loading);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_string_free(app->message);
    memset(app->password, 0, sizeof(app->password));
    free(app);
    return 0;
}
