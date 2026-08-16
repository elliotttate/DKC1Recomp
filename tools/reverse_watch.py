#!/usr/bin/env python3
"""Reverse watchpoint: WHO last changed this address before frame F?

Replays a deterministic route (or a snapshot start) under the trace-hook
host with a WRAM watch armed, then answers backward queries against the
attributed change log: the last write to each watched byte at-or-before
the target frame, with the responsible function named, plus surrounding
events for context. Determinism makes one forward pass equivalent to
reverse execution at function granularity.

Escalation: when the answer must be instruction-exact, force_lle the
reported function's region in recomp/*.cfg and re-run with DKC1_TRACE_PC
— the function attribution from this tool tells you exactly which region
to escalate.

usage:
  python tools/reverse_watch.py --rom R --route recipes/route_death.dks \\
      --address 1595:34 --before-frame 7600
  python tools/reverse_watch.py --rom R --state overlap.state \\
      --address 088B --before-frame 400 --frames 600
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--exe", type=Path,
                        default=REPO / "build/dkc1_headless_trace.exe")
    parser.add_argument("--route", type=Path)
    parser.add_argument("--state", type=Path,
                        help="start from a snapshot instead of power-on")
    parser.add_argument("--address", required=True,
                        help="hex WRAM offset, optional :len (e.g. 1595:34)")
    parser.add_argument("--before-frame", type=int, required=True)
    parser.add_argument("--frames", type=int, default=None,
                        help="run length (default: before-frame + 120)")
    parser.add_argument("--widescreen", action="store_true")
    parser.add_argument("--context", type=int, default=6,
                        help="surrounding events to show")
    parser.add_argument("--work", type=Path,
                        default=REPO / "build" / "reverse_watch")
    args = parser.parse_args()

    if not args.route and not args.state:
        sys.exit("need --route or --state")
    args.work.mkdir(parents=True, exist_ok=True)
    watch_log = args.work / "watch.jsonl"
    watch_log.unlink(missing_ok=True)

    frames = args.frames or (args.before_frame + 120)
    env = os.environ.copy()
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env.pop("DKC1_SCRIPT", None)
    env["DKC1_WIDESCREEN"] = "1" if args.widescreen else "0"
    env["SNESRECOMP_WATCH"] = args.address
    env["SNESRECOMP_WATCH_LOG"] = str(watch_log.resolve())
    if args.route:
        env["DKC1_SCRIPT"] = str(args.route.resolve())
    if args.state:
        env["DKC1_SAVESTATE_INPUT"] = str(args.state.resolve())
    print(f"replaying {frames} frames with watch {args.address} ...")
    result = subprocess.run(
        [str(args.exe.resolve()), str(args.rom.resolve()), str(frames)],
        cwd=str(args.work), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        sys.exit(f"replay failed rc={result.returncode}: "
                 f"{result.stderr[-400:]}")

    events = []
    if watch_log.exists():
        for line in watch_log.read_text(errors="replace").splitlines():
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    if not events:
        print("no changes to the watched range in the entire run")
        return 0

    before = [e for e in events if e["frame"] <= args.before_frame]
    print(f"{len(events)} attributed changes total; "
          f"{len(before)} at or before frame {args.before_frame}\n")
    if not before:
        first = events[0]
        print(f"first change is AFTER the target frame: f{first['frame']} "
              f"{first['addr']} {first['old']}->{first['new']} "
              f"by {first['writer']}")
        return 0

    # last change per byte address
    last_by_addr = {}
    for e in before:
        last_by_addr[e["addr"]] = e
    print("last change per byte at/before the target frame:")
    for addr in sorted(last_by_addr):
        e = last_by_addr[addr]
        print(f"  {addr}  {e['old']} -> {e['new']}  f{e['frame']}  "
              f"by {e['writer']} ({e['writer_pc']})")

    latest = max(before, key=lambda e: e["frame"])
    idx = events.index(latest)
    lo = max(0, idx - args.context)
    hi = min(len(events), idx + args.context + 1)
    print(f"\ncontext around the latest change (f{latest['frame']}):")
    for e in events[lo:hi]:
        marker = ">>" if e is latest else "  "
        print(f"{marker} f{e['frame']}  {e['addr']} "
              f"{e['old']}->{e['new']}  {e['writer']}")

    print(f"\nnext steps: python tools/atlas.py "
          f"{latest['writer_pc'].replace('0x', '')}   "
          f"(and structure.py for the listing); for instruction-level "
          f"attribution, force_lle that function's region and re-run "
          f"with DKC1_TRACE_PC.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
