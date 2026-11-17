# Feature matrix: T-Embed CC1101 Plus, lab profile

> Historical matrix: its lab-profile exclusion statuses predate the full-profile
> build. The current build includes the previously filtered apps and sources;
> see the repository README for remaining SDK and hardware limitations.

Last updated 2026-11-18 for branch `tembed-plus/lab-profile`
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
| excluded | was deliberately not built in the historical lab profile |

**Evidence** is how far it has actually been checked, and is the column to
trust:

| Evidence | Meaning |
| --- | --- |
| build | compiles and links for `lilygo_t_embed_cc1101`. Nothing more. |
| image check | historical lab-profile check; no longer run by the current build |
| host test | exercised by an offline test on a PC |
| hardware | observed working on a physical Plus |

**Nothing in this document has "hardware" evidence.** No T-Embed was
connected while this branch was written. Every "built" row means "upstream
ships it and it still compiles", not "it works".

## Core system

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Boot intro: "T-Embed CC1101 Plus lab firmware, made by daan1k" (bold attribution), 1.8 s before the desktop | new | build | `desktop_boot_intro()` in `applications/services/desktop/desktop.c` | shows once per power-up |
| English / Spanish / Russian UI and boot language choice | needs porting | — | no string-catalog translation layer exists; a selector alone would not translate the Flipper-style UI | translate the system and app strings, then persist and apply the boot choice |
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
| **Sub-GHz Remote**: map Up/Down/Left/Right/OK to saved `.sub` files | built (was not built) | build | upstream's bundled copy of the TX/RX helper is not compiled; the app uses the Sub-GHz app's helper, so the same region gate and rolling-code refusal apply. A file that may not be sent shows as a failed send (red blink) | build a remote from two static-code files you own; each button sends its file |
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
| **POCSAG pager decoder** (from Momentum) | new | host test: cross-checked against an independent encoder (1200/512 bps, numeric + alphanumeric, multi-batch) | receive-only (no Send flag, null encoder); FM preset. `tests/subghz_pocsag` | tune an FM pager frequency you are licensed to monitor; a page shows RIC + text |
| **X10, Nord Ice** (static), **AllStar Firefly** (rolling, from Momentum) | new | build | X10 and Nord Ice are static (transmit only inside a region, like the other static protocols); AllStar Firefly is rolling-code, so TX is refused — decode only | decode one of your own remotes of these types |
| **Hörmann BiSecur, Doorbell32, KeyFinder2, Freezer, FAAC TM, Proteco, Telcoma EDGE, BelKorn** (from Momentum/Unleashed) | new | build | Hörmann BiSecur and BelKorn are rolling-code → decode-only via the gate; the rest static. Unleashed ones use their `common.c` helper | decode one of your own remotes of these types |
| Prastel and any further Momentum/Unleashed-only protocols | needs porting | — | Prastel needs Unleashed's `public_api.h`; see `PROVENANCE.md` | — |
| Arbitrary SDR / wideband capture | unsupported | — | CC1101 is a narrowband FSK/OOK transceiver | — |

## Infrared

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Learn, decode, raw capture, saved remotes, universal remotes, transmit | built | build | protocol set is identical to Momentum and Unleashed (NEC, Kaseikyo, Pioneer, RC5, RC6, RCA, Samsung, SIRC) | learn a TV remote button, replay it at 3 m |
| **Universal remotes: Blu-ray/DVD, Monitors, Digital Signs** (from Momentum) | new | build (scenes, icons and `.ir` libraries verified in the image) | three more universal-remote libraries, added to the Universal menu beside TVs/Audio/ACs/… | open each, send a command to your own device |
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
| **SD file server** (WiFi Lab): browser file manager for the SD card, over your own network or a WPA2 hotspot | new (upstream engine, new front end) | build | upstream's `wlan_webfs.c`. Asks before every start. The HTTP API has **no login**: over your own network anyone on that network can read and change the card (the prompt says so). Hotspot is always WPA2; upstream's published default password is replaced by a random one saved in `/ext/webfs/config.txt`. Hotspot mode pauses Wi-Fi and Bluetooth. Needs `/ext/webfs/index.html` from the SD bundle. Stops when the screen is left | start both modes, upload and download a file from a browser; confirm the server is gone after Stop |
| **SMB browser** (WiFi Lab): browse a share, copy files or folders to the SD card | new (upstream engine, new front end) | build | upstream's `wlan_smb.c` on libsmb2 (SMB2/3). Server address is typed in, no network sweep. Read-only towards the server. Credentials stay in RAM. Copies land in `apps_data/wifi_lab/smb/<server>/` after a confirmation | browse a share on a PC or NAS as guest and with a password; copy one file and one folder |
| Streaming app (internet radio etc.) | built | build | now subject to TLS verification | play one HTTP and one HTTPS stream |
| Deauth (3 variants), SAE flood, beacon spam | excluded | image check | `esp_wifi_80211_tx` is not linked into the image at all | — |
| Evil portal, captive DNS, credential capture, MITM, ARP block, code injection | excluded | image check | | — |
| Handshake / PMKID capture, WPS PIN attacks | excluded | image check | | — |
| Printer hijack, client-isolation probe | excluded | image check | | — |
| **Devices on network** (WiFi Lab): ARP lookup of the joined /24, NetBIOS names | new (upstream engine, new front end) | build | upstream's `wlan_netscan.c`. Only on the network the device has joined, after a confirmation. Picking a device offers Port check, Android TV remote, SMB browser | list matches the router's client list |
| **Port check** (WiFi Lab): 19 common TCP ports of one chosen host | new | build | plain connect, 500 ms per port, one host at a time (typed address or picked from the device list). No banner grabbing, no sweep across hosts | against a PC with a known open port |
| **Android TV remote** (WiFi Lab) | new (upstream engine and button view, new scenes) | build | upstream's `wlan_androidtv.c`: Android TV Remote v2. First use needs the PIN the TV shows on its own screen. Client certificate is stored in `/ext/wifi/androidtv` | pair with a TV, navigate, volume |
| **AirPrint receiver** (WiFi Lab) | new (upstream engine, new scene) | build; image check (spam/clone/capture entry points absent) | upstream's `wlan_airprint.c`, plain receiver only: appears as a printer on the joined network and saves what is printed to `/ext/wifi/printer`. Asks before starting, stops on Back | print one page from a phone; file appears on the card |
| **Channel monitor** (WiFi Lab): frame counter for one channel, `.pcap` for Wireshark | new (upstream recorder and counter view, new capture filter) | build | management frames are stored whole; **data frames are cut to their 802.11 header**, so no payloads and no EAPOL key frames reach the card (handshake capture stays excluded). Leaves the joined network while it runs. Files in `apps_data/wifi_lab/monitor/` | open a capture in Wireshark: beacons complete, data frames header-only |
| Upstream's packet sniffer with full payloads and per-client targeting | excluded | — | it records WPA handshakes as a side effect; the channel monitor above replaces it | — |
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

Reviewed again on 2026-11-16 and left out: besides "identify" (blink a paired
board), the only thing the buddy link does is offload WPA handshake capture to
the buddy boards, which is in the excluded set. Without that there is no
feature left for the link to carry.

## USB / HID

| Feature | Status | Evidence | Notes |
| --- | --- | --- | --- |
| BadUSB / BadKB script runner (USB and BLE) | excluded | image check | the script runner (`bad_usb`) stays excluded |
| **USB Keyboard & Mouse / Bluetooth Remote** (`hid_usb`, `hid_ble`) | new | build; image check (stealth absent) | user-driven HID: keyboard, numpad, media, mouse, presenter (keynote), push-to-talk. Every keystroke is a deliberate device-button press, not a script. Appears in the main menu |
| Mouse Jiggler Stealth (hidden keep-awake) | excluded | image check | source excluded + `#ifndef FURI_PROFILE_LAB`; the plain, on-screen Mouse Jiggler remains |

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
- No on-device backup, no rebuilt mesh. (A user-driven USB/BLE HID remote is built now.)
- Region presets limit frequency only, not power or duty cycle.
- NFC: some card-level menu items still appear and lead to the explanation
  screen; an external app with its own NFC code is outside the gates.
- Host tests cover the new decoders, the region table and the BLE advert
  parser; the rest of the tree relies on upstream's own suites.
- Battery life, run-time free heap, task stacks and SD throughput are unmeasured
  (they need a device). The 2026-11-18 T-Embed linker map reports 16,383 of
  16,384 bytes of IRAM used (1 byte free) and 205,940 of 341,760 bytes of
  combined internal D/IRAM used (135,820 free before heap allocations). The
  WiFi Lab tools and Sub-GHz Remote account for about 6.4 KB of static memory;
  their working buffers are allocated only while a tool is open, mostly in
  PSRAM. The 37 decoders this fork added put 4 bytes in static RAM; their code
  and tables are in flash. A Sub-GHz receiver uses about 3.4 KB plus allocator
  overhead (upstream's own 63 decoders: 6.9 KB). The BLE Lab device table
  (3.2 KB) sits in PSRAM on ESP32-S3 boards.
- Remaining Momentum features are listed as "todo" in `MOMENTUM_PARITY.md`.
