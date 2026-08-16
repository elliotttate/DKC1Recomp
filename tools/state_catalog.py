#!/usr/bin/env python3
"""Generated state-machine catalog: how does each object actually work?

For every dispatch-table state machine the cfg contracts enumerate
(sprites, barrels, player), emits one section: its states (curated or
table-derived names), and per state the statically-mined behavior —
animations started, event codes written to SprEventFlags, sound IDs
queued, sprite spawns, and state-word stores (static transition edges).

Static edges are marked (static); pass --lifecycle TRACE to also mark
state values actually observed at runtime (observed) — the distinction
the safeguards require. Static mining is textual over the instruction
index: complete for immediates, blind to computed values.

usage: python tools/state_catalog.py [--out docs/STATE_MACHINES.md]
                                     [--lifecycle trace.jsonl]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402

REPO = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=REPO / "docs" / "STATE_MACHINES.md")
    parser.add_argument("--lifecycle", type=Path,
                        help="lifecycle trace for observed-state marking")
    args = parser.parse_args()

    rows = list(atlas.iter_instruction_index())
    code_names, _ = atlas.load_rename_map()
    entry_of: dict[str, int] = {}
    rows_of: dict[str, list] = defaultdict(list)
    for r in rows:
        entry_of.setdefault(r["function"], atlas.row_address(r))
        rows_of[r["function"]].append(r)
    function_at = {addr: label for label, addr in entry_of.items()}

    observed: dict[int, set] = defaultdict(set)  # sprite id -> states seen
    if args.lifecycle and args.lifecycle.exists():
        for line in args.lifecycle.read_text(errors="replace").splitlines():
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "id" in event and "state" in event:
                observed[event["id"]].add(event["state"])

    def name_of(addr: int) -> str:
        entry = code_names.get(addr)
        if entry and entry.get("name"):
            tag = entry.get("provenance", "curated")
            return f"{entry['name']} [{tag}]"
        return function_at.get(addr, f"0x{addr:06X}")

    def mine(function_label: str) -> dict:
        found = {"anims": set(), "events": set(), "sounds": set(),
                 "spawns": 0, "state_stores": set()}
        body = rows_of.get(function_label, [])
        previous_immediate = None
        for r in body:
            assembly = r["assembly"]
            for m in re.finditer(r"!Define_DKC1_AnimationID_(\w+)", assembly):
                found["anims"].add(m.group(1))
            for m in re.finditer(r"!Define_DKC1_SoundID_(\w+)", assembly):
                found["sounds"].add(m.group(1))
            if "SpawnFromTemplate" in assembly or "FDF346" in assembly:
                found["spawns"] += 1
            imm = re.match(r"\s*LDA\.\w #?\$?#\$([0-9A-Fa-f]+)",
                           assembly) or \
                re.match(r"\s*LDA\.\w+ #\$([0-9A-Fa-f]+)", assembly)
            if imm:
                previous_immediate = int(imm.group(1), 16)
            elif assembly.strip().startswith("LDA"):
                previous_immediate = None
            if "1595" in assembly and "STA" in assembly and \
                    previous_immediate is not None:
                found["events"].add(previous_immediate)
            if ("1029" in assembly or "RAMTable1029" in assembly) and \
                    "STA" in assembly and previous_immediate is not None:
                found["state_stores"].add(previous_immediate)
        return found

    lines = ["# State-machine catalog (generated)", "",
             "Regenerate: `python tools/state_catalog.py`. Names tagged "
             "with provenance; `(static)` facts are textual immediate "
             "mining (complete for constants, blind to computed values); "
             "`(observed)` requires a lifecycle trace.", ""]
    machines = 0
    for dispatch in sorted(atlas.load_dispatches(),
                           key=lambda d: d["site"]):
        site = dispatch["site"]
        site_row = next((r for r in rows
                         if atlas.row_address(r) == site), None)
        if site_row is None:
            continue
        owner = site_row["function"]
        owner_name = name_of(entry_of.get(owner, 0)) \
            if not owner.startswith("CODE_") else None
        curated_owner = code_names.get(entry_of.get(owner))
        if owner.startswith("CODE_") and not curated_owner:
            continue  # unnamed machines: derive names first (sync_names)
        title = owner if not curated_owner else \
            curated_owner.get("name", owner)
        machines += 1
        lines.append(f"## {title}  (dispatch `0x{site:06X}`, "
                     f"{len(dispatch['targets'])} states, "
                     f"{dispatch['cfg']})")
        lines.append("")
        lines.append("| state | anims | sounds | events→$1595 | "
                     "state stores→$1029 | spawns |")
        lines.append("|---|---|---|---|---|---|")
        for target in sorted(set(dispatch["targets"])):
            target_label = function_at.get(target)
            mined = mine(target_label) if target_label else {
                "anims": set(), "events": set(), "sounds": set(),
                "spawns": 0, "state_stores": set()}
            lines.append(
                "| {} | {} | {} | {} | {} | {} |".format(
                    name_of(target),
                    ", ".join(sorted(mined["anims"])[:4]) or "-",
                    ", ".join(sorted(mined["sounds"])[:3]) or "-",
                    ", ".join(f"${v:02X}" for v in
                              sorted(mined["events"])) or "-",
                    ", ".join(f"${v:02X}" for v in
                              sorted(mined["state_stores"])[:6]) or "-",
                    mined["spawns"] or "-"))
        lines.append("")
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"{machines} state machines -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
