#!/usr/bin/env python3
"""Verify the trace-build's nested function-window attribution model."""
from __future__ import annotations

import json
import sys
from pathlib import Path


EXPECTED = [
    ("0x00", "0x01", True, "parent", "0x111111", "entry"),
    ("0x01", "0x02", True, "child", "0x222222", "exit"),
    ("0x02", "0x03", True, "parent", "0x111111", "exit"),
    ("0x03", "0x04", False, "host/outside-function-window", None, "entry"),
    ("0x04", "0x05", False, "host/outside-function-window", None, "entry"),
    ("0x05", "0x06", False, "host/outside-function-window", None,
     "host-boundary"),
]


def verify(path: Path) -> None:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if row.get("type") == "watch_change":
            rows.append(row)
        elif row.get("type") == "watch_truncated":
            raise AssertionError(f"watch log truncated: {row}")
    actual = [
        (row.get("old"), row.get("new"), row.get("attributed"),
         row.get("writer"), row.get("writer_pc"), row.get("boundary"))
        for row in rows
    ]
    if actual != EXPECTED:
        raise AssertionError(
            "lean watch attribution mismatch\n"
            f"expected={EXPECTED!r}\nactual={actual!r}")
    if any(row.get("writer") == "child" and row.get("new") == "0x03"
           for row in rows):
        raise AssertionError("parent post-call write was stale-attributed to child")
    if any(row.get("writer") == "CODE_80C0F8_M0X0" for row in rows):
        raise AssertionError(
            "$0028 interpreter/host-boundary change was stale-attributed "
            "to the preceding joypad routine CODE_80C0F8_M0X0")
    if any(row.get("writer") == "abandoned_by_watchdog" for row in rows):
        raise AssertionError(
            "mixed watchdog/host-boundary change inherited abandoned owner")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_lean_watch_attribution.py <model-stderr>",
              file=sys.stderr)
        return 2
    verify(Path(sys.argv[1]))
    print("LEAN_WATCH_ATTRIBUTION_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
