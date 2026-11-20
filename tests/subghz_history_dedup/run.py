"""Host test for the Sub-GHz receiver-history dedup helpers.

    python tests/subghz_history_dedup/run.py

No radio, no device: the helpers in applications/main/subghz/subghz_history_dedup.h
are pure (stdint/stdbool/stddef only). Validates the consecutive-within-window
policy, whole-history membership, timestamp wrap-around and an A,B,A scenario.
Linux/macOS: gcc with ASan/UBSan. Windows: set ZIG_CC to zig.exe.
"""
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

subghz = root / "applications/main/subghz"
out = root / "build_host" / "subghz_history_dedup"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_dedup.exe" if os.name == "nt" else "test_dedup")

run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
        "-fsanitize=address,undefined",
        "-I" + str(subghz),
        str(here / "test_dedup.c"),
        "-o", str(exe),
    ],
    check=True,
)
run([str(exe)], check=True)
print("PASS: subghz history dedup host test")
