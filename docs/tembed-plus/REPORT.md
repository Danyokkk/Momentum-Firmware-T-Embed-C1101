# Report: T-Embed CC1101 Plus lab-profile firmware

Status on 2026-11-14, branch `tembed-plus/lab-profile` (base: Sor3nt
`ef4d8f3b`). Detail lives in the other documents in this folder; this is the
summary.

## What you can rely on

- The firmware **builds reproducibly** for the T-Embed CC1101 Plus with a
  pinned toolchain, and the build fails if the SDK has been modified, if the
  pinned managed components were re-resolved, or if any withheld feature is
  linked into the image.
- **Nothing has been run on a device.** No T-Embed was connected at any point.
  Every feature is at "compiles" or "passes a host test"; none is
  hardware-verified. The first-boot checklist in `RELEASE_GUIDE.md` is the
  next step.

## Implemented in this fork

| Area | What |
| --- | --- |
| Build | one `lab` profile; no SDK patching; pinned toolchain; image checker; SD bundle tool; all-boards build script |
| Sub-GHz | 25 weather-station decoders from Momentum; four upstream decoder bugs fixed, one of them a crash triggered by received data; region presets with transmit off by default; rolling-code protocols decode-only |
| NFC | read-only app: identify, read, parse, save. 39 write/emulate/clone/unlock/key-attack scenes replaced by an explanation screen; card emulation refused in the PN532 driver |
| Wi-Fi | WiFi Lab: passive scan, signal/channel list, join own network with confirmation, opt-in CSV export; scans are passive in the HAL |
| Bluetooth | BLE Lab: passive scan with name, vendor, signal, location-tag hint, opt-in CSV export |
| System | TLS verification on; OTA rollback; network updater that used unverified TLS removed |
| Docs | feature matrix with evidence levels, Momentum parity matrix, board profile, provenance and licences, release guide |

## Build results

T-Embed CC1101 Plus (`tools/tembed_plus/build.py build`, ESP-IDF v5.4.1,
Windows 11): see the table. Image check: no excluded app, source or symbol;
`esp_wifi_80211_tx` and `esp_now_init` are not linked.

All board profiles the repository carries, lab profile, compile-and-link only
(`tools/tembed_plus/cross_build.py`). None of these boards was run.

| Board profile | IDF target | Result | Image size | Image check | Note |
| --- | --- | --- | --- | --- | --- |
| `lilygo_t_embed_cc1101` (Plus defaults) | esp32s3 | builds | 3,064,400 bytes | pass | the target of this work; `build_t_embed_plus/` |
| `esp32s3_generic` | esp32s3 | builds | 3,056,672 bytes | pass | shares the T-Embed board header |
| `fatalcore` | esp32s3 | builds | 3,026,240 bytes | pass | ST25R3916 NFC: the PN532 card-emulation block does not apply to this driver; the app-level NFC gates do |
| `waveshare_c6_1.9` | esp32c6 | builds | 3,322,256 bytes | pass | ESP32-C6, no NFC/IR/nRF24; 512 KB RAM |
| `waveshare_c6_1.47` | esp32c6 | builds | 3,223,376 bytes | pass | ESP32-C6, no NFC/IR/Sub-GHz/nRF24 |
| `paradoxt` | esp32s3 | builds | 3,062,976 bytes | pass |  |
| `thorchimera` | esp32s3 | builds | 3,025,584 bytes | pass |  |
| `m5stick_s3` | esp32s3 | builds | 2,650,976 bytes | pass |  |
| `m5stack_cardputer_adv` | esp32s3 | builds | 2,634,080 bytes | pass | no PSRAM |
| `DIY` | esp32s3 | builds | 3,017,696 bytes | pass | pins from Pinout.txt |
| `thormini` | esp32s3 | **fails to link** | — | — | two functions missing from its board-specific files (upstream) |

Thormini fails to link because two functions are missing from its
board-specific files (`furi_hal_display_sync` in the SH1106 display driver and
`view_dispatcher_send_custom_event_nonblocking` in
`view_dispatcher_thormini.c`). That is upstream breakage, present before this
fork's changes; a wrong source path that stopped the board even configuring
was fixed here. Upstream's own CI builds only four of the eleven profiles.

## Tests

| Suite | Result | Covers |
| --- | --- | --- |
| `tests/subghz_ws/run.py` | 2130 checks pass | 25 weather decoders: 4 against 5 real captures, 21 against synthetic fixtures; key-file round trips; damaged files; noise and 2 M-pulse fuzz; regression tests for the fixed decoder bugs |
| `tests/subghz_region/run.py` | 212 checks pass | region preset table and lookup; default-deny |
| `tests/ble_lab/run.py` | 29 checks pass (one of them a 20 000-case fuzz loop) | BLE advertisement parser, including malformed input |
| `check_lab_exclusions.py` | passes | 4 excluded apps, 54 excluded source globs, 18 symbol rules on the linked image |
| upstream `tests/run_t_embed_features.py` | 7 of its checks fail, identically on the untouched base commit | not changed by this fork |

All host tests were run on Windows with Zig, which means without
ASan/UBSan. The CI workflow that would run them with sanitizers on Linux has
not been executed.

## Hardware verification

None. To do, in order: back up the device, flash, then work through section 5
of `RELEASE_GUIDE.md` (boot, display, encoder, SD, Sub-GHz receive, region
gate, NFC read, IR, nRF24 detection, Wi-Fi and BLE scans, USB modes, update
and rollback).

## Withheld categories

Not provided as working features. "Screen" means the menu path leads to an
on-device explanation.

| Category | How it is withheld |
| --- | --- |
| Wi-Fi deauth, SAE flood, beacon spam, evil portal, MITM, handshake capture, WPS attacks | app and engine not built; raw-frame transmit not linked |
| Sub-GHz jammer, brute-forcer | not built |
| Rolling-code generation and transmit | encoder creation refused in the library; "Decode only" dialog |
| Extended Sub-GHz TX range, region bypass | switch ignored; no bypass exists |
| NFC key attacks, key recovery, unlock, emulate, write, clone | scenes replaced (screen); PN532 target mode refused |
| nRF24 jamming, MouseJack | not built |
| BLE spam, tracker impersonation, writes to others' devices | not built |
| BadUSB / BadKB script runner | not built |
| ESP-NOW mesh | disabled until it has pairing, authentication and replay protection |
| Network firmware update over unverified TLS | removed |
| Attack-oriented external apps and vehicle/rolling-code key material | left out of the SD bundle |

## Not done

- Hardware validation.
- Parity items marked "todo" in `MOMENTUM_PARITY.md` (33 rows). Largest: three
  more IR universal remotes, POCSAG and the remaining Sub-GHz decoders, a
  confirmed-per-action HID remote, System/Clock settings, archive search, an
  on-device backup, a rebuilt mesh.
- Signed updates; power and duty-cycle limits per region.
- Rebuilding upstream's prebuilt plugins and apps from reviewed source.
- Upstream still has no top-level licence file; resolve that with the author
  before redistributing binaries.
- Thormini board link errors.
