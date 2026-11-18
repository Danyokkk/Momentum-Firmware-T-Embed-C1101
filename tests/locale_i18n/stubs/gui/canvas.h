/* Host stub of gui/canvas.h: only the language hooks the i18n module calls. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void canvas_set_ui_language(uint8_t lang);
const uint8_t* canvas_ui_language_font(uint8_t lang, bool primary);
