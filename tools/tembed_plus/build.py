#!/usr/bin/env python3
"""Reproducible build entry point for the LilyGO T-Embed CC1101 Plus lab firmware.

    python tools/tembed_plus/build.py doctor            # check toolchain, change nothing
    python tools/tembed_plus/build.py build             # configure + build
    python tools/tembed_plus/build.py size              # image size report
    python tools/tembed_plus/build.py verify            # confirm a firmware image exists
    python tools/tembed_plus/build.py flash --port COM7 # asks before writing

What it pins (tools/tembed_plus/toolchain.lock.json):
  * the ESP-IDF release; Wi-Fi adaptations are generated under the build tree,
    leaving the installed SDK untouched;
  * the board, build profile and sdkconfig defaults chain;
  * managed component versions come from dependencies.lock in the repo root.

It does not use export.bat/export.sh: the environment is taken from
`idf_tools.py export`, so it works from PowerShell, cmd, Git Bash and CI alike.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOCK = json.loads((Path(__file__).with_name("toolchain.lock.json")).read_text(encoding="utf-8"))

BOARD = LOCK["board"]
PROFILE = LOCK["profile"]
BUILD_DIR = LOCK["build_dir"]
SDKCONFIG_DEFAULTS = LOCK["sdkconfig_defaults"]


def fail(message: str) -> "NoReturn":  # type: ignore[name-defined]
    sys.exit(f"[tembed-plus] ERROR: {message}")


def note(message: str) -> None:
    print(f"[tembed-plus] {message}", flush=True)


def idf_dir() -> Path:
    for candidate in (os.environ.get("ESP_IDF_DIR"), os.environ.get("IDF_PATH")):
        if candidate and Path(candidate).is_dir():
            return Path(candidate)
    default = (
        Path(r"C:\Espressif\frameworks") / f"esp-idf-v{LOCK['esp_idf']['version']}"
        if os.name == "nt"
        else Path.home() / "esp" / f"esp-idf-v{LOCK['esp_idf']['version']}"
    )
    if default.is_dir():
        return default
    fail(
        f"ESP-IDF v{LOCK['esp_idf']['version']} not found. Set ESP_IDF_DIR, or install it "
        f"at {default} (see docs/tembed-plus/RELEASE_GUIDE.md)."
    )


def idf_tools_dir() -> Path | None:
    explicit = os.environ.get("IDF_TOOLS_PATH")
    if explicit:
        return Path(explicit)
    default = Path(r"C:\Espressif\tools") if os.name == "nt" else None
    return default if default and default.is_dir() else None


def idf_version(idf: Path) -> str:
    text = (idf / "tools" / "cmake" / "version.cmake").read_text(encoding="utf-8")
    parts = [
        re.search(rf"IDF_VERSION_{name}\s+(\d+)", text).group(1)  # type: ignore[union-attr]
        for name in ("MAJOR", "MINOR", "PATCH")
    ]
    return ".".join(parts)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_idf(idf: Path) -> list[str]:
    """Return SDK version mismatches; empty means the pinned release is present."""
    version = idf_version(idf)
    if version != LOCK["esp_idf"]["version"]:
        return [f"ESP-IDF is v{version}, lock wants v{LOCK['esp_idf']['version']}"]
    return []


def idf_environment(idf: Path) -> dict[str, str]:
    env = os.environ.copy()
    env.pop("MSYSTEM", None)  # idf_tools.py refuses to run under an MSYS shell
    env["IDF_PATH"] = str(idf)
    tools = idf_tools_dir()
    if tools:
        env["IDF_TOOLS_PATH"] = str(tools)
    result = subprocess.run(
        [sys.executable, str(idf / "tools" / "idf_tools.py"), "export", "--format", "key-value"],
        env=env,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        fail(
            "idf_tools.py export failed - are the ESP32-S3 tools installed?\n"
            f"  {sys.executable} {idf / 'tools' / 'idf_tools.py'} install --targets=esp32s3\n"
            f"  {sys.executable} {idf / 'tools' / 'idf_tools.py'} install-python-env\n"
            + result.stderr.strip()
        )
    for line in result.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key == "PATH":
            value = value.replace("%PATH%", env.get("PATH", "")).replace(
                "$PATH", env.get("PATH", "")
            )
        env[key] = value
    env["FLIPPER_BOARD"] = BOARD
    env["FLIPPER_PROFILE"] = PROFILE
    return env


def idf_py(idf: Path, env: dict[str, str], *args: str) -> int:
    python_env = Path(env["IDF_PYTHON_ENV_PATH"])
    python = python_env / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    defaults = ";".join(SDKCONFIG_DEFAULTS)
    command = [
        str(python),
        str(idf / "tools" / "idf.py"),
        "-B",
        BUILD_DIR,
        f"-DFLIPPER_BOARD={BOARD}",
        f"-DFLIPPER_PROFILE={PROFILE}",
        f"-DSDKCONFIG={BUILD_DIR}/sdkconfig",
        f"-DSDKCONFIG_DEFAULTS={defaults}",
        # CI sets FLIPPER_BUILD_LABEL (see .github/workflows/firmware.yml). Passed
        # even when empty so a label cached by an earlier build is cleared.
        f"-DFLIPPER_BUILD_LABEL={os.environ.get('FLIPPER_BUILD_LABEL', '')}",
        *args,
    ]
    note("idf.py " + " ".join(args))
    return subprocess.run(command, cwd=str(REPO_ROOT), env=env).returncode


def prepare() -> tuple[Path, dict[str, str]]:
    idf = idf_dir()
    problems = check_idf(idf)
    if problems:
        fail("toolchain does not match toolchain.lock.json:\n  - " + "\n  - ".join(problems))
    for name in SDKCONFIG_DEFAULTS:
        if not (REPO_ROOT / name).is_file():
            fail(f"missing {name}")
    return idf, idf_environment(idf)


def cmd_doctor(_args) -> int:
    idf = idf_dir()
    note(f"repo       : {REPO_ROOT}")
    note(f"ESP-IDF    : {idf} (v{idf_version(idf)})")
    note(f"board      : {BOARD}   profile: {PROFILE}")
    note(f"defaults   : {', '.join(SDKCONFIG_DEFAULTS)}")
    problems = check_idf(idf)
    for problem in problems:
        note(f"PROBLEM    : {problem}")
    if not problems:
        note("SDK        : pinned release found; Wi-Fi adaptations stay in the build tree")
        env = idf_environment(idf)
        note(f"python env : {env.get('IDF_PYTHON_ENV_PATH')}")
    return 1 if problems else 0


def cmd_build(_args) -> int:
    idf, env = prepare()
    lock = REPO_ROOT / "dependencies.lock"
    lock_before = sha256(lock)
    sdkconfig = REPO_ROOT / BUILD_DIR / "sdkconfig"
    if not sdkconfig.is_file():
        rc = idf_py(idf, env, "set-target", LOCK["idf_target"])
        if rc != 0:
            return rc
    rc = idf_py(idf, env, "build")
    if rc != 0:
        return rc
    # The managed components must be the pinned ones: the component manager
    # rewrites dependencies.lock when it re-solves, which would mean this image
    # was built from versions nobody pinned.
    if sha256(lock) != lock_before:
        fail(
            "the build rewrote dependencies.lock (managed components were re-resolved). "
            "Restore it with `git checkout -- dependencies.lock` and build again."
        )
    return cmd_verify(_args)


def cmd_size(_args) -> int:
    idf, env = prepare()
    return idf_py(idf, env, "size")


def cmd_verify(_args) -> int:
    image = REPO_ROOT / BUILD_DIR / "furi_esp32.elf"
    if not image.is_file():
        fail(f"missing {image}; run the build command first")
    note(f"firmware image is present: {image}")
    return 0


def cmd_flash(args) -> int:
    if not args.port:
        fail("--port is required (for example COM7 or /dev/ttyACM0)")
    if not args.yes:
        answer = input(
            f"Write {BUILD_DIR}/furi_esp32.bin to the device on {args.port}? This replaces "
            "the firmware currently on it. Type 'flash' to continue: "
        )
        if answer.strip().lower() != "flash":
            note("aborted, nothing written")
            return 1
    idf, env = prepare()
    return idf_py(idf, env, "-p", args.port, "flash")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("doctor").set_defaults(func=cmd_doctor)
    sub.add_parser("build").set_defaults(func=cmd_build)
    sub.add_parser("size").set_defaults(func=cmd_size)
    sub.add_parser("verify").set_defaults(func=cmd_verify)
    flash = sub.add_parser("flash")
    flash.add_argument("--port")
    flash.add_argument("--yes", action="store_true", help="skip the confirmation prompt")
    flash.set_defaults(func=cmd_flash)
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
