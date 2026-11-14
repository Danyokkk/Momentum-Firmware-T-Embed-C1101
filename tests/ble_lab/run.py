"""Host test for BLE Lab's advertisement parser; no radio, no device.

    python tests/ble_lab/run.py

Linux/macOS: gcc with ASan/UBSan. Windows: set ZIG_CC to zig.exe.
"""
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

app = root / "applications/main/ble_lab"
out = root / "build_host" / "ble_lab"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_adv.exe" if os.name == "nt" else "test_adv")
run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
        "-fsanitize=address,undefined",
        "-I" + str(app),
        str(here / "test_adv.c"),
        str(app / "ble_lab_adv.c"),
        "-o", str(exe),
    ],
    check=True,
)
run([str(exe)], check=True)
print("PASS: BLE Lab advertisement parser (the scan itself needs a device to verify)")
