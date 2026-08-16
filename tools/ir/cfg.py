"""Stage 2a: intra-function CFG + M/X width propagation.

Blocks split at control transfers only. Successor kinds:
  fall / branch / jump          — in-function edges (by address)
  external                      — target lies in another function
                                  (tail branch or fall-through past end)
  dispatch                      — JMP (table,x) with a runtime-proven
                                  contract: successors from recomp cfg
  indirect-unresolved           — indirection with no contract (counted,
                                  surfaced, not silently dropped)
  exit                          — RTS/RTL/RTI

Width propagation seeds from the decoder's proven entry_mx facts and
flows REP/SEP/PLP/XCE and callee exit_mx effects; width-suffixed
immediates are both a CHECK (a proven width that contradicts a suffix is
a hard stage-2 failure) and an ANCHOR (they re-prove unknown widths,
since the assembler sized them from the real width).
"""
from __future__ import annotations

from dataclasses import dataclass, field

from ir import isa
from ir.decode import IROp

TERMINATOR_MODES = {"absind", "absindx", "absindl"}


@dataclass
class Block:
    start: int
    ops: list[IROp] = field(default_factory=list)
    succs: list[tuple[str, int | None]] = field(default_factory=list)
    preds: list[int] = field(default_factory=list)


@dataclass
class Graph:
    name: str
    entry: int
    blocks: dict[int, Block] = field(default_factory=dict)
    op_at: dict[int, IROp] = field(default_factory=dict)
    problems: list[str] = field(default_factory=list)
    external_targets: list[int] = field(default_factory=list)
    unreachable: list[int] = field(default_factory=list)
    indirect_unresolved: int = 0
    # width accounting
    width_proven: int = 0
    width_assumed: int = 0
    width_unknown: int = 0
    width_conflicts: list[str] = field(default_factory=list)

    def order(self) -> list[Block]:
        return [self.blocks[a] for a in sorted(self.blocks)]


def _is_terminator(op: IROp) -> bool:
    if op.mnemonic in isa.RETURNS or op.mnemonic in isa.UNCONDITIONAL:
        return True
    if op.mnemonic in isa.BRANCHES:
        return True
    if op.mnemonic in isa.JUMPS:
        return True
    if op.mnemonic == "JSR" and op.mode in TERMINATOR_MODES:
        return False  # JSR (table,x) returns; call, not terminator
    return False


def build(name: str, ops: list[IROp], functions: dict[str, list[IROp]],
          dispatches: dict[int, dict]) -> Graph:
    addrs = {op.addr for op in ops}
    graph = Graph(name=name, entry=ops[0].addr)
    graph.op_at = {op.addr: op for op in ops}

    leaders = {ops[0].addr}
    for i, op in enumerate(ops):
        if op.target is not None and op.target in addrs:
            leaders.add(op.target)
        if _is_terminator(op) and i + 1 < len(ops):
            leaders.add(ops[i + 1].addr)

    block = None
    for i, op in enumerate(ops):
        if op.addr in leaders:
            block = Block(start=op.addr)
            graph.blocks[op.addr] = block
        block.ops.append(op)
        nxt = ops[i + 1].addr if i + 1 < len(ops) else None
        contiguous = nxt == op.addr + op.size

        if op.mnemonic in isa.RETURNS:
            block.succs.append(("exit", None))
        elif op.mnemonic in isa.BRANCHES:
            _edge(graph, block, "branch", op.target, addrs)
            if contiguous:
                block.succs.append(("fall", nxt))
            else:
                # fall-through leaves the function (tail-fall) or crosses
                # a listing gap: honest external edge, not an error
                block.succs.append(("external", op.addr + op.size))
                graph.external_targets.append(op.addr + op.size)
        elif op.mnemonic in isa.UNCONDITIONAL:
            _edge(graph, block, "jump", op.target, addrs)
        elif op.mnemonic in isa.JUMPS:
            if op.mode in TERMINATOR_MODES:
                dispatch = dispatches.get(op.addr)
                if dispatch:
                    for target in dispatch["targets"]:
                        _edge(graph, block, "dispatch", target, addrs)
                else:
                    block.succs.append(("indirect-unresolved", None))
                    graph.indirect_unresolved += 1
            else:
                _edge(graph, block, "jump", op.target, addrs)
        elif i + 1 < len(ops) and ops[i + 1].addr in leaders:
            if contiguous:
                block.succs.append(("fall", nxt))
            else:
                # listing gap inside a function (data island); honest edge
                block.succs.append(("external", nxt))
                graph.external_targets.append(nxt)
        elif i + 1 == len(ops):
            # function falls through past its end (recomp's "tail")
            block.succs.append(("external", op.addr + op.size))
            graph.external_targets.append(op.addr + op.size)

    for blk in graph.blocks.values():
        for kind, target in blk.succs:
            if target in graph.blocks:
                graph.blocks[target].preds.append(blk.start)

    # reachability from entry (dead islands exist in the listing)
    seen = set()
    stack = [graph.entry]
    while stack:
        addr = stack.pop()
        if addr in seen or addr not in graph.blocks:
            continue
        seen.add(addr)
        for kind, target in graph.blocks[addr].succs:
            if target in graph.blocks:
                stack.append(target)
    graph.unreachable = [a for a in graph.blocks if a not in seen]
    return graph


def _edge(graph: Graph, block: Block, kind: str, target: int | None,
          addrs: set[int]) -> None:
    if target is None:
        graph.problems.append(
            f"{kind} at {block.ops[-1].addr:06X} without resolvable "
            f"target: {block.ops[-1].expr!r}")
        return
    if target in addrs:
        block.succs.append((kind, target))
    else:
        block.succs.append(("external", target))
        graph.external_targets.append(target)


def _const(expr: str) -> int | None:
    text = expr.strip()
    if text.startswith("$"):
        try:
            return int(text[1:], 16)
        except ValueError:
            return None
    if text.isdigit():
        return int(text)
    return None


def propagate_widths(graph: Graph, facts: dict[int, dict]) -> None:
    """State = (m, x, assumed): assumed becomes True after a call with no
    exit_mx fact (call-preservation assumption) and clears only when both
    widths are re-proven by REP/SEP constants or immediate anchors."""
    fact = facts.get(graph.entry)
    entry = (fact["entry_mx"] + (False,)) if fact else (None, None, False)

    in_state: dict[int, tuple] = {graph.entry: entry}
    worklist = [graph.entry]
    # Interior recomp-func entries (dispatch/table targets the index's
    # coarser function grouping absorbed) are extra proven anchors.
    for addr, block_fact in facts.items():
        if addr != graph.entry and addr in graph.blocks:
            in_state[addr] = block_fact["entry_mx"] + (False,)
            worklist.append(addr)
    visited_out: dict[int, tuple] = {}

    while worklist:
        addr = worklist.pop()
        block = graph.blocks[addr]
        state = in_state.get(addr, (None, None, False))
        for op in block.ops:
            state = _transfer(graph, op, state, facts)
        if visited_out.get(addr) == state:
            continue
        visited_out[addr] = state
        for kind, target in block.succs:
            if target in graph.blocks:
                old = in_state.get(target)
                merged = _merge(old, state) if old is not None else state
                if merged != old:
                    in_state[target] = merged
                    worklist.append(target)


def _merge(a: tuple, b: tuple) -> tuple:
    return (a[0] if a[0] == b[0] else None,
            a[1] if a[1] == b[1] else None,
            a[2] or b[2])


def _transfer(graph: Graph, op: IROp, state: tuple,
              facts: dict[int, dict]) -> tuple:
    m, x, assumed = state
    # annotate the width IN EFFECT at this op, then apply its effects
    op.mw, op.xw, op.width_assumed = m, x, assumed

    imm_class = isa.IMM_WIDTH_CLASS.get(op.mnemonic)
    if op.mode == "imm" and imm_class and op.suffix in ("b", "w"):
        suffix_width = 1 if op.suffix == "b" else 0
        current = m if imm_class == "m" else x
        if current is None:
            # the assembler sized this from the true width: re-anchor
            if imm_class == "m":
                m = suffix_width
            else:
                x = suffix_width
            op.mw, op.xw = m, x
        elif current != suffix_width:
            graph.width_conflicts.append(
                f"{op.addr:06X} {op.mnemonic}.{op.suffix} #imm but "
                f"{imm_class.upper()}={current}"
                + (" (call-assumed)" if assumed else ""))

    # accounting over width-sensitive ops only
    if (op.mode == "imm" and imm_class) or \
            (op.mnemonic in isa.MEM_WIDTH_CLASS and op.mode != "imm"):
        klass = imm_class or isa.MEM_WIDTH_CLASS[op.mnemonic]
        width = op.mw if klass == "m" else op.xw
        if width is None:
            graph.width_unknown += 1
        elif op.width_assumed:
            graph.width_assumed += 1
        else:
            graph.width_proven += 1

    if op.mnemonic == "REP":
        mask = _const(op.expr)
        if mask is None:
            return None, None, assumed
        return (0 if mask & 0x20 else m), (0 if mask & 0x10 else x), \
            (assumed and not (mask & 0x20 and mask & 0x10))
    if op.mnemonic == "SEP":
        mask = _const(op.expr)
        if mask is None:
            return None, None, assumed
        return (1 if mask & 0x20 else m), (1 if mask & 0x10 else x), \
            (assumed and not (mask & 0x20 and mask & 0x10))
    if op.mnemonic in ("PLP", "RTI", "XCE"):
        return None, None, False
    if op.mnemonic in isa.CALLS:
        callee = facts.get(op.target) if op.target is not None else None
        # exit_mx is variant-relative: it describes the exit of the
        # callee's canonical entry_mx variant. Apply it only when this
        # call site enters in exactly that state; a different entry
        # state runs a different width variant whose exit we don't
        # have — fall back to the preserve assumption.
        if callee and callee.get("exit_mx") and \
                (m, x) == callee["entry_mx"]:
            return callee["exit_mx"] + (False,)
        return m, x, True
    return m, x, assumed
