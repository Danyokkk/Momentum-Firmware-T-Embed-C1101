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
- `lib/subghz/blocks/generic.c`: added `subghz_block_generic_serialize_common()`
  (declared by Momentum's `generic_i.h`). Written to match this port's existing
  key-file header, so unlike Momentum's version it does not write `Lat`/`Lon`.
- `lib/subghz/protocols/protocol_items.{c,h}`: the 25 decoders are registered
  in Momentum's order. Momentum itself keeps these lines commented out in its
  main registry; they are active here.

Tests: `tests/subghz_ws/run.py` (host). Real captures for Bresser 3CH (two
frame formats), Solight TE44, Vauno EN8822C and Acurite 592TXR decode to the
readings in upstream's own key files. The other 20 decoders are compiled, run
against a mixed real-traffic capture and two million synthetic pulses without a
false decode or crash, but have **no known-good capture**, so their decoding is
unverified. Nothing has been received over the air on a T-Embed.

Two of the 25 (`acurite_592txr`, `nexus_th`) also include upstream's encoder, so
the app will offer to transmit a saved reading for those. That path is compiled
but untested.

## Not imported (candidates)

Present upstream, absent here, not yet ported:

| Protocol(s) | In | Notes |
| --- | --- | --- |
| `pocsag`, `pcsg_generic` | Momentum | receive-only pager decoder; needs the FM preset path checked on the CC1101 |
| `x10` | Momentum | 310 MHz home automation |
| `allstar_firefly`, `nord_ice` | Momentum and Unleashed | static-code remotes |
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
