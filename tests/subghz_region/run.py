"""Host test for the Sub-GHz transmit region presets; no radio, no device.

    python tests/subghz_region/run.py

Linux/macOS: gcc with ASan/UBSan. Windows: set ZIG_CC to zig.exe.
"""
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

hal = root / "components/furi_hal"
out = root / "build_host" / "subghz_region"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_region.exe" if os.name == "nt" else "test_region")
run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
        "-fsanitize=address,undefined",
        "-I" + str(hal),
        str(here / "test_region.c"),
        str(hal / "furi_hal_region_table.c"),
        "-o", str(exe),
    ],
    check=True,
)
run([str(exe)], check=True)
print("PASS: Sub-GHz region presets (table logic only; the HAL hook and NVS storage are build-verified)")
