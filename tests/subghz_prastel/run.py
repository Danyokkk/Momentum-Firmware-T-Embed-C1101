"""Host test for the Prastel 42-bit rolling-code scrambling/packing math.

    python tests/subghz_prastel/run.py

No radio, no device: the scramble/unscramble/pack/unpack/button helpers are
reproduced byte-identical to lib/subghz/protocols/prastel.c and checked for the
encode->decode roundtrip property (serial masked, button and counter recovered).
Linux/macOS: gcc with ASan/UBSan. Windows: set ZIG_CC to zig.exe.
"""
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

out = root / "build_host" / "subghz_prastel"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_prastel.exe" if os.name == "nt" else "test_prastel")

run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
        "-fsanitize=address,undefined",
        str(here / "test_prastel.c"),
        "-o", str(exe),
    ],
    check=True,
)
run([str(exe)], check=True)
print("PASS: subghz prastel host test")
