#!/usr/bin/env python3
"""Audit DKC1 animation callback coverage for the $BE:8179 dispatcher.

DKC1 animation command $81 stores a 24-bit callback in the animation script.
The recomp must authorize every callback at the indirect dispatcher; an
omitted target is otherwise skipped and the script continues with missing
gameplay side effects.  The audit derives the complete set from the
byte-exact disassembly and resolves symbols through its Asar symbol file.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys


OP81_RE = re.compile(r"%DKC1_AnS1_Op81\(([^)]+)\)")
SYMBOL_RE = re.compile(
    r"(?:^|:)([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+(\S+)\s*$")
DISPATCH_RE = re.compile(
    r"^indirect_dispatch\s+8179\s+(\d+)\s+ptrcall\s+"
    r"return:810D\s+frame:3\s+targets:(\S+)$")


def parse_symbols(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        match = SYMBOL_RE.search(line)
        if match:
            result[match.group(3)] = (
                match.group(1) + match.group(2)).upper()
    return result


def parse_callbacks(assembly: str, symbols: dict[str, str]) -> tuple[int, list[str], list[str]]:
    names = [match.strip() for match in OP81_RE.findall(assembly)]
    unique_names = sorted(set(names))
    unresolved = sorted(name for name in unique_names if name not in symbols)
    targets = sorted(symbols[name] for name in unique_names if name in symbols)
    return len(names), targets, unresolved


def parse_contract(config: str) -> tuple[int, list[str]]:
    matches = [DISPATCH_RE.match(line.strip()) for line in config.splitlines()]
    matches = [match for match in matches if match]
    if len(matches) != 1:
        raise ValueError(
            f"expected one exact $BE:8179 ptrcall contract, found {len(matches)}")
    declared = int(matches[0].group(1))
    targets = matches[0].group(2).upper().split(",")
    return declared, targets


def audit(assembly: str, symbol_text: str, config: str) -> dict[str, object]:
    symbols = parse_symbols(symbol_text)
    call_count, expected, unresolved = parse_callbacks(assembly, symbols)
    declared, actual = parse_contract(config)
    expected_set, actual_set = set(expected), set(actual)
    duplicate_actual = sorted(
        target for target in actual_set if actual.count(target) > 1)
    return {
        "schema": "dkc1.animation-dispatch-audit.v1",
        "op81_call_count": call_count,
        "expected_unique_targets": len(expected),
        "declared_target_count": declared,
        "actual_target_count": len(actual),
        "unresolved_symbols": unresolved,
        "missing_targets": sorted(expected_set - actual_set),
        "extra_targets": sorted(actual_set - expected_set),
        "duplicate_actual_targets": duplicate_actual,
        "order_is_canonical": actual == expected,
        "passed": (
            not unresolved
            and declared == len(actual)
            and actual == expected
            and not duplicate_actual),
        "contract": (
            f"indirect_dispatch 8179 {len(expected)} ptrcall "
            f"return:810D frame:3 targets:{','.join(expected)}"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asm", required=True, type=Path,
                        help="Routine_Macros_DKC1.asm")
    parser.add_argument("--sym", required=True, type=Path,
                        help="DKC1_U1.sym from the byte-exact build")
    parser.add_argument("--cfg", type=Path,
                        default=Path(__file__).resolve().parents[1] /
                        "recomp" / "bankbe.cfg")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--print-contract", action="store_true")
    args = parser.parse_args()

    result = audit(
        args.asm.read_text(encoding="utf-8", errors="replace"),
        args.sym.read_text(encoding="utf-8", errors="replace"),
        args.cfg.read_text(encoding="utf-8"))
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(rendered)
    if args.print_contract:
        print(result["contract"])
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
