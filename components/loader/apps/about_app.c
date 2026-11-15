#include <furi.h>
#include <gui.h>
#include <furi_hal_board_info.h>
#include <toolbox/fw_version.h>
#include <view_dispatcher.h>
#include <widget.h>
#include <esp_app_desc.h>
#include <stdio.h>

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Widget* widget;
} AboutApp;

static uint32_t about_app_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

int32_t about_app(void* p) {
    UNUSED(p);

    AboutApp* app = malloc(sizeof(AboutApp));
    memset(app, 0, sizeof(AboutApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->widget = widget_alloc();

    /* Same layout as the stock firmware: project name and author first, then
     * the device this build runs on, then the firmware version. The device
     * name comes from the board header, so every board shows its own name. */
    widget_add_string_multiline_element(
        app->widget,
        64,
        10,
        AlignCenter,
        AlignTop,
        FontPrimary,
        FURI_ESP32_PROJECT_NAME);

    widget_add_string_multiline_element(
        app->widget,
        64,
        20,
        AlignCenter,
        AlignTop,
        FontSecondary,
#ifdef FURI_PROFILE_LAB
        FURI_ESP32_PROJECT_AUTHOR ", lab profile");
#else
        FURI_ESP32_PROJECT_AUTHOR);
#endif

    widget_add_string_multiline_element(
        app->widget,
        64,
        38,
        AlignCenter,
        AlignTop,
        FontSecondary,
        furi_hal_board_get_name());

    /* The image header's version carries the CI build label as well, for
     * example "2.0.0+lab.1a2b3c4": the same name the web flasher lists. The
     * widget copies the text, so a stack buffer is fine. */
    const esp_app_desc_t* app_desc = esp_app_get_description();
    char version_line[48];
    snprintf(
        version_line,
        sizeof(version_line),
        "FW: %s",
        (app_desc && app_desc->version[0]) ? app_desc->version : FURI_ESP32_VERSION);
    widget_add_string_multiline_element(
        app->widget, 64, 49, AlignCenter, AlignTop, FontSecondary, version_line);

    View* widget_view = widget_get_view(app->widget);
    view_set_previous_callback(widget_view, about_app_exit);

    view_dispatcher_add_view(app->view_dispatcher, 0, widget_view);
    view_dispatcher_attach_to_gui(
        app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, 0);
    widget_free(app->widget);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);

    return 0;
}