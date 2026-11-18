idf_component_register(
    SRCS "locale.c" "locale_i18n.c"
    INCLUDE_DIRS "." "locale"
    REQUIRES furi furi_hal datetime toolbox storage gui
)
