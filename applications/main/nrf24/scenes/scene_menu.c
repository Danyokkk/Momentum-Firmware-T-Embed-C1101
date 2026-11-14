#include "../nrf24_app.h"

enum SubmenuIndex {
    SubmenuIndexSpectrum,
#ifndef FURI_PROFILE_LAB
    SubmenuIndexJammer,
    SubmenuIndexMouseJacker,
#endif /* !FURI_PROFILE_LAB */
};

static void nrf24_scene_menu_submenu_callback(void* context, uint32_t index) {
    Nrf24App* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void nrf24_app_scene_menu_on_enter(void* context) {
    Nrf24App* app = context;

    submenu_add_item(
        app->submenu,
        "Spectrum Analyzer",
        SubmenuIndexSpectrum,
        nrf24_scene_menu_submenu_callback,
        app);
#ifndef FURI_PROFILE_LAB
    submenu_add_item(
        app->submenu, "Jammer", SubmenuIndexJammer, nrf24_scene_menu_submenu_callback, app);
    submenu_add_item(
        app->submenu,
        "MouseJacker",
        SubmenuIndexMouseJacker,
        nrf24_scene_menu_submenu_callback,
        app);
#endif /* !FURI_PROFILE_LAB */

    view_dispatcher_switch_to_view(app->view_dispatcher, Nrf24ViewSubmenu);
}

bool nrf24_app_scene_menu_on_event(void* context, SceneManagerEvent event) {
    Nrf24App* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubmenuIndexSpectrum:
            scene_manager_next_scene(app->scene_manager, Nrf24AppSceneSpectrum);
            consumed = true;
            break;
#ifndef FURI_PROFILE_LAB
        case SubmenuIndexJammer:
            scene_manager_next_scene(app->scene_manager, Nrf24AppSceneJam);
            consumed = true;
            break;
        case SubmenuIndexMouseJacker:
            scene_manager_next_scene(app->scene_manager, Nrf24AppSceneMjMenu);
            consumed = true;
            break;
#endif /* !FURI_PROFILE_LAB */
        default:
            break;
        }
    }

    return consumed;
}

void nrf24_app_scene_menu_on_exit(void* context) {
    Nrf24App* app = context;
    submenu_reset(app->submenu);
}
