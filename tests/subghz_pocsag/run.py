"""Host-only test for the POCSAG pager decoder; no radio, no device.

    python tests/subghz_pocsag/run.py

pocsag_encode.py is an independent POCSAG framer (built from the standard, not
from the decoder source). This compiles the real decoder against light host
stubs, replays the encoder's edge streams through it, and checks the recovered
address and message. A pass means the decode path (sync lock, batch handling,
character assembly) is self-consistent with an independent encoder; it is not
proof against a real pager, and nothing here was received over the air.

Linux/macOS: gcc with ASan/UBSan. Windows: set ZIG_CC to zig.exe
(pip install ziglang==0.13.0), same convention as tests/host/host_build.py.
"""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

protocols = root / "lib/subghz/protocols"
blocks = root / "lib/subghz/blocks"
toolbox = root / "components/toolbox"

# 1. registry consistency (text only): the four ported protocols must be
#    included and registered, and POCSAG must stay receive-only in source.
items_c = (protocols / "protocol_items.c").read_text(encoding="utf-8")
items_h = (protocols / "protocol_items.h").read_text(encoding="utf-8")
problems = []
for name in ("pocsag", "x10", "allstar_firefly", "nord_ice"):
    if f'#include "{name}.h"' not in items_h:
        problems.append(f"{name}.h is not included by protocol_items.h")
    if f"&subghz_protocol_{name}," not in items_c:
        problems.append(f"subghz_protocol_{name} is not registered in protocol_items.c")
pocsag_src = (protocols / "pocsag.c").read_text(encoding="utf-8")
if "SubGhzProtocolFlag_Send" in pocsag_src:
    problems.append("pocsag.c carries SubGhzProtocolFlag_Send; it must stay receive-only")
if problems:
    print("FAIL: registry/source consistency")
    for problem in problems:
        print("  -", problem)
    sys.exit(1)
print("registry: pocsag, x10, allstar_firefly, nord_ice registered and included")

# 2. the committed fixtures are what the independent encoder produces
if subprocess.run([sys.executable, str(here / "pocsag_encode.py"), "--check", str(here / "fixtures")]).returncode:
    sys.exit(1)

# 3. compile and replay
out = root / "build_host" / "subghz_pocsag"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_pocsag_decode.exe" if os.name == "nt" else "test_pocsag_decode")
sources = [
    here / "test_pocsag_decode.c",
    here / "host_stubs.c",
    blocks / "decoder.c",
    blocks / "math.c",
    protocols / "pocsag.c",
    protocols / "pcsg_generic.c",
]
run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-O1", "-g", "-fsanitize=address,undefined",
        "-I" + str(here / "stubs"),      # first: shadows furi / flipper_format
        "-I" + str(root),
        "-I" + str(root / "components"),
        "-I" + str(toolbox),
        "-I" + str(root / "components/mlib"),
        "-I" + str(protocols),
        *map(str, sources),
        "-lm",
        "-o", str(exe),
    ],
    check=True,
)
sys.stdout.flush()
result = run([str(exe), str(here / "fixtures")], capture_output=True, text=True)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
if result.returncode:
    print(f"FAIL: test_pocsag_decode exited with {result.returncode}")
    sys.exit(1)
