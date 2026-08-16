#!/usr/bin/env python3
"""Generated state-machine catalog: how does each object actually work?

For every dispatch-table state machine the cfg contracts enumerate
(sprites, barrels, player), emits one section: its states (curated or
table-derived names), and per state conservative static evidence — literal
animation/sound references, immediate stores to SprEventFlags and the state
word, and spawn-routine references.

Pass --lifecycle TRACE to mark object-state ordinals actually observed at
runtime.  Static mining is conservative textual analysis over the instruction
index: it reports literal references and immediate stores, and is blind to
computed values and interprocedural effects.

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
import sync_names  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
ACTOR_EVENTS = {"actor_alloc", "actor_retype", "actor_state",
                "actor_sample"}


def load_observed_states(path: Path) -> dict[int, set[int]]:
    """Return sprite-id -> $1029 states from native actor lifecycle rows."""
    observed: dict[int, set[int]] = defaultdict(set)
    for line in path.read_text(encoding="utf-8",
                               errors="replace").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("schema") != "dkc1.lifecycle.v1" or \
                event.get("event") not in ACTOR_EVENTS:
            continue
        sprite_id = event.get("id")
        state = event.get("state")
        if isinstance(sprite_id, int) and isinstance(state, int):
            observed[sprite_id].add(state)
    return observed


def sprite_id_for_owner(owner: str) -> int | None:
    """Extract the actor id only for object-specific NorSprXX machines."""
    match = re.search(r"(?:^|_)NorSpr([0-9A-Fa-f]{2})(?:_|$)", owner)
    return int(match.group(1), 16) if match else None


def mine_function(body: list[dict]) -> dict:
    """Conservatively mine literal facts from one instruction body."""
    found = {"anims": set(), "events": set(), "sounds": set(),
             "spawns": 0, "state_stores": set()}
    accumulator_constant: int | None = None
    # Operations that overwrite or may overwrite A.  Stores, compares, flag
    # operations, and index-register operations intentionally preserve it.
    accumulator_clobbers = {
        "ADC", "AND", "ASL", "DEC", "EOR", "INC", "LDA", "LSR",
        "ORA", "PLA", "ROL", "ROR", "SBC", "TDC", "TSC", "TXA",
        "TYA", "XBA", "JSR", "JSL", "JMP", "JML", "RTS", "RTL",
        "RTI", "BRK", "COP",
    }
    for index, row in enumerate(body):
        if index and row.get("label"):
            # A local target may have predecessors that did not execute the
            # preceding textual LDA, so constants do not cross labels.
            accumulator_constant = None
        assembly = row["assembly"]
        for match in re.finditer(r"!Define_DKC1_AnimationID_(\w+)",
                                 assembly):
            found["anims"].add(match.group(1))
        for match in re.finditer(r"!Define_DKC1_SoundID_(\w+)", assembly):
            found["sounds"].add(match.group(1))
        if "SpawnFromTemplate" in assembly or "FDF346" in assembly:
            found["spawns"] += 1

        stripped = assembly.strip()
        opcode_match = re.match(r"([A-Z]{3})", stripped)
        opcode = opcode_match.group(1) if opcode_match else ""
        if opcode == "STA" and accumulator_constant is not None:
            if "1595" in assembly:
                found["events"].add(accumulator_constant)
            if "1029" in assembly or "CurrentState" in assembly:
                found["state_stores"].add(accumulator_constant)

        immediate = re.fullmatch(r"LDA\.[bw]\s+#\$([0-9A-Fa-f]+)",
                                 stripped)
        if immediate:
            accumulator_constant = int(immediate.group(1), 16)
        elif opcode in accumulator_clobbers:
            accumulator_constant = None
    return found


def render_catalog(rows: list[dict], code_names: dict[int, dict],
                   dispatches: list[dict],
                   observed: dict[int, set[int]]) -> tuple[str, int]:
    entry_of: dict[str, int] = {}
    rows_of: dict[str, list] = defaultdict(list)
    row_at: dict[int, dict] = {}
    for row in rows:
        address = atlas.row_address(row)
        entry_of.setdefault(row["function"], address)
        rows_of[row["function"]].append(row)
        row_at[address] = row
    function_at = {address: label for label, address in entry_of.items()}

    def name_of(address: int) -> str:
        entry = code_names.get(address)
        if entry and entry.get("name"):
            tag = entry.get("provenance", "curated")
            return f"{entry['name']} [{tag}]"
        return function_at.get(address, f"0x{address:06X}")

    lines = ["# State-machine catalog (generated)", "",
             "Regenerate static evidence: `python tools/state_catalog.py`. "
             "Add runtime evidence with `--lifecycle trace.jsonl`. Targets "
             "are listed in literal dispatch-table ordinal order; `(static)` "
             "facts are conservative textual references/immediate stores "
             "and do not cover computed or interprocedural values. "
             "`(observed)` is emitted only for matching native actor "
             "lifecycle rows and object-specific `NorSprXX` machines.", ""]
    machines = 0
    for dispatch in sorted(dispatches, key=lambda item: item["site"]):
        site = dispatch["site"]
        site_row = row_at.get(site)
        if site_row is None:
            continue
        owner = site_row["function"]
        curated_owner = code_names.get(entry_of.get(owner))
        if owner.startswith("CODE_") and not curated_owner:
            continue
        title = owner if not curated_owner else \
            curated_owner.get("name", owner)
        sprite_id = sprite_id_for_owner(owner) or \
            sprite_id_for_owner(title)
        observed_ordinals = observed.get(sprite_id, set()) \
            if sprite_id is not None else set()
        machines += 1
        lines.append(f"## {title}  (dispatch `0x{site:06X}`, "
                     f"{len(dispatch['targets'])} states, "
                     f"{dispatch['cfg']})")
        lines.append("")
        lines.append("| ordinal / state | runtime | anim refs (static) | "
                     "sound refs (static) | events→$1595 (static) | "
                     "state stores→$1029 (static) | spawns (static) |")
        lines.append("|---|---|---|---|---|---|---|")
        for ordinal, target in enumerate(dispatch["targets"]):
            target_label = function_at.get(target)
            mined = mine_function(rows_of.get(target_label, []))
            runtime = f"(observed: sprite ${sprite_id:02X})" \
                if ordinal in observed_ordinals else "-"
            lines.append(
                "| ${:02X}: {} | {} | {} | {} | {} | {} | {} |".format(
                    ordinal, name_of(target), runtime,
                    ", ".join(sorted(mined["anims"])[:4]) or "-",
                    ", ".join(sorted(mined["sounds"])[:3]) or "-",
                    ", ".join(f"${value:02X}" for value in
                              sorted(mined["events"])) or "-",
                    ", ".join(f"${value:02X}" for value in
                              sorted(mined["state_stores"])[:6]) or "-",
                    mined["spawns"] or "-"))
        lines.append("")
    return "\n".join(lines).rstrip() + "\n", machines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=REPO / "docs" / "STATE_MACHINES.md")
    parser.add_argument("--lifecycle", type=Path,
                        help="lifecycle trace for observed-state marking")
    args = parser.parse_args()

    if args.lifecycle and not args.lifecycle.is_file():
        parser.error(f"lifecycle trace not found: {args.lifecycle}")
    try:
        sync_names.ensure_safe_output(args.out)
    except ValueError as exc:
        parser.error(str(exc))

    rows = list(atlas.iter_instruction_index())
    code_names, _ = sync_names.load_name_maps()
    observed = load_observed_states(args.lifecycle) if args.lifecycle else {}
    rendered, machines = render_catalog(
        rows, code_names, atlas.load_dispatches(), observed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(rendered, encoding="utf-8")
    print(f"{machines} state machines -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
