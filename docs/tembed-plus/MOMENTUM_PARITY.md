# Momentum feature parity: T-Embed CC1101 Plus

Every Momentum feature category, mapped to this firmware. Read with
[FEATURE_MATRIX.md](FEATURE_MATRIX.md) (what is in the image and how well it is
checked) and [PROVENANCE.md](PROVENANCE.md) (sources and licences).

| Reference | Commit | Notes |
| --- | --- | --- |
| Momentum | `d3f89df` (2026-08-18), API 87.1 | no version heading in its changelog; latest remote tag `mntm-012` |
| Unleashed | `1a5676a` (2026-11-13), API 88.27 | "Build tag 13nov2026"; latest remote tag `unlshd-095` |
| This port | branch `tembed-plus/lab-profile` on Sor3nt `ef4d8f3b` | |

Momentum's external-apps bundle (`applications/external`) is a submodule that
was not checked out, so that section comes from Momentum's ReadMe and
changelog only.

## Status words

| Status | Meaning |
| --- | --- |
| **present** | compiled into this firmware, inherited from the port |
| **added** | implemented by this fork |
| **todo** | compatible with the Plus; not implemented yet |
| **add-on** | needs hardware that is not on the Plus |
| **n/a** | the Plus cannot do it |
| **withheld** | deliberately not provided as a working feature; "screen" means the menu path leads to an on-device explanation instead |

**Verification** says how the row was checked: *build* (compiles and links),
*image* (asserted on the linked ELF by `check_lab_exclusions.py`), *host*
(offline test), *hardware*. No row has hardware verification: no device was
available. "present" therefore means "in the image", not "works".

All rows are for build target `lilygo_t_embed_cc1101`, lab profile.

## 1. Sub-GHz

Hardware: CC1101 on SPI2 (CS 12, GDO0 3, GDO2 38), band switch GPIO47/48.

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| Read (decode) | yes | present | build |
| Read RAW, RAW file save/load | yes | present | build |
| Saved signals browser, rename, delete | yes | present | build |
| Frequency Analyzer | yes | present | build |
| RF Spectrum (port extra, not in Momentum) | yes | present | build |
| Receiver config: frequency, modulation, hopping, RSSI threshold, Bin RAW, ignore filters, sound, lock keyboard | yes (port's set) | present | build |
| Receiver config: Repeater, Remove Duplicates, Delete Old Signals, Autosave, Ignore Weather/TPMS toggles | no | todo | — |
| 58 registered protocols | the port registers its own 58 (near-identical list; adds Holtek HT6P20B and Agilize); AllStar Firefly and Nord Ice added here (below) | present | build |
| 25 weather-station protocols (present in Momentum's tree but commented out of its registry) | registered and active here | added | host: 4 decoders against 5 real captures, 21 against synthetic fixtures (see `tests/subghz_ws/README.md`) |
| TPMS Schrader GG4 (commented out in Momentum) | active, plus Ford, Renault, Citroën, PMV-107J | present | build |
| POCSAG (commented out in Momentum) | receive-only pager decoder, ported and registered; FM, decode only (no Send flag, null encoder) | added | host (`tests/subghz_pocsag`: 1200/512 bps, numeric + alphanumeric, cross-checked against an independent encoder) |
| X10 (commented out in Momentum) | ported and registered; static code (310 MHz AM) | added | build |
| Hörmann BiSecur (commented out in Momentum) | ported and registered; 868 MHz FSK rolling code (Dynamic), so decode-only via the transmitter gate | added | build |
| AllStar Firefly, Nord Ice | both ported and registered. Nord Ice is static; AllStar Firefly is rolling-code (Dynamic), so the lab transmitter gate makes it decode-only | added | build |
| Unleashed-only: Doorbell32, KeyFinder2, Freezer | ported and registered (static codes), via the Unleashed `common.{c,h}` helper | added | build (a real Freezer capture exists in Unleashed's test resources for a later host test) |
| Unleashed-only: BelKorn, FAAC TM, Proteco, Telcoma EDGE | ported and registered. BelKorn is rolling-code (Dynamic) → decode-only via the gate; FAAC TM, Proteco, Telcoma EDGE are static | added | build |
| Unleashed-only: Prastel | no (needs Unleashed's `public_api.h` layer; rolling-code) | todo as decode-only | — |
| Transmit a saved static-code or RAW signal | yes, inside the chosen region's bands | present + region gate added | build; region table host-tested |
| Region lock (Momentum: region service + "Bypass Region Lock") | region presets, default "Not set" = no transmit; no bypass | added | host (`tests/subghz_region`), build |
| "Extend Freq Bands" (281–361, 378–481, 749–962 MHz) | switch ignored in the HAL | withheld | build |
| Transmit rolling-code protocols (KeeLoq, Security+, Somfy, FAAC SLH, …) | decode only; encoder creation refused in `lib/subghz/transmitter.c`; "Decode only" dialog | withheld (screen) | build |
| Add Manually | fixed-code entries only | present, filtered | build |
| Add Manually: rolling-code remotes; set serial/counter/seed; Counter mode ("Signal Settings") | hidden; encoders unavailable | withheld | build |
| Custom buttons for rolling codes (Unleashed) | not applicable once rolling TX is off | withheld | — |
| Radio Settings: TX power, protocol names in file names | yes; plus frequency offset and TX Region | present / added | build |
| Radio Settings: external module, GPS baud, debug pin | no external module on this port; debug pin only in debug mode | n/a / present | build |
| Static/hopper frequency lists (Momentum app > Protocols) | port uses its `subghz/assets` setting files | present (file-based) | build |
| GPS "Subdriving" (location stamped on captures) | no; GPIO43/44 are the nRF24's on a Plus | add-on (and conflicts with the Plus module) | — |
| Sub-GHz Remote (map saved signals to buttons) | source present, not built | todo (static/RAW only) | — |
| Playlist | yes (port feature) | present | build |
| Jammer (port feature, not Momentum) | removed | withheld | image |
| Bruteforce (port feature; external app in Momentum/Unleashed) | removed | withheld | image |
| Sub-GHz CLI, JS `subghz` module | yes; transmit subject to the same HAL and library gates | present | build |

## 2. NFC

Hardware: PN532 over I2C (`0x24`), 13.56 MHz, ISO14443-A/B and FeliCa. The
PN532 cannot do ISO15693, so NFC-V/SLIX tags are not readable on this board
regardless of software.

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| Read / identify (15 protocol families) | same 15 names in the library; ISO15693-3 and SLIX are stubs on PN532 | present (13 usable) / n/a (2) | build |
| Read Specific Card Type | yes | present | build |
| Saved cards: open, info, rename, delete | yes | present | build |
| Card info, More Info, DESFire apps, FeliCa systems, EMV transactions view | yes | present | build |
| 44 card parsers (transit, hotel, NDEF, …) | loaded as `.fal` plugins from the SD card; upstream ships 42 prebuilt | present, **prebuilt binaries** | not rebuilt here |
| MIFARE Classic: read with keys already known for that card (key cache) | yes; with no keys the result is type + UID only | present | build |
| MIFARE Classic dictionary attack, nested attack | scene replaced; not compiled | withheld (screen) | image |
| Extract MFC Keys / Detect Reader / mfkey nonces | scene replaced; not compiled; menu entries hidden | withheld (screen) | image |
| MIFARE Ultralight C dictionary attack | scene replaced; read flow no longer starts it | withheld (screen) | image |
| Unlock NTAG/Ultralight, Unlock SLIX-L, FeliCa/DES key unlock | scenes replaced; menu entries hidden | withheld (screen) | image |
| Key dictionaries management (MFC, UL-C) | scenes replaced; menu entries hidden | withheld (screen) | image |
| Emulate UID / full emulation | no protocol reports the feature; scene replaced; PN532 target mode refused in the driver | withheld (screen) | image (scene), build (driver) |
| Write, Write to Initial Card, Update from Initial Card | no protocol reports the feature; scenes replaced | withheld (screen) | image |
| Clone (port feature), Chameleon wipe/upload | menu entry hidden; scenes replaced | withheld (screen) | image |
| Add Manually (create a card to emulate), Edit UID | hidden; scenes replaced | withheld (screen) | image |
| RPC-driven emulation | app declines the RPC launch | withheld | build |
| NDEF tag emulation (URL/text record, fixed identity) | driver path left in; no menu entry reaches it | present in HAL, no UI | build |
| NFC Field detect/generate | field scene present; field *detect* is a stub on PN532 | present / n/a | build |
| MIFARE Plus keys, Ultralight AES, TexKom, Bambu parser (Unleashed) | no (a prebuilt Bambu parser `.fal` exists without source) | withheld (keys) / todo (parsers) | — |
| NFC CLI | not built | todo (read-only subset) | — |

What "withheld (screen)" means in the NFC app: 39 scenes are routed to
`nfc_scene_lab_blocked.c`, which says the build is read-only. Some per-protocol
menu items ("Unlock", "Show Keys", "Extract MFC Keys" inside a card's menu) are
still listed and lead to that screen. The per-protocol emulate/write helper
functions are still linked through support tables but nothing calls them; for
emulation the PN532 driver also refuses to enter target mode. A separately
installed external app with its own NFC code is outside these gates.

## 3. Infrared

Hardware: IR LED on GPIO2, receiver on GPIO1 (RMT).

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| Learn New Remote, decode, RAW learn | yes | present | build |
| Saved Remotes, edit, rename, delete | yes | present | build |
| 14 protocol variants in 8 families | identical set | present | build |
| Universal Remotes: TVs, Audio, Projectors, LEDs, Fans, ACs | yes (6 libraries) | present | build |
| Universal Remotes: Blu-ray/DVD, Monitors, Digital Signs, Load from Library File | no; needs three scenes, their icons and three `.ir` libraries from Momentum | todo | — |
| Easy-learn (preset button names) | no | todo | — |
| GPIO settings (external IR module) | "Infrared Settings" exists; the external pin is GPIO43, which the Plus nRF24 uses | n/a on a Plus | — |
| IR CLI | not built | todo | — |
| JS `infrared` module | yes | present | build |

## 4. 125 kHz RFID and iButton

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| LF-RFID read, saved, add manually, emulate, write, T5577 tools (26 protocols) | library compiled (23 protocols), app not built | add-on: no LF front end. The port's RDM6300 UART reader option uses GPIO43/44, taken by the Plus nRF24 | — |
| iButton read, saved, add manually, emulate, write (11 key types) | app source only, no library | n/a: no 1-Wire hardware | — |
| Archive tabs for RFID and iButton | tabs exist with no app behind them | todo: hide on boards without the hardware | build |

## 5. USB, HID, U2F

Hardware: USB D−/D+ on GPIO19/20; USB-Serial/JTAG by default, TinyUSB
composite installed on demand.

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| BadKB / BadUSB script runner (USB and BLE), ID/MAC spoofing options | removed | withheld | image |
| USB Remote / Bluetooth Remote (keyboard, media, mouse, presenter, …) | source present (`hid_usb`, `hid_ble`), not built | todo: visible, user-driven remote with on-device confirmation. Mouse jiggler "stealth" mode: withheld | — |
| U2F | yes | present | build |
| USB serial console (CDC) | yes | present | build |
| qFlipper/RPC bridge over USB | yes | present | build |
| USB mass storage (SD card to PC) | yes (lock menu) | present | build |
| USB-UART bridge (GPIO app) | not built; the UART pins are the nRF24's on a Plus | n/a on a Plus | — |
| JS `badusb`, `usbdisk` modules | not built | withheld / todo | — |

## 6. Bluetooth

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| BLE stack, on/off, unpair all | yes (Bluedroid, BLE 4.2 feature set) | present | build |
| Mobile-app serial/RPC profile | yes | present | build |
| BLE HID profile | profile code present; no app uses it | todo with the HID remote | — |
| FindMy Flipper (broadcast as a tracker tag) | not built | withheld | — |
| BLE Spam (external app) | removed | withheld | image |
| JS `blebeacon` | removed | withheld | image |
| Passive BLE scanner (not a Momentum firmware feature) | BLE Lab: passive scan, RSSI, address type, name, vendor, tag hint, CSV export | added | host (parser), build (scan) |

## 7. Wi-Fi

Momentum firmware has no Wi-Fi; it relies on external ESP boards and apps
(Marauder, GhostESP, Wardriver). The ESP32-S3 has 2.4 GHz Wi-Fi built in and no
5 GHz radio. It is not a substitute for dedicated assessment hardware: no
5 GHz, no multi-channel capture, one radio shared with Bluetooth.

| Feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| Passive network scan, channel/signal list, details | WiFi Lab | added | build |
| Join own network, status (IP, gateway, RSSI) | WiFi Lab, with confirmation | added | build |
| Scan export to SD | WiFi Lab, on request | added | build |
| Marauder / GhostESP / Blackhat companions | not shipped | withheld | — |
| Wardriver with third-party upload | not shipped | withheld | — |
| Deauth, beacon spam, evil portal, handshake capture, WPS attacks, MITM | removed | withheld | image |
| ESP flasher (flash an external ESP board) | no | add-on | — |

## 8. nRF24 and mesh

| Feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| nRF24 module detection | yes | present | build |
| 2.4 GHz activity spectrum (receive only) | yes | present | build |
| NRF24 sniffer / MouseJack (Momentum external apps; port built-in) | removed | withheld | image |
| nRF24 jammer (port feature) | removed | withheld | image |
| ESP-NOW buddy mesh (port feature, not Momentum) | start refused; `esp_now_init` not linked | withheld until rebuilt with pairing, authentication and replay protection | image |

## 9. GPIO and expansion

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| GPIO manual control, 5 V on GPIO | not built; the board exposes only I2C (Qwiic) and the UART pads | n/a (Flipper header does not exist) | — |
| I2C scanner | prebuilt `i2c_tools_cli.fap` only | todo as a built-in (Qwiic bus is real) | — |
| USB-UART bridge, I2C-SFP | no | n/a on a Plus / todo | — |
| Expansion modules service, Video Game Module options | no | n/a | — |
| GPIO pin remapping for external CC1101/NRF24/ESP/GPS | fixed by the board header | n/a | — |

## 10. System, UI and settings

| Momentum feature | In this firmware | Status | Verification |
| --- | --- | --- | --- |
| Desktop, dolphin animations, levels, passport | yes | present | build |
| Main menu styles: List, Wii, DSi, PS4, Vertical, C64, Compact, MNTM, CoverFlow | all nine | present | build |
| Main menu customisation (add/remove/reorder items) | add and remove; no reorder | present / todo | build |
| Asset packs | animations only (35 packs on SD); no icon or font overrides | present / todo | build |
| Lock screen: PIN, auto-lock, lock on boot, bad-PIN format, show time/date/statusbar | PIN set/change/remove, auto-lock time, USB-session disarm, lock-screen style | present (subset) / todo | build |
| Keybinds (press/hold × 4 directions → app or file) | favourite-app slots and dummy-mode keys; encoder has no 4-way pad | present (adapted) | build |
| Status bar: battery icon styles, clock, icons, borders | battery style (5), clock, background | present (subset) | build |
| File browser options (hidden files, internal tab, path display, sort) | no | todo | — |
| Dark mode, left-handed, text scroll, popup overlay | UI foreground/background colours, night shift | present (different) / todo | build |
| RGB backlight, rainbow | LCD backlight PWM; RGB **LED ring** colour/effect/speed (different hardware) | present (adapted) | build |
| Notification settings: volume, backlight time, LED brightness, stealth | yes; no vibro, no LCD contrast | present / n/a | build |
| Power settings: battery info, auto power-off, limit charge, reboot | yes, plus light-sleep options | present | build |
| Storage settings: SD info, format, unmount, benchmark, factory reset | yes | present | build |
| System settings: units, time/date format, log level, hand orientation, sleep method | app not built | todo | — |
| Clock & alarm settings | not built; a stopwatch "Clock" app exists | todo | — |
| Input settings (vibro) | no | n/a | — |
| Name and shell colour spoofing | "Spoofing Options" app | present | build |
| Dolphin level/XP editing | no | todo (cosmetic) | — |
| Archive: favourites, type tabs, browser, rename, delete, new folder | yes | present | build |
| Archive: search, disk-image mount, file info | no | todo | — |
| Storage: SD, internal, virtual disk images | SD; "internal" is a folder on the SD card | present / todo | build |
| CLI | core commands and `subghz`; storage/power commands likely not registered | present (subset) | build |
| RPC: system, storage, app, gui, property | yes; no desktop or gpio RPC | present (subset) | build |
| Updater: tgz/zip/web updater, DFU | different mechanism: A/B OTA slots from SD or USB-RPC, with integrity check | present (adapted) | build |
| Update rollback | confirm-after-boot rollback | added | build |
| Settings backup/restore | RPC tar backup of `/int` only; no on-device UI | todo | — |
| Crypto enclave / encrypted key storage | U2F storage only; generic "encryption" is pass-through | todo: do not rely on it for secrets | — |
| JavaScript runner | yes, 26 of Momentum's 32 modules | present | build |
| JS modules: serial, gpio, i2c, spi, vgm | not built (need HAL work or absent hardware) | todo / n/a | — |
| Region service | region presets in the HAL | added | host |

## 11. Momentum's external apps (from its docs)

| App (examples) | Here | Status |
| --- | --- | --- |
| Weather Station, POCSAG pager | weather decoders are native now; prebuilt FAPs also exist upstream | added / present (prebuilt) |
| Spectrum analyzer, Sub-GHz analyzer | prebuilt FAPs; RF Spectrum is built in | present |
| Sub-GHz Scheduler, playlist | Playlist built in | present / todo |
| ProtoPirate (vehicle key fobs) | not shipped | withheld |
| Sub-GHz Bruteforce | removed | withheld |
| MFKey, UL-C Bruteforce, UL-C Relay, ULCFkey, NFC Magic, MIFARE Fuzzer | not shipped | withheld |
| NFC Maker, NFC Login, ISO 15693-3 writer | not shipped | withheld (write/emulate) / n/a (ISO15693) |
| Metroflip, Passy and other read-only card readers | Passy prebuilt upstream | todo: rebuild from source and review |
| Picopass, Seader, Seos (iCLASS/SE readers) | no | add-on (extra reader hardware) |
| ESP32 Marauder, GhostESP, Flipper Blackhat, ESP Flasher | not shipped | withheld / add-on |
| Wardriver, NearbyFiles | not shipped | withheld |
| BLE Spam | removed | withheld |
| XRemote, IR tools | prebuilt FAPs upstream | present (prebuilt) |
| Games (Tetris, Snake, Checkers, …), TOTP authenticator | prebuilt FAPs upstream | present (prebuilt) |
| Unitemp, HC-SR04, INA2xx, Lidar, CAN tools | no | add-on (sensors on a GPIO header this board lacks) |
| FlipWeather, FlipSocial, FlipLibrary (need a Wi-Fi dev board on Flipper) | no | todo candidates using the built-in Wi-Fi, after review |

"Prebuilt" means a binary committed upstream. This fork has not rebuilt or
reviewed those; `make_sd_bundle.py` leaves them out unless asked.

## 12. Unleashed differences that matter here

| Unleashed has, Momentum lacks | Decision |
| --- | --- |
| 8 Sub-GHz protocols (listed in section 1) | 7 added (Doorbell32, KeyFinder2, Freezer, BelKorn, FAAC TM, Proteco, Telcoma EDGE); Prastel still todo. Rolling-code ones are decode-only |
| "Regional TX restrictions removed", extended range | opposite of this build's policy |
| LF-RFID and iButton fuzzers, Sub-GHz bruteforce, mfkey, Nice O-Code, Security+ PIN, seed capturer | withheld |
| Menu styles Grid, Macintosh, 3D | todo (cosmetic) |
| Clock main app | a stopwatch exists; clock/alarm is todo |
| Companion `network` and `gps` services | not applicable: this board has its own Wi-Fi and no GPS |
| NFC: Ultralight AES, MIFARE Plus keys, TexKom, Bambu parser | parsers todo; key tools withheld |
| IR | nothing extra (Momentum has three more universal libraries) |

## Counts

Rows in sections 1–12, counted by the first status word in each row (a row
such as "present / todo" counts once, under "present"):

| present | added | todo | add-on | n/a | withheld |
| --- | --- | --- | --- | --- | --- |
| 47 | 17 | 30 | 5 | 11 | 38 |

The "todo" column is the remaining work towards parity. The largest items:
the three extra IR universal remotes, a confirmed-per-action HID remote,
System/Clock settings, archive search and file-browser options, an on-device
backup, and a rebuilt mesh. Added in 2026-11: POCSAG, X10, AllStar Firefly,
Nord Ice, Hörmann BiSecur, Doorbell32, KeyFinder2, Freezer, FAAC TM, Proteco,
Telcoma EDGE and BelKorn (12 Sub-GHz protocols). Only Prastel remains, pending
its Unleashed `public_api.h` layer.
