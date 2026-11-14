"""Host-only tests for the weather-station Sub-GHz decoders; no radio, no device.

    python tests/subghz_ws/run.py

Linux/macOS: gcc with ASan/UBSan. Windows: set ZIG_CC to zig.exe
(pip install ziglang==0.13.0), same convention as tests/host/host_build.py.
"""
from pathlib import Path
import os
import re
import sys

root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
sys.path.insert(0, str(root / "tests/host"))
from host_build import run  # noqa: E402

protocols = root / "lib/subghz/protocols"
blocks = root / "lib/subghz/blocks"
toolbox = root / "components/toolbox"

# Every module that pulls in ws_generic.h is under test; nothing is hand-listed,
# so a decoder added later is compiled (and must be added to the C table).
modules = sorted(
    path.stem
    for path in protocols.glob("*.c")
    if path.stem == "ws_generic"
    or '#include "ws_generic.h"' in path.read_text(encoding="utf-8")
    or '#include "ws_generic.h"' in (protocols / (path.stem + ".h")).read_text(encoding="utf-8")
)

# 1. registry consistency (pure text, no compiler needed)
items_c = (protocols / "protocol_items.c").read_text(encoding="utf-8")
items_h = (protocols / "protocol_items.h").read_text(encoding="utf-8")
table_c = (here / "test_ws_decode.c").read_text(encoding="utf-8")
problems = []
symbols = []
for module in modules:
    if module == "ws_generic":
        continue
    source = (protocols / f"{module}.c").read_text(encoding="utf-8")
    found = re.findall(r"^const SubGhzProtocol (ws_protocol_\w+) =", source, re.MULTILINE)
    if len(found) != 1:
        problems.append(f"{module}.c defines {len(found)} protocols")
        continue
    symbol = found[0]
    symbols.append(symbol)
    if f"&{symbol}," not in items_c:
        problems.append(f"{symbol} is not registered in protocol_items.c")
    if f'#include "{module}.h"' not in items_h:
        problems.append(f"{module}.h is not included by protocol_items.h")
    if f"&{symbol}," not in table_c:
        problems.append(f"{symbol} is not in the table in test_ws_decode.c")
order = [items_c.index(f"&{s},") for s in ("ws_protocol_bresser_3ch", "ws_protocol_lacrosse_tx",
                                            "ws_protocol_solight_te44", "ws_protocol_auriol_th")]
if not (order[0] < order[1] and order[2] < order[3]):
    problems.append("registry order: bresser_3ch must precede lacrosse_tx, solight_te44 must precede auriol_th")
if problems:
    print("FAIL: registry consistency")
    for problem in problems:
        print("  -", problem)
    sys.exit(1)
print(f"registry: {len(symbols)} weather decoders registered, included and under test")

# 2. compile and run the replay tests
out = root / "build_host" / "subghz_ws"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("test_ws_decode.exe" if os.name == "nt" else "test_ws_decode")
sources = [
    here / "test_ws_decode.c",
    here / "host_stubs.c",
    blocks / "decoder.c",
    blocks / "math.c",
    blocks / "generic.c",
    toolbox / "manchester_decoder.c",
    toolbox / "float_tools.c",
] + [protocols / f"{module}.c" for module in modules]

run(
    [
        "gcc", "-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-O1", "-g", "-fsanitize=address,undefined",
        "-I" + str(here / "stubs"),      # must come first: shadows furi/flipper_format
        "-I" + str(root),
        "-I" + str(root / "components"),  # "furi/core/log.h" resolves via the stub dir first
        "-I" + str(toolbox),
        "-I" + str(root / "components/mlib"),
        "-I" + str(protocols),
        *map(str, sources),
        "-lm",
        "-o", str(exe),
    ],
    check=True,
)
run([str(exe), str(here / "fixtures")], check=True)
print("PASS: subghz weather-station decoders (host only; on-air reception is a separate hardware check)")
