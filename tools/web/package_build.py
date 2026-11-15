#!/usr/bin/env python3
"""Package a finished T-Embed Plus build for a release and for the web flasher.

    python tools/web/package_build.py --build-dir build_t_embed_plus --out dist/package \
        --label main-1a2b3c4 --channel dev --ref main --commit <sha> [--date <iso8601>] \
        [--repo owner/name] [--no-sd-bundle]

Writes into --out (which must not exist yet, or be empty):

  bootloader.bin, partition-table.bin, furi_esp32.bin, ota_data_initial.bin
  manifest.json   offsets, sizes, SHA-256 and MD5 of each part, build metadata
  SHA256SUMS      for checking downloads by hand
  sd_bundle.zip   resources-only SD card content (tools/tembed_plus/make_sd_bundle.py)

Offsets come from the build's own flash_args and are cross-checked against the
partition table inside partition-table.bin; both images must be ESP32-S3
images. A packaging mistake therefore fails here, not on somebody's device.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]

FLASH_BYTES = 16 * 1024 * 1024
ESP_IMAGE_MAGIC = 0xE9
ESP32S3_CHIP_ID = 9
APP_DESC_MAGIC = 0xABCD5432
PARTITION_MAGIC = b"\xaa\x50"
PARTITION_TABLE_OFFSET = 0x8000
LABEL_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
REPO_PATTERN = re.compile(r"^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$")

# role -> (path as written in flash_args, published file name, display name)
PARTS = {
    "bootloader": ("bootloader/bootloader.bin", "bootloader.bin", "Bootloader"),
    "partition_table": ("partition_table/partition-table.bin", "partition-table.bin", "Partition table"),
    "app": ("furi_esp32.bin", "furi_esp32.bin", "Firmware"),
    "otadata": ("ota_data_initial.bin", "ota_data_initial.bin", "OTA boot selection"),
}


class PackageError(Exception):
    pass


def digest(data: bytes, algorithm: str) -> str:
    return hashlib.new(algorithm, data).hexdigest()


def parse_flash_args(path: Path) -> tuple[dict[str, str], dict[str, int]]:
    tokens = path.read_text(encoding="utf-8").split()
    params: dict[str, str] = {}
    offsets: dict[str, int] = {}
    i = 0
    while i < len(tokens):
        token = tokens[i]
        if i + 1 >= len(tokens):
            raise PackageError(f"{path}: dangling token {token!r}")
        if token.startswith("--"):
            params[token[2:]] = tokens[i + 1]
        else:
            offsets[tokens[i + 1].replace("\\", "/")] = int(token, 16)
        i += 2
    return params, offsets


def parse_partition_table(data: bytes) -> list[dict]:
    entries = []
    for offset in range(0, len(data) - 31, 32):
        entry = data[offset : offset + 32]
        if entry[:2] != PARTITION_MAGIC:
            break  # 0xEBEB (MD5 entry) or 0xFFFF (end) both stop here
        part_type, subtype = entry[2], entry[3]
        start, size = struct.unpack_from("<II", entry, 4)
        label = entry[12:28].split(b"\0", 1)[0].decode("ascii", "replace")
        entries.append({"type": part_type, "subtype": subtype, "offset": start, "size": size, "label": label})
    if not entries:
        raise PackageError("partition-table.bin contains no partitions")
    return entries


def image_chip_id(data: bytes, name: str) -> int:
    if len(data) < 24 or data[0] != ESP_IMAGE_MAGIC:
        raise PackageError(f"{name} is not an ESP image (first byte {data[:1].hex() or 'missing'})")
    return struct.unpack_from("<H", data, 12)[0]


def read_app_description(data: bytes) -> dict[str, str]:
    # esp_image_header_t (24 bytes) + first esp_image_segment_header_t (8 bytes)
    base = 32
    if len(data) < base + 208 or struct.unpack_from("<I", data, base)[0] != APP_DESC_MAGIC:
        raise PackageError("furi_esp32.bin has no esp_app_desc_t where it should be")

    def text(start: int, length: int) -> str:
        return data[base + start : base + start + length].split(b"\0", 1)[0].decode("ascii", "replace")

    return {
        "version": text(16, 32),
        "project_name": text(48, 32),
        "time": text(80, 16),
        "date": text(96, 16),
        "idf_version": text(112, 32),
        "elf_sha256": data[base + 144 : base + 176].hex(),
    }


def check_layout(params: dict[str, str], offsets: dict[str, int], files: dict[str, bytes]) -> dict:
    if params.get("flash_size") != "16MB":
        raise PackageError(f"flash_args says flash_size {params.get('flash_size')}, expected 16MB")
    for role, (arg_path, _, _) in PARTS.items():
        if arg_path not in offsets:
            raise PackageError(f"flash_args has no entry for {arg_path}")
    if offsets[PARTS["bootloader"][0]] != 0:
        raise PackageError("the bootloader is not at 0x0")
    if offsets[PARTS["partition_table"][0]] != PARTITION_TABLE_OFFSET:
        raise PackageError("the partition table is not at 0x8000")

    partitions = parse_partition_table(files["partition_table"])
    for entry in partitions:
        if entry["offset"] + entry["size"] > FLASH_BYTES:
            raise PackageError(f"partition {entry['label']} ends beyond 16 MB")

    apps = [p for p in partitions if p["type"] == 0x00 and p["subtype"] in (0x00, 0x10)]  # factory / ota_0
    otadata = [p for p in partitions if p["type"] == 0x01 and p["subtype"] == 0x00]
    if not apps:
        raise PackageError("the partition table has no factory or ota_0 app partition")
    if not otadata:
        raise PackageError("the partition table has no otadata partition")
    app_partition = min(apps, key=lambda p: p["offset"])
    app_offset = offsets[PARTS["app"][0]]
    if app_offset != app_partition["offset"]:
        raise PackageError(
            f"flash_args puts the app at {app_offset:#x}, the partition table's first app slot "
            f"({app_partition['label']}) is at {app_partition['offset']:#x}"
        )
    if len(files["app"]) > app_partition["size"]:
        raise PackageError("furi_esp32.bin is larger than its partition")
    otadata_offset = offsets[PARTS["otadata"][0]]
    if otadata_offset != otadata[0]["offset"]:
        raise PackageError(
            f"flash_args puts ota_data_initial.bin at {otadata_offset:#x}, the otadata partition is at "
            f"{otadata[0]['offset']:#x}"
        )
    if len(files["otadata"]) != otadata[0]["size"]:
        raise PackageError("ota_data_initial.bin does not fill the otadata partition exactly")
    if set(files["otadata"]) != {0xFF}:
        raise PackageError("ota_data_initial.bin is not blank; it would not select the first app slot")

    for role in ("bootloader", "app"):
        chip = image_chip_id(files[role], PARTS[role][1])
        if chip != ESP32S3_CHIP_ID:
            raise PackageError(f"{PARTS[role][1]} is built for chip id {chip}, not the ESP32-S3 ({ESP32S3_CHIP_ID})")

    return {"app_partition": app_partition["label"], "partitions": len(partitions)}


def make_sd_bundle(destination: Path) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        staging = Path(tmp) / "sd"
        subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools/tembed_plus/make_sd_bundle.py"), "--out", str(staging)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        files = sorted(path for path in staging.rglob("*") if path.is_file())
        # Fixed timestamps and order: the same input always gives the same zip.
        with zipfile.ZipFile(destination, "w") as archive:
            for path in files:
                info = zipfile.ZipInfo(path.relative_to(staging).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                archive.writestr(info, path.read_bytes(), compresslevel=9)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--build-dir", default="build_t_embed_plus")
    parser.add_argument("--out", required=True)
    parser.add_argument("--label", required=True, help="build id, e.g. v0.1.0 or main-1a2b3c4")
    parser.add_argument("--channel", required=True, choices=["release", "prerelease", "dev"])
    parser.add_argument("--ref", default="", help="branch or tag the build came from")
    parser.add_argument("--commit", default="", help="full commit SHA")
    parser.add_argument("--date", default="", help="ISO 8601 date of the commit (default: now)")
    parser.add_argument("--repo", default="", help="owner/name of the GitHub repository")
    parser.add_argument("--no-sd-bundle", action="store_true")
    args = parser.parse_args()

    if not LABEL_PATTERN.match(args.label):
        parser.error(f"--label {args.label!r} must be 1-64 characters of letters, digits, '.', '_' or '-'")
    if args.repo and not REPO_PATTERN.match(args.repo):
        parser.error(f"--repo {args.repo!r} is not owner/name")
    if args.commit and not re.fullmatch(r"[0-9a-f]{7,40}", args.commit):
        parser.error("--commit must be a hex SHA")

    build = (REPO_ROOT / args.build_dir).resolve() if not Path(args.build_dir).is_absolute() else Path(args.build_dir)
    out = Path(args.out).resolve()
    if out.exists() and any(out.iterdir()):
        sys.exit(f"{out} exists and is not empty")
    flash_args = build / "flash_args"
    if not flash_args.is_file():
        sys.exit(f"{flash_args} not found: build first (python tools/tembed_plus/build.py build)")

    try:
        params, offsets = parse_flash_args(flash_args)
        files = {}
        for role, (arg_path, _, _) in PARTS.items():
            source = build / arg_path
            if not source.is_file():
                raise PackageError(f"{source} not found")
            files[role] = source.read_bytes()
        layout = check_layout(params, offsets, files)
        app = read_app_description(files["app"])
    except PackageError as error:
        sys.exit(f"package_build: {error}")

    out.mkdir(parents=True, exist_ok=True)
    parts = []
    for role, (arg_path, name, display) in PARTS.items():
        data = files[role]
        (out / name).write_bytes(data)
        parts.append(
            {
                "name": display,
                "file": name,
                "offset": offsets[arg_path],
                "size": len(data),
                "sha256": digest(data, "sha256"),
                "md5": digest(data, "md5"),
            }
        )
    parts.sort(key=lambda part: part["offset"])

    sd_bundle = None
    if not args.no_sd_bundle:
        zip_path = out / "sd_bundle.zip"
        make_sd_bundle(zip_path)
        blob = zip_path.read_bytes()
        sd_bundle = {"file": zip_path.name, "size": len(blob), "sha256": digest(blob, "sha256")}

    date = args.date or dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    manifest = {
        "schema": 1,
        "id": args.label,
        "label": args.label,
        "title": "Development build" if args.channel == "dev" else args.label,
        "channel": args.channel,
        "ref": args.ref,
        "commit": args.commit,
        "date": date,
        "repo": args.repo,
        "board": "lilygo_t_embed_cc1101",
        "board_name": "LilyGO T-Embed CC1101 Plus",
        "chip": "ESP32-S3",
        "profile": "lab",
        "firmware_version": app["version"],
        "idf_version": app["idf_version"],
        "elf_sha256": app["elf_sha256"],
        "flash": {
            "size": params.get("flash_size"),
            "mode": params.get("flash_mode"),
            "freq": params.get("flash_freq"),
            "app_partition": layout["app_partition"],
        },
        "parts": parts,
        "sd_bundle": sd_bundle,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    sums = [f"{part['sha256']}  {part['file']}" for part in parts]
    if sd_bundle:
        sums.append(f"{sd_bundle['sha256']}  {sd_bundle['file']}")
    (out / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")

    total = sum(part["size"] for part in parts)
    print(f"packaged {args.label} ({args.channel}) firmware {app['version']}: {len(parts)} parts, {total} bytes")
    for part in parts:
        print(f"  {part['offset']:#09x}  {part['file']:<22} {part['size']:>9}  {part['sha256'][:16]}")
    if sd_bundle:
        print(f"  sd_bundle.zip {sd_bundle['size']} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
