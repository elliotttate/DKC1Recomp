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

Output goes to docs/derived_names.json in this repository — reviewable,
regenerable, and outside the read-only reference tree.  The understanding
tools consume it as a fallback layer with the provenance shown.  Nothing
here ever mutates rename_map.json or any other reference input.
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

REPO = TOOLS.parent
OUT = REPO / "docs" / "derived_names.json"
REFERENCE = REPO / "reference"
READ_ONLY_ROOTS = (REFERENCE, atlas.DISASM_ROOT)


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def ensure_safe_output(path: Path) -> None:
    """Reject generated output under any immutable source-material root."""
    for root in READ_ONLY_ROOTS:
        if _is_within(path, root):
            raise ValueError(f"refusing to write generated output under "
                             f"read-only {root}: {path}")


def load_name_maps(
        curated_path: Path = atlas.RENAME_MAP,
        derived_path: Path | None = OUT
        ) -> tuple[dict[int, dict], dict[int, dict]]:
    """Load immutable curation, then merge the safe derived-name layer."""
    try:
        curated = json.loads(curated_path.read_text(encoding="utf-8"))
    except OSError:
        curated = {}
    code = {int(entry["ea"], 16): entry
            for entry in curated.get("code", [])}
    ram = {int(entry["ea"], 16) & 0x1FFFF: entry
           for entry in curated.get("ram", [])}
    if derived_path is not None:
        try:
            derived = json.loads(derived_path.read_text(encoding="utf-8"))
        except OSError:
            derived = {}
        for entry in derived.get("code", []):
            code.setdefault(int(entry["ea"], 16), entry)
    return code, ram


def derive_names(rows: list[dict], code_names: dict[int, dict],
                 dispatches: list[dict]) -> dict[str, dict]:
    """Derive target names using each dispatch contract's literal ordinal."""
    entry_of: dict[str, int] = {}
    function_at: dict[int, str] = {}
    for row in rows:
        address = atlas.row_address(row)
        entry_of.setdefault(row["function"], address)
        function_at[address] = row["function"]

    derived: dict[str, dict] = {}
    for dispatch in sorted(dispatches, key=lambda item: item["site"]):
        site = dispatch["site"]
        site_function = function_at.get(site)
        if site_function is None:
            continue
        site_entry = entry_of.get(site_function)
        base = None
        provenance_base = None
        curated_owner = code_names.get(site_entry)
        if curated_owner and curated_owner.get("name"):
            base = curated_owner["name"]
            provenance_base = "curated"
        elif not site_function.startswith("CODE_"):
            base = site_function.removeprefix("DKC1_")
            provenance_base = "source-defined"
        if not base:
            continue
        base = base.removesuffix("_Main")

        # Contract target order is dispatch-table order.  Address sorting (or
        # set conversion) silently renumbers states when handlers are laid out
        # in a different order, so enumerate the literal list.
        for ordinal, target in enumerate(dispatch["targets"]):
            if target in code_names:
                continue  # curation wins, always
            key = f"0x{target:06X}"
            existing = derived.get(key)
            if existing:
                if existing["via_site"] == f"0x{site:06X}":
                    existing.setdefault("state_ordinals",
                                        [existing["state_ordinal"]])
                    if ordinal not in existing["state_ordinals"]:
                        existing["state_ordinals"].append(ordinal)
                continue  # first dispatch-site attribution wins
            derived[key] = {
                "ea": key,
                "name": f"{base}_State{ordinal}",
                "provenance": "table-derived",
                "confidence": "dispatch-table-ordinal",
                "state_ordinal": ordinal,
                "via_site": f"0x{site:06X}",
                "base_from": provenance_base,
                "cfg": dispatch["cfg"],
            }
    return derived


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=OUT,
                        help=f"output JSON (default: {OUT})")
    args = parser.parse_args()
    try:
        ensure_safe_output(args.out)
    except ValueError as exc:
        parser.error(str(exc))

    rows = list(atlas.iter_instruction_index())
    code_names, _ = load_name_maps(derived_path=None)
    derived = derive_names(rows, code_names, atlas.load_dispatches())

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(
        {"schema": "dkc1.derived-names.v1",
         "note": "mechanically derived; curated rename_map.json always "
                 "takes precedence; regenerate with tools/sync_names.py",
         "code": sorted(derived.values(), key=lambda e: e["ea"])},
        indent=1) + "\n", encoding="utf-8")
    print(f"{len(derived)} derived names -> {args.out}")
    bases = {}
    for e in derived.values():
        stem = e["name"].rsplit("_State", 1)[0]
        bases[stem] = bases.get(stem, 0) + 1
    for stem, count in sorted(bases.items(), key=lambda kv: -kv[1])[:12]:
        print(f"  {stem}: {count} states")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
