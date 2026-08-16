#!/usr/bin/env python3
"""Differential function profiles: "what code ran in A but not in B?"

Consumes SNESRECOMP_FUNC_PROFILE jsonl dumps from the trace-hook build
(build_host_trace.bat). The killer query is behavioral isolation: profile
a run where the behavior happens and one where it does not, and the
functions exclusive to the first run ARE the behavior's code, named.

Alignment caveat (important): call COUNTS are noisy across runs whose
timelines differ (animation cadence, entry timing). Exclusive/missing
sets are robust; treat ratio output as a hint, not evidence, unless the
runs replay identical inputs.

Coverage mode reports how much of the generated program a profile ever
touched — the priority list for naming and for routes that still need
recording.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path
import json

REPO = Path(__file__).resolve().parent.parent


def load_profile(path: Path) -> dict[str, dict]:
    out = {}
    for line in path.read_text(errors="replace").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        out[row["pc24"]] = row
    return out


def total_functions() -> int:
    funcs = REPO / "recomp" / "funcs.h"
    try:
        return len(re.findall(r"^void ", funcs.read_text(errors="replace"),
                              re.M))
    except OSError:
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile_a", type=Path)
    parser.add_argument("profile_b", type=Path, nargs="?",
                        help="omit for coverage-only report on profile_a")
    parser.add_argument("--min-calls", type=int, default=1)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    a = load_profile(args.profile_a)
    report: dict = {}

    total = total_functions()
    if total:
        print(f"coverage: {len(a)} / ~{total} generated functions "
              f"({100 * len(a) // total}%) touched by {args.profile_a.name}")
        report["coverage"] = {"touched": len(a), "total": total}

    if args.profile_b:
        b = load_profile(args.profile_b)
        only_a = [a[k] for k in a.keys() - b.keys()
                  if a[k]["calls"] >= args.min_calls]
        only_b = [b[k] for k in b.keys() - a.keys()
                  if b[k]["calls"] >= args.min_calls]
        only_a.sort(key=lambda r: -r["calls"])
        only_b.sort(key=lambda r: -r["calls"])
        print(f"\nexclusive to A ({args.profile_a.name}): {len(only_a)}")
        for row in only_a[:25]:
            print(f"  {row['calls']:>8}x {row['pc24']} {row['name']} "
                  f"(frames {row['first_frame']}..{row['last_frame']})")
        print(f"\nexclusive to B ({args.profile_b.name}): {len(only_b)}")
        for row in only_b[:25]:
            print(f"  {row['calls']:>8}x {row['pc24']} {row['name']}")
        report["only_a"] = only_a
        report["only_b"] = only_b

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
