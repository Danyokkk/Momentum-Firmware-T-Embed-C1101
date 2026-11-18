#include "locale_i18n.h"

#include <furi.h>
#include <toolbox/saved_struct.h>
#include <storage/storage.h>

/* Implemented in the gui component (canvas.c). Declared here rather than
 * including <gui/canvas.h> so the locale component needs no dependency on gui
 * (both are always linked into the image, so the symbols resolve). */
void canvas_set_ui_language(uint8_t lang);
const uint8_t* canvas_ui_language_font(uint8_t lang, bool primary);

#define TAG "LocaleI18n"

#define LOCALE_LANG_PATH    INT_PATH(".locale_lang.settings")
#define LOCALE_LANG_MAGIC   0x1C
#define LOCALE_LANG_VERSION 1

/* Persisted form. Keep tiny and stable: appended fields only. */
typedef struct {
    uint8_t language; /**< LocaleLang */
    uint8_t configured; /**< bool: first-boot picker completed */
} LocaleLangSettings;

static LocaleLang locale_current_lang = LocaleLangEN;
static bool locale_configured = false;

/* Translation table, generated from the X-macro so the three columns stay
 * aligned row-by-row. Stored in flash (.rodata): no heap, no per-string RAM. */
static const char* const loc_table[LocStrCount][LocaleLangCount] = {
#define LOC_X(id, en, es, ru) \
    [id] = {[LocaleLangEN] = en, [LocaleLangES] = es, [LocaleLangRU] = ru},
#include "locale_i18n_strings.h"
#undef LOC_X
};

/* Native language names, each in its own script (UTF-8). */
static const char* const locale_endonyms[LocaleLangCount] = {
    [LocaleLangEN] = "English",
    [LocaleLangES] = "Espa\xC3\xB1ol", /* Español */
    [LocaleLangRU] = "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", /* Русский */
};

static void locale_lang_apply(void) {
    /* gui owns the u8g2 font pointers; we only hand it the active language. */
    canvas_set_ui_language((uint8_t)locale_current_lang);
}

void locale_i18n_init(void) {
    LocaleLangSettings s = {0};
    if(saved_struct_load(
           LOCALE_LANG_PATH, &s, sizeof(s), LOCALE_LANG_MAGIC, LOCALE_LANG_VERSION) &&
       s.language < LocaleLangCount) {
        locale_current_lang = (LocaleLang)s.language;
        locale_configured = s.configured ? true : false;
    } else {
        locale_current_lang = LocaleLangEN;
        locale_configured = false;
    }
    locale_lang_apply();
}

LocaleLang locale_lang_get(void) {
    return locale_current_lang;
}

bool locale_lang_is_configured(void) {
    return locale_configured;
}

const char* locale_lang_endonym(LocaleLang lang) {
    return (lang < LocaleLangCount) ? locale_endonyms[lang] : locale_endonyms[LocaleLangEN];
}

const uint8_t* locale_lang_font_primary(LocaleLang lang) {
    return canvas_ui_language_font((uint8_t)lang, true);
}

const uint8_t* locale_lang_font_secondary(LocaleLang lang) {
    return canvas_ui_language_font((uint8_t)lang, false);
}

void locale_lang_set(LocaleLang lang) {
    if(lang >= LocaleLangCount) return;
    locale_current_lang = lang;
    locale_configured = true;
    locale_lang_apply();

    LocaleLangSettings s = {.language = (uint8_t)lang, .configured = 1};
    if(!saved_struct_save(
           LOCALE_LANG_PATH, &s, sizeof(s), LOCALE_LANG_MAGIC, LOCALE_LANG_VERSION)) {
        FURI_LOG_E(TAG, "Failed to persist language selection");
    }
}

const char* loc_str_lang(LocaleLang lang, LocStrId id) {
    if(id >= LocStrCount) return "";
    if(lang >= LocaleLangCount) lang = LocaleLangEN;
    const char* s = loc_table[id][lang];
    if(!s) s = loc_table[id][LocaleLangEN];
    return s ? s : "";
}

const char* loc_str(LocStrId id) {
    return loc_str_lang(locale_current_lang, id);
}
