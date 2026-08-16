#!/usr/bin/env python3
"""Compare two oracle capture legs call-by-call.

Byte-identical logs = the two implementations are equivalent over this
route (registers, flags byte, captured WRAM, and cycle timing at every
outermost call). On divergence, reports the FIRST divergent call with a
field-level breakdown — including cycles_delta, because a replacement
that drifts timing desynchronizes the whole deterministic run even when
its state effects are right.

usage: python tools/oracle_diff.py stock.jsonl replaced.jsonl
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load(path: Path) -> list[dict]:
    records = []
    for line in path.read_text().splitlines():
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return records


def diff_snap(a: dict, b: dict, out: list[str], side: str) -> None:
    for key in sorted(set(a) | set(b)):
        if a.get(key) != b.get(key):
            out.append(f"    {side}.{key}: {a.get(key)!r} vs "
                       f"{b.get(key)!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    args = parser.parse_args()
    left = load(args.left)
    right = load(args.right)

    if args.left.read_bytes() == args.right.read_bytes():
        print(f"IDENTICAL: {len(left)} calls, byte-for-byte "
              f"({args.left.name} == {args.right.name})")
        return 0

    print(f"{args.left.name}: {len(left)} calls; "
          f"{args.right.name}: {len(right)} calls")
    for index, (a, b) in enumerate(zip(left, right)):
        if a == b:
            continue
        print(f"\nFIRST DIVERGENT CALL: #{index + 1} "
              f"(entry frame {a['entry'].get('frame')})")
        details: list[str] = []
        for field in ("outcome", "cycles_delta"):
            if a.get(field) != b.get(field):
                details.append(f"    {field}: {a.get(field)!r} vs "
                               f"{b.get(field)!r}")
        diff_snap(a.get("entry", {}), b.get("entry", {}), details,
                  "entry")
        diff_snap(a.get("exit", {}), b.get("exit", {}), details, "exit")
        print("\n".join(details) or "    (records differ structurally)")
        if "entry" in a and a.get("entry") == b.get("entry"):
            print("    entry states MATCH -> the divergence is the "
                  "function's own doing")
        else:
            print("    entry states differ -> divergence began UPSTREAM "
                  "of this call (earlier drift, e.g. timing)")
        return 1
    print(f"\ncommon prefix of {min(len(left), len(right))} calls "
          "matches; lengths differ (one leg captured more calls)")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
