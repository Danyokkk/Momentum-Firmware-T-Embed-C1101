#!/usr/bin/env python3
"""Write GitHub release notes (Markdown) for a build package.

    python tools/web/release_notes.py dist/package/manifest.json [--site-url URL] > notes.md
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest")
    parser.add_argument("--site-url", default="", help="URL of the web flasher")
    args = parser.parse_args()
    m = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    repo = m.get("repo", "")
    lines = []
    if m["channel"] == "prerelease":
        lines.append("> **Pre-release.** For testing; expect rough edges.\n")
    lines.append(
        "> **Experimental.** These builds have not been tested on a physical T-Embed yet. "
        "Back up the device before installing (the web flasher does this for you).\n"
    )
    lines.append(f"Unofficial lab-profile firmware for the **{m['board_name']}** (ESP32-S3, 16 MB).\n")
    lines.append("## Install")
    if args.site_url:
        lines.append(f"- In Chrome, Edge or Opera on a computer: **[web flasher]({args.site_url})**, choose `{m['id']}`.")
    lines.append(
        "- With esptool: `python -m esptool --chip esp32s3 write_flash "
        + " ".join(f"{p['offset']:#x} {p['file']}" for p in m["parts"])
        + "`"
    )
    if m.get("sd_bundle"):
        lines.append("- SD card: extract `sd_bundle.zip` to the root of a FAT32 card.")
    lines.append("- After the first boot, choose your TX region in Sub-GHz > Radio Settings; transmit is off until then.\n")
    lines.append("## Build")
    lines.append(f"- Firmware version: `{m['firmware_version']}`, ESP-IDF `{m.get('idf_version', '?')}`")
    if m.get("commit") and repo:
        lines.append(f"- Commit: [{m['commit'][:7]}](https://github.com/{repo}/commit/{m['commit']}), built by GitHub Actions")
    lines.append(
        f"- Every file has a build-provenance attestation: `gh attestation verify <file> --repo {repo or 'OWNER/REPO'}`\n"
    )
    lines.append("| File | Offset | Size | SHA-256 |")
    lines.append("| --- | --- | --- | --- |")
    for p in m["parts"]:
        lines.append(f"| `{p['file']}` | `{p['offset']:#x}` | {p['size']:,} | `{p['sha256']}` |")
    if m.get("sd_bundle"):
        sd = m["sd_bundle"]
        lines.append(f"| `{sd['file']}` | | {sd['size']:,} | `{sd['sha256']}` |")
    lines.append("")
    if repo:
        docs = f"https://github.com/{repo}/blob/{m.get('commit') or 'main'}/docs/tembed-plus"
        lines.append(
            f"What is in it and what is left out: [feature matrix]({docs}/FEATURE_MATRIX.md) · "
            f"[release guide]({docs}/RELEASE_GUIDE.md) (flashing, recovery, RF rules)."
        )
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdout.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
