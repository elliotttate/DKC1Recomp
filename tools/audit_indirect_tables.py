#!/usr/bin/env python3
"""Audit source-backed snesrecomp indirect-dispatch table contracts.

This complements ``audit_animation_dispatch.py``.  It finds cfg dispatchers
whose byte-exact assembly body jumps through an explicit ``DATA_*`` table,
extracts every symbolic ``dw`` target from that table, resolves those symbols
through the Asar symbol file, and requires the cfg allowlist to match exactly.
Dispatchers fed through RAM or computed pointers are reported as unproven;
they are never called complete merely because a route happened to run.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re


CFG_RE = re.compile(
    r"^indirect_dispatch\s+([0-9A-Fa-f]{4})\s+(\d+)\s+"
    r"(ptrtail|ptrcall)(?:\s+.*?)?\s+targets:(\S+)$")
LABEL_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*):(?:\s*;.*)?\s*$")
TABLE_JUMP_RE = re.compile(
    r"\bJMP\.\w+\s+\((DATA_[A-Za-z0-9_]+)(?:,x)?\)", re.I)
SYMBOL_RE = re.compile(
    r"(?:^|:)([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+(\S+)\s*$")
WORD_RE = re.compile(r"^\s*dw\s+(.+)$", re.I)
SYMBOL_OPERAND_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)")

# A few dispatch opcodes jump through a DP pointer populated by a nearby,
# source-explicit table. Keep these links explicit and reviewable instead of
# pretending a generic data-flow analysis proved them.
TABLE_HINTS = {
    "CODE_8086CC": ["DATA_BFFD60"],
    "CODE_80CF4C": ["DATA_80CF4F"],
    "CODE_81800A": ["DATA_818C4A"],
    "CODE_818448": ["DATA_81844B", "DATA_8184C9"],
}
ANIMATION_RECORD_CALLBACK_RE = re.compile(
    r"^\s*dw\s+(CODE_[A-Fa-f0-9]+),\$000[12],", re.M)


def parse_symbols(text: str) -> dict[str, str]:
    symbols: dict[str, str] = {}
    for line in text.splitlines():
        match = SYMBOL_RE.search(line)
        if match:
            symbols[match.group(3)] = (
                match.group(1) + match.group(2)).upper()
    return symbols


def parse_blocks(assembly: str) -> dict[str, list[str]]:
    blocks: dict[str, list[str]] = {}
    current: str | None = None
    for line in assembly.splitlines():
        match = LABEL_RE.match(line)
        if match:
            current = match.group(1)
            blocks[current] = []
        elif current is not None:
            blocks[current].append(line)
    return blocks


def cfg_bank(path: Path) -> str:
    match = re.fullmatch(r"bank([0-9a-fA-F]{2})\.cfg", path.name)
    if not match:
        raise ValueError(f"unsupported cfg filename {path.name!r}")
    return match.group(1).upper()


def parse_contracts(path: Path) -> list[dict]:
    bank = cfg_bank(path)
    records = []
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        match = CFG_RE.match(line.strip())
        if not match:
            continue
        records.append({
            "cfg": path.name,
            "line": line_number,
            "bank": bank,
            "address": match.group(1).upper(),
            "declared": int(match.group(2)),
            "kind": match.group(3),
            "targets": match.group(4).upper().split(","),
        })
    return records


def table_targets(lines: list[str], symbols: dict[str, str]) -> tuple[
        list[str], list[str]]:
    targets: list[str] = []
    unresolved: list[str] = []
    for line in lines:
        match = WORD_RE.match(line)
        if not match:
            if line.strip() and not line.lstrip().startswith(";"):
                break
            continue
        operands = match.group(1).split(";", 1)[0]
        for operand in operands.split(","):
            token_match = SYMBOL_OPERAND_RE.match(operand)
            if token_match is None:
                continue
            token = token_match.group(1)
            if token in symbols:
                targets.append(symbols[token])
            else:
                unresolved.append(token)
    return targets, unresolved


def containing_source_label(bank: str, address: str, blocks: dict[str, list[str]],
                            symbols: dict[str, str]) -> str | None:
    """Find the source block containing an opcode address from the sym map."""
    target = int(bank + address, 16)
    candidates = []
    for name in blocks:
        resolved = symbols.get(name)
        if resolved is None or not resolved.startswith(bank):
            continue
        value = int(resolved, 16)
        if value <= target:
            candidates.append((value, name))
    return max(candidates)[1] if candidates else None


def audit(assembly: str, symbol_text: str,
          contracts: list[dict]) -> dict:
    symbols = parse_symbols(symbol_text)
    blocks = parse_blocks(assembly)
    results = []
    for contract in contracts:
        requested_label = f"CODE_{contract['bank']}{contract['address']}"
        label = requested_label if requested_label in blocks else \
            containing_source_label(contract["bank"], contract["address"],
                                    blocks, symbols)
        body = blocks.get(label) if label is not None else None
        record = dict(contract)
        record["label"] = label
        record["requested_label"] = requested_label
        if body is None:
            record.update(status="unproven", reason="dispatcher_label_missing")
            results.append(record)
            continue
        jump = next((TABLE_JUMP_RE.search(line) for line in body
                     if TABLE_JUMP_RE.search(line)), None)
        tables = TABLE_HINTS.get(requested_label)
        if tables is None and jump is not None:
            tables = [jump.group(1)]
        record_callbacks = None
        if requested_label == "CODE_BCF0A9":
            record_callbacks = ANIMATION_RECORD_CALLBACK_RE.findall(assembly)
        if tables is None:
            if record_callbacks is None:
                record.update(
                    status="unproven", reason="not_explicit_data_table")
                results.append(record)
                continue
            tables = []
        missing_tables = [table for table in tables if table not in blocks]
        if missing_tables:
            record.update(status="failed", reason="table_label_missing",
                          tables=tables, missing_tables=missing_tables)
            results.append(record)
            continue
        expected = []
        unresolved = []
        if record_callbacks is not None:
            for callback in record_callbacks:
                if callback in symbols:
                    expected.append(symbols[callback])
                else:
                    unresolved.append(callback)
        else:
            for table in tables:
                table_expected, table_unresolved = table_targets(
                    blocks[table], symbols)
                expected.extend(table_expected)
                unresolved.extend(table_unresolved)
        # The cfg is an authorization set. Preserve the source order in the
        # report, but compare unique addresses because tables may alias one
        # implementation from multiple state values.
        expected_unique = list(dict.fromkeys(expected))
        actual = contract["targets"]
        duplicate_actual = sorted(
            target for target in set(actual) if actual.count(target) > 1)
        missing = sorted(set(expected_unique) - set(actual))
        extra = sorted(set(actual) - set(expected_unique))
        passed = (not unresolved and not duplicate_actual and
                  contract["declared"] == len(actual) and
                  not missing and not extra)
        record.update(
            status="passed" if passed else "failed",
            proof=("animation_record_callbacks" if record_callbacks is not None
                   else "explicit_tables"),
            tables=tables,
            source_target_count=len(expected_unique),
            actual_target_count=len(actual),
            unresolved_symbols=sorted(set(unresolved)),
            duplicate_actual_targets=duplicate_actual,
            missing_targets=missing,
            extra_targets=extra,
        )
        results.append(record)
    counts = {status: sum(r["status"] == status for r in results)
              for status in ("passed", "failed", "unproven")}
    return {
        "schema": "dkc1.indirect-table-audit.v1",
        "contracts": len(results),
        "counts": counts,
        "passed": counts["failed"] == 0,
        "results": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asm", required=True, type=Path)
    parser.add_argument("--sym", required=True, type=Path)
    parser.add_argument("--cfg-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] / "recomp")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    contracts = []
    for path in sorted(args.cfg_dir.glob("bank??.cfg")):
        contracts.extend(parse_contracts(path))
    result = audit(
        args.asm.read_text(encoding="utf-8", errors="replace"),
        args.sym.read_text(encoding="utf-8", errors="replace"), contracts)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8", newline="\n")
    else:
        print(rendered, end="")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
