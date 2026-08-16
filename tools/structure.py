#!/usr/bin/env python3
"""Symbolized, readable listing for a function — a DISPLAY AID.

Upgrades the mechanical per-instruction lift with everything we know:
curated RAM names in operands, Misc_Defines constants annotated on
immediates, local branch labels, post-branch indentation, and safe
load/store pairing hints. Every output line remains 1:1 with an exact
assembly line shown alongside — this tool performs NO semantic
transformation and makes no equivalence claims. (A real 65816 SSA IR is
the planned foundation for anything stronger; see docs/ROADMAP notes.)

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
    _, ram_names = atlas.load_rename_map()
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
    code_names, _ = atlas.load_rename_map()
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--width", type=int, default=44,
                        help="pseudo column width")
    args = parser.parse_args()

    function, rows, code_names = resolve_function(args.query)
    func_rows = [r for r in rows if r["function"] == function]
    defines = load_defines()
    ram_friendly = friendly_ram_names()

    func_addr = int(function[5:], 16) if function.startswith("CODE_") else None
    entry = code_names.get(func_addr) if func_addr else None

    # local labels for in-function branch targets
    addresses = {atlas.row_address(r) for r in func_rows}
    targets = {}
    for r in func_rows:
        for m in re.finditer(r"CODE_([0-9A-Fa-f]{6})", r["assembly"]):
            target = int(m.group(1), 16)
            if target in addresses and target != atlas.row_address(func_rows[0]):
                targets.setdefault(target, f".L{len(targets)}")

    print(f";; {function}" + (f"  == {entry['name']}" if entry else ""))
    if entry and entry.get("desc"):
        print(f";; {entry['desc']}")
    print(";; display aid: symbolized 1:1 with assembly -- "
          "no semantic transformation\n")

    indent = 1
    for r in func_rows:
        addr = atlas.row_address(r)
        if addr in targets:
            print(f"{targets[addr]}:")
            indent = 1
        pseudo = r["pseudocode"]
        assembly = r["assembly"]

        # friendly RAM names
        for label, name in ram_friendly.items():
            if label in pseudo:
                pseudo = pseudo.replace(label, name)

        # local branch labels in pseudo
        for target, label in targets.items():
            pseudo = pseudo.replace(f"CODE_{target:06X}", label)
            assembly_label = f"CODE_{target:06X}"
            if assembly_label in assembly:
                assembly = assembly.replace(assembly_label,
                                            label + f"/*{target:06X}*/")

        # annotate immediates that match known defines
        note = ""
        imm = re.search(r"#\$([0-9A-Fa-f]{2,4})", r["assembly"])
        if imm:
            value = int(imm.group(1), 16)
            is_compare = re.match(r"\s*(CMP|CPX|CPY)", r["assembly"].strip())
            # tiny immediates match dozens of unrelated defines; only
            # annotate compares (identity checks) or distinctive values.
            if is_compare or value >= 0x10:
                names = defines.get(value, [])
                if names and (is_compare or len(names) <= 2):
                    note = "  ; " + " | ".join(names[:2])

        line = f"    {pseudo:<{args.width}} ; {r['address']}  {assembly}"
        print(line + note)
        if re.match(r"if \(", pseudo):
            indent = 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
