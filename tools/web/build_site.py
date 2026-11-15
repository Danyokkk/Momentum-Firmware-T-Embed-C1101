#!/usr/bin/env python3
"""Assemble the web flasher site from web/ and one or more build packages.

    python tools/web/build_site.py --out dist/site --package dist/package \
        [--package more/packages ...] [--releases-dir packages/releases] [--repo owner/name]

- copies web/ (index.html with the repository filled in, app.js, md5.js,
  style.css, favicon.svg)
- downloads the pinned esptool-js from npm and refuses it unless it matches
  the integrity in web/vendor.json
- copies each package's firmware files into firmware/<id>/ after checking
  every size and SHA-256 against the package's manifest.json
- writes builds.json: newest releases first, then the development build

Packages come from tools/web/package_build.py (the current build) and
tools/web/fetch_releases.py (published releases). Release packages carry no
SD bundle; their download link points at the GitHub release asset.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import io
import json
import re
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
WEB = REPO_ROOT / "web"
STATIC_FILES = ["index.html", "app.js", "md5.js", "style.css", "favicon.svg"]
REPO_PATTERN = re.compile(r"^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$")
ID_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
CHANNEL_ORDER = {"release": 0, "prerelease": 1, "dev": 2}


class SiteError(Exception):
    pass


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch_vendor(cache: Path) -> dict[str, bytes]:
    spec = json.loads((WEB / "vendor.json").read_text(encoding="utf-8"))["esptool-js"]
    algorithm, expected = spec["integrity"].split("-", 1)
    if algorithm != "sha512":
        raise SiteError("web/vendor.json: only sha512 integrity values are supported")
    cache.mkdir(parents=True, exist_ok=True)
    tarball = cache / f"esptool-js-{spec['version']}.tgz"
    if tarball.is_file():
        blob = tarball.read_bytes()
    else:
        request = urllib.request.Request(spec["tarball"], headers={"User-Agent": "tembed-site-builder"})
        with urllib.request.urlopen(request, timeout=60) as response:
            blob = response.read()
    actual = base64.b64encode(hashlib.sha512(blob).digest()).decode()
    if actual != expected:
        tarball.unlink(missing_ok=True)
        raise SiteError(f"esptool-js {spec['version']} does not match the integrity pinned in web/vendor.json")
    tarball.write_bytes(blob)
    out: dict[str, bytes] = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as archive:
        for member, target in spec["files"].items():
            extracted = archive.extractfile(member)
            if extracted is None:
                raise SiteError(f"esptool-js tarball has no {member}")
            out[target] = extracted.read()
    out["__version__"] = spec["version"].encode()
    return out


def load_package(directory: Path) -> dict:
    manifest_path = directory / "manifest.json"
    if not manifest_path.is_file():
        raise SiteError(f"{directory} has no manifest.json")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise SiteError(f"{manifest_path}: unsupported schema {manifest.get('schema')}")
    if not ID_PATTERN.match(str(manifest.get("id", ""))):
        raise SiteError(f"{manifest_path}: bad id {manifest.get('id')!r}")
    if manifest.get("channel") not in CHANNEL_ORDER:
        raise SiteError(f"{manifest_path}: bad channel {manifest.get('channel')!r}")
    if manifest.get("chip") != "ESP32-S3" or (manifest.get("flash") or {}).get("size") != "16MB":
        raise SiteError(f"{manifest_path}: not an ESP32-S3 16 MB build")
    offsets = set()
    for part in manifest["parts"]:
        path = directory / part["file"]
        if "/" in part["file"] or "\\" in part["file"] or not path.is_file():
            raise SiteError(f"{directory}: part {part['file']!r} is missing")
        data = path.read_bytes()
        if len(data) != part["size"] or sha256_hex(data) != part["sha256"]:
            raise SiteError(f"{path} does not match its manifest")
        if hashlib.md5(data).hexdigest() != part["md5"]:
            raise SiteError(f"{path}: MD5 does not match its manifest")
        offsets.add(part["offset"])
    if offsets != {0x0, 0x8000, 0x10000, 0xBF0000}:
        raise SiteError(f"{manifest_path}: unexpected offsets {sorted(hex(o) for o in offsets)}")
    manifest["_dir"] = directory
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--out", required=True)
    parser.add_argument("--package", action="append", default=[], help="a package directory (repeatable)")
    parser.add_argument("--releases-dir", help="directory of release packages, one per subdirectory")
    parser.add_argument("--repo", default="", help="owner/name; taken from the packages if omitted")
    parser.add_argument("--vendor-cache", default=str(REPO_ROOT / "dist" / "vendor-cache"))
    parser.add_argument("--keep-releases", type=int, default=3, help="newest releases to offer (default 3)")
    parser.add_argument("--replace", action="store_true", help="replace an existing site directory")
    args = parser.parse_args()

    out = Path(args.out).resolve()
    if out.exists() and any(out.iterdir()):
        # Only ever delete something that is recognisably an earlier site build.
        if not (args.replace and (out / "builds.json").is_file()):
            sys.exit(f"{out} exists and is not empty (use --replace to overwrite an earlier site build)")
        shutil.rmtree(out)

    try:
        package_dirs = [Path(p).resolve() for p in args.package]
        if args.releases_dir and Path(args.releases_dir).is_dir():
            package_dirs += sorted(p for p in Path(args.releases_dir).resolve().iterdir() if p.is_dir())
        packages = [load_package(d) for d in package_dirs]

        repo = args.repo or next((p.get("repo") for p in packages if p.get("repo")), "")
        if repo and not REPO_PATTERN.match(repo):
            raise SiteError(f"repository {repo!r} is not owner/name")

        # Deduplicate by id (a release may also be passed as --package), newest first.
        unique: dict[str, dict] = {}
        for package in packages:
            unique.setdefault(package["id"], package)
        releases = [p for p in unique.values() if p["channel"] in ("release", "prerelease")]
        devs = [p for p in unique.values() if p["channel"] == "dev"]
        releases.sort(key=lambda p: p.get("date", ""), reverse=True)
        devs.sort(key=lambda p: p.get("date", ""), reverse=True)
        chosen = releases[: max(0, args.keep_releases)] + devs[:1]

        vendor = fetch_vendor(Path(args.vendor_cache))
    except SiteError as error:
        sys.exit(f"build_site: {error}")

    out.mkdir(parents=True, exist_ok=True)
    repo_url = f"https://github.com/{repo}" if repo else "https://github.com"
    for name in STATIC_FILES:
        source = WEB / name
        if name == "index.html":
            html = source.read_text(encoding="utf-8")
            html = html.replace("{{REPO_URL}}", repo_url).replace("{{REPO}}", repo or "this repository")
            html = html.replace("{{ESPTOOL_VERSION}}", vendor["__version__"].decode())
            if "{{" in html:
                sys.exit("build_site: index.html still contains an unfilled {{placeholder}}")
            (out / name).write_text(html, encoding="utf-8")
        else:
            shutil.copyfile(source, out / name)
    for target, data in vendor.items():
        if target.startswith("__"):
            continue
        destination = out / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)

    builds = []
    for package in chosen:
        bid = package["id"]
        source: Path = package["_dir"]
        destination = out / "firmware" / bid
        destination.mkdir(parents=True, exist_ok=True)
        for part in package["parts"]:
            shutil.copyfile(source / part["file"], destination / part["file"])
        public = {k: v for k, v in package.items() if not k.startswith("_")}
        sd = public.get("sd_bundle")
        if sd and (source / sd["file"]).is_file():
            shutil.copyfile(source / sd["file"], destination / sd["file"])
            sd = {**sd, "url": f"firmware/{bid}/{sd['file']}"}
        elif sd and repo and package["channel"] != "dev":
            sd = {**sd, "url": f"{repo_url}/releases/download/{bid}/{sd['file']}"}
        else:
            sd = None
        public["sd_bundle"] = sd
        (destination / "manifest.json").write_text(json.dumps(public, indent=2) + "\n", encoding="utf-8")
        sums = [f"{part['sha256']}  {part['file']}" for part in package["parts"]]
        (destination / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")
        public["base"] = f"firmware/{bid}/"
        public["manifest_url"] = f"firmware/{bid}/manifest.json"
        public["sums_url"] = f"firmware/{bid}/SHA256SUMS"
        if repo:
            if package["channel"] == "dev" and package.get("commit"):
                public["notes_url"] = f"{repo_url}/commit/{package['commit']}"
            elif package["channel"] != "dev":
                public["notes_url"] = f"{repo_url}/releases/tag/{bid}"
        builds.append(public)

    stable = [b for b in builds if b["channel"] == "release"]
    default = (stable or [b for b in builds if b["channel"] == "prerelease"] or builds or [{"id": None}])[0]["id"]
    index = {
        "schema": 1,
        "generated": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
        "repo": repo,
        "default": default,
        "builds": builds,
    }
    (out / "builds.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    (out / ".nojekyll").write_text("", encoding="utf-8")

    print(f"site written to {out}")
    for build in builds:
        marker = "*" if build["id"] == default else " "
        print(f" {marker} {build['id']:<24} {build['channel']:<10} firmware {build.get('firmware_version', '?')}")
    if not builds:
        print("  (no builds: the page will say that nothing is published yet)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
