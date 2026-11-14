#!/usr/bin/env python3
"""Synthetic Sub-GHz fixtures for the weather-station decoders that have no real capture.

    python tests/subghz_ws/make_synthetic_fixtures.py           # (re)write the fixtures
    python tests/subghz_ws/make_synthetic_fixtures.py --check   # verify the committed files
    python tests/subghz_ws/make_synthetic_fixtures.py --list    # table of what is generated

Every waveform is built from a small description of ONE reading (id, channel,
temperature, ...): fields -> bits -> checksum -> pulses. Timing, framing and the
checksum come from reading the matching decoder in lib/subghz/protocols/ (its
SubGhzBlockConst, feed() state machine, check function and header comment); the
"source:" text of each case says which lines of reasoning were used.

What a synthetic fixture is worth: it proves the decoder is self-consistent with
our reading of its source, and it pins today's behaviour against regressions. It
does NOT prove that the decoder understands a real sensor. See README.md.

Output, per case <slug> (all under fixtures/synthetic/):
    <slug>_synth_raw.sub          nominal timing
    <slug>_synth_jitter_raw.sub   every edge moved by up to +-jitter (fixed seed)
    <slug>_synth_corrupt_raw.sub  one payload bit flipped, checksum left alone
                                  (only when the decoder has an integrity check)
    <slug>_synth.sub              the reading that was encoded, as a key file;
                                  a field the test must not assert is left out
                                  and explained in a comment line
The output is deterministic: no clock, no unseeded randomness.
"""
from __future__ import annotations

import argparse
import random
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

HERE = Path(__file__).resolve().parent
OUT = HERE / "fixtures" / "synthetic"

HIGH, LOW = True, False
LEAD_IN = 20000  # us of silence before the first frame
NONE = 255  # WS_NO_BATT / WS_NO_HUMIDITY / WS_NO_CHANNEL / WS_NO_BTN
TS = 1700000000  # constant "Ts" so the key files are stable (never compared)

Segment = tuple[bool, int]


# ------------------------------------------------------------------ bit helpers

def bits_of(value: int, count: int) -> list[int]:
    """MSB first, the order every decoder here shifts bits in."""
    return [(value >> i) & 1 for i in range(count - 1, -1, -1)]


def value_of(bits: list[int]) -> int:
    value = 0
    for bit in bits:
        value = value << 1 | bit
    return value


def twos(value: int, width: int) -> int:
    return value & ((1 << width) - 1)


def rev8(value: int) -> int:
    return value_of(bits_of(value & 0xFF, 8)[::-1])


def nibbles_lsb_first(nibbles: list[int]) -> list[int]:
    """Oregon Scientific sends each nibble least-significant bit first."""
    return [(n >> i) & 1 for n in nibbles for i in range(4)]


def even_parity_msb(value7: int) -> int:
    """Acurite: bit 7 makes the byte's population count even."""
    return value7 | (bin(value7).count("1") & 1) << 7


def f_to_c(temp_f: float) -> float:
    return (temp_f - 32.0) / 1.8


# ---------------------------------------- checksums, ported from lib/subghz/blocks/math.c

def crc4(message: list[int], polynomial: int, init: int) -> int:
    remainder = (init << 4) & 0xFF
    poly = (polynomial << 4) & 0xFF  # uint8_t in C: 0x13 << 4 keeps only 0x30
    for byte in message:
        remainder ^= byte
        for _ in range(8):
            remainder = ((remainder << 1) ^ poly) & 0xFF if remainder & 0x80 else (remainder << 1) & 0xFF
    return remainder >> 4 & 0x0F


def crc8(message: list[int], polynomial: int, init: int) -> int:
    remainder = init
    for byte in message:
        remainder ^= byte
        for _ in range(8):
            remainder = ((remainder << 1) ^ polynomial) & 0xFF if remainder & 0x80 else (remainder << 1) & 0xFF
    return remainder


def lfsr_digest8(message: list[int], gen: int, key: int) -> int:
    total = 0
    for byte in message:
        for i in range(7, -1, -1):
            if (byte >> i) & 1:
                total ^= key
            key = (key >> 1) ^ gen if key & 1 else key >> 1
    return total


def lfsr_digest8_reflect(message: list[int], gen: int, key: int) -> int:
    total = 0
    for byte in reversed(message):
        for i in range(8):
            if (byte >> i) & 1:
                total ^= key
            key = ((key << 1) ^ gen) & 0xFF if key & 0x80 else (key << 1) & 0xFF
    return total


def byte_at(value: int, shift: int) -> int:
    return (value >> shift) & 0xFF


# ------------------------------------------------------------- waveform helpers

def distance(bits: list[int], pulse: int, gap0: int, gap1: int) -> list[Segment]:
    """Pulse-distance coding: constant pulse, the following gap carries the bit."""
    out: list[Segment] = []
    for bit in bits:
        out += [(HIGH, pulse), (LOW, gap1 if bit else gap0)]
    return out


def width(bits: list[int], zero: tuple[int, int], one: tuple[int, int]) -> list[Segment]:
    """Pulse-width coding: (high, low) microseconds for a 0 and for a 1."""
    out: list[Segment] = []
    for bit in bits:
        high, low = one if bit else zero
        out += [(HIGH, high), (LOW, low)]
    return out


def pairs(count: int, high: int, low: int) -> list[Segment]:
    return [(HIGH, high), (LOW, low)] * count


def manchester(bits: list[int], half: int, one: tuple[bool, bool]) -> list[Segment]:
    """Two half-bits per bit; `one` gives the levels of the halves of a 1."""
    out: list[Segment] = []
    for bit in bits:
        first, second = one if bit else (one[1], one[0])
        out += [(first, half), (second, half)]
    return out


def merge(segments: list[Segment]) -> list[Segment]:
    """Join neighbours of the same level into the pulses a receiver would see."""
    out: list[Segment] = []
    for level, duration in segments:
        if duration <= 0:
            continue
        if out and out[-1][0] == level:
            out[-1] = (level, out[-1][1] + duration)
        else:
            out.append((level, duration))
    return out


# ------------------------------------------------------------------------ cases

@dataclass
class Case:
    slug: str
    protocol: str  # decoder name, as in the key file
    modulation: str
    integrity: str | None  # None: the decoder has no checksum/CRC/parity at all
    source: str  # how the waveform was derived from the decoder source
    bit_count: int  # "Bit" the decoder reports
    data: int  # "Data" the decoder reports
    fields: dict  # Id/Batt/Hum/Ch/Btn/Temp that were encoded
    payload: list[int]  # the bits handed to `waveform`
    waveform: Callable[[list[int]], list[Segment]]
    flip: int | None = None  # payload index flipped for the corrupt fixture
    jitter: float = 0.10
    jitter_note: str = ""
    unchecked: dict = field(default_factory=dict)  # field -> why the test must not assert it


def infactory() -> Case:
    ident, batt_low, hum, channel = 0x5C, 1, 67, 2
    temp_f = 23.0  # -5.0 C
    raw = round(temp_f * 10) + 900
    d = ident << 32 | batt_low << 26 | raw << 12 | (hum // 10) << 8 | (hum % 10) << 4 | channel
    msg = [byte_at(d, 32), (d >> 24 & 0x0F) | (d & 0x0F) << 4, byte_at(d, 16), byte_at(d, 8), byte_at(d, 0)]
    d |= (crc4(msg[:4], 0x13, 0) ^ (msg[4] >> 4)) << 28

    def waveform(bits):
        frame = (pairs(4, 1000, 1000) + [(HIGH, 500), (LOW, 8000)]
                 + distance(bits, 500, 2000, 4000) + [(HIGH, 500), (LOW, 16000)])
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "infactory", "inFactory-TH", "OOK pulse-distance (PPM)", "CRC-4",
        "4 x (1000 high, 1000 low) preamble, 500/8000 sync, 40 bits as 500 high + 2000 (0) or "
        "4000 (1) low, 500 high + 16000 low end mark; layout iiiiiiii cccc ub?? t*12 h*8(BCD) ??nn",
        40, d, dict(Id=ident, Batt=batt_low, Hum=hum, Ch=channel, Btn=NONE, Temp=f_to_c(temp_f)),
        bits_of(d, 40), waveform, flip=40 - 1 - 14)


def thermopro_tx4() -> Case:
    ident, batt_low, btn, channel, hum = 0xB3, 1, 0, 3, 45
    temp_c = -12.3
    d = (0b1001 << 33 | ident << 25 | batt_low << 24 | btn << 23 | (channel - 1) << 21
         | twos(round(temp_c * 10), 12) << 9 | hum << 1)

    def waveform(bits):
        sync = [(HIGH, 500), (LOW, 9000)]
        return [(LOW, LEAD_IN)] + sync + (distance(bits, 500, 2000, 4000) + sync) * 4

    return Case(
        "thermopro_tx4", "ThermoPRO-TX4", "OOK pulse-distance (PPM)", None,
        "9000 low sync before and after, 37 bits as 500 high + 2000 (0) or 4000 (1) low; "
        "layout type(1001) id*8 batt btn ch*2 temp*12(signed) hum*8 pad; "
        "the only acceptance test is the type nibble",
        37, d, dict(Id=ident, Batt=batt_low, Hum=hum, Ch=channel, Btn=btn, Temp=temp_c),
        bits_of(d, 37), waveform)


def nexus_th() -> Case:
    ident, batt_ok, channel, hum = 0x7E, 0, 2, 58
    temp_c = -7.5
    d = ident << 28 | batt_ok << 27 | (channel - 1) << 24 | twos(round(temp_c * 10), 12) << 12 | 0xF << 8 | hum
    rubicson = [byte_at(d, 28), byte_at(d, 20), byte_at(d, 12), 0xF0, d & 0xFF]
    assert crc8(rubicson, 0x31, 0x6C) != 0, "payload would be rejected as a Rubicson frame"

    def waveform(bits):
        sync = [(HIGH, 490), (LOW, 3920)]
        return [(LOW, LEAD_IN)] + sync + (distance(bits, 490, 980, 1960) + sync) * 4

    return Case(
        "nexus_th", "Nexus-TH", "OOK pulse-distance (PPM)", None,
        "3920 low sync before and after, 36 bits as 490 high + 980 (0) or 1960 (1) low (same as "
        "the upstream encoder in nexus_th.c); layout id*8 battOK 0 ch*2 temp*12(signed) 1111 hum*8; "
        "accepted on the constant nibble and range checks only",
        36, d, dict(Id=ident, Batt=1 - batt_ok, Hum=hum, Ch=channel, Btn=NONE, Temp=temp_c),
        bits_of(d, 36), waveform)


def gt_wt_02_sum(d: int) -> int:
    total = (d >> 5) & 0xE
    for i in range(7):
        total += (d >> 9 >> (i * 4)) & 0xF
    return total & 0x3F


def gt_wt_02(slug: str, ident: int, batt_low: int, temp_c: float) -> Case:
    btn, channel, hum = 0, 2, 55
    d = ident << 29 | batt_low << 28 | btn << 27 | (channel - 1) << 25 | twos(round(temp_c * 10), 12) << 13 | hum << 6
    d |= gt_wt_02_sum(d)

    def waveform(bits):
        sync = [(HIGH, 500), (LOW, 9000)]
        return [(LOW, LEAD_IN)] + sync + (distance(bits, 500, 2000, 4000) + sync) * 3

    return Case(
        slug, "GT-WT02", "OOK pulse-distance (PPM)", "nibble sum modulo 64",
        "9000 low sync before and after, 37 bits as 500 high + 2000 (0) or 4000 (1) low; "
        "layout id*8 batt btn ch*2 temp*12(signed) hum*7 sum*6",
        37, d, dict(Id=ident, Batt=batt_low, Hum=hum, Ch=channel, Btn=btn, Temp=temp_c),
        bits_of(d, 37), waveform, flip=37 - 1 - 15)


def gt_wt_03_sum(d: int) -> int:
    total = 0
    for shift in (33, 25, 17, 9):
        key = 0x3100
        for i in range(7, -1, -1):
            if (byte_at(d, shift) >> i) & 1:
                total ^= key & 0xFF
            key >>= 1
    return total ^ 0x2D


def gt_wt_03() -> Case:
    ident, hum, batt_low, btn, channel = 0x5A, 48, 1, 1, 3
    temp_c = -4.4
    d = (ident << 33 | hum << 25 | batt_low << 24 | btn << 23 | (channel - 1) << 21
         | twos(round(temp_c * 10), 12) << 9)
    d |= gt_wt_03_sum(d) << 1

    def waveform(bits):
        frame = pairs(4, 855, 855) + width(bits, (285, 570), (570, 285))
        return [(LOW, LEAD_IN)] + frame * 3 + pairs(2, 855, 855) + [(LOW, LEAD_IN)]

    return Case(
        "gt_wt_03", "GT-WT03", "OOK pulse-width (PWM)", "XOR hash with a shifting key",
        "4 x (855 high, 855 low) preamble, 41 bits as 285 high + 570 low (0) or 570 high + 285 low "
        "(1); a frame is emitted when the next preamble starts, so frames are back to back; "
        "layout id*8 hum*8 batt btn ch*2 temp*12(signed) chk*8 pad",
        41, d, dict(Id=ident, Batt=batt_low, Hum=hum, Ch=channel, Btn=btn, Temp=temp_c),
        bits_of(d, 41), waveform, flip=41 - 1 - 27)


def acurite_606tx() -> Case:
    ident, batt_low = 0xC7, 1
    temp_c = -10.5
    d = ident << 24 | batt_low << 23 | twos(round(temp_c * 10), 12) << 8
    d |= lfsr_digest8([byte_at(d, 24), byte_at(d, 16), byte_at(d, 8)], 0x98, 0xF1)

    def waveform(bits):
        sync = [(HIGH, 500), (LOW, 8500)]
        return [(LOW, LEAD_IN)] + sync + (distance(bits, 500, 2000, 4000) + sync) * 3

    return Case(
        "acurite_606tx", "Acurite-606TX", "OOK pulse-distance (PPM)", "LFSR digest-8",
        "8500 low sync before and after, 32 bits as 500 high + 2000 (0) or 4000 (1) low; "
        "layout id*8 batt ??? temp*12(signed) digest*8",
        32, d, dict(Id=ident, Batt=batt_low, Hum=NONE, Ch=NONE, Btn=NONE, Temp=temp_c),
        bits_of(d, 32), waveform, flip=32 - 1 - 10)


def acurite_609txc() -> Case:
    ident, batt_low, hum = 0x8D, 1, 64
    temp_c = -8.7
    d = ident << 32 | batt_low << 31 | twos(round(temp_c * 10), 12) << 16 | hum << 8
    d |= (byte_at(d, 32) + byte_at(d, 24) + byte_at(d, 16) + byte_at(d, 8)) & 0xFF

    def waveform(bits):
        sync = [(HIGH, 500), (LOW, 8500)]
        return [(LOW, LEAD_IN)] + sync + (distance(bits, 500, 1000, 2000) + sync) * 3

    return Case(
        "acurite_609txc", "Acurite-609TXC", "OOK pulse-distance (PPM)", "byte sum",
        "8500 low sync before and after, 40 bits as 500 high + 1000 (0) or 2000 (1) low; "
        "layout id*8 batt ??? temp*12(signed) hum*8 sum*8",
        40, d, dict(Id=ident, Batt=batt_low, Hum=hum, Ch=NONE, Btn=NONE, Temp=temp_c),
        bits_of(d, 40), waveform, flip=40 - 1 - 9)


def acurite_986() -> Case:
    ident, sensor, batt_low = 0x3C5A, 1, 1
    temp_f = -13  # sign-and-magnitude byte; -25.0 C
    temp_byte = 0x80 | -temp_f if temp_f < 0 else temp_f
    # Bytes travel least-significant bit first; the decoder shifts them in as they arrive.
    d = rev8(temp_byte) << 32 | rev8(ident >> 8) << 24 | rev8(ident) << 16 | sensor << 15 | batt_low << 14
    d |= crc8([byte_at(d, 32), byte_at(d, 24), byte_at(d, 16), byte_at(d, 8)], 0x07, 0x00)

    def waveform(bits):
        frame = pairs(4, 1750, 1750) + width(bits, (880, 520), (520, 880)) + [(HIGH, 520), (LOW, 4000)]
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "acurite_986", "Acurite-986", "OOK pulse-width (decoder reads the gap)", "CRC-8 poly 0x07",
        "4 x (1750 high, 1750 low) sync, 40 bits, then one pulse followed by a gap the decoder "
        "cannot read as a bit. The decoder ignores the pulse width and takes a low under 800 us "
        "as 0, 800 us or more as 1 (300..1300 accepted); 520/880 us are the rtl_433 figures for "
        "this sensor, one each side of that threshold. Layout (each byte LSB first) "
        "temp*8(sign+magnitude, F) id*16 sensor batt ?*6 crc*8",
        40, d, dict(Id=ident, Batt=batt_low, Hum=NONE, Ch=sensor + 1, Btn=NONE, Temp=f_to_c(temp_f)),
        bits_of(d, 40), waveform, flip=3, jitter=0.05,
        jitter_note="a 1 is a gap of 800 us or more; 880 nominal leaves 9 %")


LACROSSE_TX_ID = 0x4B  # 7 bits
LACROSSE_TX_TYPES = (0x0, 0xE)  # temperature, humidity: the only types the decoder knows


def lacrosse_tx_bits(msg_type: int, value: float) -> list[int]:
    """The 44 bits of one message, with its (valid) 4-bit nibble sum."""
    ident = LACROSSE_TX_ID
    v1, v2, v3 = int(value) // 10, int(value) % 10, round(value * 10) % 10
    parity = (bin(v1).count("1") + bin(v2).count("1") + bin(v3).count("1")) & 1
    nibbles = [0x0, 0xA, msg_type, ident >> 3, (ident & 7) << 1 | parity, v1, v2, v3, v1, v2]
    nibbles.append(sum(nibbles) & 0xF)
    return [b for n in nibbles for b in bits_of(n, 4)]


def lacrosse_tx_frame(air: list[int]) -> list[Segment]:
    frame: list[Segment] = []
    for bit in air:
        frame += [(HIGH, 550 if bit else 1300), (LOW, 1000)]
    frame[-1] = (LOW, 30000)  # the last gap is the pause between repeats
    return frame


def lacrosse_tx(slug: str, msg_type: int, value: float) -> Case:
    ident = LACROSSE_TX_ID
    bits = lacrosse_tx_bits(msg_type, value)
    d = value_of(bits[4:])  # decoding starts at the first 1 of the 1010 nibble

    def waveform(air):
        return [(LOW, LEAD_IN)] + lacrosse_tx_frame(air) * 3

    if msg_type == 0x0:
        fields = dict(Id=ident, Batt=NONE, Hum=NONE, Ch=NONE, Btn=NONE, Temp=value - 50.0)
        unchecked = {}
    else:
        fields = dict(Id=ident, Batt=NONE, Hum=int(value), Ch=NONE, Btn=NONE)
        unchecked = {"Temp": "a humidity message does not carry a temperature and the decoder leaves the field untouched"}
    return Case(
        slug, "LaCrosse_TX", "OOK pulse-width (PWM)", "4-bit nibble sum",
        "44 bits, 1300 high (0) or 550 high (1) followed by a 1000 low; the four leading 0 bits "
        "are the preamble the decoder counts, decoding starts at the 1010 nibble, a low over "
        "3000 ends the frame; nibbles 0 A type addr addr+parity v1 v2 v3 v1 v2 sum",
        44, d, fields, bits, waveform, flip=4 * 6 + 1, jitter=0.08,
        jitter_note="the 1300 us pulse is accepted within +-120 us (9.2 %)", unchecked=unchecked)


def lacrosse_tx141thbv2() -> Case:
    ident, batt_low, btn, channel, hum = 0xD2, 1, 0, 3, 72
    temp_c = -10.5
    d = (ident << 32 | batt_low << 31 | btn << 30 | (channel - 1) << 28
         | (round(temp_c * 10) + 500) << 16 | hum << 8)
    d |= lfsr_digest8_reflect([byte_at(d, 32), byte_at(d, 24), byte_at(d, 16), byte_at(d, 8)], 0x31, 0xF4)

    def waveform(bits):
        frame = pairs(4, 832, 832) + width(bits, (208, 417), (417, 208))
        return [(LOW, LEAD_IN)] + frame * 3 + pairs(2, 832, 832) + [(LOW, LEAD_IN)]

    return Case(
        "lacrosse_tx141thbv2", "TX141THBv2", "OOK pulse-width (PWM)", "LFSR digest-8 (reflected)",
        "4 x (832 high, 832 low) preamble, 40 bits as 208 high + 417 low (0) or 417 high + 208 low "
        "(1); a frame is emitted when the next preamble starts, so frames are back to back; "
        "layout id*8 batt test ch*2 temp*12(+500) hum*8 digest*8",
        40, d, dict(Id=ident, Batt=batt_low, Hum=hum, Ch=channel, Btn=btn, Temp=temp_c),
        bits_of(d, 40), waveform, flip=40 - 1 - 10, jitter=0.03,
        jitter_note="the frame end is only recognised when the next preamble pulse is under 864 us "
                    "(832 nominal, +3.8 %)")


def ambient_weather() -> Case:
    ident, batt_low, channel, hum = 0x9B, 1, 3, 45
    temp_f = 14.0  # -10.0 C
    packet = (0x45 << 40 | ident << 32 | batt_low << 31 | (channel - 1) << 28
              | (round(temp_f * 10) + 400) << 16 | hum << 8)
    packet |= lfsr_digest8([byte_at(packet, s) for s in (40, 32, 24, 16, 8)], 0x98, 0x3E) ^ 0x64

    def waveform(bits):
        # 0x001 is the tail of the preamble the decoder insists on; the first frame gets a few
        # more zeros, as a real preamble has. Decoder bit 1 = high then low.
        air = [0] * 8 + (bits_of(0x001, 12) + bits) * 3
        return [(LOW, LEAD_IN)] + manchester(air, 500, (HIGH, LOW)) + [(LOW, LEAD_IN)]

    return Case(
        "ambient_weather", "Ambient_Weather", "OOK Manchester", "LFSR digest-8",
        "Manchester, 500 us half-bit, 1 = high then low; preamble ...0001 then 48 bits starting "
        "0x45, three repeats without a gap; layout 0x45 id*8 batt ch*3 temp*12(F*10+400) hum*8 "
        "digest*8. The decoder reports its whole 64-bit shift register, so Data starts 00 01",
        48, 0x0001 << 48 | packet,
        dict(Id=ident, Batt=batt_low, Hum=hum, Ch=channel, Btn=NONE, Temp=f_to_c(temp_f)),
        bits_of(packet, 48), waveform, flip=48 - 1 - 10)


def auriol_ahfl_frame(ident, batt_bit, btn, channel_bits, temp_c, hum) -> int:
    d = (ident << 34 | batt_bit << 33 | btn << 32 | channel_bits << 30
         | twos(round(temp_c * 10), 12) << 18 | hum << 11 | 0b0100 << 6)
    return d | sum((d >> 6 >> (i * 4)) & 0xF for i in range(9)) & 0x3F


def auriol_ahfl_waveform(bits):
    sync = [(HIGH, 500), (LOW, 9000)]
    return [(LOW, LEAD_IN)] + sync + (distance(bits, 500, 2000, 4000) + sync) * 3


AHFL_HEADER_EXAMPLE = "10111100-1-0-00-000011101000-01101100-0100-001011"  # auriol_ahfl.c, header comment


def auriol_ahfl(slug: str, ident: int, batt_bit: int, btn: int, channel: int, temp_c: float, hum: int,
                literal: str | None = None) -> Case:
    d = auriol_ahfl_frame(ident, batt_bit, btn, channel - 1, temp_c, hum)
    total = sum((d >> 6 >> (i * 4)) & 0xF for i in range(9))
    if literal:
        # The bits are taken from the comment as printed; the field values above are our reading
        # of the layout it documents. They must describe the same frame.
        assert bits_of(d, 42) == [int(c) for c in literal.replace("-", "")], slug
    return Case(
        slug, "Auriol AHFL", "OOK pulse-distance (PPM)", "nibble sum modulo 64",
        "9000 low sync before and after, 42 bits as 500 high + 2000 (0) or 4000 (1) low (the "
        "code, not the 1000/2000/4000 of its header comment); layout id*8 battOK btn ch*2 "
        f"temp*12(signed) hum*7 0 0100 sum*6. Nibble sum {total}, sent as {total % 64}"
        + (" (upstream rejected every sum of 64 or more)" if total >= 64 else "")
        + ("; this is the example frame of the decoder's header comment" if literal else ""),
        42, d, dict(Id=ident, Hum=hum, Ch=channel, Btn=btn, Temp=temp_c),
        bits_of(d, 42), auriol_ahfl_waveform, flip=42 - 1 - 20, jitter=0.01,
        jitter_note="the leading sync gap is accepted within +-150 us of 9000 (1.7 %)",
        unchecked={"Batt": "header comment says the bit is 1 = battery OK, the code reports it as battery low; "
                           f"encoded bit = {batt_bit}"})


def auriol_hg06061() -> Case:
    ident, batt_ok, channel, hum = 0xA6, 1, 2, 57
    temp_c = -9.2
    d = (ident << 29 | batt_ok << 28 | (channel - 1) << 25 | twos(round(temp_c * 10), 12) << 13
         | 0xF << 9 | hum << 1)

    def waveform(bits):
        sync = [(HIGH, 500), (LOW, 4000)]
        return [(LOW, LEAD_IN)] + sync + (distance(bits, 500, 1000, 2000) + sync) * 4

    return Case(
        "auriol_hg06061", "Auriol HG06061", "OOK pulse-distance (PPM)", None,
        "4000 low sync before and after, 37 bits as 500 high + 1000 (0) or 2000 (1) low; layout "
        "from the header comment plus one trailing bit, which is what the code's channel, "
        "temperature, constant and humidity shifts assume: id*8 battOK 0 ch*2 temp*12(signed) "
        "1111 hum*8 pad; accepted on the constant bits only",
        37, d, dict(Hum=hum, Ch=channel, Btn=NONE, Temp=temp_c),
        bits_of(d, 37), waveform, jitter=0.03,
        jitter_note="sync gap accepted within +-150 us of 4000 (3.75 %)",
        unchecked={"Id": "the code takes bits 38..31 of a 37-bit frame, i.e. the top six id bits; encoded id = 0xA6",
                   "Batt": "the code reads bit 30, which is an id bit in this layout; encoded battery-OK bit = 1"})


def emos_e601x() -> Case:
    ident, channel, hum, wind, direction, flags = 0x5E, 2, 61, 0x12, 0x3, 0b0100
    temp_c = -6.5
    year, month, day, hour, minute, second = 26, 11, 14, 10, 20, 30
    stamp = year << 26 | month << 22 | day << 17 | hour << 12 | minute << 6 | second
    body = (stamp << 14 | channel << 12 | twos(round(temp_c * 10), 12)).to_bytes(6, "big")
    frame = [0xAA, 0xA5, 0x83, ident, *body, hum, wind, direction << 4 | flags]
    frame.append(sum(frame) & 0xFF)
    bits = [b for byte in frame for b in bits_of(byte, 8)]  # 112 bits; the repeat counter follows

    def waveform(payload):
        out = [(LOW, LEAD_IN)]
        for repeat in range(3):
            out += [(HIGH, 1820), (LOW, 260)] + width(payload + bits_of(repeat, 8), (260, 800), (800, 260))
        return out + [(HIGH, 1820), (LOW, LEAD_IN)]

    return Case(
        "emos_e601x", "EMOS E601x", "OOK pulse-width (PWM)", "byte sum",
        "1820 high + 260 low start mark, 120 bits as 260 high + 800 low (0) or 800 high + 260 low "
        "(1), frames back to back with a rising repeat counter; bytes AA A5 83 id, 2+32 bits of "
        "date/time, ch*2 temp*12(signed), hum, wind, dir/flags, sum, repeat. Data is bytes 7..12",
        120, int.from_bytes(bytes(frame[7:13]), "big"),
        dict(Id=ident, Hum=hum, Ch=channel, Btn=NONE, Temp=temp_c),
        bits, waveform, flip=8 * 10 + 5,
        unchecked={"Batt": "the code reads bit 2 of the checksum byte; the header comment puts the battery flag in "
                           "the flags nibble (encoded flags = 0100, battery good)"})


def kedsum_th() -> Case:
    ident, batt_bits, channel, hum, flags = 0x6A, 0b00, 3, 52, 0b1000
    temp_f = 14.0  # -10.0 C
    raw = round(temp_f * 10) + 900
    d = (ident << 32 | batt_bits << 30 | (channel - 1) << 28 | (raw & 0xF) << 24 | (raw >> 4 & 0xF) << 20
         | (raw >> 8 & 0xF) << 16 | (hum & 0xF) << 12 | (hum >> 4) << 8 | flags << 4)
    d |= crc4([byte_at(d, 32), byte_at(d, 24), byte_at(d, 16), byte_at(d, 8)], 0x03, 0) ^ flags

    def waveform(bits):
        mark = [(HIGH, 500), (LOW, 8000)]
        return [(LOW, LEAD_IN)] + (mark * 2 + distance(bits, 500, 2000, 4000) + mark) * 3

    return Case(
        "kedsum_th", "Kedsum-TH", "OOK pulse-distance (PPM)", "CRC-4",
        "500 high + 8000 low marks before and after, 42 bits as 500 high + 2000 (0) or 4000 (1) "
        "low; layout 00 id*8 batt*2 ch*2 temp(3 nibbles, low first, F*10+900) hum(2 nibbles, low "
        "first) flags crc*4",
        42, d, dict(Id=ident, Batt=1, Hum=hum, Ch=channel, Btn=NONE, Temp=f_to_c(temp_f)),
        bits_of(d, 42), waveform, flip=42 - 1 - 13, jitter=0.06,
        jitter_note="the 8000 us mark is accepted within +-600 us (7.5 %)")


def tx_8300() -> Case:
    hum, batt_bits, channel, negative, ident = 65, 0b01, 2, 1, 0x55
    digits = (1, 2, 3)  # 12.3, sign carried separately
    p1 = (hum // 10 << 28 | hum % 10 << 24 | batt_bits << 22 | channel << 20 | negative << 19
          | ident << 12 | digits[0] << 8 | digits[1] << 4 | digits[2])
    assert p1 >> 31 == 0, "the decoder treats the first payload bit as part of the sync"
    x = sum(p1 >> i & 0xF for i in range(0, 32, 4))
    y = sum(p1 >> i & 0x5 for i in range(0, 32, 4))
    check = (~x & 0xF) << 4 | (~y & 0xF)
    bits = bits_of(p1, 31) + bits_of(~p1 & 0xFFFFFFFF, 32) + bits_of(check, 8)

    def waveform(payload):
        frame = (distance([0, 0], 1940, 1940, 3880) + [(HIGH, 3880), (LOW, 3880)]
                 + distance(payload, 1940, 1940, 3880) + [(HIGH, 1940), (LOW, 12000)])
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "tx_8300", "TX8300", "OOK pulse-distance (PPM)", "inverted copy + Fletcher-like byte",
        "two 1940/1940 preamble bits, a 3880 high + 3880 low mark the decoder counts as bit 1 of "
        "72, then 31 + 32 + 8 bits as 1940 high + 1940 (0) or 3880 (1) low, ended by a low of "
        "9700 or more; payload hum(BCD) batt*2 ch*2 sign id*7 temp(3 BCD digits), its inverse, "
        "check byte",
        72, p1, dict(Id=ident, Batt=1, Hum=hum, Ch=channel, Btn=NONE, Temp=-12.3),
        bits, waveform, flip=31 - 1 - 5, jitter=0.05,
        jitter_note="the 3880 us start pulse is accepted within +-250 us (6.4 %)")


def oregon_v1() -> Case:
    ident_byte = 0b01 << 6 | 0b0110  # channel 2, rolling id 6
    batt_low, negative = 1, 1
    tens, ones, tenths = 1, 2, 4
    b0, b1, b2 = ident_byte, ones << 4 | tenths, batt_low << 7 | negative << 5 | tens
    total = b0 + b1 + b2
    word = ((total & 0xFF) + (total >> 8)) << 24 | b2 << 16 | b1 << 8 | b0
    bits = bits_of(word, 32)[::-1]  # the word goes out least-significant bit first
    assert bits[0] == 0, "only the first-bit-0 path of the decoder is covered, see README.md"

    def waveform(air):
        frame = (pairs(11, 1465, 1465) + [(HIGH, 1465), (LOW, 4395)] + [(HIGH, 5860), (LOW, 5200)]
                 + manchester(air, 1465, (HIGH, LOW)) + [(LOW, 30000)])
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "oregon_v1", "Oregon-v1", "OOK Manchester", "byte sum with end-around carry",
        "12 preamble pulses of 1465/1465 the last followed by a 4395 gap, 5860 sync pulse, 5200 "
        "gap, then 32 Manchester bits, 1465 us half-bit, 1 = high then low. The first bit is 0, so "
        "its low half lengthens the sync gap to 6665 and the decoder takes its first-bit-0 path. "
        "Word (sent LSB first): sum, batt ? sign ? tens, ones tenths, ch*2 ?? id*4",
        32, value_of(bits), dict(Id=ident_byte, Hum=NONE, Ch=2, Btn=NONE, Temp=-12.4),
        bits, waveform, flip=10, jitter=0.05,
        jitter_note="the 5860 us sync pulse is accepted within +-350 us (6 %)",
        unchecked={"Batt": "the code reads bit 23 of the un-reversed word, which is the low bit of the tenths "
                           "digit, not the battery flag of its header comment (encoded flag = 1)"})


def wendox_w6726() -> Case:
    ident, batt_low = 0b10110, 1
    temp_c = -12.3
    raw = 0x1FF - (round(-temp_c * 10) + 11)  # negative branch: (~raw & 0x1FF) + 1 - 12 tenths
    d = ident << 24 | 0 << 23 | raw << 14 | batt_low << 6
    d |= crc4([byte_at(d, 28), byte_at(d, 20), byte_at(d, 12), byte_at(d, 4)], 0x9, 0xD)

    def waveform(bits):
        frame = pairs(7, 1955, 5865) + width(bits, (1955, 5865), (5865, 1955)) + [(LOW, 12 * 1955)]
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "wendox_w6726", "Wendox W6726", "OOK pulse-width (PWM)", "CRC-4",
        "7 preamble pulses of 1955 high + 5865 low, 29 bits as 1955 high + 5865 low (0) or 5865 "
        "high + 1955 low (1), the first of which must be 1, then 12 x 1955 of guard time; layout "
        "id*5 positive temp*9 ?*7 batt ?? crc*4",
        29, d, dict(Id=ident, Batt=batt_low, Hum=NONE, Ch=NONE, Btn=NONE, Temp=temp_c),
        bits_of(d, 29), waveform, flip=29 - 1 - 16)


def acurite_txr_bytes(channel_raw: int, ident: int, body: list[int]) -> list[int]:
    data = [channel_raw << 6 | ident >> 8, ident & 0xFF] + [even_parity_msb(b) for b in body]
    return data + [sum(data) & 0xFF]


def acurite_5n1() -> Case:
    ident, batt_ok, wind, hum = 0x0C3A, 0, 0x25, 62
    temp_f = 14.0  # -10.0 C
    raw = round(temp_f * 10) + 400
    data = acurite_txr_bytes(0b10, ident, [batt_ok << 6 | 0x38, wind >> 3, (wind & 7) << 4 | raw >> 7,
                                           raw & 0x7F, hum])
    d = int.from_bytes(bytes(data), "big")

    def waveform(bits):
        frame = pairs(4, 600, 600) + width(bits, (200, 400), (400, 200)) + [(HIGH, 200), (LOW, 2200)]
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "acurite_5n1", "Acurite 5n1", "OOK pulse-width (PWM)", "byte sum + parity",
        "4 x (600 high, 600 low) sync, 64 bits as 200 high + 400 low (0) or 400 high + 200 low (1), "
        "then a short pulse and a gap of 1000 or more, which is what ends a frame; the 200/2200 "
        "tail is the one seen after each frame in the real Acurite 592TXR capture (same framing); "
        "bytes CCiiiiii iiiiiiii pB111000 p??wwwww pwwwtttt pttttttt phhhhhhh sum",
        64, d, dict(Id=ident, Batt=1 - batt_ok, Hum=hum, Ch=2, Btn=NONE, Temp=f_to_c(temp_f)),
        bits_of(d, 64), waveform, flip=64 - 1 - 10)


def oregon_checksum(nibbles: list[int]) -> list[int]:
    total = sum(nibbles) & 0xFF
    return [total & 0xF, total >> 4]  # low nibble first; neither decoder verifies it


def oregon2() -> Case:
    fixed = [0x1, 0xD, 0x2, 0x0, 0x2, 0xA, 0x7, 0x4]  # THGR122N, channel 2, rolling A7, low battery
    var = [3, 2, 1, 8, 7, 4]  # -12.3 C (tenths, ones, tens, sign), 47 % (ones, tens)
    nibbles = fixed + var
    bits = nibbles_lsb_first(nibbles + oregon_checksum(nibbles))

    def waveform(payload):
        air = [1] * 16 + [0, 1, 0, 1] + payload
        doubled = [x for bit in air for x in (1 - bit, bit)]  # v2.1 sends every bit inverted, then true
        frame = manchester(doubled, 500, (HIGH, LOW)) + [(LOW, 10000)]
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "oregon2", "Oregon2", "OOK Manchester, every bit sent twice (inverted, true)", None,
        "v2.1: 16 ones, sync 0101, then nibbles LSB first, 500 us half-bit; a 1 is low-high-high-"
        "low on air. Sensor 1D20 (THGR122N): id*16 ch*4 rolling*8 flags*4, temp tenths/ones/"
        "tens/sign, hum ones/tens, checksum*8. The checksum is sent correctly but the decoder "
        "never looks at it",
        32, value_of([b for n in fixed for b in bits_of(n, 4)]),
        dict(Id=0x1D20, Batt=1, Hum=47, Ch=2, Btn=NONE, Temp=-12.3),
        bits, waveform)


def oregon3() -> Case:
    fixed = [0xF, 0x8, 0x2, 0x4, 0x3, 0x5, 0xC, 0x4]  # THGR221, channel 3, rolling 5C, low battery
    var = [7, 5, 0, 8, 3, 6, 0]  # -5.7 C (tenths, ones, tens, sign), 63 % (ones, tens), 0
    nibbles = fixed + var
    bits = nibbles_lsb_first(nibbles + oregon_checksum(nibbles))

    def waveform(payload, ones=25):
        air = [1] * ones + [0, 1, 0, 1] + payload
        frame = manchester(air, 500, (HIGH, LOW)) + [(LOW, 10000)]
        return [(LOW, LEAD_IN)] + frame * 3

    return Case(
        "oregon3", "Oregon3", "OOK Manchester", None,
        "v3.0: preamble of ones, sync 0101, then nibbles LSB first, 500 us half-bit, 1 = high "
        "then low. 25 ones are sent: the decoder wants 24 and always loses the first bit after a "
        "gap. Sensor F824 (THGR221): id*16 ch*4 rolling*8 flags*4, temp tenths/ones/tens/sign, "
        "hum ones/tens, 0, checksum*8. The checksum is sent correctly but the decoder never looks "
        "at it",
        32, value_of([b for n in fixed for b in bits_of(n, 4)]),
        dict(Id=0xF824, Batt=1, Hum=63, Ch=3, Btn=NONE, Temp=-5.7),
        bits, waveform)


CASES: list[Callable[[], Case]] = [
    infactory, thermopro_tx4, nexus_th,
    lambda: gt_wt_02("gt_wt_02", 0x34, 1, -12.1),
    # Same sensor with an id whose top nibble is 1001: ThermoPRO-TX4 has the same waveform and
    # accepts anything that starts 1001 or 0110, so it decodes this frame as well (README.md).
    lambda: gt_wt_02("gt_wt_02_id9", 0x9A, 0, 21.5),
    gt_wt_03, acurite_606tx, acurite_609txc, acurite_986,
    lambda: lacrosse_tx("lacrosse_tx", 0x0, 37.7),  # -12.3 C, sent as value + 50
    lambda: lacrosse_tx("lacrosse_tx_hum", 0xE, 46.0),
    lacrosse_tx141thbv2, ambient_weather,
    lambda: auriol_ahfl("auriol_ahfl", 0x21, 0, 0, 2, -8.4, 40),  # nibble sum 50
    lambda: auriol_ahfl("auriol_ahfl_high_sum", 0xEB, 1, 1, 2, -15.5, 77),  # nibble sum 87
    lambda: auriol_ahfl("auriol_ahfl_header_example", 0b10111100, 1, 0, 1, 23.2, 0b0110110,
                        literal=AHFL_HEADER_EXAMPLE),  # nibble sum 75
    auriol_hg06061, emos_e601x, kedsum_th, tx_8300,
    oregon_v1, wendox_w6726, acurite_5n1, oregon2, oregon3,
]


def lacrosse_tx_unknown_type() -> tuple[str, str]:
    """Well-formed LaCrosse_TX frames (valid sum) of every type the decoder does not know.

    Upstream called furi_crash() on such a frame, i.e. a transmitter or a lucky burst of noise
    could halt the device; this fork's decoder ignores the frame. The test expects no decode
    and, above all, no abort. Same builder as lacrosse_tx_synth_raw.sub, which does decode."""
    segments: list[Segment] = [(LOW, LEAD_IN)]
    types = [t for t in range(16) if t not in LACROSSE_TX_TYPES]
    for msg_type in types:
        segments += lacrosse_tx_frame(lacrosse_tx_bits(msg_type, 37.7)) * 2
    text = raw_text(segments, "LaCrosse_TX frames with a valid checksum and the message types "
                    + " ".join(f"{t:X}" for t in types) + " (none of them temperature 0 or humidity E), "
                    "two of each: the decoder must report nothing.")
    return "lacrosse_tx_unknown_type_raw.sub", text


# ------------------------------------------------- key file for the encoder round trip

def acurite_592txr_key() -> tuple[str, str]:
    """The reading of the real capture fixtures/acurite_592txr.sub, rebuilt from its fields.

    test_ws_decode.c first checks that the real capture decodes to exactly this key file
    (which validates this generator's reading of the Acurite layout against a real sensor)
    and then uses it to drive the upstream encoder."""
    ident, batt_ok, hum, temp_c = 6315, 1, 83, 5.2
    raw = round(temp_c * 10) + 1000
    data = acurite_txr_bytes(0b11, ident, [batt_ok << 6 | 0x04, hum, raw >> 7, raw & 0x7F])
    fields = dict(Id=ident, Batt=1 - batt_ok, Hum=hum, Ch=1, Btn=NONE, Temp=temp_c)
    text = key_text("Acurite 592TXR", 56, int.from_bytes(bytes(data), "big"), fields, {},
                    "Key file rebuilt by make_synthetic_fixtures.py from the reading in the real capture\n"
                    "# ../acurite_592txr.sub (id 6315, channel A, 5.2 C, 83 %, battery OK).")
    return "acurite_592txr_key.sub", text


# ----------------------------------------- waveforms that are deliberately NOT in the suite

def probes() -> list[tuple[str, str]]:
    """Waveforms we believe are legitimate but the (unchanged, upstream) decoder does not accept.

    They are replayed for information only (no check counts on them); each is
    described in README.md under "Findings"."""
    out = []

    # Oregon v3.0 with the documented 24-bit preamble instead of the 25 ones the suite uses.
    case = oregon3()
    out.append(("probe_oregon3_24_ones_raw.sub",
                raw_text(case.waveform(case.payload, 24),
                         "Oregon3: same frame as oregon3_synth_raw.sub with a 24-one preamble")))

    # Oregon v1 whose first bit is 1: plain Manchester after an un-lengthened 5200 us sync gap.
    case = oregon_v1()
    bits = list(case.payload)
    word = value_of(bits[::-1]) & 0x00FFFFFF | 0x01  # rolling id 7 instead of 6
    total = (word & 0xFF) + (word >> 8 & 0xFF) + (word >> 16 & 0xFF)
    word |= ((total & 0xFF) + (total >> 8)) << 24
    out.append(("probe_oregon_v1_first_bit_1_raw.sub",
                raw_text(case.waveform(bits_of(word, 32)[::-1]),
                         "Oregon-v1: channel 2, id 7, -12.4 C; first transmitted bit is 1")))
    return out


# ---------------------------------------------------------------------- output

def jittered(pulses: list[Segment], amount: float, seed: int) -> list[Segment]:
    rng = random.Random(seed)
    return [(level, max(1, round(duration * (1.0 + rng.uniform(-amount, amount))))) for level, duration in pulses]


def raw_text(segments: list[Segment], what: str) -> str:
    pulses = merge(segments)
    lines = [
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        "Frequency: 433920000",
        "Preset: FuriHalSubGhzPresetOok650Async",
        "Protocol: RAW",
        "# SYNTHETIC waveform written by tests/subghz_ws/make_synthetic_fixtures.py.",
        "# It is not a capture of a real sensor. " + what,
    ]
    values = [str(duration if level else -duration) for level, duration in pulses]
    for start in range(0, len(values), 512):
        lines.append("RAW_Data: " + " ".join(values[start:start + 512]))
    return "\n".join(lines) + "\n"


def key_text(protocol: str, bit_count: int, data: int, fields: dict, unchecked: dict, what: str) -> str:
    lines = [
        "Filetype: Flipper SubGhz Key File",
        "Version: 1",
        "# " + what,
        "Frequency: 433920000",
        "Preset: FuriHalSubGhzPresetOok650Async",
        "Protocol: " + protocol,
    ]
    values = {
        "Id": lambda: str(fields["Id"]),
        "Bit": lambda: str(bit_count),
        "Data": lambda: " ".join(f"{b:02X}" for b in data.to_bytes(8, "big")),
        "Batt": lambda: str(fields["Batt"]),
        "Hum": lambda: str(fields["Hum"]),
        "Ts": lambda: str(TS),
        "Ch": lambda: str(fields["Ch"]),
        "Btn": lambda: str(fields["Btn"]),
        "Temp": lambda: f"{fields['Temp']:.6f}",
    }
    for key, render in values.items():
        if key in unchecked:
            lines.append(f"# {key} is not asserted: {unchecked[key]}")
        else:
            lines.append(f"{key}: {render()}")
    return "\n".join(lines) + "\n"


def build() -> dict[str, str]:
    """Relative file name -> content, for everything under fixtures/synthetic/."""
    files: dict[str, str] = {}
    slugs = set()
    for make in CASES:
        case = make()
        assert case.slug not in slugs
        slugs.add(case.slug)
        assert set(case.fields) | set(case.unchecked) == {"Id", "Batt", "Hum", "Ch", "Btn", "Temp"}, case.slug
        assert not set(case.fields) & set(case.unchecked), case.slug
        assert (case.flip is None) == (case.integrity is None), case.slug
        reading = ", ".join(f"{k}={v:.1f}" if k == "Temp" else f"{k}={v}" for k, v in case.fields.items())
        nominal = merge(case.waveform(case.payload))
        files[f"{case.slug}_synth_raw.sub"] = raw_text(
            nominal, f"Decoder under test: {case.protocol}. Encoded reading: {reading}.")
        files[f"{case.slug}_synth_jitter_raw.sub"] = raw_text(
            jittered(nominal, case.jitter, zlib.crc32(case.slug.encode())),
            f"As {case.slug}_synth_raw.sub with every pulse stretched or shrunk by up to "
            f"{case.jitter * 100:g} % (fixed seed).")
        if case.flip is not None:
            damaged = list(case.payload)
            damaged[case.flip] ^= 1
            files[f"{case.slug}_synth_corrupt_raw.sub"] = raw_text(
                case.waveform(damaged),
                f"As {case.slug}_synth_raw.sub with payload bit {case.flip} flipped and the "
                f"{case.integrity} left as it was: {case.protocol} must reject it.")
        files[f"{case.slug}_synth.sub"] = key_text(
            case.protocol, case.bit_count, case.data, case.fields, case.unchecked,
            f"The reading encoded into {case.slug}_synth_raw.sub (written by make_synthetic_fixtures.py).")
    for name, text in (acurite_592txr_key(), lacrosse_tx_unknown_type()):
        files[name] = text
    for name, text in probes():
        files[name] = text
    return files


def selftest() -> None:
    """Checksum ports against the real example frames quoted in the decoder headers."""
    # gt_wt_02.c: "34 00 ed 47 60" and "34 8f 87 15 90", 37 bits each
    for example in (0x3400ED4760, 0x348F871590):
        d = example >> 3
        assert gt_wt_02_sum(d & ~0x3F) == d & 0x3F, hex(example)
    # gt_wt_03.c lists its examples inverted (rtl_433 polarity): "01 c8 d0 2b 76 80" etc., 41 bits
    for example in (0x17CFBEFA6A80, 0x17CFFEFAEA80, 0x01CF6F11B280, 0x01C8D02B7680):
        d = (~example & 0xFFFFFFFFFFFF) >> 7
        assert gt_wt_03_sum(d & ~0x1FE) == d >> 1 & 0xFF, hex(example)
    # infactory.c: 0000 1111 | 0011 0000 | 0101 1100 | 1110 0111 | 0110 0001
    d = 0x0F305CE761
    msg = [byte_at(d, 32), (d >> 24 & 0x0F) | (d & 0x0F) << 4, byte_at(d, 16), byte_at(d, 8), byte_at(d, 0)]
    assert crc4(msg[:4], 0x13, 0) ^ (msg[4] >> 4) == d >> 28 & 0xF
    # acurite_986.c: 0110 0100 | 1010 1011 | 0110 0010 | 0000 0000 | 0111 0110
    assert crc8([0x64, 0xAB, 0x62, 0x00], 0x07, 0x00) == 0x76


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="compare with the committed files, write nothing")
    parser.add_argument("--list", action="store_true", help="print the cases, write nothing")
    args = parser.parse_args()

    selftest()
    if args.list:
        for make in CASES:
            case = make()
            print(f"{case.slug:22} {case.protocol:16} {case.modulation:44} "
                  f"integrity: {case.integrity or 'NONE'}; jitter +-{case.jitter * 100:g} %"
                  + (f" ({case.jitter_note})" if case.jitter_note else ""))
            for key, why in case.unchecked.items():
                print(f"{'':22} {key} not asserted: {why}")
        return 0

    files = build()
    if args.check:
        stale = []
        for name, text in files.items():
            path = OUT / name
            if not path.exists() or path.read_bytes().replace(b"\r\n", b"\n") != text.encode():
                stale.append(name)
        extra = sorted(p.name for p in OUT.glob("*.sub") if p.name not in files)
        for name in stale:
            print(f"  stale or missing: fixtures/synthetic/{name}")
        for name in extra:
            print(f"  not produced by the generator: fixtures/synthetic/{name}")
        if stale or extra:
            print("FAIL: fixtures/synthetic is out of date; run make_synthetic_fixtures.py and commit the result")
            return 1
        print(f"synthetic fixtures: {len(files)} files match make_synthetic_fixtures.py")
        return 0

    OUT.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (OUT / name).write_bytes(text.encode())
    for path in OUT.glob("*.sub"):
        if path.name not in files:
            path.unlink()
    print(f"wrote {len(files)} files to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
