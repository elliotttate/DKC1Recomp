#!/usr/bin/env python3
"""Backward data slices over the validated IR.

--store ADDR: every static write site covering a WRAM address, each with
the SSA backward slice of the stored value (what fed it, through
registers, phis, partial writes, down to constants / memory loads /
function entry). The static complement of tools/reverse_watch.py: the
watch tells you who DID write on a route; this tells you who CAN.

usage:
  python tools/slice.py --store 1595
  python tools/slice.py --store 11A1 --callers
  python tools/slice.py --value-of BFA123      # slice at one op site
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import cfg as ircfg  # noqa: E402
from ir import decode, memtype, summarize  # noqa: E402
from ir import ssa as irssa  # noqa: E402

# data-carrying variables worth following backward (flags only via C,
# which feeds ADC/SBC/ROL/ROR results)
DATA_VARS = {"A", "X", "Y", "C", "D", "DB"}


class Corpus:
    def __init__(self) -> None:
        self.functions = decode.load_functions()
        self.facts = decode.load_func_facts()
        self.dispatches = {d["site"]: d for d in atlas.load_dispatches()}
        self.resolver = memtype.Resolver()
        self.code_names, _ = atlas.load_rename_map()
        self._built: dict[str, tuple] = {}

    def ir_of(self, function: str):
        if function not in self._built:
            ops = self.functions[function]
            for op in ops:
                self.resolver.annotate(op)
            graph = ircfg.build(function, ops, self.functions,
                                self.dispatches)
            ircfg.propagate_widths(graph, self.facts)
            ssa = irssa.build(graph)
            self._built[function] = (graph, ssa)
        return self._built[function]

    def display(self, function: str, entry_addr: int) -> str:
        entry = self.code_names.get(entry_addr)
        if entry and entry.get("name"):
            return f"{entry['name']} ({function})"
        return function


def slice_value(corpus: Corpus, function: str, op_addr: int,
                var: str = "A", limit: int = 40) -> list[str]:
    """Backward slice of `var` as consumed at op_addr."""
    graph, ssa = corpus.ir_of(function)
    start = ssa.uses.get(op_addr, {}).get(var)
    if start is None:
        return [f"({var} not read at {op_addr:06X})"]
    lines: list[str] = []
    seen: set[tuple[str, int]] = set()
    work: list[tuple[str, int, int]] = [(var, start, 0)]
    while work and len(lines) < limit:
        v, ver, depth = work.pop(0)
        if (v, ver) in seen:
            continue
        seen.add((v, ver))
        pad = "  " * depth
        site = ssa.def_site.get((v, ver))
        if site == "entry":
            lines.append(f"{pad}{v} = <function entry value>")
            continue
        if isinstance(site, tuple) and site[0] == "phi":
            record = ssa.phis[site[1]][v]
            versions = sorted(set(record["in"].values()))
            lines.append(
                f"{pad}{v} = merge of {len(record['in'])} paths at "
                f"L_{site[1] & 0xFFFF:04X}")
            for iv in versions:
                work.append((v, iv, depth + 1))
            continue
        op = graph.op_at.get(site)
        if op is None:
            lines.append(f"{pad}{v}v{ver}: unknown def site {site}")
            continue
        desc = _describe(op)
        lines.append(f"{pad}{v} <- {desc}   [{op.addr:06X}]")
        # follow the def op's own data inputs
        for used_var, used_ver in ssa.uses.get(op.addr, {}).items():
            if used_var in DATA_VARS:
                work.append((used_var, used_ver, depth + 1))
        if v == "A" and ("A", ver) in ssa.partial:
            work.append(("A", ssa.partial[("A", ver)], depth + 1))
    if work:
        lines.append(f"... slice truncated at {limit} lines")
    return lines


def _describe(op) -> str:
    if op.mode == "imm":
        return f"{op.mnemonic} #{op.expr}"
    target = op.sym or op.expr or ""
    if op.index and not op.sym:
        target += f",{op.index}"
    return f"{op.mnemonic} {target}".strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--store", help="hex WRAM address")
    parser.add_argument("--value-of",
                        help="hex op address: slice A as consumed there")
    parser.add_argument("--callers", action="store_true",
                        help="list static callers of each writer")
    parser.add_argument("--readers", action="store_true",
                        help="also list static readers")
    args = parser.parse_args()
    corpus = Corpus()

    if args.value_of:
        addr = int(args.value_of, 16)
        function = None
        for name, ops in corpus.functions.items():
            if any(op.addr == addr for op in ops):
                function = name
                break
        if function is None:
            sys.exit(f"no instruction at {addr:06X}")
        print(f"value of A consumed at {addr:06X} in {function}:")
        for line in slice_value(corpus, function, addr):
            print(f"  {line}")
        return 0

    if not args.store:
        sys.exit("need --store ADDR or --value-of OPADDR")
    addr = int(args.store, 16)
    summaries = summarize.build_summaries(
        corpus.functions, corpus.resolver, corpus.dispatches)
    writers = summarize.writers_of(summaries, addr)
    print(f"=== static writers covering ${addr:04X} "
          f"({len(writers)} sites) ===")
    print("(indexed sites are matched with the actor-slot span 0x33; an "
          "indexed store whose runtime index reaches further — e.g. a "
          "table copy — is NOT listed. reverse_watch remains the "
          "runtime-complete answer.)")
    for function, access in sorted(writers,
                                   key=lambda h: h[1].op_addr):
        summary = summaries[function]
        label = corpus.display(function, summary.entry)
        idx = "[indexed]" if access.indexed else ""
        width = f"{access.width}b" if access.width else "?w"
        print(f"\n{label}  store at {access.op_addr:06X} "
              f"({access.sym or hex(access.ea)}) {width} {idx}")
        graph, ssa = corpus.ir_of(function)
        op = graph.op_at[access.op_addr]
        var = {"STA": "A", "STX": "X", "STY": "Y"}.get(op.mnemonic)
        if op.mnemonic == "STZ":
            print("    value: 0 (STZ)")
        elif op.mnemonic in ("INC", "DEC", "ASL", "LSR", "ROL", "ROR",
                             "TRB", "TSB"):
            print(f"    value: read-modify-write ({op.mnemonic})")
        elif var:
            for line in slice_value(corpus, function, access.op_addr,
                                    var, limit=12):
                print(f"    {line}")
        if args.callers:
            callers = sorted({
                name for name, s in summaries.items()
                if summary.entry in s.calls or
                summary.entry in s.dispatch_targets})
            print(f"    callers: {', '.join(callers[:8]) or '(none/dispatch)'}"
                  + (f" +{len(callers) - 8} more" if len(callers) > 8
                     else ""))
    if args.readers:
        readers = summarize.readers_of(summaries, addr)
        print(f"\n=== static readers ({len(readers)} sites) ===")
        for function, access in sorted(readers,
                                       key=lambda h: h[1].op_addr):
            label = corpus.display(function,
                                   summaries[function].entry)
            print(f"  {label}  read at {access.op_addr:06X} "
                  f"({access.sym or hex(access.ea)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
