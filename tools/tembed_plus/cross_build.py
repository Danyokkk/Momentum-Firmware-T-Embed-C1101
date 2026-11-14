#!/usr/bin/env python3
"""Build every board profile this repository claims to support, lab profile.

    python tools/tembed_plus/cross_build.py                      # all boards
    python tools/tembed_plus/cross_build.py --boards fatalcore paradoxt
    python tools/tembed_plus/cross_build.py --results out.json

Each board gets its own build directory (build_x_<board>) and its own
sdkconfig, using the same defaults chain as upstream's build.sh:
sdkconfig.defaults plus sdkconfig.defaults.<board> when that file exists.
After a successful build the linked image is checked with
check_lab_exclusions.py (apps, sources and symbols; the sdkconfig expectations
there are specific to the T-Embed Plus defaults and are skipped).

This only proves that a profile compiles and links. None of these boards has
been run.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build as tembed  # noqa: E402

REPO_ROOT = tembed.REPO_ROOT

# FLIPPER_BOARD id -> IDF target. Order: boards upstream's CI builds first.
BOARDS = {
    "lilygo_t_embed_cc1101": "esp32s3",
    "esp32s3_generic": "esp32s3",
    "fatalcore": "esp32s3",
    "waveshare_c6_1.9": "esp32c6",
    "waveshare_c6_1.47": "esp32c6",
    "paradoxt": "esp32s3",
    "thormini": "esp32s3",
    "thorchimera": "esp32s3",
    "m5stick_s3": "esp32s3",
    "m5stack_cardputer_adv": "esp32s3",
    "DIY": "esp32s3",
}


def build_board(board: str, target: str, idf: Path, env: dict[str, str]) -> dict:
    build_dir = f"build_x_{board}"
    defaults = ["sdkconfig.defaults"]
    if (REPO_ROOT / f"sdkconfig.defaults.{board}").is_file():
        defaults.append(f"sdkconfig.defaults.{board}")
    python_env = Path(env["IDF_PYTHON_ENV_PATH"])
    python = python_env / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    base = [
        str(python),
        str(idf / "tools" / "idf.py"),
        "-B",
        build_dir,
        f"-DFLIPPER_BOARD={board}",
        "-DFLIPPER_PROFILE=lab",
        f"-DSDKCONFIG={build_dir}/sdkconfig",
        f"-DSDKCONFIG_DEFAULTS={';'.join(defaults)}",
    ]
    board_env = dict(env, FLIPPER_BOARD=board, FLIPPER_PROFILE="lab")
    log_path = REPO_ROOT / f"{build_dir}.log"
    started = time.time()
    result = {"board": board, "target": target, "defaults": defaults, "log": log_path.name}
    with log_path.open("w", encoding="utf-8", errors="replace") as log:
        steps = []
        if not (REPO_ROOT / build_dir / "sdkconfig").is_file():
            steps.append(["set-target", target])
        steps.append(["build"])
        for step in steps:
            rc = subprocess.run(
                base + step, cwd=str(REPO_ROOT), env=board_env, stdout=log, stderr=subprocess.STDOUT
            ).returncode
            if rc != 0:
                result.update(status="FAILED", failed_step=step[0], seconds=round(time.time() - started))
                return result
    binary = REPO_ROOT / build_dir / "furi_esp32.bin"
    result["image_bytes"] = binary.stat().st_size if binary.is_file() else None
    check = subprocess.run(
        [
            sys.executable,
            str(Path(__file__).with_name("check_lab_exclusions.py")),
            "--build-dir",
            str(REPO_ROOT / build_dir),
            "--no-sdkconfig",
        ],
        env=board_env,
        capture_output=True,
        text=True,
    )
    result["image_check"] = "pass" if check.returncode == 0 else check.stdout.strip()
    result["status"] = "ok" if check.returncode == 0 else "LINKED-BUT-CHECK-FAILED"
    result["seconds"] = round(time.time() - started)
    return result


def first_error(log_path: Path) -> str:
    try:
        lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return ""
    for line in lines:
        if " error:" in line or "CMake Error" in line or "undefined reference" in line:
            return line.strip()[-220:]
    return lines[-1].strip()[-220:] if lines else ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--boards", nargs="*", choices=list(BOARDS), default=None)
    parser.add_argument("--results", default="cross_build_results.json")
    args = parser.parse_args()

    idf = tembed.idf_dir()
    problems = tembed.check_idf(idf)
    if problems:
        tembed.fail("toolchain does not match toolchain.lock.json:\n  - " + "\n  - ".join(problems))
    env = tembed.idf_environment(idf)

    results = []
    for board in args.boards or list(BOARDS):
        tembed.note(f"=== {board} ({BOARDS[board]}) ===")
        outcome = build_board(board, BOARDS[board], idf, env)
        if outcome["status"] == "FAILED":
            outcome["first_error"] = first_error(REPO_ROOT / outcome["log"])
        results.append(outcome)
        tembed.note(json.dumps(outcome))
        (REPO_ROOT / args.results).write_text(json.dumps(results, indent=2), encoding="utf-8")

    problems = tembed.check_idf(idf)
    if problems:
        tembed.fail("a build modified the SDK:\n  - " + "\n  - ".join(problems))
    failed = [r["board"] for r in results if r["status"] != "ok"]
    tembed.note(f"{len(results) - len(failed)}/{len(results)} boards ok" + (f"; not ok: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
