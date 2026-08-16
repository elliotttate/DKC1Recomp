#!/usr/bin/env python3
"""Derive names mechanically, with provenance — never overwrite curation.

Sources of truth, in precedence order:
  1. curated  — Tools/IDA/work/rename_map.json (human-written; immutable
                here)
  2. source-defined — semantic labels already present in the disassembly
                (e.g. DKC1_NorSpr22_SteelKeg_Main)
  3. table-derived — this tool: unnamed dispatch-contract targets inside a
                named function become <Base>_StateN (state machines run
                through JMP (table,x) sites our cfg contracts enumerate)

Output goes to derived_names.json BESIDE the curated map — reviewable,
regenerable, and consumed by atlas/structure as a fallback layer with the
provenance shown. Nothing here ever mutates rename_map.json.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402

OUT = atlas.DISASM_ROOT / "Tools" / "IDA" / "work" / "derived_names.json"


def main() -> int:
    rows = list(atlas.iter_instruction_index())
    code_names, _ = atlas.load_rename_map()
    curated_addresses = set(code_names)

    # function label per address (disassembly's own semantic labels)
    function_of = {}
    label_semantic = {}
    for r in rows:
        addr = atlas.row_address(r)
        function_of.setdefault(r["function"], addr if
                               r["function"].startswith("CODE_") is False or
                               True else addr)
    # map function label -> entry address (first row of the function)
    entry_of: dict[str, int] = {}
    for r in rows:
        entry_of.setdefault(r["function"], atlas.row_address(r))
    for label, addr in entry_of.items():
        if not label.startswith("CODE_"):
            label_semantic[addr] = label

    # dispatch sites -> containing function name (curated > source label)
    derived: dict[str, dict] = {}
    for dispatch in atlas.load_dispatches():
        site = dispatch["site"]
        site_function = None
        best = None
        for label, addr in entry_of.items():
            if addr <= site and (best is None or addr > best):
                rows_of = None  # containment via function column instead
        # containment via the row at the site address
        for r in rows:
            if atlas.row_address(r) == site:
                site_function = r["function"]
                break
        if site_function is None:
            continue
        site_entry = entry_of.get(site_function)
        base = None
        provenance_base = None
        if site_entry in curated_addresses and \
                code_names[site_entry].get("name"):
            base = code_names[site_entry]["name"]
            provenance_base = "curated"
        elif not site_function.startswith("CODE_"):
            base = site_function.replace("DKC1_", "")
            provenance_base = "source-defined"
        if not base:
            continue
        base = base.replace("_Main", "")
        for ordinal, target in enumerate(sorted(set(dispatch["targets"]))):
            if target in curated_addresses:
                continue  # curation wins, always
            key = f"0x{target:06X}"
            if key in derived:
                continue  # first (lowest site) attribution wins
            derived[key] = {
                "ea": key,
                "name": f"{base}_State{ordinal}",
                "provenance": "table-derived",
                "confidence": "inferred-order",
                "via_site": f"0x{site:06X}",
                "base_from": provenance_base,
                "cfg": dispatch["cfg"],
            }

    OUT.write_text(json.dumps(
        {"schema": "dkc1.derived-names.v1",
         "note": "mechanically derived; curated rename_map.json always "
                 "takes precedence; regenerate with tools/sync_names.py",
         "code": sorted(derived.values(), key=lambda e: e["ea"])},
        indent=1))
    print(f"{len(derived)} derived names -> {OUT}")
    bases = {}
    for e in derived.values():
        stem = e["name"].rsplit("_State", 1)[0]
        bases[stem] = bases.get(stem, 0) + 1
    for stem, count in sorted(bases.items(), key=lambda kv: -kv[1])[:12]:
        print(f"  {stem}: {count} states")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
