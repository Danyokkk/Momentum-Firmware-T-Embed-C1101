#!/usr/bin/env python3
"""Download recent published releases' web-flasher files for build_site.py.

    GITHUB_TOKEN=... python tools/web/fetch_releases.py --repo owner/name --out packages/releases [--limit 3]

For each of the newest published releases (drafts skipped) that has a
manifest.json asset, downloads the manifest and the four firmware parts it
lists into <out>/<tag>/ and checks every SHA-256. Releases made by hand
without a manifest are skipped with a note. The SD bundle is not downloaded:
the site links to it on the release page.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

API = "https://api.github.com"
REPO_PATTERN = re.compile(r"^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$")
TAG_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


def request(url: str, accept: str, token: str | None) -> bytes:
    req = urllib.request.Request(url, headers={"Accept": accept, "User-Agent": "tembed-site-builder"})
    if token:
        # Not forwarded on redirect: asset downloads redirect to a storage host
        # that rejects requests carrying a GitHub token.
        req.add_unredirected_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=120) as response:
        return response.read()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--repo", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--limit", type=int, default=3)
    args = parser.parse_args()
    if not REPO_PATTERN.match(args.repo):
        parser.error("--repo must be owner/name")
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    try:
        releases = json.loads(request(f"{API}/repos/{args.repo}/releases?per_page=30", "application/vnd.github+json", token))
    except urllib.error.HTTPError as error:
        sys.exit(f"fetch_releases: listing releases failed: HTTP {error.code}")

    fetched = 0
    for release in releases:
        if fetched >= args.limit:
            break
        tag = release.get("tag_name", "")
        if release.get("draft") or not TAG_PATTERN.match(tag):
            continue
        assets = {asset["name"]: asset for asset in release.get("assets", [])}
        if "manifest.json" not in assets:
            print(f"skip {tag}: no manifest.json asset (not made by the release workflow)")
            continue
        manifest = json.loads(request(assets["manifest.json"]["url"], "application/octet-stream", token))
        if manifest.get("id") != tag:
            print(f"skip {tag}: its manifest is for {manifest.get('id')!r}")
            continue
        parts = manifest.get("parts") or []
        missing = [str(part.get("file")) for part in parts
                   if part.get("file") not in assets or "/" in str(part.get("file"))]
        if not parts or missing:
            # Checked before anything is written: build_site.py rejects a
            # release folder without a manifest, so never leave a partial one.
            print(f"skip {tag}: missing assets: {', '.join(missing) or 'manifest lists no parts'}")
            continue
        target = out / tag
        target.mkdir(parents=True, exist_ok=True)
        for part in parts:
            name = part["file"]
            data = request(assets[name]["url"], "application/octet-stream", token)
            if len(data) != part["size"] or hashlib.sha256(data).hexdigest() != part["sha256"]:
                sys.exit(f"fetch_releases: {tag}/{name} does not match its manifest")
            (target / name).write_bytes(data)
        (target / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"fetched {tag} ({manifest.get('channel')}, firmware {manifest.get('firmware_version')})")
        fetched += 1

    if fetched == 0:
        print("no published releases with web-flasher files yet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
