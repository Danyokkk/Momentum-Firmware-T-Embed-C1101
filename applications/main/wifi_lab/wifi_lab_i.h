/**
 * WiFi Lab - shared declarations for wifi_lab.c and wifi_lab_tools.c.
 */
#pragma once

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

/* Engines from upstream's WiFi app, compiled into this one (application.fam). */
#include <wlan_app/wlan_webfs.h>
#include <wlan_app/wlan_smb.h>

struct WlanAndroidTv;
struct WlanSnifferView;

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
    WifiLabViewAtvRemote,
    WifiLabViewSniffer,
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
    WifiLabSceneWebfsMenu,
    WifiLabSceneWebfsConfirm,
    WifiLabSceneWebfsRun,
    WifiLabSceneSmbInput,
    WifiLabSceneSmbConnect,
    WifiLabSceneSmbBrowse,
    WifiLabSceneSmbConfirm,
    WifiLabSceneSmbDownload,
    WifiLabSceneConfirm,
    WifiLabSceneHosts,
    WifiLabSceneHostActions,
    WifiLabSceneTargetIp,
    WifiLabScenePortScan,
    WifiLabSceneAtvRemote,
    WifiLabSceneAtvPair,
    WifiLabSceneAirprint,
    WifiLabSceneMonitor,
    WifiLabSceneCount,
} WifiLabScene;

typedef enum {
    WifiLabEventScanDone = 0x1000,
    WifiLabEventJoinPressed,
    WifiLabEventPasswordEntered,
    WifiLabEventTextDone,
    WifiLabEventStop,
    WifiLabEventRetry,
    WifiLabEventMonitorToggle,
    WifiLabEventMonitorChannelUp,
    WifiLabEventMonitorChannelDown,
} WifiLabEvent;

enum {
    WifiLabStartScan,
    WifiLabStartStatus,
    WifiLabStartDisconnect,
    WifiLabStartFileServer,
    WifiLabStartSmb,
    WifiLabStartHosts,
    WifiLabStartPortScan,
    WifiLabStartAtv,
    WifiLabStartAirprint,
    WifiLabStartMonitor,
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

    /* SD file server */
    bool webfs_hotspot;
    int webfs_clients;
    char webfs_ssid[WLAN_WEBFS_SSID_MAX + 1];
    char webfs_pw[WLAN_WEBFS_PW_MAX + 1];

    /* SMB browser */
    WlanSmb* smb; /* allocated on first use */
    uint8_t smb_input;
    bool smb_loading;
    bool smb_download_done;
    bool smb_dl_is_dir;
    char smb_ip[16];
    char smb_user[WLAN_SMB_USER_MAX];
    char smb_pass[WLAN_SMB_PASS_MAX];
    char smb_share[WLAN_SMB_SHARE_MAX];
    char smb_path[WLAN_SMB_PATH_MAX];
    char smb_dl_path[WLAN_SMB_PATH_MAX];
    WlanSmbEntry smb_entry; /* scratch: too large for the app stack */

    /* Generic "are you sure" step in front of a tool (wifi_lab_tools.c) */
    const char* confirm_header;
    const char* confirm_text;
    uint32_t confirm_next;

    /* Tools that act on one host */
    char target_ip[16];
    uint32_t target_next; /* scene to open once target_ip is entered */
    char host_name[24];

    /* Android TV remote */
    struct WlanAndroidTv* atv; /* allocated on first use */
    View* atv_view;
    uint8_t atv_phase;
    bool atv_abort; /* pairing was cancelled: leave the remote scene too */
    char atv_pin[8];

    /* Channel monitor */
    struct WlanSnifferView* monitor_view;
    uint8_t monitor_channel;
    bool monitor_running;
    uint32_t monitor_ticks;

    /* Shared by the tools: what is on screen, to avoid redrawing every tick */
    uint32_t tools_shown;
} WifiLab;

/* wifi_lab.c */
bool wifi_lab_ensure_radio(WifiLab* app);
void wifi_lab_show_message(WifiLab* app, const char* text);
void wifi_lab_submenu_callback(void* context, uint32_t index);
void wifi_lab_dialog_callback(DialogExResult result, void* context);
void wifi_lab_text_callback(void* context);
bool wifi_lab_ip_valid(const char* text);

/* wifi_lab_tools.c */
void wifi_lab_tools_alloc(WifiLab* app);
void wifi_lab_tools_free(WifiLab* app);
/* Handles the tool entries of the start menu; false if the event is not one. */
bool wifi_lab_tools_start_event(WifiLab* app, uint32_t event);
void wifi_lab_tools_start_menu(WifiLab* app);

#define WIFI_LAB_TOOL_SCENES(X) \
    X(WifiLabSceneConfirm, confirm) \
    X(WifiLabSceneHosts, hosts) \
    X(WifiLabSceneHostActions, host_actions) \
    X(WifiLabSceneTargetIp, target_ip) \
    X(WifiLabScenePortScan, port_scan) \
    X(WifiLabSceneAtvRemote, atv_remote) \
    X(WifiLabSceneAtvPair, atv_pair) \
    X(WifiLabSceneAirprint, airprint) \
    X(WifiLabSceneMonitor, monitor)

#define WIFI_LAB_TOOL_SCENE_DECL(id, name) \
    void wifi_lab_scene_##name##_on_enter(void* context); \
    bool wifi_lab_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void wifi_lab_scene_##name##_on_exit(void* context);
WIFI_LAB_TOOL_SCENES(WIFI_LAB_TOOL_SCENE_DECL)
