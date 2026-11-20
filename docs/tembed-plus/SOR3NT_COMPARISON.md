# This firmware vs. the Sor3nt ESP32 port

How the LilyGO T-Embed CC1101 Plus firmware in this repository compares to the
reference it is based on, [Sor3nt/Flipper-Zero-ESP32-Port](https://github.com/Sor3nt/Flipper-Zero-ESP32-Port).

Snapshot date **2026-11-20**. Counts are taken from each tree directly
(`git`/GitHub API), not from marketing copy. "Strict superset" means every item
the other project has is present here and there are more besides.

This fork is built on the full **Momentum** firmware as the feature reference,
with **Unleashed** as the secondary protocol source; Sor3nt's port is a reduced
reimplementation. So on decoder and parser breadth the gap is structural, not
incremental.

## Headline

| Area | Sor3nt port | This firmware | Verdict |
| --- | --- | --- | --- |
| **Sub-GHz protocol decoders** | 68 protocol source files | **109** files — the complete Momentum set (94) + Unleashed's extras + `prastel` | **strict superset of Momentum *and* Unleashed** |
| **NFC supported-card parsers** | 37 | **40** | **full Momentum parity**, ahead of Sor3nt |
| **IR universal remotes** | 6 libraries (`ac, audio, fans, leds, projectors, tv`) | **9** (adds `bluray_dvd`, `digital_sign`, `monitor`) | **+3 categories** |
| **NRF24** | app present | app present (scanner, spectrum, Wi-Fi-channel view) | match on passive analysis; offensive jam/MouseJack deliberately not expanded — see below |
| Weather-station decoders | — (not registered) | 25 registered + host-tested | added here |
| UI languages | English only | **EN / ES / RU** with first-boot picker + Settings toggle | added here |
| Region-aware TX safety | raw TX by default | region presets, default "Not set" = no transmit; rolling-code TX refused at the gate | added here |
| Web flasher + CI | static flasher page | per-board CI build, web flasher, OTA + rollback | added here |

## Sub-GHz (what changed today)

The decoder set was already the full Momentum union. The one protocol present
in Unleashed but not here was **Prastel** (42-bit rolling code). It is now
ported (`lib/subghz/protocols/prastel.{c,h}`), registered, and compiled into the
image (build verified; host test `tests/subghz_prastel`, 9,531 encode→decode
round-trips). Being a rolling code it is **decode-only** via the transmitter
gate in `lib/subghz/transmitter.c`.

With Prastel in, there is **no Sub-GHz protocol decoder in either Momentum
(`d3f89df`) or Unleashed (`dev`) that is missing here.** See
[PROVENANCE.md](PROVENANCE.md) §5/§5a and [MOMENTUM_PARITY.md](MOMENTUM_PARITY.md).

## NFC (what changed today)

Three read-only MIFARE Classic transit parsers from Momentum — `sevppk_tk`,
`sk_tk`, `szppk_so` (suburban-rail cards) — were ported into
`applications/main/nfc/plugins/supported_cards/` and registered in
`application.fam`, taking supported-card parity with Momentum to **40/40**. Like
every supported-cards plugin they are read-only: they interpret a card that was
already read — no keys, no write, no emulation. They build on the same native
`.fal` plugin path (`tools/build_nfc_parsers.sh`) as the existing 37; all 22
NFC/`bit_lib`/`datetime` APIs they call resolve in this tree.

NFC in this fork stays **read-only by design**: key-recovery, cloning,
emulation and write scenes are not part of the shipped behaviour (see
[FEATURE_MATRIX.md](FEATURE_MATRIX.md) → NFC).

## NRF24 — why "more" here does not mean "more jamming"

Both trees ship the `nrf24` app. This firmware keeps and leads on the **passive
and analysis** side — channel scanner, spectrum view, and the 2.4 GHz
Wi-Fi-channel view — which is what authorised RF survey and research work needs.

The port's offensive NRF paths (continuous-carrier **jamming** and **MouseJack**
HID injection against third-party devices) were **not expanded**. Jamming is a
denial-of-service technique and MouseJack is covert HID injection; adding more of
either does not make the tool more capable for legitimate, authorised testing,
and it is out of scope for a passive-by-default build. "Beating the reference"
here means more decoders, more parsers, more remotes and better analysis — not a
bigger disruption surface. If a specific NRF capability is needed for a named,
authorised engagement, raise it as its own request with that context.

## Not a gap, a choice

A few things Sor3nt ships are intentionally passive-by-default here rather than
absent: raw 802.11 management-frame TX (deauth) is behind an off-by-default
`TEMBED_LAB_RF` build flag with a runtime arm guard, and the default image is
the passive one (`TEMBED_LAB_RF=OFF`, confirmed in the build log). Rolling-code
transmit is refused at the Sub-GHz gate. These are documented in
[FEATURE_MATRIX.md](FEATURE_MATRIX.md) and [PROVENANCE.md](PROVENANCE.md).

## How to re-verify the counts

```bash
# Sub-GHz decoder files
ls lib/subghz/protocols/*.c | wc -l                     # 109
# NFC supported-card parsers
ls applications/main/nfc/plugins/supported_cards/*.c | grep -v nfc_supported_card_plugin | wc -l   # 40
# IR universal-remote libraries
ls applications/main/infrared/resources/infrared/assets/*.ir | wc -l   # 9
```

against the same paths in the Sor3nt repository.
