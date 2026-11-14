/**
 * Lab profile: one shared scene that stands in for every NFC scene this build
 * does not provide (see ADD_SCENE_GATED in nfc_scene_config.h).
 *
 * The NFC app here identifies and reads tags, parses what it can, and saves
 * the result. Writing, card emulation, cloning, password/key unlocking and
 * key-recovery attacks are not built. Any menu path that used to lead to one
 * of them lands here instead of failing silently, so the upstream menu
 * structure can stay recognisable without the function behind it.
 */
#include "../nfc_app_i.h"

#ifdef FURI_PROFILE_LAB

void nfc_scene_lab_blocked_on_enter(void* context) {
    NfcApp* nfc = context;
    Widget* widget = nfc->widget;

    widget_reset(widget);
    widget_add_text_scroll_element(
        widget,
        0,
        0,
        128,
        64,
        "\e#Not in this build\n"
        "This firmware reads and\n"
        "identifies NFC tags only.\n"
        "\n"
        "Writing, emulation, cloning,\n"
        "unlocking and key recovery\n"
        "are not included.\n"
        "\n"
        "Press Back.");
    view_dispatcher_switch_to_view(nfc->view_dispatcher, NfcViewWidget);
}

bool nfc_scene_lab_blocked_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    /* Back is handled by the scene manager: return to the previous scene. */
    return false;
}

void nfc_scene_lab_blocked_on_exit(void* context) {
    NfcApp* nfc = context;
    widget_reset(nfc->widget);
}

#endif /* FURI_PROFILE_LAB */
