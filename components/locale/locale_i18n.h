/**
 * @file locale_i18n.h
 * GUI language selection and string translation for the T-Embed CC1101 Plus.
 *
 * Three UI languages are supported: English (default), Spanish and Russian.
 * The selection is persisted in internal flash and restored on boot. Fonts are
 * switched per active language so Spanish accents and Cyrillic render; see
 * canvas_set_ui_language() in gui/canvas.h.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UI language. Values are stable (persisted on flash) — append only. */
typedef enum {
    LocaleLangEN = 0, /**< English (default / fallback) */
    LocaleLangES = 1, /**< Spanish */
    LocaleLangRU = 2, /**< Russian */
    LocaleLangCount,
} LocaleLang;

/**
 * Translatable string identifiers. Keep in sync with the table in
 * locale_i18n.c via the LOCALE_I18N_STRINGS X-macro.
 */
typedef enum {
#define LOC_X(id, en, es, ru) id,
#include "locale_i18n_strings.h"
#undef LOC_X
    LocStrCount,
} LocStrId;

/** Load the persisted language and apply its font. Call once at startup. */
void locale_i18n_init(void);

/** Active UI language. */
LocaleLang locale_lang_get(void);

/** Set, persist, and apply the UI language (updates fonts immediately). */
void locale_lang_set(LocaleLang lang);

/** True once the user has chosen a language (first-boot picker completed). */
bool locale_lang_is_configured(void);

/** Native name of a language, e.g. "English", "Espanol", "Russian" in-script. */
const char* locale_lang_endonym(LocaleLang lang);

/** u8g2 font (primary/secondary) that renders the given language's glyphs.
 *  Returns NULL to mean "canvas default for this slot". Used by the first-boot
 *  picker to draw every option in its own script. */
const uint8_t* locale_lang_font_primary(LocaleLang lang);
const uint8_t* locale_lang_font_secondary(LocaleLang lang);

/** Translated string for the active language (English fallback if missing). */
const char* loc_str(LocStrId id);

/** Translated string for a specific language (English fallback if missing). */
const char* loc_str_lang(LocaleLang lang, LocStrId id);

#ifdef __cplusplus
}
#endif
