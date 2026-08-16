#!/usr/bin/env python3
"""WRAM-ownership + routine conflict checker for mod declarations.

A mod declaration is a small JSON file:
  {
    "id": "dkc1.example",
    "class": "presentation" | "gameplay",
    "replaces": ["BFA0F7", "Player_HandleHitEvents", ...]
  }

Checks, all IR-evidence based (build/ir/symbols.json):
  - routine conflicts: two mods replacing the same function;
  - WRAM ownership conflicts: intersecting write sets between mods
    (call-closed at declaration level: the direct write sets of the
    replaced functions);
  - presentation-class violations: a presentation mod may not replace
    any function whose write set touches gameplay WRAM at all — margin
    rendering belongs host-side, not in replaced game routines. This is
    the same discipline the widescreen work already proves with
    byte-identical WRAM A/B runs;
  - oracle eligibility: replacing a function that is not oracle-ready
    is flagged (its replacement cannot be validated by state diff).

usage: python tools/mod_conflicts.py mods/a.json mods/b.json ...
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
SYMBOLS = REPO / "build" / "ir" / "symbols.json"


def resolve_function(query: str, symbols: dict) -> str | None:
    query_upper = query.upper()
    if f"CODE_{query_upper}" in symbols:
        return f"CODE_{query_upper}"
    if query in symbols:
        return query
    for label, record in symbols.items():
        if (record.get("name") or "").lower() == query.lower():
            return label
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mods", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        symbols = json.loads(SYMBOLS.read_text())["functions"]
    except OSError:
        sys.exit(f"missing {SYMBOLS}; run tools/gen_symbols.py first")

    mods = []
    problems = 0
    for path in args.mods:
        declaration = json.loads(path.read_text())
        mod_class = declaration.get("class", "gameplay")
        if mod_class not in ("presentation", "gameplay"):
            print(f"ERROR {declaration.get('id', path.name)}: unknown "
                  f"class {mod_class!r}")
            problems += 1
            continue
        resolved = {}
        for query in declaration.get("replaces", []):
            label = resolve_function(str(query), symbols)
            if label is None:
                print(f"ERROR {declaration['id']}: unknown function "
                      f"{query!r}")
                problems += 1
                continue
            resolved[label] = symbols[label]
        mods.append({"id": declaration.get("id", path.name),
                     "class": mod_class, "functions": resolved})

    # routine conflicts
    owners: dict[str, list[str]] = {}
    for mod in mods:
        for label in mod["functions"]:
            owners.setdefault(label, []).append(mod["id"])
    for label, ids in sorted(owners.items()):
        if len(ids) > 1:
            print(f"ROUTINE CONFLICT: {label} replaced by "
                  f"{', '.join(ids)}")
            problems += 1

    # WRAM ownership conflicts between mods (symbolic write sets)
    for i, a in enumerate(mods):
        writes_a = {w for r in a["functions"].values()
                    for w in r["writes"]}
        for b in mods[i + 1:]:
            writes_b = {w for r in b["functions"].values()
                        for w in r["writes"]}
            shared = sorted(writes_a & writes_b)
            if shared:
                print(f"WRAM OVERLAP: {a['id']} and {b['id']} both "
                      f"write {', '.join(shared[:6])}"
                      + (f" +{len(shared) - 6} more"
                         if len(shared) > 6 else ""))
                problems += 1

    # per-mod checks
    for mod in mods:
        for label, record in sorted(mod["functions"].items()):
            display = record.get("name") or label
            if mod["class"] == "presentation" and (
                    record["writes"] or record["indirect_writes"]):
                print(f"PRESENTATION VIOLATION: {mod['id']} replaces "
                      f"{display}, which writes gameplay WRAM "
                      f"({', '.join(record['writes'][:4]) or 'indirect'})"
                      " — presentation belongs host-side")
                problems += 1
            if record.get("oracle") != "oracle-ready":
                print(f"WARNING: {mod['id']}: {display} is "
                      f"{record.get('oracle') or 'un-summarized'} — a "
                      "replacement cannot be validated by state diff "
                      f"({'; '.join(record.get('oracle_blockers', []))})")
            if not record["executed_in"]:
                print(f"WARNING: {mod['id']}: {display} has no runtime "
                      "evidence in the profile corpus — record a route "
                      "before replacing it")

    print(f"\n{len(mods)} mods checked: "
          f"{'OK' if problems == 0 else f'{problems} problem(s)'}")
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
