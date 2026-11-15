#!/usr/bin/env python3
"""Independent POCSAG framer: build a (level, duration) edge stream for a known
pager message, so the decoder can be cross-checked against it.

This encodes the POCSAG/ITU-R M.584 frame structure (preamble, frame sync word,
batches of 16 codewords, address and alphanumeric/numeric message codewords)
from the standard. It deliberately does NOT reuse the firmware decoder's source,
so a decode that recovers the address and text is a genuine cross-check of the
framing, not a tautology. It does not compute BCH(31,21) check bits: the decoder
does not verify them, and the fixture is labelled synthetic for that reason.

    python pocsag_encode.py --out fixtures/        # (re)write the committed fixtures
    python pocsag_encode.py --check fixtures/       # verify they still match

Each case writes <name>.edges (one "<level> <duration>" per line, level 0/1) and
<name>.expect (RIC, FUNC, MSG lines) for the C harness.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

TE_SHORT = {1200: 833, 512: 1950, 2400: 410}  # microseconds per bit, by baud

FRAME_SYNC_CODE = 0x7CD215D8
IDLE_CODE_WORD = 0x7A89C197
FUNC_NUM, FUNC_ALERT1, FUNC_ALERT2, FUNC_ALPHANUM = 0, 1, 2, 3
BCD_CHARS = "*U -)("  # values 10..15 for numeric messages


def address_codeword(ric: int, func: int) -> int:
    """bit31=0, bits30..13 = ric>>3, bits12..11 = func, check bits left 0."""
    return ((ric >> 3) & 0x3FFFF) << 13 | (func & 0x3) << 11


def alnum_message_bits(text: str) -> list[int]:
    """7-bit ASCII, least-significant bit of each char sent first."""
    bits: list[int] = []
    for ch in text.encode("ascii"):
        for i in range(7):
            bits.append((ch >> i) & 1)
    return bits


def numeric_message_bits(text: str) -> list[int]:
    """4 bits per digit, bit-reversed within the nibble, high nibble sent first."""
    rev = {c: i for i, c in enumerate("0123456789" + BCD_CHARS[:6])}
    bits: list[int] = []
    for ch in text:
        val = rev[ch]
        val = (val & 0x5) << 1 | (val & 0xA) >> 1
        val = (val & 0x3) << 2 | (val & 0xC) >> 2
        for i in range(3, -1, -1):
            bits.append((val >> i) & 1)
    return bits


def message_codewords(msg_bits: list[int]) -> list[int]:
    """Pack a message bit stream into codewords: bit31=1, 20 payload bits 30..11."""
    words = []
    for off in range(0, len(msg_bits), 20):
        chunk = msg_bits[off:off + 20]
        chunk += [1] * (20 - len(chunk))  # pad with 1s: never a premature NUL char
        word = 1 << 31
        for i, bit in enumerate(chunk):
            if bit:
                word |= 1 << (30 - i)
        words.append(word)
    return words


def build_codewords(ric: int, func: int, text: str) -> list[int]:
    """Codewords from slot 0: idle filler, the address word at its ric&7 slot,
    then the message words. The message may run past one 16-word batch; the
    caller inserts a frame sync word every 16 codewords, which is where a real
    POCSAG transmission continues a long message."""
    slot = (ric & 0x7) * 2
    if func in (FUNC_ALPHANUM, FUNC_ALERT2):
        payload = message_codewords(alnum_message_bits(text))
    elif func == FUNC_NUM:
        payload = message_codewords(numeric_message_bits(text))
    else:
        payload = []
    words = [IDLE_CODE_WORD] * slot + [address_codeword(ric, func)] + payload
    words.append(IDLE_CODE_WORD)  # idle after the message ends it
    while len(words) % 16 != 0:   # pad the final batch
        words.append(IDLE_CODE_WORD)
    return words


def codeword_bits(word: int) -> list[int]:
    return [(word >> (31 - i)) & 1 for i in range(32)]


def encode(ric: int, func: int, text: str, baud: int = 1200) -> tuple[list[tuple[int, int]], int]:
    te = TE_SHORT[baud]
    bits: list[int] = [1, 0] * 32  # preamble: alternating, >= 24 bits
    words = build_codewords(ric, func, text)
    for i in range(0, len(words), 16):
        bits += codeword_bits(FRAME_SYNC_CODE)  # frame sync starts each batch
        for word in words[i:i + 16]:
            bits += codeword_bits(word)
    # The decoder inverts the line level ("POCSAG signals are inverted"), so the
    # transmitted level is the complement of the logical bit. One edge per bit.
    edges = [(1 - bit, te) for bit in bits]
    # End of packet: a gap that is not a whole number of bit periods, so the
    # decoder leaves the bit grid and flushes (fires its callback).
    edges.append((0, te * 3 + te // 2))
    return edges, te


CASES = [
    ("alnum_text", 1234568, FUNC_ALPHANUM, "TEST PAGE", 1200),
    ("alnum_long", 2000000, FUNC_ALPHANUM, "The quick brown fox 0123", 1200),
    ("numeric", 1234561, FUNC_NUM, "12345", 1200),
    ("alnum_512", 999999, FUNC_ALPHANUM, "SLOW", 512),
]


def write_case(directory: Path, name: str, ric: int, func: int, text: str, baud: int) -> tuple[str, str]:
    edges, _ = encode(ric, func, text, baud)
    edges_text = "".join(f"{level} {dur}\n" for level, dur in edges)
    expect_text = f"RIC {ric}\nFUNC {func}\nMSG {text}\n"
    # newline="" so \n is written verbatim (no CRLF translation on Windows),
    # keeping the committed fixtures byte-identical across platforms.
    (directory / f"{name}.edges").write_text(edges_text, encoding="ascii", newline="")
    (directory / f"{name}.expect").write_text(expect_text, encoding="ascii", newline="")
    return edges_text, expect_text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("out", nargs="?", default=str(Path(__file__).resolve().parent / "fixtures"))
    parser.add_argument("--check", action="store_true", help="verify committed fixtures match; write nothing")
    args = parser.parse_args()
    directory = Path(args.out)
    directory.mkdir(parents=True, exist_ok=True)
    mismatched = []
    for name, ric, func, text, baud in CASES:
        if args.check:
            edges, _ = encode(ric, func, text, baud)
            edges_text = "".join(f"{level} {dur}\n" for level, dur in edges)
            expect_text = f"RIC {ric}\nFUNC {func}\nMSG {text}\n"
            for suffix, produced in ((".edges", edges_text), (".expect", expect_text)):
                path = directory / f"{name}{suffix}"
                if not path.is_file() or path.read_text(encoding="ascii") != produced:
                    mismatched.append(str(path))
        else:
            write_case(directory, name, ric, func, text, baud)
    if args.check:
        if mismatched:
            print("FAIL: committed POCSAG fixtures do not match the generator:")
            for path in mismatched:
                print("  -", path)
            return 1
        print(f"POCSAG fixtures match the generator ({len(CASES)} cases)")
        return 0
    print(f"wrote {len(CASES)} POCSAG fixtures to {directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
