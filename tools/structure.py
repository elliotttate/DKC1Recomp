#!/usr/bin/env python3
"""Symbolized, readable listing for a function — a DISPLAY AID.

Upgrades the mechanical per-instruction lift with everything we know:
curated RAM names in operands, context-qualified Misc_Defines annotations,
and local control-flow labels.  Instruction rows deliberately remain flat:
this tool has no CFG/SSA basis for reconstructing block nesting.
Every output row remains 1:1 with its exact assembly line shown alongside —
this tool performs NO semantic transformation and makes no equivalence
claims. (A real 65816 SSA IR is the planned foundation for anything stronger;
see docs/SSA_IR_DESIGN.md.)

usage:  python tools/structure.py BFC745
        python tools/structure.py Player_HandleHitEvents
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402  (shares path resolution + loaders)
import sync_names  # noqa: E402


CONTEXT_NAMESPACES = (
    ("EntranceID", "EntranceID_"),
    ("Animation", "AnimationID_"),
    ("Sound", "SoundID_"),
    ("Music", "MusicID_"),
    ("LevelType", "LevelTypeID_"),
    ("LevelID", "LevelID_"),
    ("CurrentLevel", "LevelID_"),
    ("RAMTable0D45", "NorSpr"),
    ("SpriteID", "NorSpr"),
)


def load_defines() -> dict[int, list[str]]:
    """value -> define names from Misc_Defines_DKC1.asm."""
    path = atlas.DISASM_ROOT / "DKC1" / "Misc_Defines_DKC1.asm"
    values: dict[int, list[str]] = {}
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return values
    for match in re.finditer(
            r"^!Define_DKC1_(\S+)\s*=\s*\$([0-9A-Fa-f]+)", text, re.M):
        values.setdefault(int(match.group(2), 16), []).append(match.group(1))
    return values


def friendly_ram_names() -> dict[str, str]:
    """disassembly RAM label -> curated friendly name (when known)."""
    _, ram_names = sync_names.load_name_maps()
    label_to_addr = {}
    for addr, labels in atlas.load_ram_map_labels().items():
        for label in labels:
            label_to_addr[label] = addr
    mapping = {}
    for label, addr in label_to_addr.items():
        entry = ram_names.get(addr)
        if entry and entry.get("name"):
            mapping["!" + label] = entry["name"]
    return mapping


def resolve_function(query: str):
    rows = list(atlas.iter_instruction_index())
    code_names, _ = sync_names.load_name_maps()
    query_upper = query.upper()
    # by address
    if re.fullmatch(r"(0X)?[0-9A-F]{5,6}", query_upper):
        addr = int(query_upper.replace("0X", ""), 16)
        for r in rows:
            if atlas.row_address(r) == addr:
                return r["function"], rows, code_names
    # by curated or label name
    for ea, entry in code_names.items():
        if entry.get("name", "").lower() == query.lower():
            for r in rows:
                if atlas.row_address(r) == ea:
                    return r["function"], rows, code_names
    for r in rows:
        if r["function"].upper() == query_upper or \
                r["function"].upper() == "CODE_" + query_upper:
            return r["function"], rows, code_names
    sys.exit(f"function not found: {query}")


def contextual_define_names(rows: list[dict], index: int,
                            candidates: list[str]) -> list[str]:
    """Select one constant namespace only when nearby data flow names it.

    Numeric immediates are shared across many unrelated ID domains.  A raw
    value match is therefore not evidence.  This deliberately small heuristic
    only annotates a compare whose source load, or an immediate load whose
    nearby destination, contains an explicit namespace-bearing RAM label.
    """
    assembly = rows[index]["assembly"].strip()
    opcode_match = re.match(r"([A-Z]{3})", assembly)
    opcode = opcode_match.group(1) if opcode_match else ""
    context_rows: list[dict] = []
    if opcode in {"CMP", "CPX", "CPY"}:
        wanted_load = {"CMP": "LDA", "CPX": "LDX", "CPY": "LDY"}[opcode]
        for prior in reversed(rows[max(0, index - 5):index]):
            prior_opcode = prior["assembly"].strip()[:3]
            if prior_opcode == wanted_load:
                context_rows = [prior]
                break
            if prior.get("label"):
                break
    elif opcode == "LDA":
        for following in rows[index + 1:index + 5]:
            following_opcode = following["assembly"].strip()[:3]
            if following_opcode in {"STA", "JSR", "JSL"}:
                context_rows = [following]
                break
            if following.get("label") or following_opcode == "LDA":
                break
    if not context_rows:
        return []

    context = " ".join(
        f"{row.get('assembly', '')} {row.get('pseudocode', '')}"
        for row in context_rows)
    for marker, prefix in CONTEXT_NAMESPACES:
        if marker in context:
            matches = [name for name in candidates
                       if name.startswith(prefix)]
            return matches if len(matches) == 1 else []
    return []


def render_listing(function: str, rows: list[dict],
                   code_names: dict[int, dict],
                   defines: dict[int, list[str]],
                   ram_friendly: dict[str, str], width: int = 44) -> str:
    """Render flat, symbolized rows without implying control structure."""
    func_rows = [row for row in rows if row["function"] == function]
    if not func_rows:
        raise ValueError(f"function has no instruction rows: {function}")
    func_addr = atlas.row_address(func_rows[0])
    entry = code_names.get(func_addr)

    addresses = {atlas.row_address(row) for row in func_rows}
    targets: dict[int, str] = {}
    for row in func_rows:
        for match in re.finditer(r"CODE_([0-9A-Fa-f]{6})",
                                 row["assembly"]):
            target = int(match.group(1), 16)
            if target in addresses and target != func_addr:
                targets.setdefault(target, f".L{len(targets)}")

    output = [f";; {function}" +
              (f"  == {entry['name']}" if entry else "")]
    if entry and entry.get("desc"):
        output.append(f";; {entry['desc']}")
    output.extend([
        ";; display aid: flat, symbolized 1:1 instruction rows with exact asm",
        ";; local labels are cross-references, not reconstructed blocks",
        "",
    ])

    friendly_items = sorted(ram_friendly.items(),
                            key=lambda item: len(item[0]), reverse=True)
    for index, row in enumerate(func_rows):
        address = atlas.row_address(row)
        if address in targets:
            output.append(f"{targets[address]}:")
        pseudo = row["pseudocode"]
        for label, name in friendly_items:
            pseudo = pseudo.replace(label, name)
        for target, label in targets.items():
            pseudo = pseudo.replace(f"CODE_{target:06X}", label)

        note = ""
        immediate = re.search(r"#\$([0-9A-Fa-f]{2,4})", row["assembly"])
        if immediate:
            value = int(immediate.group(1), 16)
            names = contextual_define_names(
                func_rows, index, defines.get(value, []))
            if names:
                note = "  ; " + names[0]

        line = (f"    {pseudo:<{width}} ; {row['address']}  "
                f"{row['assembly']}")
        output.append(line + note)
    return "\n".join(output) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--width", type=int, default=44,
                        help="pseudo column width")
    args = parser.parse_args()

    function, rows, code_names = resolve_function(args.query)
    defines = load_defines()
    ram_friendly = friendly_ram_names()
    print(render_listing(function, rows, code_names, defines,
                         ram_friendly, args.width), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
