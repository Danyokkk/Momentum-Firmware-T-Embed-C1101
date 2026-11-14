# T-Embed CC1101 Plus: lab-profile fork

Branch `tembed-plus/lab-profile`, started 2026-11-14 from
Sor3nt/Flipper-Zero-ESP32-Port `ef4d8f3b`.

A Flipper-style firmware for the LilyGO T-Embed CC1101 Plus that is
passive by default and scoped to equipment you own or are authorised to test.
Momentum is the feature reference; Unleashed is a secondary source for
protocols Momentum lacks.

**State in one line:** it builds reproducibly and the excluded features are
verifiably absent from the image; it has not been run on a device.

| Document | What it answers |
| --- | --- |
| [FEATURE_MATRIX.md](FEATURE_MATRIX.md) | what is in, what is out, and how well each item has been checked |
| [BOARD_PROFILE.md](BOARD_PROFILE.md) | Plus hardware, pin map, shared-bus conflicts, open hardware questions |
| [PROVENANCE.md](PROVENANCE.md) | upstream commits reviewed, what was imported from where, licences |
| [RELEASE_GUIDE.md](RELEASE_GUIDE.md) | build, back up, flash, SD card, update, rollback, recovery, RF rules |

## What this branch changes

1. **Build profile.** One profile, `lab`. `CMakeLists.txt` sets
   `FURI_PROFILE_LAB=1` for all code; `fam_config.py` lists the apps and source
   files that are not compiled; mixed apps guard the remaining references.
2. **The build no longer modifies the SDK.** Upstream patched Espressif's
   `libnet80211.a` in place inside the ESP-IDF installation so the Wi-Fi blob
   would send deauthentication frames, and swapped the SDK's `esp_wps.c` for a
   modified copy. Both are gone. `build.py` refuses an SDK whose blob does not
   match Espressif's release.
3. **Removed from the image:** the WiFi attack app, WPS attack engine, raw
   802.11 transmit and beacon spam in the Wi-Fi HAL, BLE spam, BadUSB, nRF24
   jammer and MouseJack, Sub-GHz jammer and brute-forcer, the Sub-GHz
   extended-TX switch, the unauthenticated ESP-NOW mesh, and the network
   updater that ran with TLS verification off.
4. **Added:** 25 weather-station Sub-GHz decoders from Momentum with host
   tests; a small passive Wi-Fi tool (`wifi_lab`); passive scanning in the
   Wi-Fi HAL; TLS verification on; OTA rollback; a pinned build wrapper, an
   image checker and an SD bundle tool under `tools/tembed_plus/`.

## How the port boots (audit summary)

`main/app_main.c` → `furi_init()` → `furi_hal_init_early()` (CS lines high,
nRF24 CE low, `PWR_EN` high) → `furi_hal_init()` (NVS, RTC, power, Sub-GHz,
USB, LEDs, display, speaker, NFC) → one thread per entry in the generated
`FLIPPER_SERVICES` table (input, gui, storage, power, loader, desktop, …) →
`FLIPPER_ON_SYSTEM_START` hooks → idle. The encoder is brought up by the input
service from `targets/lilygo_t_embed_cc1101/target_input.c`; the SD card is
mounted by the storage service.

Which apps exist is decided at configure time: `main/CMakeLists.txt` runs
`tools/fam/generate.py` over the `application.fam` manifests named in
`fam_config.py` and writes `applications.c` plus one object library per app.
That single list is why a profile can remove an app completely.

The board is chosen with `-DFLIPPER_BOARD=lilygo_t_embed_cc1101`, which selects
`components/furi_hal/boards/board_lilygo_t_embed_cc1101.h` and the matching
`sdkconfig.defaults.<board>`.

## Verification done so far

| Check | Result |
| --- | --- |
| Firmware build, ESP-IDF v5.4.1, Windows 11 | passes; see `RELEASE_GUIDE.md` for the image size |
| `check_lab_exclusions.py` on the linked ELF | passes: no excluded app, source or symbol; `esp_wifi_80211_tx` and `esp_now_init` are not linked |
| `tests/subghz_ws/run.py` | passes: 548 checks (Windows build, so without ASan/UBSan) |
| SDK blob hash after the build | unchanged |
| Upstream's suite `tests/run_t_embed_features.py` | 7 of its checks fail, **identically on an untouched checkout of the base commit and on this branch** (run side by side on Windows with Zig), so this branch adds no failures there. Examples: `run_radio_lifecycle_tests.py` looks for a function (`survey_restore_owned`) that is not in `wlan_hal.c`; `bw16_i2c_app/run.py` has stale hashes for four files; `plus_sd_recovery/hal_run.py` does not compile against its own stubs |
| Other boards (Waveshare C6, Fatal Core, …) | not built on this branch; the lab profile applies to them too and may need per-board fixes |
| Anything on a physical T-Embed | not done |

## Things worth telling upstream

- No top-level `LICENSE` file.
- The in-place SDK patch affects every other project built with the same
  ESP-IDF checkout.
- `components/wifi/CMakeLists.txt` splices `$ENV{IDF_PATH}` into a CMake list;
  with a Windows-style path that is a parse error (`\E`).
- The `subghz_load_dangerous_settings` manifest had no `sources`, so its target
  compiled the whole Sub-GHz app a second time.
- `solight_te44.c` battery-flag precedence bug (also in Momentum).
- The two stale tests above.
