#!/usr/bin/env python3
"""IR stage gates. Nothing downstream consumes a stage that hasn't passed.

stage1: every instruction_index row decodes into a structured IROp; the
        computed length matches the listing's size column; the computed
        opcode byte matches the actual ROM byte (HiROM mirror fold).
stage2: CFG edge sanity (every branch target resolves to an instruction),
        M/X width propagation from the cfg entry facts must agree with
        every width-suffixed immediate in the corpus, SSA invariants
        (every use reaches a def or a function entry).
stage3: operand resolution coverage (resolved effective addresses and
        region classification percentages, honest residue listing).

usage: python tools/ir_validate.py --stage1 [--rom PATH]
       python tools/ir_validate.py --all --rom PATH
"""
from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import decode  # noqa: E402

DEFAULT_ROM = Path(r"D:\Downloads\DKLR\DKC-Recomp\DKC1_USA1.sfc")


def stage1(rom_path: Path | None) -> bool:
    rows = list(atlas.iter_instruction_index())
    rom = decode.load_rom(rom_path) if rom_path and rom_path.exists() \
        else None
    parse_fail: list[str] = []
    size_fail: list[str] = []
    byte_fail: list[str] = []
    unmapped = 0
    for row in rows:
        try:
            op = decode.decode_row(row)
        except Exception as exc:  # noqa: BLE001 — gate reports everything
            parse_fail.append(f"{row['address']} {row['assembly'][:60]!r}"
                              f" -> {exc}")
            continue
        if op.size != int(row["size"]):
            size_fail.append(f"{row['address']} {row['assembly'][:50]!r} "
                             f"computed {op.size} listing {row['size']}")
        if rom is not None:
            offset = decode.rom_offset(op.addr)
            if offset is None or offset >= len(rom):
                unmapped += 1
            elif rom[offset] != op.opcode:
                byte_fail.append(
                    f"{row['address']} {row['assembly'][:50]!r} computed "
                    f"{op.opcode:02X} rom {rom[offset]:02X}")

    print(f"stage1: {len(rows)} rows")
    print(f"  structured parse : {len(rows) - len(parse_fail)}"
          f" ({len(parse_fail)} failures)")
    print(f"  size column match: {len(rows) - len(parse_fail) - len(size_fail)}"
          f" ({len(size_fail)} mismatches)")
    if rom is not None:
        checked = len(rows) - len(parse_fail) - unmapped
        print(f"  ROM opcode byte  : {checked - len(byte_fail)}/{checked}"
              f" ({len(byte_fail)} mismatches, {unmapped} unmapped)")
    else:
        print("  ROM opcode byte  : SKIPPED (no ROM)")
    for name, fails in (("parse", parse_fail), ("size", size_fail),
                        ("byte", byte_fail)):
        for line in fails[:8]:
            print(f"    {name}: {line}")
        if len(fails) > 8:
            print(f"    ... {len(fails) - 8} more {name} failures")
    ok = not parse_fail and not size_fail and not byte_fail
    print(f"stage1: {'PASS' if ok else 'FAIL'}")
    return ok


def stage2() -> bool:
    import json
    from ir import cfg as ircfg  # noqa: E402
    from ir import ssa as irssa  # noqa: E402
    functions = decode.load_functions()
    facts = decode.load_func_facts()
    dispatches = {d["site"]: d for d in atlas.load_dispatches()}
    known = json.loads(
        (TOOLS / "ir" / "known_discrepancies.json").read_text())
    known_conflicts = {e["addr"] for e in known["width_conflicts"]}

    edge_fail: list[str] = []
    width_conflict: list[str] = []
    ssa_fail: list[str] = []
    stats = collections.Counter()
    for name, ops in sorted(functions.items()):
        graph = ircfg.build(name, ops, functions, dispatches)
        stats["functions"] += 1
        stats["blocks"] += len(graph.blocks)
        for problem in graph.problems:
            edge_fail.append(f"{name}: {problem}")
        ircfg.propagate_widths(graph, facts)
        stats["width_proven"] += graph.width_proven
        stats["width_assumed"] += graph.width_assumed
        stats["width_unknown"] += graph.width_unknown
        for conflict in graph.width_conflicts:
            if conflict.split()[0] in known_conflicts:
                stats["known_discrepancies"] += 1
            else:
                width_conflict.append(f"{name}: {conflict}")
        try:
            ssa = irssa.build(graph)
            stats["phis"] += ssa.phi_count
            for problem in ssa.problems:
                ssa_fail.append(f"{name}: {problem}")
        except Exception as exc:  # noqa: BLE001
            ssa_fail.append(f"{name}: SSA build raised {exc}")

    print(f"stage2: {stats['functions']} functions, {stats['blocks']} "
          f"blocks, {stats['phis']} phis")
    total_w = (stats["width_proven"] + stats["width_assumed"] +
               stats["width_unknown"]) or 1
    print(f"  width facts: {stats['width_proven']} proven, "
          f"{stats['width_assumed']} call-assumed, "
          f"{stats['width_unknown']} unknown "
          f"({100.0 * stats['width_unknown'] / total_w:.2f}% unknown)")
    print(f"  immediate-suffix width conflicts: {len(width_conflict)} "
          f"(+{stats['known_discrepancies']} documented listing "
          f"discrepancies, see tools/ir/known_discrepancies.json)")
    print(f"  CFG problems: {len(edge_fail)}")
    print(f"  SSA problems: {len(ssa_fail)}")
    for name, fails in (("cfg", edge_fail), ("width", width_conflict),
                        ("ssa", ssa_fail)):
        for line in fails[:8]:
            print(f"    {name}: {line}")
        if len(fails) > 8:
            print(f"    ... {len(fails) - 8} more {name} problems")
    ok = not edge_fail and not width_conflict and not ssa_fail
    print(f"stage2: {'PASS' if ok else 'FAIL'}")
    return ok


def stage3() -> bool:
    from ir import memtype  # noqa: E402
    functions = decode.load_functions()
    resolver = memtype.Resolver()
    stats = collections.Counter()
    residue = collections.Counter()
    for ops in functions.values():
        for op in ops:
            kind = resolver.annotate(op)
            stats[kind] += 1
            if kind == "unresolved" and op.expr:
                residue[op.expr[:40]] += 1
    total = sum(stats.values()) or 1
    mem_total = total - stats["nonmem"] or 1
    resolved = sum(v for k, v in stats.items()
                   if k not in ("nonmem", "unresolved"))
    print(f"stage3: {total} ops, {mem_total} with memory operands")
    for kind, count in stats.most_common():
        print(f"  {kind:14} {count}")
    print(f"  resolution: {100.0 * resolved / mem_total:.2f}% of memory "
          f"operands classified")
    print("  top unresolved forms:")
    for expr, count in residue.most_common(10):
        print(f"    {count:5d}  {expr}")
    ok = resolved / mem_total > 0.97
    print(f"stage3: {'PASS' if ok else 'FAIL'} (gate: >97% classified)")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage1", action="store_true")
    parser.add_argument("--stage2", action="store_true")
    parser.add_argument("--stage3", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    args = parser.parse_args()
    ok = True
    if args.stage1 or args.all:
        ok &= stage1(args.rom)
    if args.stage2 or args.all:
        ok &= stage2()
    if args.stage3 or args.all:
        ok &= stage3()
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
