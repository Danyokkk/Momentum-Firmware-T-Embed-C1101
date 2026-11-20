/* Host test: UI language string table, English fallback, apply-hook, and
 * persistence round-trip. No device, no GUI — the canvas and saved_struct
 * dependencies are mocked below. See run.py. */
#include <locale/locale_i18n.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* ---- mock saved_struct: one in-memory slot ---- */
static struct {
    bool present;
    uint8_t magic, version;
    size_t size;
    uint8_t buf[64];
} g_slot;

bool saved_struct_save(
    const char* path,
    const void* data,
    size_t size,
    uint8_t magic,
    uint8_t version) {
    (void)path;
    assert(size <= sizeof(g_slot.buf));
    g_slot.present = true;
    g_slot.magic = magic;
    g_slot.version = version;
    g_slot.size = size;
    memcpy(g_slot.buf, data, size);
    return true;
}

bool saved_struct_load(const char* path, void* data, size_t size, uint8_t magic, uint8_t version) {
    (void)path;
    if(!g_slot.present || g_slot.magic != magic || g_slot.version != version ||
       g_slot.size != size)
        return false;
    memcpy(data, g_slot.buf, size);
    return true;
}

/* ---- mock canvas ---- */
static int g_canvas_lang = -1;
void canvas_set_ui_language(uint8_t lang) {
    g_canvas_lang = (int)lang;
}
const uint8_t* canvas_ui_language_font(uint8_t lang, bool primary) {
    /* distinct, non-null handle per (lang, slot) */
    return (const uint8_t*)(uintptr_t)(((size_t)lang << 1) | (primary ? 1u : 0u)) + 1;
}

#define CHECK(cond)                                            \
    do {                                                       \
        if(!(cond)) {                                           \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__);   \
            return 1;                                          \
        }                                                      \
    } while(0)

int main(void) {
    /* Empty store: first boot -> English, not configured. */
    g_slot.present = false;
    locale_i18n_init();
    CHECK(locale_lang_get() == LocaleLangEN);
    CHECK(!locale_lang_is_configured());
    CHECK(g_canvas_lang == (int)LocaleLangEN); /* apply-hook ran */

    /* Default language strings are English. */
    CHECK(strcmp(loc_str(LocStrSettings), "Settings") == 0);
    CHECK(strcmp(loc_str(LocStrLanguage), "Language") == 0);

    /* Switch to Spanish: active strings change, accents preserved, hook fired. */
    locale_lang_set(LocaleLangES);
    CHECK(locale_lang_is_configured());
    CHECK(g_canvas_lang == (int)LocaleLangES);
    CHECK(strcmp(loc_str(LocStrSettings), "Ajustes") == 0);
    CHECK(strcmp(loc_str(LocStrBack), "Atr\xC3\xA1s") == 0); /* Atrás */

    /* Explicit-language lookup is independent of the active language. */
    CHECK(strcmp(loc_str_lang(LocaleLangRU, LocStrSettings),
                 "\xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8") ==
          0); /* Настройки */
    CHECK(strcmp(loc_str_lang(LocaleLangEN, LocStrSettings), "Settings") == 0);

    /* Table completeness: every id has non-empty text in every language. */
    for(int id = 0; id < LocStrCount; id++) {
        for(int lang = 0; lang < LocaleLangCount; lang++) {
            const char* s = loc_str_lang((LocaleLang)lang, (LocStrId)id);
            CHECK(s != NULL && s[0] != '\0');
        }
    }

    /* Out-of-range language falls back to English. */
    CHECK(strcmp(loc_str_lang((LocaleLang)99, LocStrLanguage), "Language") == 0);

    /* Endonyms are present and distinct. */
    CHECK(strcmp(locale_lang_endonym(LocaleLangEN), "English") == 0);
    CHECK(locale_lang_endonym(LocaleLangES)[0] != '\0');
    CHECK(locale_lang_endonym(LocaleLangRU)[0] != '\0');

    /* Extended coverage (2026-11): power/storage settings + show-hidden toggle. */
    CHECK(strcmp(loc_str_lang(LocaleLangEN, LocStrFactoryReset), "Factory Reset") == 0);
    CHECK(strcmp(loc_str_lang(LocaleLangES, LocStrFactoryReset), "Rest. de f\xC3\xA1" "brica") == 0); /* fábrica */
    CHECK(strcmp(loc_str_lang(LocaleLangEN, LocStrShowHidden), "Show Hidden") == 0);
    CHECK(loc_str_lang(LocaleLangRU, LocStrReboot)[0] != '\0'); /* Cyrillic present */

    /* Persistence round-trip: save Russian, wipe in-memory state via a reload. */
    locale_lang_set(LocaleLangRU);
    g_canvas_lang = -1;
    locale_i18n_init(); /* reads the mock store */
    CHECK(locale_lang_get() == LocaleLangRU);
    CHECK(locale_lang_is_configured());
    CHECK(g_canvas_lang == (int)LocaleLangRU);

    /* Per-language picker fonts are non-null and differ by language. */
    CHECK(locale_lang_font_primary(LocaleLangEN) != NULL);
    CHECK(locale_lang_font_primary(LocaleLangES) != locale_lang_font_primary(LocaleLangEN));
    CHECK(locale_lang_font_primary(LocaleLangRU) != locale_lang_font_primary(LocaleLangES));

    printf("PASS: locale i18n (table, fallback, apply-hook, persistence)\n");
    return 0;
}
