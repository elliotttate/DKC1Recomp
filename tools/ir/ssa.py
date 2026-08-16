"""Stage 2b: SSA over registers and flags, per function.

Variables: A X Y S D DB and flags C Z N V I Dflag. PB is static per
function; M/X width bits are static annotations from cfg.propagate_widths
(they are facts about instruction encoding, not runtime data here).

Partial writes: an A-writing op at M=1 defines only A's low byte; its
new SSA version records the version whose high byte survives
(ssa.partial), so 16-bit consumers slice through both. X/Y writes at
X=1 clear the high byte in hardware — full defs. Memory stays out of
SSA; stage 4 models it as explicit effects.

Version 0 of every variable is the function-entry value.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from ir import isa
from ir.cfg import Graph
from ir.decode import IROp

VARS = ["A", "X", "Y", "S", "D", "DB",
        "C", "Z", "N", "V", "I", "Dflag"]

# A-writers that stay 16-bit regardless of M
FULL_A_WRITERS = {"TDC", "TSC", "XBA"}

REP_FLAG_BITS = [(0x01, "C"), (0x02, "Z"), (0x04, "I"), (0x08, "Dflag"),
                 (0x40, "V"), (0x80, "N")]


def op_def_use(op: IROp) -> tuple[set[str], set[str]]:
    info = isa.rw(op.mnemonic)
    reads = set(info.get("r", [])) | set(info.get("fr", []))
    writes = set(info.get("w", [])) | set(info.get("f", []))

    if op.mnemonic in ("REP", "SEP"):
        mask = _mask(op.expr)
        if mask is None:
            writes |= {"C", "Z", "N", "V", "I", "Dflag"}
        else:
            writes |= {flag for bit, flag in REP_FLAG_BITS if mask & bit}

    # accumulator forms of RMW ops operate on A
    if op.mode == "acc":
        reads.add("A")
        writes.add("A")

    # addressing-mode operand dependencies
    if op.index == "x":
        reads.add("X")
    elif op.index == "y":
        reads.add("Y")
    elif op.index == "s":
        reads.add("S")
    if op.mode in ("dp", "dpx", "dpy", "ind", "indy", "indx",
                   "indl", "indly"):
        reads.add("D")
    if op.mode in ("abs", "absx", "absy", "ind", "indy", "indx",
                   "sriy") and op.mnemonic not in ("PEA", "JMP", "JML",
                                                   "JSR", "JSL", "PER"):
        reads.add("DB")
    # M/X/E are static width machinery (cfg.propagate_widths), not SSA
    return reads & set(VARS), writes & set(VARS)


def _mask(expr: str) -> int | None:
    text = expr.strip()
    if text.startswith("$"):
        try:
            return int(text[1:], 16)
        except ValueError:
            return None
    return int(text) if text.isdigit() else None


@dataclass
class SSA:
    graph: Graph
    uses: dict[int, dict[str, int]] = field(default_factory=dict)
    defs: dict[int, dict[str, int]] = field(default_factory=dict)
    partial: dict[tuple[str, int], int] = field(default_factory=dict)
    # block -> var -> {"ver": int, "in": {pred_block: version}}
    phis: dict[int, dict[str, dict]] = field(default_factory=dict)
    def_site: dict[tuple[str, int], object] = field(default_factory=dict)
    phi_count: int = 0
    problems: list[str] = field(default_factory=list)


def build(graph: Graph) -> SSA:
    ssa = SSA(graph=graph)
    reachable = [a for a in sorted(graph.blocks)
                 if a not in set(graph.unreachable)]
    if not reachable:
        return ssa
    blocks = {a: graph.blocks[a] for a in reachable}
    preds = {a: [p for p in blocks[a].preds if p in blocks]
             for a in reachable}

    idom = _dominators(graph, blocks, preds)
    frontier = _frontiers(blocks, preds, idom)
    children: dict[int, list[int]] = {a: [] for a in blocks}
    for addr, dom in idom.items():
        if dom is not None and dom != addr:
            children[dom].append(addr)

    # phi placement (Cytron worklist), unpruned
    def_blocks: dict[str, set[int]] = {v: {graph.entry} for v in VARS}
    for addr, block in blocks.items():
        for op in block.ops:
            _, writes = op_def_use(op)
            for var in writes:
                def_blocks[var].add(addr)
    phi_vars: dict[int, set[str]] = {a: set() for a in blocks}
    for var in VARS:
        work = list(def_blocks[var])
        placed: set[int] = set()
        while work:
            blk = work.pop()
            for f in frontier.get(blk, ()):
                if f not in placed:
                    placed.add(f)
                    phi_vars[f].add(var)
                    if f not in def_blocks[var]:
                        def_blocks[var].add(f)
                        work.append(f)

    # phi records exist BEFORE renaming: predecessors may rename first
    for addr in blocks:
        ssa.phis[addr] = {var: {"ver": None, "in": {}}
                          for var in sorted(phi_vars[addr])}

    counter = {v: 0 for v in VARS}
    stacks = {v: [0] for v in VARS}
    for var in VARS:
        ssa.def_site[(var, 0)] = "entry"

    def new_version(var: str) -> int:
        counter[var] += 1
        return counter[var]

    def rename(addr: int) -> None:
        block = blocks[addr]
        pushed: list[str] = []
        for var, record in ssa.phis[addr].items():
            ver = new_version(var)
            record["ver"] = ver
            ssa.def_site[(var, ver)] = ("phi", addr)
            stacks[var].append(ver)
            pushed.append(var)
            ssa.phi_count += 1
        for op in block.ops:
            reads, writes = op_def_use(op)
            ssa.uses[op.addr] = {v: stacks[v][-1] for v in sorted(reads)}
            defs: dict[str, int] = {}
            for var in sorted(writes):
                ver = new_version(var)
                if var == "A" and _partial_a(op):
                    ssa.partial[("A", ver)] = stacks["A"][-1]
                ssa.def_site[(var, ver)] = op.addr
                stacks[var].append(ver)
                pushed.append(var)
                defs[var] = ver
            ssa.defs[op.addr] = defs
        for kind, target in block.succs:
            if target in blocks:
                for var, record in ssa.phis[target].items():
                    record["in"][addr] = stacks[var][-1]
        for child in sorted(children[addr]):
            rename(child)
        for var in reversed(pushed):
            stacks[var].pop()

    import sys
    old_limit = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old_limit, 2 * len(blocks) + 100))
    try:
        rename(graph.entry)
    finally:
        sys.setrecursionlimit(old_limit)

    # invariant: every phi has an incoming value from every reachable pred
    for addr, per_var in ssa.phis.items():
        for var, record in per_var.items():
            if record["ver"] is None:
                ssa.problems.append(
                    f"phi for {var} at {addr:06X} never renamed")
                continue
            for p in preds[addr]:
                if p not in record["in"]:
                    ssa.problems.append(
                        f"phi {var}v{record['ver']} at {addr:06X} missing "
                        f"incoming from {p:06X}")
    return ssa


def _dominators(graph: Graph, blocks: dict, preds: dict) -> dict:
    """Cooper-Harvey-Kennedy iterative dominators on reverse postorder."""
    order: list[int] = []
    seen: set[int] = set()

    def post(addr: int) -> None:
        stack = [(addr, iter([t for k, t in blocks[addr].succs
                              if t in blocks]))]
        seen.add(addr)
        while stack:
            node, it = stack[-1]
            advanced = False
            for succ in it:
                if succ not in seen:
                    seen.add(succ)
                    stack.append(
                        (succ, iter([t for k, t in blocks[succ].succs
                                     if t in blocks])))
                    advanced = True
                    break
            if not advanced:
                order.append(node)
                stack.pop()

    post(graph.entry)
    rpo = list(reversed(order))
    number = {a: i for i, a in enumerate(rpo)}
    idom: dict[int, int | None] = {a: None for a in rpo}
    idom[graph.entry] = graph.entry

    def intersect(a: int, b: int) -> int:
        while a != b:
            while number[a] > number[b]:
                a = idom[a]
            while number[b] > number[a]:
                b = idom[b]
        return a

    changed = True
    while changed:
        changed = False
        for addr in rpo:
            if addr == graph.entry:
                continue
            candidates = [p for p in preds[addr] if idom.get(p) is not None]
            if not candidates:
                continue
            new = candidates[0]
            for p in candidates[1:]:
                new = intersect(new, p)
            if idom[addr] != new:
                idom[addr] = new
                changed = True
    return idom


def _frontiers(blocks: dict, preds: dict, idom: dict) -> dict:
    frontier: dict[int, set[int]] = {a: set() for a in blocks}
    for addr in blocks:
        ps = [p for p in preds[addr] if idom.get(p) is not None]
        if len(ps) < 2:
            continue
        for p in ps:
            runner = p
            while runner is not None and runner != idom[addr]:
                frontier[runner].add(addr)
                runner = idom[runner]
    return frontier


def _partial_a(op: IROp) -> bool:
    if op.mnemonic in FULL_A_WRITERS:
        return False
    # A write at M=1 (or unknown width, conservatively) is a low-byte def
    return op.mw != 0


def branch_condition(ssa: SSA, op: IROp) -> str | None:
    """Readable condition for a conditional branch, ONLY when the flag's
    reaching def is a proven pattern; None otherwise."""
    if op.mnemonic not in isa.BRANCHES:
        return None
    flag, taken_set = isa.BRANCHES[op.mnemonic]
    ver = ssa.uses.get(op.addr, {}).get(flag)
    if ver is None:
        return None
    site = ssa.def_site.get((flag, ver))
    if not isinstance(site, int):
        return None
    defop = ssa.graph.op_at.get(site)
    if defop is None:
        return None
    rhs = _operand_text(defop)
    if defop.mnemonic in ("CMP", "CPX", "CPY"):
        reg = {"CMP": "A", "CPX": "X", "CPY": "Y"}[defop.mnemonic]
        if flag == "Z":
            return f"{reg} {'==' if taken_set else '!='} {rhs}"
        if flag == "C":
            return f"{reg} {'>=' if taken_set else '<'} {rhs}  (unsigned)"
        if flag == "N":
            return f"({reg} - {rhs}) {'<' if taken_set else '>='} 0" \
                "  (signed bit)"
    result = _result_text(defop)
    if result is None:
        return None
    if flag == "Z":
        return f"{result} {'==' if taken_set else '!='} 0"
    if flag == "N":
        return f"{result} {'has' if taken_set else 'lacks'} sign bit"
    if flag == "V" and defop.mnemonic == "BIT" and defop.mode != "imm":
        return f"bit6 of {rhs} {'set' if taken_set else 'clear'}"
    if flag == "C" and defop.mnemonic in ("ASL", "LSR", "ROL", "ROR"):
        return f"shifted-out bit of {result} " \
               f"{'set' if taken_set else 'clear'}"
    return None


def _operand_text(op: IROp) -> str:
    if op.mode == "imm":
        return f"#{op.expr}"
    return op.sym or op.expr or op.mnemonic


def _result_text(op: IROp) -> str | None:
    if op.mnemonic in ("LDA", "PLA", "TXA", "TYA", "TDC", "TSC"):
        return "A"
    if op.mnemonic in ("LDX", "PLX", "TAX", "TSX", "TYX", "INX", "DEX"):
        return "X"
    if op.mnemonic in ("LDY", "PLY", "TAY", "TXY", "INY", "DEY"):
        return "Y"
    if op.mnemonic in ("AND", "ORA", "EOR", "ADC", "SBC"):
        return f"(A {_alu_sign(op.mnemonic)} {_operand_text(op)})"
    if op.mnemonic == "BIT":
        return f"(A & {_operand_text(op)})"
    if op.mnemonic in ("INC", "DEC", "ASL", "LSR", "ROL", "ROR"):
        target = "A" if op.mode == "acc" else (op.sym or op.expr)
        step = {"INC": "+ 1", "DEC": "- 1"}.get(op.mnemonic)
        return f"({target} {step})" if step else target
    return None


def _alu_sign(mnemonic: str) -> str:
    return {"AND": "&", "ORA": "|", "EOR": "^", "ADC": "+",
            "SBC": "-"}[mnemonic]
