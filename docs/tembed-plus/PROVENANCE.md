# Provenance of imported and removed code

One entry per import. Update this file in the same commit as any new import.

## Upstream snapshots reviewed (2026-11-14)

| Project | Role | Commit | Commit date | Nearest release |
| --- | --- | --- | --- | --- |
| [Sor3nt/Flipper-Zero-ESP32-Port](https://github.com/Sor3nt/Flipper-Zero-ESP32-Port) | base of this fork | `ef4d8f3b6ff6f1625176ae9f312572c3f0b34af4` | 2026-11-13 | `v2.0.0` (2026-09-06) + 272 commits |
| [Next-Flip/Momentum-Firmware](https://github.com/Next-Flip/Momentum-Firmware) | primary feature reference | `d3f89dfe2ef6b01839201598e9be1590cba80322` | 2026-08-18 | latest tag `mntm-012` |
| [DarkFlippers/unleashed-firmware](https://github.com/DarkFlippers/unleashed-firmware) | secondary protocol reference | `1a5676a0a6f66e47725bed8023b60fc9d29fc8d7` | 2026-11-13 | latest tag `unlshd-095` |
| [Xinyuan-LilyGO/T-Embed-CC1101](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101) | hardware reference | `c36826e0f91311cb591c4545fccf91e254fc55dc` | 2026-10-09 | hardware V1.0 (2024-07-29) |
| [espressif/esp-idf](https://github.com/espressif/esp-idf) | SDK | `4c2820d377d1375e787bcef612f0c32c1427d183` | 2025-03-27 | `v5.4.1` |

These are the states that were read. Re-check upstream before the next import
rather than assuming they are still current.

## Licences

- Momentum and Unleashed are GPL-3.0 (`LICENSE` in each repository), as is the
  Flipper firmware they derive from. Everything imported from them here stays
  under GPL-3.0 with its original file contents.
- **The Sor3nt port has no top-level `LICENSE` file** at the reviewed commit.
  Most of its tree is derived from GPL-3.0 Flipper firmware, so GPL-3.0 applies
  to those parts, but the author has not stated a licence for the original
  ESP32 code. Ask upstream to add one before redistributing binaries. Several
  directories under `applications_user/` carry their own licences; none of them
  were touched.
- Fixture captures under `tests/subghz_ws/fixtures/` are copied unmodified from
  Momentum's unit-test resources (GPL-3.0).

## Imports

### 1. Weather-station Sub-GHz decoders (Momentum → this fork)

| | |
| --- | --- |
| Source | Momentum `d3f89df`, `lib/subghz/protocols/` and `lib/subghz/blocks/generic_i.h` |
| Destination | same paths in this repository |
| Files | 53: `ws_generic.{c,h}`, `generic_i.h`, and `.c`/`.h` for 25 decoders (list below) |
| Modifications | 52 files byte-identical apart from line endings. One changed: see below. |
| Why from Momentum, not Unleashed | Unleashed does not carry these in `lib/subghz/protocols` |

Decoders: `acurite_592txr`, `acurite_5n1`, `acurite_606tx`, `acurite_609txc`,
`acurite_986`, `ambient_weather`, `auriol_ahfl`, `auriol_hg0601a`,
`bresser_3ch`, `emos_e601x`, `gt_wt_02`, `gt_wt_03`, `infactory`, `kedsum_th`,
`lacrosse_tx`, `lacrosse_tx141thbv2`, `nexus_th`, `oregon2`, `oregon3`,
`oregon_v1`, `solight_te44`, `thermopro_tx4`, `tx_8300`, `vauno_en8822c`,
`wendox_w6726`.

Local changes:

- `lib/subghz/protocols/solight_te44.c`: `battery_low = !(data >> 27) & 0x01`
  became `!((data >> 27) & 0x01)`. The original negates the whole shifted word
  instead of testing bit 27; a host compiler flags it
  (`-Wlogical-not-parentheses`). Worth sending upstream.
- `lib/subghz/protocols/lacrosse_tx.c`: an unknown message type is now
  rejected. Upstream calls `furi_crash()` there, on data that comes straight
  off the air behind a 4-bit checksum, so a stray or crafted frame could reboot
  the device.
- `lib/subghz/protocols/lacrosse_tx.c`, `emos_e601x.c`: loading a key file now
  checks the same bit count the decoder saves (44 and 120). Upstream checked a
  different constant and refused the files it had written itself.
- `lib/subghz/protocols/auriol_ahfl.c`: the 6-bit checksum is compared modulo
  64. Upstream compared the unmasked nibble sum and rejected every frame whose
  sum reached 64, including the example in its own header comment.
- `lib/subghz/blocks/generic.c`: added `subghz_block_generic_serialize_common()`
  (declared by Momentum's `generic_i.h`). Written to match this port's existing
  key-file header, so unlike Momentum's version it does not write `Lat`/`Lon`.
- `lib/subghz/protocols/protocol_items.{c,h}`: the 25 decoders are registered
  in Momentum's order. Momentum itself keeps these lines commented out in its
  main registry; they are active here.

Tests: `tests/subghz_ws/run.py` (host). Four decoders are checked against five
real upstream captures: Bresser 3CH (two frame formats), Solight TE44, Vauno
EN8822C and Acurite 592TXR. The other 21 are checked against synthetic
waveforms generated from each decoder's own source
(`tests/subghz_ws/make_synthetic_fixtures.py`). A synthetic fixture shows the
decoder is consistent with our reading of it and guards against regressions; it
does **not** show the decoder matches a real sensor. `tests/subghz_ws/README.md`
lists further suspected upstream decoder issues that were left unchanged
because fixing a bit layout without a real capture would be guesswork. Nothing
has been received over the air on a T-Embed.

Five of the 25 decoders (ThermoPRO-TX4, Nexus-TH, Auriol HG06061, Oregon2,
Oregon3) have no working integrity check, so they cannot tell a damaged frame
from a good one.

Two of the 25 (`acurite_592txr`, `nexus_th`) also include upstream's encoder, so
the app will offer to transmit a saved reading for those. That path is compiled
but untested.

### 2. Sub-GHz region presets (Flipper Devices documentation → this fork)

| | |
| --- | --- |
| Source | the per-region transmission band table at https://docs.flipper.net/zero/sub-ghz/frequencies, read 2026-11-14 |
| Destination | `components/furi_hal/furi_hal_region_table.{c,h}`, `furi_hal_region.c` |
| Nature | band edges (facts) transcribed into a new table; no upstream code copied. The `FuriHalRegion` struct and API names are the port's existing ones (from Flipper firmware, GPL-3.0) |
| Tests | `tests/subghz_region/run.py` (host) |

The page lists country groups; the presets here use one short label per group.
Only band edges are enforced. Upstream Flipper firmware provisions regions from
its update server and also carries per-band power and duty-cycle values, which
this table does not have.

### 3. BLE Lab (new code, with one borrowed idea)

`applications/main/ble_lab/` is new. Two things came from the port's removed
Bluetooth app (`applications/main/ble_spam`, Sor3nt, no stated licence): the
order of calls that borrows the Bluetooth stack from the system service and
returns it, and the list of public advertising fields used as device hints
(Apple company id 0x004C with types 0x12/0x07/0x10, Samsung 0x0075, Tile
service UUIDs 0xFEED/0xFD84). The parser and its tests were written from the
Bluetooth Core Specification Supplement's AD-structure format, not copied.

### 4. WiFi Lab

`applications/main/wifi_lab/` is new code that calls the port's existing
`wlan_hal` and `wifi` service APIs.

Its SD file server and SMB browser (added 2026-11-16) are new front-end scenes
over two of upstream's engines, compiled unmodified from where upstream keeps
them (`sources=` in `applications/main/wifi_lab/application.fam`):

| File | What it is | Origin |
| --- | --- | --- |
| `applications/main/wlan_app/wlan_webfs.{c,h}` | HTTP file manager for the SD card (hotspot or existing connection) | upstream port, unmodified |
| `applications/main/wlan_app/wlan_smb.{c,h}` | SMB2/3 client wrapper over `components/libsmb2` | upstream port, unmodified |

Upstream's own scenes for these features are not used: they depend on the
excluded WiFi app (its LAN host sweep feeds the SMB server list). The lab
front end adds the start confirmations, the random hotspot password and the
typed-in server address.

### 5. POCSAG and additional Sub-GHz protocols (Momentum → this fork)

| | |
| --- | --- |
| Source | Momentum `d3f89df`, `lib/subghz/protocols/`: `pocsag.{c,h}`, `pcsg_generic.{c,h}`, `x10.{c,h}`, `allstar_firefly.{c,h}`, `nord_ice.{c,h}` |
| Destination | `lib/subghz/protocols/` in this repository |
| Modifications | byte-identical apart from one line in `pocsag.c`: its `#include <furi/core/string.h>` became `<furi/core/furi_string.h>`, this port's header name. Registered in `protocol_items.{c,h}` |
| Why from Momentum | these are present in Momentum's tree but commented out of its registry; this fork registers them |

POCSAG is a receive-only pager decoder: its protocol definition carries no
`SubGhzProtocolFlag_Send` and a null encoder, so it decodes but never transmits.
X10 and Nord Ice are static-code remotes, transmittable only inside a chosen
region like the other static protocols. AllStar Firefly is rolling-code
(`SubGhzProtocolTypeDynamic`), so the lab transmitter gate in
`lib/subghz/transmitter.c` refuses to build an encoder for it: decode only.

Tests: `tests/subghz_pocsag/run.py` (host). `pocsag_encode.py` is an
independent POCSAG framer written from the ITU-R M.584 structure, not from the
decoder source; the C harness replays its edge streams through the real decoder
and checks the recovered address and message (1200 and 512 bps, numeric and
alphanumeric, single- and multi-batch). The decoder performs no BCH error
correction and the fixtures are synthetic, so this is self-consistency against
an independent encoder, not proof against a real pager; nothing was received
over the air. X10, AllStar Firefly and Nord Ice are build-verified only.

A second batch followed: `hormann_bisecur.{c,h}` from Momentum `d3f89df`, and
from Unleashed `1a5676a` the helper `common.{c,h}` plus `doorbell32`,
`keyfinder2`, `freezer`, `faac_tm`, `proteco`, `telcoma_edge` and `belkorn`
(all `lib/subghz/protocols/`, byte-identical, registered in
`protocol_items.{c,h}`). The Unleashed protocols use that project's
`SubGhzProtocolDecoderCommon`/`EncoderCommon` wrapper (`common.c`), which is
absent from this port's Momentum lineage; it grafted on unchanged because the
port's `SubGhzBlockDecoder`/`SubGhzBlockGeneric` layouts match (the
`_Static_assert` offset checks in `common.h` pass at compile time). Hörmann
BiSecur and BelKorn are rolling-code (Dynamic), so the transmitter gate makes
them decode-only; the rest are static. All are build-verified only. A real
Freezer capture exists in Unleashed's unit-test resources
(`applications/debug/unit_tests/resources/unit_tests/subghz/freezer*.sub`) for
a later host test. Prastel was left out: it needs Unleashed's `public_api.h`.

### 6. Infrared universal remotes (Momentum → this fork)

| | |
| --- | --- |
| Source | Momentum `d3f89df`: `applications/main/infrared/scenes/infrared_scene_universal_{bluray,monitor,digital_sign}.c`, the `.ir` libraries `resources/infrared/assets/{bluray_dvd,monitor,digital_sign}.ir`, and 25 button icons from `assets/icons/Infrared/*.png` |
| Destination | the same scene and asset paths here; the scenes are wired into `infrared_scene_config.h` and the Universal menu (`infrared_scene_universal.c`) |
| Icons | the 25 PNGs the new scenes need and the global set lacked were converted with this port's own `tools/fam/compile_icons.py` (Pillow) and appended to `components/assets/assets_icons.{c,h}` between `BEGIN/END tembed-plus IR universal-remote icons` markers. They use the uncompressed icon format (first byte `0x00`), which the renderer supports, so they are slightly larger than the heatshrink-compressed originals |
| Also ported | the `from_file` scene (load a custom `.ir` universal library). It needed three functions added to the port's IR engine (`lib/infrared/signal/infrared_brute_force.{c,h}`): `calculate_messages_ex` (auto-create records for unknown button names), `get_button_count` and `get_button_name`, ported from Momentum's engine (same `InfraredBruteForceRecordDict` structure). `calculate_messages` now delegates to `calculate_messages_ex(false, false)`, preserving the existing remotes' behaviour exactly |

### 7. Web flasher and CI

| | |
| --- | --- |
| New code | `web/` (page, styles, flasher logic, MD5), `tools/web/` (packaging, site builder, release fetcher, release notes), `tests/web/`, `.github/workflows/firmware.yml` and `boards.yml` |
| Model | Sor3nt's browser flasher (https://sor3nt.github.io/interface.html) for the flow: choose a build, connect, install. The code here was written for this fork |
| esptool-js | 0.7.0, Apache-2.0, from the npm registry. Not committed: `build_site.py` downloads the tarball when it builds the site and refuses it unless it matches the sha512 integrity pinned in `web/vendor.json`. The site serves `bundle.js` next to its `LICENSE` |
| MD5 | written from RFC 1321 (Web Crypto has no MD5, and the ROM loader checks writes with it). The 64 round constants were computed from the RFC's sine formula; `tests/web/test_md5.mjs` checks the code against the RFC's test vectors and Node's crypto |
| CI dependencies | only GitHub's own `actions/*` actions, by major-version tag, and the `espressif/idf:v5.4.1` container image for firmware builds |

To update esptool-js: change the version, tarball and integrity in
`web/vendor.json` (the integrity is the `dist.integrity` field of
`https://registry.npmjs.org/esptool-js/<version>`), read its changelog for API
changes, and test a flash on a device before releasing.

## Not imported (candidates)

Present upstream, absent here, not yet ported:

| Protocol(s) | In | Notes |
| --- | --- | --- |
| `hormann_bisecur` | Momentum | 868 MHz FSK rolling code: decode-only candidate |
| `doorbell32`, `keyfinder2`, `freezer` | Unleashed only | simple static protocols; upstream fixtures exist for `freezer` |
| `belkorn`, `faac_tm`, `prastel`, `proteco`, `telcoma_edge` | Unleashed only | gate/door remotes with encoders. Port as decode-only, or not at all, after deciding the transmit policy in `FEATURE_MATRIX.md` |

Infrared: the encoder/decoder set under `lib/infrared/encoder_decoder` is the
same list of protocols in all three projects (NEC, Kaseikyo, Pioneer, RC5, RC6,
RCA, Samsung, SIRC), so there is nothing to port at that level.

## Removed from the build

Not deleted from the tree (so upstream merges stay clean), but not compiled.
See `FEATURE_MATRIX.md` for the reasons and `fam_config.py` /
`CMakeLists.txt` for the mechanism.

| What | Upstream origin | How it is excluded |
| --- | --- | --- |
| In-place patch of the SDK's `libnet80211.a` | Sor3nt `CMakeLists.txt`, `tools/patch_wifi_lib.py` | block removed from `CMakeLists.txt`; `build.py` refuses a patched SDK |
| WPS attack engine and SDK `esp_wps.c` swap | Sor3nt `components/wifi/wifite/`, `cmake/wifite_sdk_adapter.cmake` | sources and include removed from the build; API exports guarded |
| WiFi attack app | Sor3nt `applications/main/wlan_app` | `LAB_EXCLUDED_APPS` |
| BLE spam app, BadUSB app, `js_blebeacon` | Sor3nt `applications/main/{ble_spam,bad_usb}`, `applications/system/js_app` | `LAB_EXCLUDED_APPS` |
| nRF24 jammer and MouseJack | Sor3nt `applications/main/nrf24` | `LAB_EXCLUDED_SOURCES` + `#ifndef FURI_PROFILE_LAB` |
| Sub-GHz jammer and brute-forcer | Sor3nt `applications/main/subghz` | `LAB_EXCLUDED_SOURCES` + `#ifndef FURI_PROFILE_LAB` |
| Raw 802.11 TX, beacon spam | Sor3nt `components/wifi/wlan_hal.c` | `#ifndef FURI_PROFILE_LAB` |
| ESP-NOW mesh start | Sor3nt `applications/services/desktop/helpers/mesh_service.c` | early return under `FURI_PROFILE_LAB`; menu entry hidden |
| NFC write, emulate, clone, unlock and key-recovery scenes (39) | Flipper NFC app as carried by Sor3nt `applications/main/nfc` | `ADD_SCENE_GATED` + `LAB_EXCLUDED_SOURCES`; replaced by `nfc_scene_lab_blocked.c` |
| PN532 card-emulation (target) mode | Sor3nt `components/furi_hal/furi_hal_nfc.c` | early return in `furi_hal_nfc_listener_wait_event()` |
| Rolling-code encoders | Flipper/Unleashed protocol encoders in `lib/subghz/protocols` | `subghz_transmitter_alloc_init()` returns NULL for `SubGhzProtocolTypeDynamic`; the encoder sources are still compiled because each shares a file with its decoder |
| Rolling-code "Add Manually" entries, counter-mode editing | Unleashed-derived `subghz_scene_set_type.c`, `subghz_scene_signal_settings.c` | filtered or hidden under `FURI_PROFILE_LAB` |
