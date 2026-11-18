# UI languages (English / Spanish / Russian)

The T-Embed CC1101 Plus firmware ships a three-language user interface. English
is the default; Spanish and Russian are selectable. The choice is made on first
boot and can be changed later in **Settings → System → Language**.

## First boot

On the very first power-up (before any language is stored) the firmware shows a
language picker ahead of the "made by daan1k" splash. Each option is drawn in
its own script — `English`, `Español`, `Русский` — so every choice is legible
regardless of which font is active. Turn the encoder to move, press to confirm.
The selection is saved immediately and the picker never appears again unless the
stored setting is removed.

## Changing the language later

**Settings → System → Language** cycles English → Español → Русский. The change
applies live (the whole settings list re-renders in the chosen script) and is
persisted, so it survives a reboot.

## Where it is stored

The selection lives in internal flash at `/int/.locale_lang.settings` (a
`saved_struct` blob, magic `0x1C`, version `1`). It does **not** use the SD card,
so the language is available on every boot even with no card inserted. Removing
that file (or a factory wipe of `/int`) returns the device to the first-boot
picker.

## Fonts and scripts

The UI renders through two font slots, `FontPrimary` and `FontSecondary`. The
active language selects which u8g2 faces those slots use — one place
(`canvas_set_font` in `components/gui/canvas.c`), so every menu, dialog and error
that uses the standard fonts switches script together:

| Language | Primary face | Secondary face | Covers |
| --- | --- | --- | --- |
| English | `helvB08_tr` | `haxrcorp4089_tr` | ASCII (unchanged from stock) |
| Spanish | `helvB08_tf` | `helvR08_tf` | Latin-1 — á é í ó ú ñ ¿ ¡ |
| Russian | `6x12_t_cyrillic` | `5x8_t_cyrillic` | ASCII + Cyrillic |

English keeps the exact original faces, so English screens are pixel-identical to
before. Spanish uses the same Helvetica face with the full Latin-1 glyph set, so
accents render with no change to the layout. Russian uses Cyrillic faces of a
similar size. Strings are drawn with `u8g2_DrawUTF8`, so UTF-8 text renders
directly.

## Translation coverage

Translations live in one table, `components/locale/locale_i18n_strings.h`, with
English / Spanish / Russian columns kept aligned by an X-macro. English is always
the fallback: a missing translation shows the English text rather than blanking.

Currently translated surfaces:

- the first-boot language picker,
- **Settings → System** item labels (Language, Hand Orient, Units, Time/Date
  Format, Log Level/Device/Baud, Debug, Heap Trace, Sleep Method, File Naming),
- a shared set of common words, status/progress strings and SD-card errors
  (`OK`, `Cancel`, `Back`, `Yes`/`No`, `Loading…`, `No SD card`, `SD card full`,
  …) available to any screen via `loc_str()`.

Other apps' menus still render in English until their strings are routed through
`loc_str()`; because the font follows the active language, those English strings
remain fully legible in every language mode. Adding a screen to the translated
set is: add a row to `locale_i18n_strings.h`, then call
`loc_str(LocStr…)` where the literal was.

## For developers

```c
#include <locale/locale_i18n.h>

const char* label = loc_str(LocStrSettings);   // active language, EN fallback
LocaleLang lang   = locale_lang_get();         // LocaleLangEN / ES / RU
locale_lang_set(LocaleLangES);                 // apply + persist
```

Validation: `python tests/locale_i18n/run.py` (host test, no device) checks the
table, the English fallback, the font apply-hook and that a chosen language
survives a reload. Font rendering itself is verified by the on-device build.

## Memory impact

Measured with `python tools/tembed_plus/build.py size` for
`lilygo_t_embed_cc1101`, comparing the passive image before and after the
language feature:

| Metric | Before (no i18n) | After (i18n) | Delta |
| --- | --- | --- | --- |
| App image (`furi_esp32.bin`) | 3,777,792 B | 3,792,224 B | **+14,432 B (~14.1 KB)** |
| App partition free | 39% of 0x5f0000 | 39% of 0x5f0000 | unchanged |
| Static RAM (DIRAM) | ~239,399 B | 239,403 B (70.05%) | **~+4 B** |

The ~14 KB of flash is the string table plus the three added font faces
(`helvB08_tf`, `helvR08_tf`, `6x12_t_cyrillic`, `5x8_t_cyrillic`), all in
`.rodata`. RAM is essentially unchanged: the only new RAM is three static bytes
(active language, configured flag, canvas language); every string and font is
flash-resident `const`. 102,357 B of DIRAM remain free.
