#!/usr/bin/env python3
"""Assemble the SD-card content for the lab profile from the repo's sdcard/ overlay.

    python tools/tembed_plus/make_sd_bundle.py --out dist/sd_bundle            # resources only
    python tools/tembed_plus/make_sd_bundle.py --out dist/sd_bundle --prebuilt-faps
    python tools/tembed_plus/make_sd_bundle.py --list                          # show the decisions

Three classes of file:

  excluded  never copied: companions of features this fork does not build, and
            key material that only those features use.
  prebuilt  .fap/.fal binaries committed upstream. They were not built by this
            fork from reviewed source, so they are left out unless
            --prebuilt-faps is given, and then only the allow-listed ones.
  resource  everything else (asset packs, IR/NFC/Sub-GHz data files, ...).

The script writes MANIFEST.sha256 next to the copied files and never touches a
card directly: copy the output folder to the card yourself.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SDCARD = REPO_ROOT / "sdcard"

# (glob relative to sdcard/, reason). First match wins.
EXCLUDED = [
    ("apps/GPIO/bw16_r4tkn*.fap", "BW16 companion controller: deauth and capture+deauth commands"),
    ("apps/GPIO/esp32/esp32_wifi_marauder.fap", "front-end for a Marauder attack board"),
    ("apps/GPIO/fz_nrf24_jammer.fap", "2.4 GHz jammer"),
    ("apps/GPIO/tesla_mod.fap", "vehicle-targeting app; not reviewed"),
    ("apps/Misc/reverse_shell.fap", "remote file access to the SD card over TCP"),
    ("apps/Misc/findmy.fap", "tracker (AirTag/Tile) impersonation beacons"),
    ("apps/Misc/wardriver*.fap", "collects third-party network data and uploads it to external services"),
    ("apps/Misc/wdgwars_map.fap", "depends on the wardriver upload service"),
    ("apps/NFC/nfc_magic.fap", "magic-card writing and its own key dictionary attack"),
    ("apps/NFC/mifare_fuzzer.fap", "reader fuzzing by UID emulation"),
    ("apps/SubGhz/proto_pirate.fap", "vehicle key-fob decoder shipped with a manufacturer keystore"),
    ("apps_assets/proto_pirate/*", "vehicle key-fob keystore and plugins"),
    ("apps_data/js_app/plugins/js_badusb.fal", "scripted keystroke injection plugin"),
    ("badusb/*", "keystroke-injection scripts for the BadUSB app, which is not built"),
    ("subghz/assets/keeloq_mfcodes_user*", "user-supplied rolling-code manufacturer keys"),
    ("subghz/assets/dangerous_settings*", "TX band extension switch; ignored by this firmware anyway"),
    ("apps_data/wifi/*", "saved Wi-Fi data from the upstream author's bundle"),
]

# Prebuilt binaries that are benign on their own. Still unverified builds.
PREBUILT_ALLOW = [
    "apps/Games/*.fap",
    "apps/GPIO/i2c_tools_cli.fap",
    "apps/Infrared/flipirfreq.fap",
    "apps/Infrared/flipper_xremote.fap",
    "apps/Misc/totp.fap",
    "apps/Misc/wifite.fap",
    "apps/NFC/nfc_rfid_detector.fap",
    "apps/SubGhz/spectrum_analyzer.fap",
    "apps/SubGhz/sub_analyzer.fap",
    "apps/SubGhz/weather_station.fap",
    "apps/SubGhz/pocsag_pager.fap",
    "apps/SubGhz/rtl433.fap",
    "apps/USB/pc_monitor_usb.fap",
    "apps_data/nfc/plugins/*.fal",  # read-only card parsers for the NFC app
]

BINARY_SUFFIXES = {".fap", ".fal"}


def classify(relative: str) -> tuple[str, str]:
    for pattern, reason in EXCLUDED:
        if fnmatch.fnmatch(relative, pattern):
            return "excluded", reason
    if Path(relative).suffix.lower() in BINARY_SUFFIXES:
        if any(fnmatch.fnmatch(relative, pattern) for pattern in PREBUILT_ALLOW):
            return "prebuilt", "allow-listed prebuilt binary (not built by this fork)"
        return "excluded", "prebuilt binary that is not on the allow-list"
    return "resource", ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--out", help="output directory (must not exist or be empty)")
    parser.add_argument("--prebuilt-faps", action="store_true", help="also copy allow-listed prebuilt .fap files")
    parser.add_argument("--list", action="store_true", help="print the decision for every file and exit")
    args = parser.parse_args()

    files = sorted(path for path in SDCARD.rglob("*") if path.is_file())
    decisions = []
    for path in files:
        relative = path.relative_to(SDCARD).as_posix()
        if relative in ("README.md", "files.txt", "Manifest"):
            continue  # upstream bookkeeping for its own update flow
        kind, reason = classify(relative)
        decisions.append((relative, kind, reason, path))

    counts = {"resource": 0, "prebuilt": 0, "excluded": 0}
    for _, kind, _, _ in decisions:
        counts[kind] += 1

    if args.list:
        for relative, kind, reason, _ in decisions:
            if kind != "resource":
                print(f"{kind:9} {relative}  -- {reason}")
        print(f"\n{counts['resource']} resources, {counts['prebuilt']} allow-listed prebuilt, "
              f"{counts['excluded']} excluded")
        return 0

    if not args.out:
        parser.error("--out is required unless --list is given")
    out = Path(args.out).resolve()
    if out.exists() and any(out.iterdir()):
        sys.exit(f"{out} exists and is not empty; choose a new directory")
    out.mkdir(parents=True, exist_ok=True)

    manifest = []
    for relative, kind, _, path in decisions:
        if kind == "excluded" or (kind == "prebuilt" and not args.prebuilt_faps):
            continue
        target = out / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        manifest.append(f"{sha256(target)}  {relative}")
    (out / "MANIFEST.sha256").write_text("\n".join(manifest) + "\n", encoding="utf-8")

    copied_prebuilt = counts["prebuilt"] if args.prebuilt_faps else 0
    print(f"wrote {len(manifest)} files to {out}")
    print(f"  resources: {counts['resource']}, prebuilt binaries: {copied_prebuilt}, "
          f"left out: {counts['excluded'] + counts['prebuilt'] - copied_prebuilt}")
    if copied_prebuilt:
        print("  note: prebuilt .fap files come from upstream as binaries; they were not "
              "rebuilt or reviewed here and may fail to load against this firmware's API.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
