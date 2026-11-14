/**
 * BLE Lab - passive Bluetooth LE discovery for the lab profile.
 *
 * Lists nearby advertisers with signal strength, address type, advertised
 * name, vendor and a hint when the payload looks like a location tag (useful
 * for noticing a tag that is travelling with you). The list can be saved to
 * the SD card on request.
 *
 * It only listens. It never connects, pairs, sends scan requests, writes to a
 * device or advertises anything, and it has no code to do so.
 */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <storage/storage.h>

#include <wifi.h>

#include "ble_lab_radio.h"

#define BLE_LAB_EXPORT_DIR EXT_PATH("apps_data/ble_lab")
#define BLE_LAB_REFRESH_MS 2000

typedef enum {
    BleLabViewSubmenu,
    BleLabViewWidget,
} BleLabView;

typedef enum {
    BleLabSceneStart,
    BleLabSceneList,
    BleLabSceneDetail,
    BleLabSceneMessage,
    BleLabSceneCount,
} BleLabScene;

enum {
    BleLabStartScan,
    BleLabStartAbout,
};

#define BLE_LAB_LIST_SAVE  0xFFFFu
#define BLE_LAB_LIST_CLEAR 0xFFFEu

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    Widget* widget;

    BleLabDevice devices[BLE_LAB_MAX_DEVICES];
    uint16_t device_count;
    uint8_t selected_addr[6];
    uint32_t last_refresh;
    FuriString* message;
} BleLab;

static void ble_lab_show_message(BleLab* app, const char* text) {
    furi_string_set(app->message, text);
    scene_manager_next_scene(app->scene_manager, BleLabSceneMessage);
}

static const char* ble_lab_device_title(const BleLabDevice* device) {
    if(device->adv.name[0]) return device->adv.name;
    const char* kind = ble_lab_kind_label(device->adv.kind);
    if(kind[0]) return kind;
    const char* company = ble_lab_company_name(device->adv.company_id);
    return company ? company : "(no name)";
}

static void ble_lab_csv_field(FuriString* out, const char* text) {
    furi_string_push_back(out, '"');
    for(const char* c = text; *c; c++) {
        if(*c == '"') furi_string_push_back(out, '"');
        furi_string_push_back(out, *c);
    }
    furi_string_push_back(out, '"');
}

static bool ble_lab_export(BleLab* app, FuriString* path_out) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;

    storage_simply_mkdir(storage, BLE_LAB_EXPORT_DIR);
    furi_string_printf(
        path_out, "%s/scan_%lu.csv", BLE_LAB_EXPORT_DIR, (unsigned long)furi_hal_rtc_get_timestamp());
    if(storage_file_open(file, furi_string_get_cstr(path_out), FSAM_WRITE, FSOM_CREATE_NEW)) {
        FuriString* line = furi_string_alloc_set(
            "address,address_type,rssi_dbm,best_rssi_dbm,packets,name,company_id,hint\n");
        ok = storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line)) ==
             furi_string_size(line);
        for(uint16_t i = 0; ok && i < app->device_count; i++) {
            const BleLabDevice* d = &app->devices[i];
            furi_string_printf(
                line,
                "%02X:%02X:%02X:%02X:%02X:%02X,%s,%d,%d,%u,",
                d->addr[0],
                d->addr[1],
                d->addr[2],
                d->addr[3],
                d->addr[4],
                d->addr[5],
                d->addr_random ? "random" : "public",
                d->rssi,
                d->rssi_best,
                d->packets);
            ble_lab_csv_field(line, d->adv.name);
            if(d->adv.company_id != BLE_LAB_NO_COMPANY) {
                furi_string_cat_printf(line, ",0x%04lX,", (unsigned long)d->adv.company_id);
            } else {
                furi_string_cat(line, ",,");
            }
            ble_lab_csv_field(line, ble_lab_kind_label(d->adv.kind));
            furi_string_push_back(line, '\n');
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

static void ble_lab_submenu_callback(void* context, uint32_t index) {
    BleLab* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

/* ---- Start ---- */
static void ble_lab_scene_start_on_enter(void* context) {
    BleLab* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "BLE Lab");
    submenu_add_item(app->submenu, "Scan (passive)", BleLabStartScan, ble_lab_submenu_callback, app);
    submenu_add_item(app->submenu, "About", BleLabStartAbout, ble_lab_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleLabViewSubmenu);
}

static bool ble_lab_scene_start_on_event(void* context, SceneManagerEvent event) {
    BleLab* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == BleLabStartScan) {
        Wifi* wifi = furi_record_open(RECORD_WIFI);
        const bool wifi_on = wifi_is_enabled(wifi);
        furi_record_close(RECORD_WIFI);
        if(wifi_on) {
            /* Wi-Fi and Bluetooth share one radio and the firmware parks
             * Bluetooth while Wi-Fi is on. Do not fight that from an app. */
            ble_lab_show_message(
                app, "WiFi is on.\nTurn it off in the\nlock menu, then scan.");
        } else if(ble_lab_radio_start()) {
            scene_manager_next_scene(app->scene_manager, BleLabSceneList);
        } else {
            ble_lab_show_message(app, "Bluetooth did not start.");
        }
        return true;
    }
    if(event.event == BleLabStartAbout) {
        ble_lab_show_message(
            app,
            "\e#Passive scan\n"
            "Listens for BLE\n"
            "advertisements only.\n"
            "No connecting, pairing,\n"
            "writing or advertising.\n"
            "\n"
            "\"FindMy tag\", \"Tile\" etc.\n"
            "are hints from public\n"
            "advert fields, not proof.");
        return true;
    }
    return false;
}

static void ble_lab_scene_start_on_exit(void* context) {
    BleLab* app = context;
    submenu_reset(app->submenu);
}

/* ---- List ---- */
static void ble_lab_list_rebuild(BleLab* app) {
    const uint32_t cursor = submenu_get_selected_item(app->submenu);
    char label[48];
    app->device_count = ble_lab_radio_snapshot(app->devices, BLE_LAB_MAX_DEVICES);
    submenu_reset(app->submenu);
    snprintf(label, sizeof(label), "%u heard", app->device_count);
    submenu_set_header(app->submenu, label);
    for(uint16_t i = 0; i < app->device_count; i++) {
        const BleLabDevice* d = &app->devices[i];
        snprintf(label, sizeof(label), "%d %s", d->rssi, ble_lab_device_title(d));
        submenu_add_item(app->submenu, label, i, ble_lab_submenu_callback, app);
    }
    submenu_add_item(app->submenu, "Save list to SD", BLE_LAB_LIST_SAVE, ble_lab_submenu_callback, app);
    submenu_add_item(app->submenu, "Clear list", BLE_LAB_LIST_CLEAR, ble_lab_submenu_callback, app);
    submenu_set_selected_item(app->submenu, cursor);
    app->last_refresh = furi_get_tick();
}

static void ble_lab_scene_list_on_enter(void* context) {
    BleLab* app = context;
    submenu_reset(app->submenu);
    ble_lab_list_rebuild(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleLabViewSubmenu);
}

static bool ble_lab_scene_list_on_event(void* context, SceneManagerEvent event) {
    BleLab* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        if(furi_get_tick() - app->last_refresh > furi_ms_to_ticks(BLE_LAB_REFRESH_MS)) {
            ble_lab_list_rebuild(app);
        }
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BLE_LAB_LIST_SAVE) {
            FuriString* path = furi_string_alloc();
            if(ble_lab_export(app, path)) {
                furi_string_printf(app->message, "Saved:\n%s", furi_string_get_cstr(path));
            } else {
                furi_string_set(app->message, "Could not write to the SD card.");
            }
            furi_string_free(path);
            scene_manager_next_scene(app->scene_manager, BleLabSceneMessage);
            return true;
        }
        if(event.event == BLE_LAB_LIST_CLEAR) {
            ble_lab_radio_clear();
            ble_lab_list_rebuild(app);
            return true;
        }
        if(event.event < app->device_count) {
            memcpy(app->selected_addr, app->devices[event.event].addr, 6);
            scene_manager_next_scene(app->scene_manager, BleLabSceneDetail);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        ble_lab_radio_stop();
        return false; /* then go back to Start */
    }
    return false;
}

static void ble_lab_scene_list_on_exit(void* context) {
    BleLab* app = context;
    submenu_reset(app->submenu);
}

/* ---- Detail ---- */
static void ble_lab_detail_render(BleLab* app) {
    FuriString* text = furi_string_alloc();
    const BleLabDevice* d = NULL;
    app->device_count = ble_lab_radio_snapshot(app->devices, BLE_LAB_MAX_DEVICES);
    for(uint16_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].addr, app->selected_addr, 6) == 0) {
            d = &app->devices[i];
            break;
        }
    }
    if(!d) {
        furi_string_set(text, "No longer in the list.");
    } else {
        furi_string_printf(
            text,
            "%s\n%02X:%02X:%02X:%02X:%02X:%02X %s\nRSSI %d (best %d) dBm\nPackets %u, seen %lus ago",
            ble_lab_device_title(d),
            d->addr[0],
            d->addr[1],
            d->addr[2],
            d->addr[3],
            d->addr[4],
            d->addr[5],
            d->addr_random ? "random" : "public",
            d->rssi,
            d->rssi_best,
            d->packets,
            (unsigned long)((furi_get_tick() - d->last_seen) / furi_ms_to_ticks(1000)));
        if(d->adv.company_id != BLE_LAB_NO_COMPANY) {
            const char* company = ble_lab_company_name(d->adv.company_id);
            furi_string_cat_printf(
                text,
                "\nVendor 0x%04lX %s",
                (unsigned long)d->adv.company_id,
                company ? company : "");
        }
        if(ble_lab_kind_label(d->adv.kind)[0]) {
            furi_string_cat_printf(text, "\nLooks like: %s", ble_lab_kind_label(d->adv.kind));
        }
        for(uint8_t i = 0; i < d->adv.service16_count; i++) {
            furi_string_cat_printf(text, "%s0x%04X", i ? " " : "\nServices ", d->adv.service16[i]);
        }
        if(d->adv.has_tx_power) furi_string_cat_printf(text, "\nTX power %d dBm", d->adv.tx_power);
    }
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);
}

static void ble_lab_scene_detail_on_enter(void* context) {
    BleLab* app = context;
    ble_lab_detail_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleLabViewWidget);
}

static bool ble_lab_scene_detail_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void ble_lab_scene_detail_on_exit(void* context) {
    BleLab* app = context;
    widget_reset(app->widget);
}

/* ---- Message ---- */
static void ble_lab_scene_message_on_enter(void* context) {
    BleLab* app = context;
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->message));
    view_dispatcher_switch_to_view(app->view_dispatcher, BleLabViewWidget);
}

static bool ble_lab_scene_message_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void ble_lab_scene_message_on_exit(void* context) {
    BleLab* app = context;
    widget_reset(app->widget);
}

static void (*const ble_lab_on_enter[])(void*) = {
    [BleLabSceneStart] = ble_lab_scene_start_on_enter,
    [BleLabSceneList] = ble_lab_scene_list_on_enter,
    [BleLabSceneDetail] = ble_lab_scene_detail_on_enter,
    [BleLabSceneMessage] = ble_lab_scene_message_on_enter,
};

static bool (*const ble_lab_on_event[])(void*, SceneManagerEvent) = {
    [BleLabSceneStart] = ble_lab_scene_start_on_event,
    [BleLabSceneList] = ble_lab_scene_list_on_event,
    [BleLabSceneDetail] = ble_lab_scene_detail_on_event,
    [BleLabSceneMessage] = ble_lab_scene_message_on_event,
};

static void (*const ble_lab_on_exit[])(void*) = {
    [BleLabSceneStart] = ble_lab_scene_start_on_exit,
    [BleLabSceneList] = ble_lab_scene_list_on_exit,
    [BleLabSceneDetail] = ble_lab_scene_detail_on_exit,
    [BleLabSceneMessage] = ble_lab_scene_message_on_exit,
};

static const SceneManagerHandlers ble_lab_scene_handlers = {
    .on_enter_handlers = ble_lab_on_enter,
    .on_event_handlers = ble_lab_on_event,
    .on_exit_handlers = ble_lab_on_exit,
    .scene_num = BleLabSceneCount,
};

static bool ble_lab_custom_event_callback(void* context, uint32_t event) {
    BleLab* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool ble_lab_back_event_callback(void* context) {
    BleLab* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void ble_lab_tick_event_callback(void* context) {
    BleLab* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

int32_t ble_lab_app(void* p) {
    UNUSED(p);
    BleLab* app = malloc(sizeof(BleLab));
    app->message = furi_string_alloc();
    app->gui = furi_record_open(RECORD_GUI);
    app->scene_manager = scene_manager_alloc(&ble_lab_scene_handlers, app);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, ble_lab_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ble_lab_back_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, ble_lab_tick_event_callback, 500);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BleLabViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BleLabViewWidget, widget_get_view(app->widget));

    scene_manager_next_scene(app->scene_manager, BleLabSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    /* Whatever path the app was left by, Bluetooth goes back to the system. */
    ble_lab_radio_stop();

    view_dispatcher_remove_view(app->view_dispatcher, BleLabViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, BleLabViewWidget);
    submenu_free(app->submenu);
    widget_free(app->widget);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_string_free(app->message);
    free(app);
    return 0;
}
