# T-Embed CC1101 Plus: lab-profile fork

> Historical project notes. The lab-profile app/source exclusions and linked
> image deny-list described here have been removed; the root README describes
> the current full-profile build.

Branch `tembed-plus/lab-profile`, started 2026-11-14 from
Sor3nt/Flipper-Zero-ESP32-Port `ef4d8f3b`.

A Flipper-style firmware for the LilyGO T-Embed CC1101 Plus that is
passive by default and scoped to equipment you own or are authorised to test.
Momentum is the feature reference; Unleashed is a secondary source for
protocols Momentum lacks.

**Historical state:** it built reproducibly with the exclusions listed below.
The current build no longer applies those app/source exclusions or the linked
image deny-list. It has not been run on a device.

| Document | What it answers |
| --- | --- |
| [REPORT.md](REPORT.md) | one-page summary: what was implemented, build and test results, what is withheld, what is not done |
| [FEATURE_MATRIX.md](FEATURE_MATRIX.md) | what is in, what is out, and how well each item has been checked |
| [MOMENTUM_PARITY.md](MOMENTUM_PARITY.md) | every Momentum feature category mapped to this target: present, added, todo, add-on, n/a or withheld |
| [BOARD_PROFILE.md](BOARD_PROFILE.md) | Plus hardware, pin map, shared-bus conflicts, open hardware questions |
| [PROVENANCE.md](PROVENANCE.md) | upstream commits reviewed, what was imported from where, licences |
| [SOR3NT_COMPARISON.md](SOR3NT_COMPARISON.md) | how this firmware compares to the Sor3nt ESP32 port it is based on, category by category, with counts |
| [RELEASE_GUIDE.md](RELEASE_GUIDE.md) | web flasher; build, back up, flash, SD card, update, rollback, recovery, RF rules; CI and releases |

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
4. **Second pass (after the parity brief):** the NFC app is read-only
   (39 write/emulate/clone/unlock/key-attack scenes replaced by an explanation
   screen, card emulation refused in the PN532 driver); rolling-code Sub-GHz
   protocols are decode-only (no encoder can be created); Sub-GHz transmit
   needs a region preset and is off until one is chosen; a passive BLE scanner
   (`ble_lab`) was added.
5. **Added:** 25 weather-station Sub-GHz decoders from Momentum with host
   tests; a small passive Wi-Fi tool (`wifi_lab`); passive scanning in the
   Wi-Fi HAL; TLS verification on; OTA rollback; a pinned build wrapper, an
   image checker and an SD bundle tool under `tools/tembed_plus/`.
6. **Distribution (third pass):** a browser flasher (`web/`, assembled by
   `tools/web/`) on GitHub Pages; GitHub Actions that run the host tests with
   sanitizers, build and check the image, package it, attest it, deploy the
   site from `main` and publish a release for every `v*` tag
   (`.github/workflows/firmware.yml`, plus `boards.yml` for the other boards);
   and the commit or tag in the firmware version on the About screen.

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
| Lab-profile linked-image deny-list | historical check; removed from the current build |
| `tests/subghz_ws/run.py` | passes: 2130 checks (Windows build, so without ASan/UBSan) |
| `tests/subghz_region/run.py`, `tests/ble_lab/run.py` | pass: 212 and 29 checks |
| SDK blob hash after the build | unchanged |
| Upstream's suite `tests/run_t_embed_features.py` | 7 of its checks fail, **identically on an untouched checkout of the base commit and on this branch** (run side by side on Windows with Zig), so this branch adds no failures there. Examples: `run_radio_lifecycle_tests.py` looks for a function (`survey_restore_owned`) that is not in `wlan_hal.c`; `bw16_i2c_app/run.py` has stale hashes for four files; `plus_sd_recovery/hal_run.py` does not compile against its own stubs |
| Other boards | 9 of the 10 other profiles compile, link and pass the image check; thormini fails to link on upstream breakage. Table in `REPORT.md`. None was run |
| GitHub Actions, Linux (first runs 2026-11-15) | host tests pass with gcc ASan/UBSan; the T-Embed Plus build passes in the `espressif/idf:v5.4.1` container with the SDK, lock and image checks; the same 9 other boards build there and thormini fails the same way; packaging and attestation work |
| Web flasher | `tests/web/test_md5.mjs` passes; in Chrome the page loads the build list, matches all four files to the manifest by size, SHA-256 and MD5, and handles "no device" and phone widths. Never run against a T-Embed |
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
- Momentum weather decoders: `lacrosse_tx.c` calls `furi_crash()` on a received
  frame with an unknown type; `lacrosse_tx.c` and `emos_e601x.c` refuse the key
  files they save; `auriol_ahfl.c` compares a 6-bit checksum unmasked. More in
  `tests/subghz_ws/README.md`.
- Thormini: `components/gui/CMakeLists.txt` pointed at a file in the wrong
  folder, and the board still fails to link on two missing functions.
- Building an ESP32-C6 board from the same tree makes the component manager
  rewrite `dependencies.lock` (it re-solved TinyUSB from 0.21.0~4 to ~6), so
  later ESP32-S3 builds are no longer pinned. Per-target lock files avoid it.
- `subghz_txrx_tx_start()` frees a NULL transmitter (which asserts) when a
  protocol has no encoder.
- The two stale tests above.
