#!/usr/bin/env python3
"""Canonical symbol database — GENERATED, one record per function.

Consolidates every validated source we hold into one machine-readable
file (build/ir/symbols.json), so names, widths, effects, and evidence
stop drifting between tools:

  - curated / table-derived names + provenance (rename_map,
    derived_names — rename_map stays the ONLY hand-edited input)
  - proven entry/exit M/X widths (recomp cfg facts)
  - read/write sets with symbolic names, honest indirect counts
    (IR stage-4 summaries)
  - static callers (inverted call + dispatch-target edges)
  - dispatch roles with contract provenance
  - runtime evidence: which profile-corpus routes executed it
  - differential-oracle eligibility (oracle_specs)

Everything here is derived; regenerate after any source changes:
  python tools/ir/summarize.py && python tools/oracle_spec.py --emit-all
  python tools/gen_symbols.py

usage: python tools/gen_symbols.py [--show BBA849]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import decode  # noqa: E402

REPO = TOOLS.parent
OUT = REPO / "build" / "ir" / "symbols.json"
SUMMARIES = REPO / "build" / "ir" / "summaries.json"
ORACLE = REPO / "build" / "ir" / "oracle_specs.json"
PROFILES = REPO / "build" / "profiles"


def mode_string(mx) -> str | None:
    if mx is None:
        return None
    m, x = mx
    part = lambda bit, reg: f"{reg}{'?' if bit is None else 16 - 8 * bit}"
    return part(m, "M") + part(x, "X")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--show", help="print one record (hex address)")
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()

    try:
        summaries = json.loads(SUMMARIES.read_text())["functions"]
    except OSError:
        raise SystemExit(f"missing {SUMMARIES}; run tools/ir/summarize.py")
    try:
        oracle = json.loads(ORACLE.read_text())["functions"]
    except OSError:
        oracle = {}

    facts = decode.load_func_facts()
    code_names, _ = atlas.load_rename_map()
    dispatches = atlas.load_dispatches()

    entry_of = {name: int(s["entry"], 16) for name, s in summaries.items()}
    label_of_entry = {v: k for k, v in entry_of.items()}

    # invert call + dispatch edges into callers
    callers: dict[str, set[str]] = {name: set() for name in summaries}
    for name, s in summaries.items():
        for target_hex in list(s["calls"]) + list(s["dispatch_targets"]):
            callee = label_of_entry.get(int(target_hex, 16))
            if callee:
                callers[callee].add(name)

    # dispatch roles by function
    dispatch_roles: dict[str, list[dict]] = {}
    for dispatch in dispatches:
        for name, entry in entry_of.items():
            if entry == dispatch["site"] or entry in dispatch["targets"]:
                dispatch_roles.setdefault(name, []).append({
                    "role": "site" if entry == dispatch["site"]
                    else "target",
                    "site": f"0x{dispatch['site']:06X}",
                    "kind": dispatch["kind"],
                    "cfg": dispatch["cfg"],
                })

    # runtime evidence from the profile corpus
    executed: dict[int, dict[str, int]] = {}
    for profile in sorted(PROFILES.glob("*.profile.jsonl")):
        route = profile.stem.replace(".profile", "")
        for line in profile.read_text(errors="replace").splitlines():
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            executed.setdefault(int(row["pc24"], 16), {})[route] = \
                row.get("calls", 0)

    def access_names(bucket: list[dict]) -> list[str]:
        seen = []
        for access in bucket:
            text = access.get("sym") or access["ea"]
            if text not in seen:
                seen.append(text)
        return seen

    records = {}
    for name, s in sorted(summaries.items()):
        entry = entry_of[name]
        fact = facts.get(entry)
        curated = code_names.get(entry)
        spec = oracle.get(name, {})
        records[name] = {
            "label": name,
            "address": f"0x{entry:06X}",
            "name": curated.get("name") if curated else None,
            "name_provenance": (curated.get("provenance") or "curated")
            if curated and curated.get("name") else None,
            "desc": (curated.get("desc") or None) if curated else None,
            "entry_mode": mode_string(fact["entry_mx"]) if fact else None,
            "exit_mode": mode_string(fact["exit_mx"])
            if fact and fact.get("exit_mx") else None,
            "reads": access_names(s["reads"]),
            "writes": access_names(s["writes"]),
            "indirect_reads": s["indirect_reads"],
            "indirect_writes": s["indirect_writes"],
            "calls": sorted({label_of_entry.get(int(c, 16), c)
                             for c in s["calls"]}),
            "callers": sorted(callers[name]),
            "dispatch": dispatch_roles.get(name, []),
            "executed_in": executed.get(entry, {}),
            "oracle": spec.get("eligibility"),
            "oracle_blockers": spec.get("blockers", []),
        }

    payload = {
        "schema": "dkc1.symbols.v1",
        "note": ("GENERATED — regenerate via tools/gen_symbols.py after "
                 "summaries/oracle/profile updates. rename_map.json is "
                 "the only hand-edited name source; edit names there."),
        "functions": records,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=1))

    named = sum(1 for r in records.values() if r["name"])
    ready = sum(1 for r in records.values()
                if r["oracle"] == "oracle-ready")
    seen_runtime = sum(1 for r in records.values() if r["executed_in"])
    print(f"{len(records)} functions -> {args.out}")
    print(f"  named: {named} ({named * 100 // len(records)}%), "
          f"runtime-evidenced: {seen_runtime}, oracle-ready: {ready}")

    if args.show:
        addr = int(args.show, 16)
        label = label_of_entry.get(addr)
        if label is None:
            print(f"no function entry at 0x{addr:06X}")
            return 1
        print(json.dumps(records[label], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
