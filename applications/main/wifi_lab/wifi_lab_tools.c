/**
 * WiFi Lab - network tools.
 *
 * New front-end scenes over engines from upstream's WiFi app (compiled into
 * this app, see application.fam):
 *   - devices on this network (ARP lookup of the joined /24, NetBIOS names)
 *   - port check of one host (19 common TCP ports, plain connect)
 *   - Android TV remote (pair once with the PIN the TV shows)
 *   - AirPrint receiver (a printer that saves what is printed to the SD card)
 *   - channel monitor (who is on a channel; frame headers to a .pcap)
 *
 * Every tool starts from an explicit menu choice and a confirmation that says
 * what it will do, runs only while its screen is open, and stops on Back.
 * Nothing here transmits forged frames, and the monitor never stores data
 * payloads, so it cannot record WPA handshakes or anyone's traffic contents.
 */
#include "wifi_lab_i.h"

#include <wlan_app/wlan_netscan.h>
#include <wlan_app/wlan_androidtv.h>
#include <wlan_app/views/wlan_androidtv_remote_view.h>
#include <wlan_app/wlan_airprint.h>
#include <wlan_app/wlan_pcap_rec.h>
#include <wlan_app/views/wlan_sniffer_view.h>

#include <lwip/sockets.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define WIFI_LAB_MONITOR_DIR EXT_PATH("apps_data/wifi_lab/monitor")

static bool wifi_lab_tools_online(WifiLab* app) {
    if(wlan_hal_is_started() && wlan_hal_is_connected()) return true;
    wifi_lab_show_message(app, "Join a network first\n(Scan, pick it, Join).");
    return false;
}

/* Two centred lines and an optional centre button. */
static void wifi_lab_tools_button_callback(GuiButtonType result, InputType type, void* context) {
    WifiLab* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiLabEventRetry);
    }
}

static void wifi_lab_tools_status(
    WifiLab* app,
    const char* header,
    const char* text,
    const char* button) {
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, header);
    widget_add_string_multiline_element(
        app->widget, 64, 32, AlignCenter, AlignCenter, FontSecondary, text);
    if(button) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, button, wifi_lab_tools_button_callback, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewWidget);
}

static void wifi_lab_tools_ip_prefix(char* out, size_t size) {
    const uint32_t ip = wlan_hal_get_own_ip();
    snprintf(
        out,
        size,
        "%u.%u.%u.",
        (unsigned)(ip & 0xFF),
        (unsigned)((ip >> 8) & 0xFF),
        (unsigned)((ip >> 16) & 0xFF));
}

static void wifi_lab_tools_confirm(
    WifiLab* app,
    const char* header,
    const char* text,
    uint32_t next_scene) {
    app->confirm_header = header;
    app->confirm_text = text;
    app->confirm_next = next_scene;
    scene_manager_next_scene(app->scene_manager, WifiLabSceneConfirm);
}

/* ---------------------------------------------------------- start menu --- */

void wifi_lab_tools_start_menu(WifiLab* app) {
    submenu_add_item(
        app->submenu, "Devices on network", WifiLabStartHosts, wifi_lab_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Port check", WifiLabStartPortScan, wifi_lab_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Android TV remote", WifiLabStartAtv, wifi_lab_submenu_callback, app);
    submenu_add_item(
        app->submenu, "AirPrint receiver", WifiLabStartAirprint, wifi_lab_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Channel monitor", WifiLabStartMonitor, wifi_lab_submenu_callback, app);
}

bool wifi_lab_tools_start_event(WifiLab* app, uint32_t event) {
    switch(event) {
    case WifiLabStartHosts:
        if(wifi_lab_tools_online(app)) {
            scene_manager_set_scene_state(app->scene_manager, WifiLabSceneHosts, 0);
            wifi_lab_tools_confirm(
                app,
                "Devices on network",
                "Asks every address on\nthis network who is\nthere (ARP). Yours only.",
                WifiLabSceneHosts);
        }
        return true;
    case WifiLabStartPortScan:
    case WifiLabStartAtv:
        if(wifi_lab_tools_online(app)) {
            app->target_next = event == WifiLabStartAtv ? WifiLabSceneAtvRemote :
                                                           WifiLabScenePortScan;
            app->host_name[0] = '\0';
            if(!wifi_lab_ip_valid(app->target_ip)) {
                wifi_lab_tools_ip_prefix(app->target_ip, sizeof(app->target_ip));
            }
            scene_manager_next_scene(app->scene_manager, WifiLabSceneTargetIp);
        }
        return true;
    case WifiLabStartAirprint:
        if(wifi_lab_tools_online(app)) {
            wifi_lab_tools_confirm(
                app,
                "AirPrint receiver",
                "Shows up as a printer on\nthis network. What is\nprinted is saved to SD.",
                WifiLabSceneAirprint);
        }
        return true;
    case WifiLabStartMonitor:
        wifi_lab_tools_confirm(
            app,
            "Channel monitor",
            "Listens on one channel.\nLeaves your network.\nSaves frame headers only.",
            WifiLabSceneMonitor);
        return true;
    default:
        return false;
    }
}

/* ------------------------------------------------------------- confirm --- */

void wifi_lab_scene_confirm_on_enter(void* context) {
    WifiLab* app = context;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, wifi_lab_dialog_callback);
    dialog_ex_set_header(app->dialog, app->confirm_header, 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->confirm_text, 64, 16, AlignCenter, AlignTop);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Start");
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewDialog);
}

bool wifi_lab_scene_confirm_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DialogExResultRight) {
        /* Replace this dialog, so Back from the tool lands on the menu. */
        const uint32_t next = app->confirm_next;
        scene_manager_previous_scene(app->scene_manager);
        scene_manager_next_scene(app->scene_manager, next);
        return true;
    }
    if(event.event == DialogExResultLeft) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void wifi_lab_scene_confirm_on_exit(void* context) {
    WifiLab* app = context;
    dialog_ex_reset(app->dialog);
}

/* -------------------------------------------------- devices on network --- */

enum {
    WifiLabHostsScanning,
    WifiLabHostsNames,
    WifiLabHostsListed,
};

#define WIFI_LAB_HOSTS_NAME_TICKS 24 /* 6 s for NetBIOS names, then show the list anyway */

static void wifi_lab_hosts_format_ip(uint32_t ip, char* out, size_t size) {
    snprintf(
        out,
        size,
        "%u.%u.%u.%u",
        (unsigned)(ip & 0xFF),
        (unsigned)((ip >> 8) & 0xFF),
        (unsigned)((ip >> 16) & 0xFF),
        (unsigned)((ip >> 24) & 0xFF));
}

static void wifi_lab_hosts_fill(WifiLab* app) {
    char label[64];
    char ip[16];
    WlanNetscanHost host;
    const uint8_t count = wlan_netscan_get_host_count();
    submenu_reset(app->submenu);
    snprintf(label, sizeof(label), "%u device%s", count, count == 1 ? "" : "s");
    submenu_set_header(app->submenu, label);
    for(uint8_t i = 0; i < count; i++) {
        if(!wlan_netscan_get_host(i, &host)) continue;
        wifi_lab_hosts_format_ip(host.ip, ip, sizeof(ip));
        if(host.hostname[0]) {
            snprintf(label, sizeof(label), "%s %s", ip, host.hostname);
        } else {
            snprintf(
                label,
                sizeof(label),
                "%s %02X:%02X:%02X",
                ip,
                host.mac[0],
                host.mac[1],
                host.mac[2]);
        }
        submenu_add_item(app->submenu, label, i, wifi_lab_submenu_callback, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewSubmenu);
}

void wifi_lab_scene_hosts_on_enter(void* context) {
    WifiLab* app = context;
    if(scene_manager_get_scene_state(app->scene_manager, WifiLabSceneHosts) ==
       WifiLabHostsListed) {
        wifi_lab_hosts_fill(app); /* back from a host: keep the list */
        return;
    }
    wlan_netscan_reset();
    app->tools_shown = UINT32_MAX;
    wifi_lab_tools_status(app, "Devices on network", "Looking...", NULL);
}

bool wifi_lab_scene_hosts_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    SceneManager* scenes = app->scene_manager;
    const uint32_t state = scene_manager_get_scene_state(scenes, WifiLabSceneHosts);
    if(event.type == SceneManagerEventTypeTick) {
        if(state == WifiLabHostsScanning) {
            /* Each step asks for four addresses; two steps per tick. */
            wlan_netscan_arp_step();
            if(wlan_netscan_arp_step()) {
                wlan_netscan_start_hostname_resolve();
                app->tools_shown = 0;
                scene_manager_set_scene_state(scenes, WifiLabSceneHosts, WifiLabHostsNames);
                wifi_lab_tools_status(app, "Devices on network", "Reading names...", NULL);
            } else if(wlan_netscan_get_progress() != app->tools_shown) {
                char text[32];
                app->tools_shown = wlan_netscan_get_progress();
                snprintf(
                    text,
                    sizeof(text),
                    "Looking... %u%%\n%u found",
                    (unsigned)app->tools_shown,
                    wlan_netscan_get_host_count());
                wifi_lab_tools_status(app, "Devices on network", text, NULL);
            }
        } else if(state == WifiLabHostsNames) {
            if(wlan_netscan_hostname_done() || ++app->tools_shown > WIFI_LAB_HOSTS_NAME_TICKS) {
                wlan_netscan_stop_hostname_resolve();
                scene_manager_set_scene_state(scenes, WifiLabSceneHosts, WifiLabHostsListed);
                wifi_lab_hosts_fill(app);
            }
        }
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom && state == WifiLabHostsListed) {
        WlanNetscanHost host;
        if(wlan_netscan_get_host((uint8_t)event.event, &host)) {
            wifi_lab_hosts_format_ip(host.ip, app->target_ip, sizeof(app->target_ip));
            strlcpy(app->host_name, host.hostname, sizeof(app->host_name));
            scene_manager_next_scene(scenes, WifiLabSceneHostActions);
        }
        return true;
    }
    return false;
}

void wifi_lab_scene_hosts_on_exit(void* context) {
    WifiLab* app = context;
    wlan_netscan_stop_hostname_resolve();
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}

enum {
    WifiLabHostPortScan,
    WifiLabHostAtv,
    WifiLabHostSmb,
};

void wifi_lab_scene_host_actions_on_enter(void* context) {
    WifiLab* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, app->host_name[0] ? app->host_name : app->target_ip);
    submenu_add_item(
        app->submenu, "Port check", WifiLabHostPortScan, wifi_lab_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Android TV remote", WifiLabHostAtv, wifi_lab_submenu_callback, app);
    submenu_add_item(app->submenu, "SMB browser", WifiLabHostSmb, wifi_lab_submenu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewSubmenu);
}

bool wifi_lab_scene_host_actions_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case WifiLabHostPortScan:
        scene_manager_next_scene(app->scene_manager, WifiLabScenePortScan);
        return true;
    case WifiLabHostAtv:
        scene_manager_next_scene(app->scene_manager, WifiLabSceneAtvRemote);
        return true;
    case WifiLabHostSmb:
        if(!app->smb) app->smb = wlan_smb_alloc();
        if(!app->smb) {
            wifi_lab_show_message(app, "Not enough memory\nfor the SMB client.");
            return true;
        }
        strlcpy(app->smb_ip, app->target_ip, sizeof(app->smb_ip));
        app->smb_input = 1; /* address is known: start at the user name */
        scene_manager_next_scene(app->scene_manager, WifiLabSceneSmbInput);
        return true;
    default:
        return false;
    }
}

void wifi_lab_scene_host_actions_on_exit(void* context) {
    WifiLab* app = context;
    submenu_reset(app->submenu);
}

/* ---------------------------------------------------- typed-in address --- */

void wifi_lab_scene_target_ip_on_enter(void* context) {
    WifiLab* app = context;
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Device IP address");
    text_input_set_result_callback(
        app->text_input,
        wifi_lab_text_callback,
        app,
        app->target_ip,
        sizeof(app->target_ip),
        false);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewTextInput);
}

bool wifi_lab_scene_target_ip_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WifiLabEventTextDone) {
        if(wifi_lab_ip_valid(app->target_ip)) {
            scene_manager_next_scene(app->scene_manager, app->target_next);
        } else {
            wifi_lab_show_message(app, "Enter an IP address,\nfor example\n192.168.1.20");
        }
        return true;
    }
    return false;
}

void wifi_lab_scene_target_ip_on_exit(void* context) {
    WifiLab* app = context;
    text_input_reset(app->text_input);
}

/* ---------------------------------------------------------- port check --- */
/*
 * A plain TCP connect to each port of one host the user chose. lwIP sockets
 * are driven from a pthread, not a FuriThread (their thread-local storage
 * differs; upstream's scenes do the same).
 */

#define WIFI_LAB_PORT_TIMEOUT_MS 500

static const struct {
    uint16_t port;
    const char* name;
} wifi_lab_ports[] = {
    {21, "FTP"},      {22, "SSH"},        {23, "Telnet"},    {25, "SMTP"},    {53, "DNS"},
    {80, "HTTP"},     {110, "POP3"},      {135, "RPC"},      {139, "NetBIOS"}, {143, "IMAP"},
    {443, "HTTPS"},   {445, "SMB"},       {993, "IMAPS"},    {995, "POP3S"},  {3306, "MySQL"},
    {3389, "RDP"},    {5900, "VNC"},      {8080, "HTTP-Alt"}, {8443, "HTTPS-Alt"},
};

static struct {
    pthread_t thread;
    bool thread_started;
    volatile bool cancel;
    volatile bool done;
    volatile uint8_t checked;
    volatile uint32_t open_mask; /* bit i = wifi_lab_ports[i] accepted */
    uint32_t ip;
} wifi_lab_portscan;

static bool wifi_lab_port_open(uint32_t ip, uint16_t port) {
    const int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock < 0) return false;
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = ip,
    };
    bool open = false;
    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        open = true;
    } else if(errno == EINPROGRESS) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(sock, &writable);
        struct timeval timeout = {.tv_sec = 0, .tv_usec = WIFI_LAB_PORT_TIMEOUT_MS * 1000};
        if(select(sock + 1, NULL, &writable, NULL, &timeout) > 0) {
            int error = 0;
            socklen_t length = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &length);
            open = error == 0;
        }
    }
    close(sock);
    return open;
}

static void* wifi_lab_portscan_worker(void* arg) {
    UNUSED(arg);
    for(uint8_t i = 0; i < COUNT_OF(wifi_lab_ports) && !wifi_lab_portscan.cancel; i++) {
        if(wifi_lab_port_open(wifi_lab_portscan.ip, wifi_lab_ports[i].port)) {
            wifi_lab_portscan.open_mask |= 1UL << i;
        }
        wifi_lab_portscan.checked = i + 1;
    }
    wifi_lab_portscan.done = true;
    return NULL;
}

static void wifi_lab_portscan_stop(void) {
    wifi_lab_portscan.cancel = true;
    if(wifi_lab_portscan.thread_started) {
        pthread_join(wifi_lab_portscan.thread, NULL);
        wifi_lab_portscan.thread_started = false;
    }
}

static void wifi_lab_portscan_render(WifiLab* app) {
    FuriString* text = furi_string_alloc();
    const uint32_t mask = wifi_lab_portscan.open_mask;
    furi_string_printf(text, "%s\n", app->target_ip);
    if(wifi_lab_portscan.done) {
        furi_string_cat_printf(text, "%s\n", mask ? "Open ports:" : "None of the 19 common\nports is open.");
    } else {
        furi_string_cat_printf(
            text,
            "Checking %u/%u...\n",
            wifi_lab_portscan.checked,
            (unsigned)COUNT_OF(wifi_lab_ports));
    }
    for(uint8_t i = 0; i < COUNT_OF(wifi_lab_ports); i++) {
        if(mask & (1UL << i)) {
            furi_string_cat_printf(
                text, "%u %s\n", wifi_lab_ports[i].port, wifi_lab_ports[i].name);
        }
    }
    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(text));
    furi_string_free(text);
}

void wifi_lab_scene_port_scan_on_enter(void* context) {
    WifiLab* app = context;
    memset(&wifi_lab_portscan, 0, sizeof(wifi_lab_portscan));
    wifi_lab_portscan.ip = inet_addr(app->target_ip);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 6144);
    wifi_lab_portscan.thread_started =
        pthread_create(&wifi_lab_portscan.thread, &attr, wifi_lab_portscan_worker, NULL) == 0;
    pthread_attr_destroy(&attr);
    if(!wifi_lab_portscan.thread_started) wifi_lab_portscan.done = true;

    app->tools_shown = UINT32_MAX;
    wifi_lab_portscan_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewWidget);
}

bool wifi_lab_scene_port_scan_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        /* Redraw only on change: a redraw resets the scroll position. */
        const uint32_t shown = ((uint32_t)wifi_lab_portscan.done << 31) |
                               ((uint32_t)wifi_lab_portscan.checked << 20) |
                               (wifi_lab_portscan.open_mask & 0xFFFFF);
        if(shown != app->tools_shown) {
            app->tools_shown = shown;
            wifi_lab_portscan_render(app);
        }
        return true;
    }
    return false;
}

void wifi_lab_scene_port_scan_on_exit(void* context) {
    WifiLab* app = context;
    wifi_lab_portscan_stop();
    widget_reset(app->widget);
}

/* --------------------------------------------------- Android TV remote --- */
/*
 * The engine speaks the Android TV Remote v2 protocol: the TV shows a PIN on
 * its own screen the first time, so only someone in front of the TV can pair.
 * The client certificate is kept on the SD card (/ext/wifi/androidtv).
 */

enum {
    WifiLabAtvConnecting,
    WifiLabAtvLive,
    WifiLabAtvFailed,
    WifiLabAtvPairStarting,
    WifiLabAtvPairPin,
    WifiLabAtvPairVerifying,
};

static void wifi_lab_atv_key_callback(void* context, int keycode) {
    WifiLab* app = context;
    wlan_androidtv_send_key(app->atv, keycode);
}

static bool wifi_lab_atv_ready(WifiLab* app) {
    if(!app->atv) app->atv = wlan_androidtv_alloc();
    if(app->atv) return true;
    app->atv_phase = WifiLabAtvFailed;
    wifi_lab_tools_status(app, "Android TV", "Not enough memory", NULL);
    return false;
}

static void wifi_lab_atv_connect(WifiLab* app) {
    if(!wifi_lab_atv_ready(app)) return;
    app->atv_phase = WifiLabAtvConnecting;
    wifi_lab_tools_status(app, "Android TV", "Connecting...", NULL);
    wlan_androidtv_op_connect(app->atv, app->target_ip);
}

void wifi_lab_scene_atv_remote_on_enter(void* context) {
    WifiLab* app = context;
    if(app->atv_abort) return; /* the first tick leaves */
    wlan_androidtv_remote_view_set_key_callback(app->atv_view, wifi_lab_atv_key_callback, app);
    wifi_lab_atv_connect(app);
}

bool wifi_lab_scene_atv_remote_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == WifiLabEventRetry) {
        wifi_lab_atv_connect(app);
        return true;
    }
    if(event.type == SceneManagerEventTypeTick) {
        if(app->atv_abort) {
            app->atv_abort = false;
            scene_manager_previous_scene(app->scene_manager);
            return true;
        }
        if(!app->atv) return true;
        const WlanAtvState state = wlan_androidtv_state(app->atv);
        if(app->atv_phase == WifiLabAtvConnecting) {
            if(state == WlanAtvStateConnected) {
                char name[WLAN_ATV_NAME_MAX];
                wlan_androidtv_device_name(app->atv, name, sizeof(name));
                app->atv_phase = WifiLabAtvLive;
                wlan_androidtv_remote_view_set_status(
                    app->atv_view,
                    name[0] ? name : app->host_name[0] ? app->host_name : "Android TV",
                    true);
                view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewAtvRemote);
            } else if(state == WlanAtvStateNeedsPair) {
                scene_manager_next_scene(app->scene_manager, WifiLabSceneAtvPair);
            } else if(state == WlanAtvStateError) {
                app->atv_phase = WifiLabAtvFailed;
                wifi_lab_tools_status(app, "Android TV", wlan_androidtv_error(app->atv), "Retry");
            }
        } else if(app->atv_phase == WifiLabAtvLive) {
            const bool live = wlan_androidtv_is_connected(app->atv);
            wlan_androidtv_remote_view_set_status(app->atv_view, NULL, live);
            if(!live) {
                const char* error = wlan_androidtv_error(app->atv);
                app->atv_phase = WifiLabAtvFailed;
                wifi_lab_tools_status(
                    app, "Android TV", error[0] ? error : "Connection lost", "Retry");
            }
        }
        return true;
    }
    return false;
}

void wifi_lab_scene_atv_remote_on_exit(void* context) {
    WifiLab* app = context;
    /* Session down whenever this screen is left (Back, or on the way to pairing). */
    if(app->atv) wlan_androidtv_disconnect(app->atv);
    widget_reset(app->widget);
}

static void wifi_lab_atv_pair_start(WifiLab* app) {
    if(!wifi_lab_atv_ready(app)) return;
    app->atv_phase = WifiLabAtvPairStarting;
    wifi_lab_tools_status(app, "Pairing", "Look at your TV...", NULL);
    wlan_androidtv_op_pair_start(app->atv, app->target_ip);
}

void wifi_lab_scene_atv_pair_on_enter(void* context) {
    WifiLab* app = context;
    wifi_lab_atv_pair_start(app);
}

bool wifi_lab_scene_atv_pair_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiLabEventTextDone) {
            app->atv_phase = WifiLabAtvPairVerifying;
            wifi_lab_tools_status(app, "Pairing", "Checking the PIN...", NULL);
            wlan_androidtv_op_pair_finish(app->atv, app->atv_pin);
            return true;
        }
        if(event.event == WifiLabEventRetry) {
            wifi_lab_atv_pair_start(app);
            return true;
        }
        return false;
    }
    if(event.type == SceneManagerEventTypeTick) {
        if(!app->atv) return true;
        const WlanAtvState state = wlan_androidtv_state(app->atv);
        if(app->atv_phase == WifiLabAtvPairStarting && state == WlanAtvStatePairShowPin) {
            app->atv_phase = WifiLabAtvPairPin;
            app->atv_pin[0] = '\0';
            text_input_reset(app->text_input);
            text_input_set_header_text(app->text_input, "PIN shown on the TV");
            text_input_set_minimum_length(app->text_input, WLAN_ATV_PIN_LEN);
            text_input_set_result_callback(
                app->text_input,
                wifi_lab_text_callback,
                app,
                app->atv_pin,
                sizeof(app->atv_pin),
                true);
            view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewTextInput);
        } else if(app->atv_phase == WifiLabAtvPairVerifying && state == WlanAtvStatePaired) {
            /* Trusted now: back to the remote scene, which connects. */
            scene_manager_previous_scene(app->scene_manager);
        } else if(
            (app->atv_phase == WifiLabAtvPairStarting ||
             app->atv_phase == WifiLabAtvPairVerifying) &&
            state == WlanAtvStateError) {
            app->atv_phase = WifiLabAtvFailed;
            wifi_lab_tools_status(app, "Pairing failed", wlan_androidtv_error(app->atv), "Retry");
        }
        return true;
    }
    if(event.type == SceneManagerEventTypeBack) {
        /* Give up on this TV: leave the remote scene underneath as well. */
        if(app->atv) wlan_androidtv_disconnect(app->atv);
        app->atv_abort = true;
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void wifi_lab_scene_atv_pair_on_exit(void* context) {
    WifiLab* app = context;
    text_input_reset(app->text_input);
    memset(app->atv_pin, 0, sizeof(app->atv_pin));
    widget_reset(app->widget);
}

/* --------------------------------------------------- AirPrint receiver --- */
/*
 * Only the plain receiver of the engine is used. Its other entry points
 * (advertising many fake printers, taking over another printer's name or
 * address) are never called and are not linked: check_lab_exclusions.py
 * fails the build if they appear in the image.
 */

static void wifi_lab_airprint_render(WifiLab* app) {
    char line[72];
    char ip[16] = {0};
    Widget* widget = app->widget;
    widget_reset(widget);
    if(!wlan_airprint_is_running()) {
        widget_add_string_element(
            widget, 64, 24, AlignCenter, AlignCenter, FontSecondary, "Could not start.");
        return;
    }
    wlan_airprint_get_ip(ip, sizeof(ip));
    widget_add_string_element(
        widget, 64, 0, AlignCenter, AlignTop, FontPrimary, wlan_airprint_get_name());
    snprintf(line, sizeof(line), "%s:%u", ip, wlan_airprint_get_port());
    widget_add_string_element(widget, 64, 14, AlignCenter, AlignTop, FontSecondary, line);
    snprintf(line, sizeof(line), "Received: %lu", (unsigned long)wlan_airprint_get_job_count());
    widget_add_string_element(widget, 64, 26, AlignCenter, AlignTop, FontSecondary, line);
    const char* last = wlan_airprint_get_last_file();
    widget_add_string_element(
        widget, 64, 38, AlignCenter, AlignTop, FontSecondary, last[0] ? last : "Saves to wifi/printer");
    widget_add_string_element(
        widget, 64, 52, AlignCenter, AlignTop, FontSecondary, "Back stops");
}

void wifi_lab_scene_airprint_on_enter(void* context) {
    WifiLab* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewLoading);
    wlan_airprint_start();
    app->tools_shown = wlan_airprint_get_job_count();
    wifi_lab_airprint_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewWidget);
}

bool wifi_lab_scene_airprint_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        if(wlan_airprint_is_running() && wlan_airprint_get_job_count() != app->tools_shown) {
            app->tools_shown = wlan_airprint_get_job_count();
            wifi_lab_airprint_render(app);
        }
        return true;
    }
    return false;
}

void wifi_lab_scene_airprint_on_exit(void* context) {
    WifiLab* app = context;
    wlan_airprint_stop();
    widget_reset(app->widget);
}

/* ----------------------------------------------------- channel monitor --- */
/*
 * Counts the frames on one channel and writes them to a .pcap for Wireshark.
 * Management frames (beacons, probes, association) are stored whole. Data
 * frames are cut to their 802.11 header: who talks to whom and how much, but
 * no payload. That leaves out EAPOL key frames, so a capture from this tool
 * cannot be used to attack a network's password, and no traffic contents of
 * other people end up on the card. Control frames are ignored.
 */

#define WIFI_LAB_MONITOR_CHANNEL_MIN 1
#define WIFI_LAB_MONITOR_CHANNEL_MAX 13

static volatile uint32_t wifi_lab_monitor_frames;
static volatile bool wifi_lab_monitor_saving;

static void wifi_lab_monitor_frame(void* buffer, wifi_promiscuous_pkt_type_t type) {
    const wifi_promiscuous_pkt_t* packet = buffer;
    uint16_t length = (uint16_t)packet->rx_ctrl.sig_len;
    if(length < 24) return;
    if(type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;

    if(type == WIFI_PKT_DATA) {
        const uint8_t* frame = packet->payload;
        uint16_t header = 24;
        if((frame[1] & 0x03) == 0x03) header += 6; /* four-address frame */
        if(frame[0] & 0x80) header += 2; /* QoS control */
        if(length > header) length = header;
    }
    wifi_lab_monitor_frames++;
    if(wifi_lab_monitor_saving) wlan_pcap_rec_frame(packet->payload, length);
}

static void wifi_lab_monitor_stop(WifiLab* app) {
    if(!app->monitor_running) return;
    /* Callback off first: the recorder must not be fed while it shuts down. */
    wlan_hal_set_promiscuous(false, NULL);
    if(wifi_lab_monitor_saving) {
        wifi_lab_monitor_saving = false;
        wlan_pcap_rec_stop();
    }
    app->monitor_running = false;
    wlan_sniffer_view_set_running(app->monitor_view, false);
    wlan_sniffer_view_set_saving(app->monitor_view, false);
}

static void wifi_lab_monitor_start(WifiLab* app) {
    char path[112];
    DateTime now;
    if(!wifi_lab_ensure_radio(app)) return;
    if(wlan_hal_is_connected()) {
        wlan_hal_disconnect();
        app->joined = false;
    }
    wifi_lab_monitor_frames = 0;
    app->monitor_ticks = 0;
    wlan_sniffer_view_reset_counters(app->monitor_view);

    furi_hal_rtc_get_datetime(&now);
    snprintf(
        path,
        sizeof(path),
        "%s/%04u%02u%02u_%02u%02u%02u_ch%u.pcap",
        WIFI_LAB_MONITOR_DIR,
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second,
        app->monitor_channel);
    wifi_lab_monitor_saving = wlan_pcap_rec_start(path);
    wlan_sniffer_view_set_saving(app->monitor_view, wifi_lab_monitor_saving);
    wlan_sniffer_view_set_saved(app->monitor_view, 0, 0);

    wlan_hal_set_channel(app->monitor_channel);
    wlan_hal_set_promiscuous(true, wifi_lab_monitor_frame);
    app->monitor_running = true;
    wlan_sniffer_view_set_running(app->monitor_view, true);
}

static void wifi_lab_monitor_action(WlanSnifferViewAction action, void* context) {
    WifiLab* app = context;
    uint32_t event;
    switch(action) {
    case WlanSnifferViewActionToggle:
        event = WifiLabEventMonitorToggle;
        break;
    case WlanSnifferViewActionChannelUp:
        event = WifiLabEventMonitorChannelUp;
        break;
    case WlanSnifferViewActionChannelDown:
        event = WifiLabEventMonitorChannelDown;
        break;
    default:
        return;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void wifi_lab_monitor_set_channel(WifiLab* app, uint8_t channel) {
    if(channel < WIFI_LAB_MONITOR_CHANNEL_MIN || channel > WIFI_LAB_MONITOR_CHANNEL_MAX) return;
    app->monitor_channel = channel;
    wlan_sniffer_view_set_channel(app->monitor_view, channel);
    if(app->monitor_running) wlan_hal_set_channel(channel);
}

void wifi_lab_scene_monitor_on_enter(void* context) {
    WifiLab* app = context;
    if(app->monitor_channel < WIFI_LAB_MONITOR_CHANNEL_MIN) {
        app->monitor_channel = WIFI_LAB_MONITOR_CHANNEL_MIN;
    }
    app->monitor_running = false;
    wlan_sniffer_view_reset_counters(app->monitor_view);
    wlan_sniffer_view_set_title(app->monitor_view, "Channel monitor");
    wlan_sniffer_view_set_channel_mode(app->monitor_view, true);
    wlan_sniffer_view_set_channel(app->monitor_view, app->monitor_channel);
    wlan_sniffer_view_set_target_count(app->monitor_view, 0);
    wlan_sniffer_view_set_running(app->monitor_view, false);
    wlan_sniffer_view_set_saving(app->monitor_view, false);
    wlan_sniffer_view_set_action_callback(app->monitor_view, wifi_lab_monitor_action, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiLabViewSniffer);
}

bool wifi_lab_scene_monitor_on_event(void* context, SceneManagerEvent event) {
    WifiLab* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case WifiLabEventMonitorToggle:
            if(app->monitor_running) {
                wifi_lab_monitor_stop(app);
            } else {
                wifi_lab_monitor_start(app);
            }
            return true;
        /* Same direction as upstream's view: turning "down" goes up a channel. */
        case WifiLabEventMonitorChannelDown:
            wifi_lab_monitor_set_channel(app, app->monitor_channel + 1);
            return true;
        case WifiLabEventMonitorChannelUp:
            wifi_lab_monitor_set_channel(app, app->monitor_channel - 1);
            return true;
        default:
            return false;
        }
    }
    if(event.type == SceneManagerEventTypeTick) {
        if(app->monitor_running) {
            app->monitor_ticks++;
            wlan_sniffer_view_set_received(app->monitor_view, wifi_lab_monitor_frames);
            wlan_sniffer_view_set_elapsed(app->monitor_view, app->monitor_ticks / 4);
            if(wifi_lab_monitor_saving) {
                wlan_sniffer_view_set_saved(
                    app->monitor_view, wlan_pcap_rec_frames(), wlan_pcap_rec_drops());
            }
        }
        return true;
    }
    return false;
}

void wifi_lab_scene_monitor_on_exit(void* context) {
    WifiLab* app = context;
    wifi_lab_monitor_stop(app);
    wlan_sniffer_view_set_action_callback(app->monitor_view, NULL, NULL);
}

/* ------------------------------------------------------- alloc / free --- */

void wifi_lab_tools_alloc(WifiLab* app) {
    app->atv_view = wlan_androidtv_remote_view_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WifiLabViewAtvRemote, app->atv_view);
    app->monitor_view = wlan_sniffer_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiLabViewSniffer, wlan_sniffer_view_get_view(app->monitor_view));
}

void wifi_lab_tools_free(WifiLab* app) {
    /* Scenes stop their own work in on_exit; these cover an exit that skips it. */
    wifi_lab_portscan_stop();
    wlan_netscan_stop_hostname_resolve();
    wifi_lab_monitor_stop(app);
    if(wlan_airprint_is_running()) wlan_airprint_stop();
    if(app->atv) {
        wlan_androidtv_disconnect(app->atv);
        wlan_androidtv_free(app->atv);
        app->atv = NULL;
    }
    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewAtvRemote);
    wlan_androidtv_remote_view_free(app->atv_view);
    view_dispatcher_remove_view(app->view_dispatcher, WifiLabViewSniffer);
    wlan_sniffer_view_free(app->monitor_view);
}
