"""Stage 4: per-function read/write/call summaries from the typed IR.

Direct summaries are exact over what stage 3 proved: every wram/mmio
load and store with its address, width, indexed-ness, and op site.
Indirect accesses and stack traffic are COUNTED, never guessed — a
function with indirect writes has an honestly-unbounded write set and
its summary says so.

Transitive (control-flow-closed) effects are computed on demand with
cycle guards; calls, dispatch sites, and external tail continuations
all expand through their source-backed targets.
Serialized to build/ir/summaries.json for impact.py / oracle specs.
"""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import isa, memtype  # noqa: E402
from ir import decode  # noqa: E402

REPO = TOOLS.parent
OUT = REPO / "build" / "ir" / "summaries.json"

# actor SoA arrays are indexed by slot ($02..$32 step 2): an indexed
# access to base B covers [B, B+0x33]
SOA_SPAN = 0x33


@dataclass
class Access:
    ea: int
    width: int | None      # 8 / 16 / None(unknown)
    indexed: bool
    op_addr: int
    sym: str = ""
    region: str = "wram"

    def covers(self, addr: int) -> bool:
        if self.indexed:
            return self.ea <= addr <= self.ea + (
                SOA_SPAN if self.region == "wram" else 0xFF)
        # unknown width covers 2 bytes (conservative)
        span = 1 if self.width == 8 else 2
        return self.ea <= addr < self.ea + span


@dataclass
class Summary:
    function: str
    entry: int
    reads: list[Access] = field(default_factory=list)
    writes: list[Access] = field(default_factory=list)
    indirect_reads: int = 0
    indirect_writes: int = 0
    calls: list[int] = field(default_factory=list)
    external: list[int] = field(default_factory=list)
    dispatch_targets: list[int] = field(default_factory=list)
    body_addrs: set[int] = field(default_factory=set)


def _width_of(op) -> int | None:
    klass = isa.MEM_WIDTH_CLASS.get(op.mnemonic)
    if not klass:
        return 8
    bit = op.mw if klass == "m" else op.xw
    if bit is None:
        return None
    return 8 if bit else 16


def build_summaries(functions=None, resolver=None,
                    dispatches=None) -> dict[str, Summary]:
    from ir import cfg as ircfg
    functions = functions or decode.load_functions()
    resolver = resolver or memtype.Resolver()
    if dispatches is None:
        dispatches = {d["site"]: d for d in atlas.load_dispatches()}
    facts = decode.load_func_facts()
    summaries: dict[str, Summary] = {}
    for name, ops in functions.items():
        graph = ircfg.build(name, ops, functions, dispatches)
        if ops[0].mw is None and ops[0].xw is None:
            ircfg.propagate_widths(graph, facts)
        summary = Summary(
            function=name, entry=ops[0].addr,
            external=list(dict.fromkeys(graph.external_targets)),
            body_addrs={op.addr for op in ops})
        for op in ops:
            if op.ea is None and op.region == "":
                resolver.annotate(op)
            info = isa.rw(op.mnemonic)
            is_read = info.get("mem") or info.get("rmw")
            is_write = info.get("store") or info.get("rmw")
            if op.mode == "imm":
                is_read = is_write = False
            if op.region in ("wram", "mmio") and op.ea is not None:
                access = Access(
                    ea=op.ea if op.region == "mmio" else op.ea & 0x1FFFF,
                    width=_width_of(op), indexed=op.index in ("x", "y"),
                    op_addr=op.addr, sym=op.sym, region=op.region)
                if is_read:
                    summary.reads.append(access)
                if is_write:
                    summary.writes.append(access)
            elif op.region == "indirect":
                if is_read:
                    summary.indirect_reads += 1
                if is_write:
                    summary.indirect_writes += 1
            if op.mnemonic in isa.CALLS and op.target is not None:
                summary.calls.append(op.target)
            if op.mnemonic == "JSR" and op.mode == "absindx":
                dispatch = dispatches.get(op.addr)
                if dispatch:
                    summary.dispatch_targets.extend(dispatch["targets"])
            if op.mnemonic in ("JMP", "JML") and \
                    op.mode in ("absind", "absindx", "absindl"):
                dispatch = dispatches.get(op.addr)
                if dispatch:
                    summary.dispatch_targets.extend(dispatch["targets"])
        summaries[name] = summary
    return summaries


def write_json(summaries: dict[str, Summary], out: Path = OUT) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    payload = {}
    for name, s in summaries.items():
        payload[name] = {
            "entry": f"0x{s.entry:06X}",
            "reads": [_acc(a) for a in s.reads],
            "writes": [_acc(a) for a in s.writes],
            "indirect_reads": s.indirect_reads,
            "indirect_writes": s.indirect_writes,
            "calls": [f"0x{c:06X}" for c in s.calls],
            "external": [f"0x{t:06X}" for t in s.external],
            "dispatch_targets": [f"0x{t:06X}"
                                 for t in s.dispatch_targets],
        }
    out.write_text(json.dumps(
        {"schema": "dkc1.ir.summaries.v1", "functions": payload},
        indent=1))


def _acc(a: Access) -> dict:
    entry = {"ea": f"0x{a.ea:X}", "width": a.width,
             "indexed": a.indexed, "at": f"0x{a.op_addr:06X}",
             "region": a.region}
    if a.sym:
        entry["sym"] = a.sym
    return entry


def writers_of(summaries: dict[str, Summary],
               addr: int) -> list[tuple[str, Access]]:
    hits = []
    for name, s in summaries.items():
        for access in s.writes:
            if access.region == "wram" and access.covers(addr):
                hits.append((name, access))
    return hits


def readers_of(summaries: dict[str, Summary],
               addr: int) -> list[tuple[str, Access]]:
    hits = []
    for name, s in summaries.items():
        for access in s.reads:
            if access.region == "wram" and access.covers(addr):
                hits.append((name, access))
    return hits


def transitive_effects(summaries: dict[str, Summary], name: str,
                       max_depth: int = 8) -> dict:
    """Control-flow-closed effects with honest unboundedness flags."""
    by_entry = {s.entry: s for s in summaries.values()}
    by_body_addr = {addr: s for s in summaries.values()
                    for addr in s.body_addrs}
    seen: set[str] = set()
    reads: list[Access] = []
    writes: list[Access] = []
    unbounded = {"indirect_reads": 0, "indirect_writes": 0,
                 "unresolved_calls": 0, "unresolved_external": 0}

    def walk(fn: str, depth: int) -> None:
        if fn in seen or depth > max_depth:
            if depth > max_depth:
                unbounded["unresolved_calls"] += 1
            return
        seen.add(fn)
        s = summaries.get(fn)
        if s is None:
            unbounded["unresolved_calls"] += 1
            return
        reads.extend(s.reads)
        writes.extend(s.writes)
        unbounded["indirect_reads"] += s.indirect_reads
        unbounded["indirect_writes"] += s.indirect_writes
        for target in list(s.calls) + list(s.dispatch_targets):
            callee = by_entry.get(target)
            if callee is not None:
                walk(callee.function, depth + 1)
            else:
                unbounded["unresolved_calls"] += 1
        for target in s.external:
            continuation = by_entry.get(target) or by_body_addr.get(target)
            if continuation is not None:
                walk(continuation.function, depth + 1)
            else:
                unbounded["unresolved_external"] += 1

    walk(name, 0)
    return {"functions_visited": len(seen), "reads": reads,
            "writes": writes, **unbounded}


if __name__ == "__main__":
    summaries = build_summaries()
    write_json(summaries)
    total_w = sum(len(s.writes) for s in summaries.values())
    total_r = sum(len(s.reads) for s in summaries.values())
    print(f"{len(summaries)} functions -> {OUT} "
          f"({total_r} reads, {total_w} writes)")
