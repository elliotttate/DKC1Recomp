#!/usr/bin/env python3
"""Run one differential-oracle capture leg for a function.

Resolves the function (symbols.json), derives the WRAM capture ranges
from its oracle spec (reads + writes + SoA arrays, clamped to the
engine's range budget), arms the trace host, and replays a
deterministic route. Two legs (e.g. stock exe vs replacement exe, or
native vs force_lle rebuild) then compare with tools/oracle_diff.py —
byte-identical logs = proven-equivalent over that route.

usage:
  python tools/oracle_run.py CODE_BDF88A --rom R --route recipes/route_jungle.dks \\
      --frames 9000 --out build/oracle/stock.jsonl
  python tools/oracle_run.py BDF88A --rom R --route ... \\
      --exe build/dkc1_headless_replace_trace.exe --out build/oracle/replaced.jsonl
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

REPO = TOOLS.parent
MAX_RANGES = 24
MAX_BYTES = 1024
SOA_SPAN = 0x34


def derive_ranges(label: str) -> tuple[str, list[str]]:
    specs = json.loads(
        (REPO / "build/ir/oracle_specs.json").read_text())["functions"]
    spec = specs.get(label)
    if spec is None:
        sys.exit(f"no oracle spec for {label}; run oracle_spec.py "
                 "--emit-all")
    ranges: list[tuple[int, int]] = []
    for section in ("capture", "compare"):
        for addr in spec[section].get("wram_reads",
                                      spec[section].get("wram_writes",
                                                        [])):
            ranges.append((int(addr, 16) & 0x1FFFF, 2))
        for arr in spec[section].get("wram_read_arrays",
                                     spec[section].get(
                                         "wram_write_arrays", [])):
            ranges.append((int(arr["base"], 16) & 0x1FFFF, SOA_SPAN))
    # merge overlaps, clamp to engine budget (drop largest-last, honest)
    merged: list[list[int]] = []
    for addr, length in sorted(set(ranges)):
        if merged and addr <= merged[-1][0] + merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], addr + length -
                                merged[-1][0])
        else:
            merged.append([addr, length])
    dropped = []
    while len(merged) > MAX_RANGES or \
            sum(l for _, l in merged) > MAX_BYTES:
        dropped.append(merged.pop())
    note = f"dropped {len(dropped)} ranges over engine budget" \
        if dropped else ""
    return note, [f"{a:X}:{l:X}" for a, l in merged]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("function")
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--route", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=9000)
    parser.add_argument("--exe", type=Path,
                        default=REPO / "build/dkc1_headless_trace.exe")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--widescreen", type=int, default=1)
    parser.add_argument("--ranges",
                        help="override capture ranges (a:l[,a:l...])")
    args = parser.parse_args()

    symbols = json.loads(
        (REPO / "build/ir/symbols.json").read_text())["functions"]
    label = None
    query = args.function.upper().replace("0X", "")
    for name, record in symbols.items():
        if name.upper() in (query, f"CODE_{query}") or \
                (record.get("name") or "").upper() == query:
            label = name
            break
    if label is None:
        sys.exit(f"unknown function {args.function}")
    pc24 = symbols[label]["address"].replace("0x", "")

    if args.ranges:
        note, ranges = "", args.ranges.split(",")
    else:
        note, ranges = derive_ranges(label)
    if note:
        print(f"note: {note}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.unlink(missing_ok=True)
    env = os.environ.copy()
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env["DKC1_WIDESCREEN"] = str(args.widescreen)
    env["DKC1_SCRIPT"] = str(args.route.resolve())
    env["SNESRECOMP_ORACLE"] = pc24
    env["SNESRECOMP_ORACLE_RANGES"] = ",".join(ranges)
    env["SNESRECOMP_ORACLE_LOG"] = str(args.out.resolve())
    work = args.out.parent
    print(f"capturing {label} (0x{pc24}) ranges={','.join(ranges)}")
    result = subprocess.run(
        [str(args.exe.resolve()), str(args.rom.resolve()),
         str(args.frames)],
        cwd=str(work), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        sys.exit(f"run failed rc={result.returncode}: "
                 f"{result.stderr[-400:]}")
    calls = sum(1 for _ in args.out.open()) if args.out.exists() else 0
    print(f"{calls} calls captured -> {args.out}")
    if calls == 0:
        print("WARNING: zero captures — function never ran on this "
              "route (check coverage before concluding equivalence)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
