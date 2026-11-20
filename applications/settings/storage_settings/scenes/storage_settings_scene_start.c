#include "../storage_settings.h"
#include <locale/locale_i18n.h>

void storage_settings_scene_start_on_enter(void* context) {
    StorageSettings* app = context;

    FS_Error sd_status = storage_sd_status(app->fs_api);
    SubmenuSettingsHelperOption* options = app->helper_descriptor->options;
    options[0].name = loc_str(LocStrAboutIntStorage);
    options[1].name = loc_str(LocStrAboutSdCard);
    options[STORAGE_SETTINGS_MOUNT_INDEX].name =
        (sd_status != FSE_OK) ? loc_str(LocStrMountSdCard) : loc_str(LocStrUnmountSdCard);
    options[3].name = loc_str(LocStrFormatSdCard);
    options[4].name = loc_str(LocStrBenchmarkSdCard);
    options[5].name = loc_str(LocStrFactoryReset);
    submenu_settings_helpers_scene_enter(app->settings_helper);
}

bool storage_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    StorageSettings* app = context;
    return submenu_settings_helpers_scene_event(app->settings_helper, event);
}

void storage_settings_scene_start_on_exit(void* context) {
    StorageSettings* app = context;
    submenu_settings_helpers_scene_exit(app->settings_helper);
}
