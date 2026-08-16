#!/usr/bin/env python3
"""Structured pseudocode from the validated IR (stages 1-3).

Unlike tools/structure.py (display-only symbolizer), this renders from
the SSA IR: real basic blocks, proven M/X widths on every access, typed
memory operands, and branch conditions recovered ONLY where the flag's
reaching definition is a proven pattern (CMP/load/ALU); everything else
stays a raw flag test. Every line keeps its 1:1 assembly cross-link.

Instruction-index function labels are seed groups, not proof of closed
control flow.  The view never silently splices adjacent seeds: a contiguous
tail fallthrough is a prominent partial-view warning with the exact external
continuation address, and unresolved indirect successors fail closed likewise.

usage:  python tools/irview.py Player_HandleHitEvents
        python tools/irview.py BFC745 --ssa     # show SSA versions/phis
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from structure import resolve_function  # noqa: E402
from ir import cfg as ircfg  # noqa: E402
from ir import decode, memtype  # noqa: E402
from ir import ssa as irssa  # noqa: E402
from ir.decode import IROp  # noqa: E402
from ir import isa  # noqa: E402

ALU = {"ADC": "+=", "SBC": "-=", "AND": "&=", "ORA": "|=", "EOR": "^="}
SHIFT = {"ASL": "<<= 1", "LSR": ">>= 1", "ROL": "<<<= 1 (thru C)",
         "ROR": ">>>= 1 (thru C)"}
REG_OF = {"LDA": "A", "LDX": "X", "LDY": "Y",
          "STA": "A", "STX": "X", "STY": "Y"}
TRANSFERS = {"TAX": "X = A", "TAY": "Y = A", "TXA": "A = X",
             "TYA": "A = Y", "TSX": "X = S", "TXS": "S = X",
             "TXY": "Y = X", "TYX": "X = Y", "TCD": "D = A",
             "TDC": "A = D", "TCS": "S = A", "TSC": "A = S",
             "XBA": "A = swap_bytes(A)"}
PUSHPOP = {"PHA": "push A", "PHX": "push X", "PHY": "push Y",
           "PHP": "push P", "PHB": "push DB", "PHD": "push D",
           "PHK": "push PB", "PLA": "A = pop", "PLX": "X = pop",
           "PLY": "Y = pop", "PLP": "P = pop", "PLB": "DB = pop",
           "PLD": "D = pop"}


def operand_text(op: IROp) -> str:
    if op.mode == "imm":
        # No define-name guessing: raw hex here means the disassembly
        # author did NOT know the domain; a value-collision annotation
        # (e.g. "#$0019 (MusicID_Failure)" on a state id) is an
        # attractive lie. When the domain IS known, op.expr already
        # spells the define.
        return f"#{op.expr}"
    text = op.sym or op.expr
    if not op.sym and op.index and op.mode != "block":
        text = f"{text}[{op.index.upper()}]"
    if op.mode in ("ind", "indy", "indx", "indl", "indly"):
        star = "**" if op.mode in ("indl", "indly") else "*"
        text = f"{star}({text})" + \
            ("[Y]" if op.mode in ("indy", "indly") else "")
    if op.region == "mmio" and op.sym:
        text = f"MMIO.{op.sym.replace('REGISTER_', '')}"
    return text


def width_note(op: IROp) -> str:
    klass = isa.IMM_WIDTH_CLASS.get(op.mnemonic) if op.mode == "imm" \
        else isa.MEM_WIDTH_CLASS.get(op.mnemonic)
    if not klass:
        return ""
    bit = op.mw if klass == "m" else op.xw
    if bit is None:
        return " (?w)"
    note = "8" if bit else "16"
    return f" ({note}{'~' if op.width_assumed else ''})"


def render_op(op: IROp, ssa, labels: set[int],
              code_names: dict) -> str:
    src = operand_text(op)
    wn = width_note(op)
    m = op.mnemonic
    if m in ("LDA", "LDX", "LDY"):
        return f"{REG_OF[m]} = {src}{wn}"
    if m in ("STA", "STX", "STY"):
        return f"{src} = {REG_OF[m]}{wn}"
    if m == "STZ":
        return f"{src} = 0{wn}"
    if m in ALU:
        extra = " + C" if m == "ADC" else (" - !C" if m == "SBC" else "")
        return f"A {ALU[m]} {src}{extra}{wn}"
    if m in ("CMP", "CPX", "CPY"):
        reg = {"CMP": "A", "CPX": "X", "CPY": "Y"}[m]
        return f"compare {reg}, {src}{wn}"
    if m == "BIT":
        return f"test A & {src}{wn}"
    if m in SHIFT:
        target = "A" if op.mode == "acc" else src
        return f"{target} {SHIFT[m]}{wn}"
    if m in ("INC", "DEC"):
        target = "A" if op.mode == "acc" else src
        return f"{target} {'+= 1' if m == 'INC' else '-= 1'}{wn}"
    if m in ("INX", "DEX"):
        return f"X {'+= 1' if m == 'INX' else '-= 1'}"
    if m in ("INY", "DEY"):
        return f"Y {'+= 1' if m == 'INY' else '-= 1'}"
    if m in ("TRB", "TSB"):
        verb = "clear" if m == "TRB" else "set"
        return f"{verb} bits A in {src}{wn}"
    if m in TRANSFERS:
        return TRANSFERS[m]
    if m in PUSHPOP:
        return PUSHPOP[m]
    if m in ("PEA", "PER", "PEI"):
        return f"push {src}"
    if m in ("REP", "SEP"):
        mask = ircfg._const(op.expr) or 0
        bits = []
        if mask & 0x20:
            bits.append("M=16" if m == "REP" else "M=8")
        if mask & 0x10:
            bits.append("X=16" if m == "REP" else "X=8")
        others = mask & ~0x30
        if others:
            bits.append(f"{m.lower()} P&${others:02X}")
        return ", ".join(bits) or f"{m} #{op.expr}"
    if m in isa.CALLS:
        return f"call {call_name(op, code_names)}"
    if m in isa.RETURNS:
        return "return" + (" (from interrupt)" if m == "RTI" else "")
    if m in isa.BRANCHES:
        cond = irssa.branch_condition(ssa, op) if ssa else None
        flag, taken = isa.BRANCHES[m]
        cond = cond or f"{'' if taken else '!'}{flag}"
        return f"if ({cond}) goto {jump_label(op, labels)}"
    if m in ("BRA", "BRL"):
        return f"goto {jump_label(op, labels)}"
    if m in ("JMP", "JML"):
        if op.mode in ("absind", "absindx", "absindl"):
            return f"dispatch via ({op.expr}{',' + op.index.upper() if op.index else ''})"
        return f"goto {jump_label(op, labels)}"
    if m in ("MVN", "MVP"):
        return f"block move {op.expr} -> {op.expr2} (A+1 bytes)"
    return m.lower()


def call_name(op: IROp, code_names: dict) -> str:
    if op.target is not None:
        entry = code_names.get(op.target)
        if entry and entry.get("name"):
            return entry["name"]
        return f"CODE_{op.target:06X}"
    return op.expr


def jump_label(op: IROp, labels: set[int]) -> str:
    if op.target is None:
        return op.expr
    if op.target in labels:
        return f"L_{op.target & 0xFFFF:04X}"
    return f"CODE_{op.target:06X} (external)"


def _external_is_fallthrough(op: IROp, target: int | None) -> bool:
    """Distinguish a hidden seed-boundary fall from a rendered transfer."""
    if target is None:
        return False
    if op.mnemonic in isa.BRANCHES:
        return target != op.target
    if op.mnemonic in isa.UNCONDITIONAL or op.mnemonic in isa.JUMPS or \
            op.mnemonic in isa.RETURNS:
        return False
    return target == op.addr + op.size


def _target_text(target: int, functions: dict[str, list[IROp]],
                 code_names: dict[int, dict]) -> str:
    entry = code_names.get(target)
    if entry and entry.get("name"):
        return f"{entry['name']} (0x{target:06X})"
    for name, ops in functions.items():
        if ops and ops[0].addr == target:
            return f"{name} (0x{target:06X}, external seed)"
        for op in ops:
            if op.addr == target:
                return (f"{name}+0x{target - ops[0].addr:X} "
                        f"(0x{target:06X}, external seed body)")
    return f"0x{target:06X} (not present in the instruction index)"


def _tail_fallthroughs(graph) -> list[tuple[int, int]]:
    tails = []
    for block in graph.blocks.values():
        last = block.ops[-1]
        for kind, target in block.succs:
            if kind == "external" and \
                    _external_is_fallthrough(last, target):
                tails.append((last.addr, target))
    return tails


def _dispatch_summary(block, functions, code_names) -> str | None:
    op = block.ops[-1]
    if op.mnemonic not in isa.JUMPS or op.mode not in ircfg.TERMINATOR_MODES:
        return None
    if any(kind == "indirect-unresolved" for kind, _ in block.succs):
        return ("      !! unresolved indirect successor; no dispatch "
                "contract, so this control-flow view is incomplete")
    targets = [target for kind, target in block.succs
               if kind in ("dispatch", "external") and target is not None]
    if not targets:
        return None
    shown = ", ".join(_target_text(target, functions, code_names)
                      for target in targets[:4])
    more = f", ... +{len(targets) - 4}" if len(targets) > 4 else ""
    return f"      dispatch successors ({len(targets)}): {shown}{more}"


def render_view(function: str, ops: list[IROp],
                functions: dict[str, list[IROp]], facts: dict[int, dict],
                dispatches: dict[int, dict], code_names: dict[int, dict],
                show_ssa: bool = False) -> str:
    """Render one seed group and make every incomplete boundary explicit."""
    graph = ircfg.build(function, ops, functions, dispatches)
    ircfg.propagate_widths(graph, facts)
    ssa = irssa.build(graph)
    resolver = memtype.Resolver()
    for op in ops:
        resolver.annotate(op)

    entry = code_names.get(graph.entry)
    display = entry["name"] if entry and entry.get("name") else function
    fact = facts.get(graph.entry)
    output = [f"=== {display} (0x{graph.entry:06X}) ==="]
    if fact:
        output.append(
            f"entry M={fact['entry_mx'][0]} X={fact['entry_mx'][1]}" +
            (f", exit M={fact['exit_mx'][0]} X={fact['exit_mx'][1]}"
             if fact.get("exit_mx") else ""))

    tails = _tail_fallthroughs(graph)
    audit = (f"boundary audit: {len(tails)} external fallthrough(s), "
             f"{graph.indirect_unresolved} unresolved indirect successor(s), "
             f"{len(graph.problems)} CFG problem(s), "
             f"{len(ssa.problems)} SSA problem(s)")
    output.append(audit)
    if tails or graph.indirect_unresolved or graph.problems or ssa.problems:
        output.append(
            "WARNING: partial control-flow view; follow the !! boundary "
            "markers before treating this seed as a complete routine")
    if graph.width_conflicts:
        output.append("WARNING: width conflicts: " +
                      "; ".join(graph.width_conflicts))
    for problem in graph.problems:
        output.append(f"WARNING: CFG: {problem}")
    for problem in ssa.problems:
        output.append(f"WARNING: SSA: {problem}")
    if graph.external_entries:
        output.append(
            "externally-entered blocks (live secondary roots — labeled, "
            "referenced from outside this seed): "
            f"{[hex(address) for address in graph.external_entries]}")
    if graph.unreachable:
        output.append(
            "unreachable blocks (listed separately, never implied reachable): "
            f"{[hex(address) for address in graph.unreachable]}")
    output.append("")

    labels = set(graph.blocks)
    unreachable = set(graph.unreachable)
    external_entries = set(graph.external_entries)
    for block in graph.order():
        preds = f"  ; preds: {', '.join(f'L_{p & 0xFFFF:04X}' for p in block.preds)}" \
            if block.preds else ""
        reachability = "  ; UNREACHABLE FROM ENTRY" \
            if block.start in unreachable else ""
        if block.start in external_entries:
            reachability = "  ; EXTERNAL ENTRY (entered from outside " \
                "this seed)"
        output.append(f"L_{block.start & 0xFFFF:04X}:{preds}{reachability}")
        if show_ssa and ssa.phis.get(block.start):
            for var, record in ssa.phis[block.start].items():
                incoming = ", ".join(
                    f"L_{pred & 0xFFFF:04X}:v{version}"
                    for pred, version in sorted(record["in"].items()))
                output.append(
                    f"      {var}v{record['ver']} = phi({incoming})")
        for op in block.ops:
            text = render_op(op, ssa, labels, code_names)
            asm = f"{op.mnemonic}" + (f".{op.suffix}" if op.suffix else "")
            # reconstruct the operand's original spelling per mode so
            # the cross-link column stays faithful (indirection wrappers
            # are part of the instruction, not decoration)
            operand = op.expr
            if op.mode in ("ind", "absind"):
                operand = f"({operand})"
            elif op.mode in ("indx", "absindx"):
                operand = f"({operand},x)"
            elif op.mode == "indy":
                operand = f"({operand}),y"
            elif op.mode == "sr" and op.index == "s":
                operand = f"{operand},s"
            elif op.mode == "sriy":
                operand = f"({operand},s),y"
            elif op.mode in ("indl", "absindl"):
                operand = f"[{operand}]"
            elif op.mode == "indly":
                operand = f"[{operand}],y"
            elif op.expr2:
                operand += f",{op.expr2}"
            elif op.index and op.mode not in ("imp", "acc"):
                operand += f",{op.index}"
            if op.mode == "imm":
                operand = "#" + operand
            output.append(
                f"      {text:<52} ; {op.addr:06X}  {asm} {operand}")

        last = block.ops[-1]
        for kind, target in block.succs:
            if kind == "external" and \
                    _external_is_fallthrough(last, target):
                output.append(
                    "      !! TAIL-FALLTHROUGH -> " +
                    _target_text(target, functions, code_names) +
                    "; continuation is outside this seed and is not expanded")
        dispatch_summary = _dispatch_summary(block, functions, code_names)
        if dispatch_summary:
            output.append(dispatch_summary)
        output.append("")
    return "\n".join(output).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--ssa", action="store_true",
                        help="show SSA versions and phi nodes")
    args = parser.parse_args()

    function, _rows, code_names = resolve_function(args.query)
    functions = decode.load_functions()
    ops = functions[function]
    facts = decode.load_func_facts()
    dispatches = {d["site"]: d for d in atlas.load_dispatches()}
    print(render_view(function, ops, functions, facts, dispatches,
                      code_names, args.ssa), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
