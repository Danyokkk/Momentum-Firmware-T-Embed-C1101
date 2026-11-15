#!/usr/bin/env python3
"""Copy the telling lines of a failed build log into GitHub Actions annotations.

    python3 tools/ci/annotate_log.py LOG [LOG ...] --title "T-Embed Plus build failed"

GitHub shows job logs only to signed-in users; annotations appear on the run
page and in the public check-runs API, so a failure can be diagnosed without
the full log. For each log that exists this emits up to two error
annotations: the first lines that look like errors, and the end of the log
(where idf.py prints fatal errors that contain no "error" keyword).
Always exits 0: it reports a failure, it must not cause one.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
ERRORISH = re.compile(
    r"error:|Error:|CMake Error|FAILED:|FatalError|fatal error|undefined reference|"
    r"\[tembed-plus\] ERROR|Traceback \(most recent call last\)|ninja: build stopped|"
    r"No such file or directory|Refusing"
)
MAX_MATCHES = 15
TAIL_LINES = 25


def escape_data(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_property(text: str) -> str:
    return escape_data(text).replace(":", "%3A").replace(",", "%2C")


def annotate(title: str, lines: list[str]) -> None:
    if lines:
        print(f"::error title={escape_property(title)}::{escape_data(chr(10).join(lines))}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--title", default="Build failed")
    args = parser.parse_args()
    found = False
    for name in args.logs:
        path = Path(name)
        if not path.is_file():
            continue
        found = True
        lines = [ANSI.sub("", line).rstrip()[:400] for line in path.read_text(encoding="utf-8", errors="replace").splitlines()]
        lines = [line for line in lines if line.strip()]
        matches: list[str] = []
        for line in lines:
            if ERRORISH.search(line) and line not in matches:
                matches.append(line)
                if len(matches) == MAX_MATCHES:
                    break
        annotate(f"{args.title}: first errors in {path.name}", matches)
        annotate(f"{args.title}: end of {path.name}", lines[-TAIL_LINES:])
    if not found:
        print(f"annotate_log: none of {', '.join(args.logs)} exists", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
