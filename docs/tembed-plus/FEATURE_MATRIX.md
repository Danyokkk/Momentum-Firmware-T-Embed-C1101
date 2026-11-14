# Feature matrix: T-Embed CC1101 Plus, lab profile

Last updated 2026-11-14 for branch `tembed-plus/lab-profile`
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

## Sub-GHz (CC1101)

| Feature | Status | Evidence | Notes | Acceptance check on hardware |
| --- | --- | --- | --- | --- |
| Read, Read RAW, saved-signal browser, frequency analyzer, RF spectrum | built | build | | receive a known 433.92 MHz remote you own; save; reload |
| 58 protocol entries already in upstream's registry (remotes, sensors, RAW, BinRAW) | built | build | includes rolling-code families | decode at least one static and one sensor protocol |
| TPMS decoders (Schrader, Ford, Renault, Citroën, PMV-107J) | built | build | receive-side | — |
| **25 weather-station decoders** (from Momentum) | new | host test: 5 verified against real captures, 20 compile + noise only | see `PROVENANCE.md`; decode-only except Acurite 592TXR and Nexus-TH | receive your own outdoor sensor; reading matches its display |
| Transmit a saved signal | built | build | limited to 300–350, 387–467.75, 779–928 MHz in the HAL | transmit to your own receiver; confirm on a second receiver |
| Extended ("dangerous") TX range | excluded | build | HAL ignores the SD-card switch | request 280 MHz TX, expect refusal |
| Region-aware TX limits | needs porting | — | `furi_hal_region` is a stub that allows everything inside the three bands. **You are responsible for your local band plan and duty cycle.** | — |
| Add Manually (incl. rolling-code remotes), Playlist | built | build | inherited; see "Open decisions" | — |
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
| Tag detection, UID read, supported-protocol parsers, save | built | build | PN532 limits apply: no ISO 15693, limited emulation | read an NTAG and a MIFARE Classic test card you own |
| MIFARE Classic key dictionary attack, mfkey32 nonce collection | built — **not yet gated** | build | inherited; see "Open decisions" | — |
| Emulation, write, clone | built — **no confirmation step yet** | build | inherited; needs the on-device confirmation the brief asks for | — |
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
| Passive BLE scan / tracker detector | needs porting | — | lived inside the removed BLE app next to the spam features; needs extracting on its own |
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

## Open decisions

These are inherited behaviours that the brief's safety rules touch but that
were **not changed** in this pass. They need an owner decision, then work.

1. **NFC key attacks.** MIFARE Classic dictionary/nested attacks and mfkey32
   collection are still in the NFC app. They are how you recover keys for your
   own cards, and also how you attack someone else's. Options: gate behind the
   lab profile like the others, or keep with an explicit confirmation screen.
2. **NFC emulate/write** has no "authorised test media only" confirmation.
3. **Sub-GHz "Add Manually"** can generate rolling-code remotes for ~50
   manufacturers, and **Playlist** replays saved signals in sequence. Both are
   standard Flipper features within the legal TX bands, and both can be
   misused against access systems.
4. **Region enforcement** for Sub-GHz transmit does not exist yet.
5. **Excluded sources are still in the tree**, uncompiled, to keep merges from
   upstream clean. Deleting them is cleaner but makes every upstream merge
   conflict in those directories.
6. **Mesh**: rebuild it properly, or drop it.

## Known gaps against the brief

- No hardware validation of anything.
- No signed updates (integrity check and rollback only).
- No passive BLE tool, no visible-HID demo, no region table.
- Host tests cover the new decoders only; the rest of the tree relies on
  upstream's own suites.
- Battery life, RAM headroom, task stacks and SD throughput are unmeasured.
