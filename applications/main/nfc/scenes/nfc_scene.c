#include "nfc_scene.h"

// Generate scene on_enter handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
#ifdef FURI_PROFILE_LAB
#define ADD_SCENE_GATED(prefix, name, id) prefix##_scene_lab_blocked_on_enter,
#endif
void (*const nfc_on_enter_handlers[])(void*) = {
#include "nfc_scene_config.h"
};
#undef ADD_SCENE
#ifdef FURI_PROFILE_LAB
#undef ADD_SCENE_GATED
#endif

// Generate scene on_event handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
#ifdef FURI_PROFILE_LAB
#define ADD_SCENE_GATED(prefix, name, id) prefix##_scene_lab_blocked_on_event,
#endif
bool (*const nfc_on_event_handlers[])(void* context, SceneManagerEvent event) = {
#include "nfc_scene_config.h"
};
#undef ADD_SCENE
#ifdef FURI_PROFILE_LAB
#undef ADD_SCENE_GATED
#endif

// Generate scene on_exit handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
#ifdef FURI_PROFILE_LAB
#define ADD_SCENE_GATED(prefix, name, id) prefix##_scene_lab_blocked_on_exit,
#endif
void (*const nfc_on_exit_handlers[])(void* context) = {
#include "nfc_scene_config.h"
};
#undef ADD_SCENE
#ifdef FURI_PROFILE_LAB
#undef ADD_SCENE_GATED
#endif

// Initialize scene handlers configuration structure
const SceneManagerHandlers nfc_scene_handlers = {
    .on_enter_handlers = nfc_on_enter_handlers,
    .on_event_handlers = nfc_on_event_handlers,
    .on_exit_handlers = nfc_on_exit_handlers,
    .scene_num = NfcSceneNum,
};
