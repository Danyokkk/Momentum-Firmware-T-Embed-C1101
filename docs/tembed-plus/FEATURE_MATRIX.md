# Feature matrix: T-Embed CC1101 Plus, lab profile

Last updated 2026-11-15 for branch `tembed-plus/lab-profile`
(base: Sor3nt `ef4d8f3b`).

## How to read this

**Status** is what the feature is in this build:

| Status | Meaning |
| --- | --- |
| built | compiled into the image; behaviour inherited from upstream unless noted |
| new | added by this fork |
| needs porting | wanted, exists in Momentum/Unleashed or needs writing, not done yet |
| needs hardware | requires something that is not on the Plus |
| unsupported | the Plus hardware cannot do it |
| excluded | deliberately not built (safety scope) |

**Evidence** is how far it has actually been checked, and is the column to
trust:

| Evidence | Meaning |
| --- | --- |
| build | compiles and links for `lilygo_t_embed_cc1101`. Nothing more. |
| image check | `check_lab_exclusions.py` confirmed it on the linked ELF |
| host test | exercised by an offline test on a PC |
| hardware | observed working on a physical Plus |

**Nothing in this document has "hardware" evidence.** No T-Embed was
connected while this branch was written. Every "built" row means "upstream
ships it and it still compiles", not "it works".

## Core system

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Boot to desktop, encoder navigation, side key = back | built | build | unchanged from upstream | cold boot 10× from battery; no boot loop; menu reachable |
| Display (ST7789 320×170) | built | build | reset-pin question in `BOARD_PROFILE.md` | full-screen pattern, no offset or tearing |
| SD card, archive browser | built | build | shared SPI with radios; Plus SD-recovery option is off | mount SanDisk ≤ 32 GB; browse, create, delete; survive 20 Sub-GHz RX sessions |
| Settings, About, power/battery (BQ27220/BQ25896) | built | build | | battery % tracks a charge cycle |
| Asset packs / themes | built | build | SD bundle carries 35 packs | select one, reboot, still applied |
| External apps (`.fap`) from SD | built | build | prebuilt upstream FAPs may no longer load: the app API table lost the WPS-attack exports | load one allow-listed FAP |
| JavaScript runner | built | build | `js_blebeacon` module removed | run a GUI-only script |
| USB: serial console, qFlipper bridge, USB storage | built | build | qFlipper/storage switch the PHY away from the flashing interface | enable and disable each; reflash afterwards via download mode |
| U2F security key | built | build | uses the stock shared attestation key from the SD card, as official Flipper firmware does | register with a test account |
| Firmware update from SD / USB-RPC (`ota_updater`) | built | build | image integrity (SHA-256 in the image) is checked by `esp_ota_end`; images are **not signed** | stage a build in `/ext/update`, update, confirm version |
| OTA rollback | new | build | new image must stay up 20 s or the previous one boots again | flash a deliberately crashing build by OTA; confirm the old one returns |
| Network firmware/SD update | excluded | image check | fetched upstream's own images with TLS verification off | — |
| TLS server verification | new (config) | build | was globally disabled; now a client without a CA fails | HTTPS radio stream either verifies or fails closed |
| Reproducible build + toolchain lock | new | host | `tools/tembed_plus/build.py` built this image on Windows 11 | build on a second machine, compare `furi_esp32.bin` hash |
| Firmware version names the build | new | build | CI sets `FLIPPER_BUILD_LABEL`, giving versions such as `2.0.0+lab.v0.1.0` or `2.0.0+lab.1a2b3c4` in the image header; `package_build.py` reads it back; the About screen shows it | About shows the version the web flasher listed |
| Web flasher (install, backup, restore, erase in the browser) | new | host test | `web/`, built into a site by `tools/web/build_site.py`. Its MD5 code passes `tests/web/test_md5.mjs`; in Chrome, without a device, the page loaded the build list and matched all four files to the manifest | install from the site with backup on; restore that backup; read back and compare |
| CI: sanitizer host tests, build, packaging, attestation, site deploy, releases | new | build | `.github/workflows/firmware.yml` and `boards.yml`. First runs (2026-11-15): sanitizer host tests, the T-Embed build and its checks, 9 of 10 other boards, packaging and attestation passed. The release job runs only for a tag and has not run yet | — |

## Sub-GHz (CC1101)

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Read, Read RAW, saved-signal browser, frequency analyzer, RF spectrum | built | build | | receive a known 433.92 MHz remote you own; save; reload |
| 58 protocol entries already in upstream's registry (remotes, sensors, RAW, BinRAW) | built | build | includes rolling-code families | decode at least one static and one sensor protocol |
| TPMS decoders (Schrader, Ford, Renault, Citroën, PMV-107J) | built | build | receive-side | — |
| **25 weather-station decoders** (from Momentum) | new | host test: 4 decoders against 5 real captures, 21 against synthetic fixtures | see `PROVENANCE.md` and `tests/subghz_ws/README.md`; decode-only except Acurite 592TXR and Nexus-TH | receive your own outdoor sensor; reading matches its display |
| Transmit a saved static-code or RAW signal | built | build | allowed only inside the chosen region's bands, and only inside 300–350, 387–467.75, 779–928 MHz | set the region; transmit to your own receiver; confirm on a second receiver |
| **TX Region** (Radio Settings) | new | host test (table), build (HAL hook, NVS) | ten presets from Flipper's published table; default "Not set" = no transmit at all. Enforced in `furi_hal_subghz_is_tx_allowed()`, so it covers the app, CLI, scripts and external apps. Power and duty cycle are not enforced | with "Not set": TX refused with the "No TX region set" dialog. With EU: 433.92 allowed, 315.00 refused |
| Transmit rolling-code protocols | excluded | build | the library refuses to create an encoder for any `SubGhzProtocolTypeDynamic` protocol; they still decode. "Decode only" dialog on send | open a saved KeeLoq file, press Send, expect the dialog and no RF |
| Extended ("dangerous") TX range | excluded | build | HAL ignores the SD-card switch | request 280 MHz TX, expect refusal |
| Add Manually | built, filtered | build | lists fixed-code entries only (Princeton, CAME, Nice Flo, Gate TX, Linear, …); rolling-code entries and counter editing are hidden | list contains no "KL:", FAAC SLH, Somfy or Security+ entries |
| Playlist | built | build | replays saved files in order; each transmit goes through the same region and rolling-code gates | — |
| Jammer | excluded | image check | | menu entry absent |
| Code brute-forcer | excluded | image check | | menu entry absent |
| POCSAG, X10, Hörmann BiSecur, other Momentum/Unleashed-only protocols | needs porting | — | candidates listed in `PROVENANCE.md` | — |
| Arbitrary SDR / wideband capture | unsupported | — | CC1101 is a narrowband FSK/OOK transceiver | — |

## Infrared

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Learn, decode, raw capture, saved remotes, universal remotes, transmit | built | build | protocol set is identical to Momentum and Unleashed (NEC, Kaseikyo, Pioneer, RC5, RC6, RCA, Samsung, SIRC) | learn a TV remote button, replay it at 3 m |
| Extra protocol decoders | — | — | nothing to port: all three projects carry the same eight | — |

## NFC (PN532, 13.56 MHz)

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Tag detection, UID read, supported-protocol parsing, save | built | build | PN532 limits apply: no ISO 15693. MIFARE Classic without known keys shows type and UID only | read an NTAG and a MIFARE Classic test card you own |
| Card parsers (transit, NDEF, …) | built loader, **prebuilt plugins** | not rebuilt | 42 `.fal` files from upstream's SD overlay; optional in the SD bundle | open a card one parser recognises |
| MIFARE Classic / Ultralight C dictionary attacks, mfkey32 nonce collection, key lists | excluded | image check | scenes replaced by a "Not in this build" screen; sources not compiled | no "Extract MFC Keys" in the start menu; reading a keyless MFC ends at the result screen |
| Unlock NTAG/Ultralight/SLIX, DES key unlock | excluded | image check | same mechanism | Extra Actions shows only "Read Specific Card Type" |
| Emulation, write, clone, Add Manually, Edit UID, RPC emulation | excluded | image check (scenes); build (driver) | no protocol reports these features; scenes replaced; the PN532 driver never enters card-emulation mode. Per-protocol helper functions remain linked but unreachable | a saved card's menu has no Emulate or Write; hold the device to a reader and confirm it is not seen as a card |
| 125 kHz LF RFID | unsupported / needs hardware | — | no LF front end. Upstream's RDM6300 UART accessory uses GPIO43/44, which the Plus nRF24 occupies | — |
| iButton | unsupported | — | no 1-Wire hardware | — |

## nRF24

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Module detection | built | build | probe on entry; absent module shows "not found" | on a Plus: detected. On a base unit: clean "not found" |
| 2.4 GHz activity spectrum (receive-only, RPD per channel) | built | build | | channel activity rises near a busy Wi-Fi channel |
| Jammer (carrier and packet flood) | excluded | image check | driver transmit half is not compiled | menu entry absent |
| MouseJack sniff / keystroke injection | excluded | image check | | menu entry absent |

## Wi-Fi (2.4 GHz only)

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| **WiFi Lab**: passive scan, signal/channel list, per-network detail | new | build | scan type is passive in the HAL: no probe requests | scan next to a monitor-mode sniffer: zero frames from the device MAC |
| Join your own network (on-device confirmation), status page | new | build | password kept in RAM; saving to SD is a separate yes/no and is plain text | join, check IP; decline saving, reboot, password is gone |
| Save scan list to SD (CSV) | new | build | only on request; `apps_data/wifi_lab/` | open the CSV on a PC |
| Streaming app (internet radio etc.) | built | build | now subject to TLS verification | play one HTTP and one HTTPS stream |
| Deauth (3 variants), SAE flood, beacon spam | excluded | image check | `esp_wifi_80211_tx` is not linked into the image at all | — |
| Evil portal, captive DNS, credential capture, MITM, ARP block, code injection | excluded | image check | | — |
| Handshake / PMKID capture, WPS PIN attacks | excluded | image check | | — |
| Printer hijack, client-isolation probe | excluded | image check | | — |
| Port scanner, SMB browser, Android TV remote, Web file server, packet sniffer | not built | — | benign or dual-use parts of the removed app; candidates to re-add individually with confirmation prompts | — |
| 5 GHz Wi-Fi | unsupported | — | ESP32-S3 is 2.4 GHz only | — |

## Bluetooth LE

| Feature | Status | Evidence | Notes |
| --- | --- | --- | --- |
| BLE stack, Bluetooth settings, mobile-app link | built | build | Wi-Fi and BLE share the radio; enabling one parks the other |
| **BLE Lab**: passive scan, RSSI, address type, name, vendor, location-tag hint, CSV export | new | host test (advert parser), build (scan) | passive scan only; no connect, pair, write or advertise code. Needs Wi-Fi off (shared radio) | list fills near a phone; a sniffer shows no scan requests from the device |
| Advertisement spam, writes to third-party devices | excluded | image check | |
| Tracker impersonation (FindMy) | excluded | — | FAP left out of the SD bundle |

## Mesh / Buddy (ESP-NOW)

| Feature | Status | Evidence | Notes |
| --- | --- | --- | --- |
| Buddy link | excluded (disabled) | image check | `esp_now_init` is not linked. As audited: no encryption, no authentication, no replay protection, auto-accept pairing, and a buddy runs feature commands from any sender |
| Authenticated, encrypted, replay-protected link with explicit pairing and reset | needs porting | — | prerequisite for turning it back on |

## USB / HID

| Feature | Status | Evidence | Notes |
| --- | --- | --- | --- |
| BadUSB / BadKB script runner (USB and BLE) | excluded | image check | |
| Visible, user-started HID demo with preview and confirmation | needs porting | — | not written |

## Data handling

| Item | State |
| --- | --- |
| Telemetry or phone-home | none found in the code that is built |
| Captures | stay on the SD card; nothing uploads them |
| Wi-Fi passwords | plain text in `/ext/wifi/<ssid>.txt` if you choose "Save" (inherited format) |
| Third-party upload apps (wardriver, map) | not in the SD bundle |
| Committed key material | KeeLoq manufacturer keys and a vehicle key-fob keystore exist in upstream's `sdcard/`; the bundle tool leaves the user keys and the keystore out. The U2F attestation pair is the public stock one. |

## Decisions taken (second brief, 2026-11-14)

The follow-up brief settled what the first pass had left open:

| Question | Decision | Where |
| --- | --- | --- |
| NFC key attacks | not provided; explanation screen | `nfc_scene_config.h`, `nfc_scene_lab_blocked.c` |
| NFC emulate / write / clone | not provided; read-only app | same, plus `furi_hal_nfc.c` |
| Rolling-code generation and transmit | not provided; decode only | `lib/subghz/transmitter.c` |
| Region enforcement | presets, default no-transmit | `furi_hal_region*.c` |

## Still open

1. **Excluded sources are still in the tree**, uncompiled, to keep merges from
   upstream clean. Deleting them is cleaner but makes every upstream merge
   conflict in those directories.
2. **Mesh**: rebuild it with pairing, authentication and replay protection, or
   drop it.
3. **Prebuilt plugins and apps** (NFC parsers, games, tools) come from upstream
   as binaries. They should be rebuilt from reviewed source before being relied
   on; some may not load against this firmware's API table.
4. **Transmit confirmation.** Static-code and RAW transmit is region-gated but
   has no extra "I own this receiver" prompt beyond pressing Send.
5. **Upstream licence.** Still no top-level licence file upstream; ask the
   author before redistributing builds.

## Known gaps against the brief

- No hardware validation of anything.
- No signed updates (integrity check and rollback only).
- No HID demonstration app, no on-device backup, no rebuilt mesh.
- Region presets limit frequency only, not power or duty cycle.
- NFC: some card-level menu items still appear and lead to the explanation
  screen; an external app with its own NFC code is outside the gates.
- Host tests cover the new decoders, the region table and the BLE advert
  parser; the rest of the tree relies on upstream's own suites.
- Battery life, RAM headroom, task stacks and SD throughput are unmeasured.
- Remaining Momentum features are listed as "todo" in `MOMENTUM_PARITY.md`.
