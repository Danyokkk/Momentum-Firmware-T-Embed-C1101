"""Host test for the UI language subsystem (strings, fallback, persistence).

    python tests/locale_i18n/run.py

No radio, no device, no GUI: canvas and saved_struct are mocked. Validates the
translation table, English fallback, the font apply-hook, and that a chosen
language survives a reload. Linux/macOS: gcc with ASan/UBSan. Windows: set
ZIG_CC to zig.exe.
"""
from pathlib import Path
import os
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

locale = root / "components/locale"
out = root / "build_host" / "locale_i18n"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_locale_i18n.exe" if os.name == "nt" else "test_locale_i18n")

run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
        "-fsanitize=address,undefined",
        "-I" + str(here / "stubs"),
        "-I" + str(locale),
        str(here / "test_locale_i18n.c"),
        str(locale / "locale_i18n.c"),
        "-o", str(exe),
    ],
    check=True,
)
run([str(exe)], check=True)
print("PASS: locale i18n host test (fonts/rendering are build-verified on device)")
