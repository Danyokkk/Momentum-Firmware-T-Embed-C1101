# Weather-station decoder tests (host only)

    python tests/subghz_ws/run.py        # Windows: set ZIG_CC to zig.exe first

Compiles the 25 unmodified decoders in `lib/subghz/protocols/` against the stubs
in `stubs/` and replays `.sub` files through `feed()`, pulse by pulse. No radio.

## Real and synthetic fixtures

| Fixtures | Decoders | What a pass means |
| --- | --- | --- |
| `fixtures/*.sub`, captures of real sensors (Momentum's unit-test resources) | Bresser-3CH (both formats), Solight TE44, Vauno-EN8822C, Acurite 592TXR | The decoder understands a real transmitter. |
| `fixtures/synthetic/*_synth*.sub`, written by `make_synthetic_fixtures.py` | the other 21 (list: `make_synthetic_fixtures.py --list`) | See "Limitation". |
| `fixtures/test_random_raw.sub`, real mixed traffic | all 25 at once | No false decodes from foreign signals. |

No real capture of the 21 other sensors was available. Their fixtures are
committed; `run.py` never regenerates them, it only checks
(`make_synthetic_fixtures.py --check`) that they still equal the generator's output.

## How the synthetic waveforms were derived

Each case in `make_synthetic_fixtures.py` describes one reading (id, channel,
temperature, humidity, battery) and turns it into fields -> bits -> checksum ->
pulses. Everything comes from reading that decoder's source:

- pulse lengths: `te_short` / `te_long` of its `SubGhzBlockConst`, at nominal value;
- framing (preamble, sync, how a 0 and a 1 look, what ends a frame, how often a
  frame must repeat before the decoder reports it): its `feed()` state machine;
- bit layout and checksum: its `remote_controller()` and check function, with the
  header comment as a second opinion.

Two exceptions, both noted in the case's `source` text: the Acurite-986 decoder
only fixes a threshold (gap under/over 800 us), so the 520/880 us gaps are the
rtl_433 figures; and the end mark of the Acurite 5n1 frame is copied from the real
Acurite 592TXR capture, which uses the same framing.

Independent evidence that the generator reads the sources correctly:
`selftest()` checks its checksum code against the real example frames quoted in
the GT-WT02, GT-WT03, inFactory and Acurite-986 headers, and the real Acurite
592TXR capture decodes to exactly the key file the generator builds from that
reading (`acurite_592txr_key.sub`).

Per case the test checks: the decoder alone reports at least one frame and its
Id/Bit/Data/Batt/Hum/Ch/Btn/Temp equal what was encoded (Temp within 0.05); the
key file saved from that decode loads again unchanged; the same reading comes
out with every pulse off by up to 10 % (less where the decoder's window is
narrower, `--list` says why); with all 25 decoders running no other decoder
reports the frame, except the pairs in `synth_allowed_overlaps`; and, where the
decoder has a checksum, a frame with one flipped payload bit is rejected.
Expected values are the encoded reading, never a copy of decoder output.

## Limitation

A synthetic fixture proves that a decoder is self-consistent with **our reading of
its source** and guards against regressions. It does not prove that the decoder
matches a real sensor: if the upstream author misread the protocol, the fixture
repeats the mistake. Timing is ideal (plus uniform jitter), with none of the
pulse stretching, noise or truncated repeats of a real receiver. Only a capture
of the actual sensor, or reception on the device, closes that gap.

## Fields the suite deliberately does not assert

Where code and header comment disagree the expected file leaves the field out
(with a comment) instead of blessing either side:

- Auriol AHFL `Batt`: comment says bit = 1 means battery OK, code reports the bit as "low" (`auriol_ahfl.c:139`).
- Auriol HG06061 `Id`, `Batt`: `>> 31` and `>> 30` on a 37-bit frame whose other fields are taken as "36 bits + 1 pad"; id loses its two low bits and the battery flag is an id bit (`auriol_hg0601a.c:130-131`).
- EMOS E601x `Batt`: read from bit 10, which is inside the checksum byte (`emos_e601x.c:141`).
- Oregon-v1 `Batt`: read from the un-reversed word, i.e. the low bit of the tenths digit (`oregon_v1.c:155`).
- LaCrosse_TX humidity message `Temp`: not part of the message; the decoder leaves the old value.

## Findings

### Fixed in this fork

Each has a regression test in this suite. See `docs/tembed-plus/PROVENANCE.md`.

- **LaCrosse_TX crash on received data.** A frame with a valid 4-bit sum and a
  type nibble other than 0 or E reached `furi_crash()` in
  `ws_protocol_lacrosse_tx_remote_controller()`. It is now rejected
  (`lacrosse_tx_unknown_type_raw.sub`).
- **LaCrosse_TX and EMOS E601x could not load the key files they save.** They
  store `Bit: 44` / `Bit: 120` but `deserialize` checked
  `min_count_bit_for_found` (40 / 24). The check now uses the saved size.
- **Auriol AHFL rejected every frame whose nibble sum is 64 or more**,
  including the example in its own header (sum 75, sent as 11): the 6-bit
  checksum was compared with an unmasked sum. It is now compared modulo 64
  (`auriol_ahfl_header_example`, `auriol_ahfl_high_sum`).
- **Solight TE44 battery flag** (found earlier by the compiler):
  `!(data >> 27) & 1` negated the whole shifted word.

### Reported upstream issues left unchanged

Changing a bit layout or a framing rule without a real capture would be
guesswork, so these are recorded, not fixed. The `probe` lines of the test
output replay the first two.

- Oregon3 needs 24 decoded preamble ones but always loses the first bit after a gap, so the documented 24-one preamble never syncs; the fixture sends 25 (`oregon3.c:177`).
- Oregon-v1 frames whose first bit is 1 are not decoded when sent as plain Manchester; that code path only accepts an inverted stream and then stores `Data` with `FF FF FF FF` on top (`oregon_v1.c:215-219`, `:248`). The fixture covers the first-bit-0 path only.
- Acurite 592TXR encoder stops after the last data bit; its decoder needs a pulse and a gap of 1000 us or more after bit 56, so the encoder's output alone decodes 0 times. The round-trip test adds the end mark a real sensor sends (`acurite_592txr.c:315-359` vs `:221`).
- `Repeat` is read with a count of 12 (Nexus-TH) and 6 (Acurite 592TXR) into a single `size_t`: a key file with several numbers after `Repeat:` writes past the field (`nexus_th.c:184`, `acurite_592txr.c:379`). By inspection only. These are the two decoders with an encoder; it matters only when such a file is loaded for transmit.
- ThermoPRO-TX4, Nexus-TH and Auriol HG06061 drop every second back-to-back repeat (state after a decode: `thermopro_tx4.c:180`, `nexus_th.c:280`, `auriol_hg0601a.c:181`). Harmless, the sensors repeat.
- TX141THBv2 recognises the frame end only if the next preamble pulse is under 864 us, 32 us above nominal (`lacrosse_tx141thbv2.c:221`, `te_short * 3` where the preamble test uses `* 4`).
- No checksum, CRC or parity at all: ThermoPRO-TX4, Nexus-TH, Auriol HG06061, Oregon2, Oregon3 (both Oregon decoders receive the checksum and ignore it). The single decode from upstream's mixed capture `test_random_raw.sub` is Oregon2, and with no integrity check it cannot be told whether that is a real Oregon frame or a false positive.
- Overlap: GT-WT02 and ThermoPRO-TX4 are the same waveform; a GT-WT02 sensor whose id starts 1001 or 0110 is also reported as a ThermoPRO-TX4 with nonsense values (`gt_wt_02_id9`, the one allow-listed pair).
- Field bits that look wrong and are therefore not asserted: see "unchecked" above.

## Tools

    python tests/subghz_ws/make_synthetic_fixtures.py            # rewrite fixtures/synthetic
    build_host/subghz_ws/test_ws_decode --replay file.sub ...    # what all 25 decoders make of a file
