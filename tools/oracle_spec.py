#!/usr/bin/env python3
"""Stage 5 (host side): differential-oracle capture specs from the IR.

For a function, emit exactly WHAT the oracle harness must capture at a
traced entry and compare at exit:
  capture: A X Y S D DB P + every address in the control-flow-closed read set
  compare: exit registers/flags + the control-flow-closed ordered write set
Eligibility is honest: a function whose closure contains indirect
writes, unresolved calls or continuations, or MMIO/DMA effects cannot
be compared by state-diff alone and is marked accordingly (the harness
must run it under an LLE shadow with a write log instead).

The engine-side remainder (entry-state snapshot in the trace hook,
interpreter re-execution, diff) consumes these specs; see
docs/SSA_IR_DESIGN.md stage 5.

usage: python tools/oracle_spec.py Player_HandleHitEvents
       python tools/oracle_spec.py --emit-all   # build/ir/oracle_specs.json
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import atlas  # noqa: E402
from ir import decode, memtype, summarize  # noqa: E402

REPO = TOOLS.parent
OUT_ALL = REPO / "build" / "ir" / "oracle_specs.json"


def spec_for(name: str, summaries: dict) -> dict:
    effects = summarize.transitive_effects(summaries, name)
    reads = sorted({a.ea for a in effects["reads"]
                    if a.region == "wram" and not a.indexed})
    read_arrays = sorted({a.ea for a in effects["reads"]
                          if a.region == "wram" and a.indexed})
    writes = sorted({a.ea for a in effects["writes"]
                     if a.region == "wram" and not a.indexed})
    write_arrays = sorted({a.ea for a in effects["writes"]
                           if a.region == "wram" and a.indexed})
    mmio_writes = sorted({a.ea for a in effects["writes"]
                          if a.region == "mmio"})
    blockers = []
    if effects["indirect_writes"]:
        blockers.append(f"{effects['indirect_writes']} indirect writes")
    if effects["unresolved_calls"]:
        blockers.append(
            f"{effects['unresolved_calls']} unresolved/deep calls")
    if effects["unresolved_external"]:
        blockers.append(
            f"{effects['unresolved_external']} unresolved/deep external "
            "control-flow continuations")
    if mmio_writes:
        blockers.append(f"{len(mmio_writes)} MMIO writes "
                        "(order-sensitive; needs write-log compare)")
    return {
        "function": name,
        "entry": f"0x{summaries[name].entry:06X}",
        "closure_functions": effects["functions_visited"],
        "capture": {
            "registers": ["A", "X", "Y", "S", "D", "DB", "P", "PB"],
            "wram_reads": [f"0x{a:X}" for a in reads],
            "wram_read_arrays": [
                {"base": f"0x{a:X}", "span": "0x34"}
                for a in read_arrays],
            "indirect_reads": effects["indirect_reads"],
        },
        "compare": {
            "registers": ["A", "X", "Y", "S", "D", "DB", "P"],
            "wram_writes": [f"0x{a:X}" for a in writes],
            "wram_write_arrays": [
                {"base": f"0x{a:X}", "span": "0x34"}
                for a in write_arrays],
            "mmio_writes": [f"0x{a:X}" for a in mmio_writes],
        },
        "eligibility": "oracle-ready" if not blockers
        else "needs-lle-shadow",
        "blockers": blockers,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query", nargs="?")
    parser.add_argument("--emit-all", action="store_true")
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if not args.query and not args.emit_all:
        parser.error("need a function or --emit-all")

    summaries = summarize.build_summaries()
    if args.emit_all:
        specs = {}
        ready = 0
        for name in sorted(summaries):
            spec = spec_for(name, summaries)
            specs[name] = spec
            ready += spec["eligibility"] == "oracle-ready"
        OUT_ALL.parent.mkdir(parents=True, exist_ok=True)
        OUT_ALL.write_text(json.dumps(
            {"schema": "dkc1.ir.oracle-specs.v1", "functions": specs},
            indent=1))
        print(f"{len(specs)} specs -> {OUT_ALL} "
              f"({ready} oracle-ready, {len(specs) - ready} "
              f"needs-lle-shadow)")
        return 0

    name = args.query
    if name not in summaries:
        code_names, _ = atlas.load_rename_map()
        for ea, entry in code_names.items():
            if entry.get("name", "").lower() == name.lower():
                for label, s in summaries.items():
                    if s.entry == ea:
                        name = label
                        break
        if name not in summaries:
            sys.exit(f"unknown function {args.query}")
    spec = spec_for(name, summaries)
    text = json.dumps(spec, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
