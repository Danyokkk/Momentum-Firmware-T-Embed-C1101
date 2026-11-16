"""Host-only test for the RTC daily-alarm firing logic; no device.

    python tests/rtc_alarm/run.py

Compiles furi_hal_rtc_alarm_due() (a pure header) with a small driver and
checks it fires once per matching minute and re-arms afterwards. The 1 Hz timer
that calls it and the alarm notification are a separate hardware check.

Linux/macOS: gcc. Windows: set ZIG_CC to zig.exe (pip install ziglang==0.13.0).
"""
import os
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

out = root / "build_host" / "rtc_alarm"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_rtc_alarm.exe" if os.name == "nt" else "test_rtc_alarm")
run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror",
        "-O1", "-g", "-fsanitize=address,undefined",
        "-I" + str(root / "components/furi_hal"),
        str(here / "test_rtc_alarm.c"),
        "-o", str(exe),
    ],
    check=True,
)
result = run([str(exe)], capture_output=True, text=True)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
sys.exit(result.returncode)
