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
WATCH_MAX_BYTES = 4096
WATCH_MAX_RANGES = 16


def validate_watch_spec(spec: str) -> list[tuple[int, int]]:
    """Mirror the lean runtime's fail-closed range grammar."""
    ranges: list[tuple[int, int]] = []
    total = 0
    for raw_part in spec.split(","):
        part = raw_part.strip()
        if not part or len(ranges) >= WATCH_MAX_RANGES:
            raise ValueError("empty or too many watch ranges")
        pieces = part.split(":")
        if len(pieces) > 2 or not pieces[0]:
            raise ValueError(f"malformed watch range: {part!r}")
        try:
            address = int(pieces[0], 16)
            length = int(pieces[1], 16) if len(pieces) == 2 else 2
        except ValueError as exc:
            raise ValueError(f"non-hex watch range: {part!r}") from exc
        if not 0 <= address <= 0x1FFFF or not 1 <= length <= WATCH_MAX_BYTES:
            raise ValueError(f"watch range outside WRAM: {part!r}")
        if address + length > 0x20000 or total + length > WATCH_MAX_BYTES:
            raise ValueError(f"watch range exceeds bounded capture: {part!r}")
        if any(address < old_address + old_length and
               old_address < address + length
               for old_address, old_length in ranges):
            raise ValueError(f"overlapping watch range: {part!r}")
        ranges.append((address, length))
        total += length
    return ranges


def load_watch_events(path: Path) -> tuple[list[dict], list[dict]]:
    changes: list[dict] = []
    truncations: list[dict] = []
    if not path.exists():
        return changes, truncations
    for line in path.read_text(errors="replace").splitlines():
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if row.get("type") == "watch_truncated":
            truncations.append(row)
        elif row.get("type") in (None, "watch_change") and all(
                key in row for key in ("frame", "addr", "old", "new")):
            changes.append(row)
    return changes, truncations


def writer_text(event: dict) -> str:
    pc = event.get("writer_pc")
    return (f"{event.get('writer', '?')} ({pc})" if pc
            else f"{event.get('writer', 'unattributed')} (unattributed)")


def attribution_counts(events: list[dict]) -> tuple[int, int]:
    """Count explicit rows, with writer_pc as the legacy-schema fallback."""
    attributed = sum(bool(event.get(
        "attributed", event.get("writer_pc") is not None)) for event in events)
    return attributed, len(events) - attributed


def attribution_summary(events: list[dict], before_count: int,
                        before_frame: int) -> str:
    attributed, unattributed = attribution_counts(events)
    return (f"{len(events)} observed changes total "
            f"({attributed} attributed, {unattributed} unattributed); "
            f"{before_count} observed at or before frame {before_frame}")


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
    try:
        validate_watch_spec(args.address)
    except ValueError as exc:
        sys.exit(f"invalid --address: {exc}")
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

    events, truncations = load_watch_events(watch_log)
    if truncations:
        frames_text = ", ".join(str(row.get("frame", "?"))
                                for row in truncations[:8])
        sys.exit("watch log was truncated at frame(s) " + frames_text +
                 "; narrow the watched ranges before drawing a conclusion")
    if not events:
        print("no changes to the watched range in the entire run")
        return 0

    before = [e for e in events if e["frame"] <= args.before_frame]
    print(attribution_summary(events, len(before), args.before_frame) + "\n")
    if not before:
        first = events[0]
        print(f"first change is AFTER the target frame: f{first['frame']} "
              f"{first['addr']} {first['old']}->{first['new']} "
              f"by {writer_text(first)}")
        return 0

    # last change per byte address
    last_by_addr = {}
    for e in before:
        last_by_addr[e["addr"]] = e
    print("last change per byte at/before the target frame:")
    for addr in sorted(last_by_addr):
        e = last_by_addr[addr]
        print(f"  {addr}  {e['old']} -> {e['new']}  f{e['frame']}  "
              f"by {writer_text(e)}")

    idx = max(index for index, event in enumerate(events)
              if event["frame"] <= args.before_frame)
    latest = events[idx]
    lo = max(0, idx - args.context)
    hi = min(len(events), idx + args.context + 1)
    print(f"\ncontext around the latest change (f{latest['frame']}):")
    for e in events[lo:hi]:
        marker = ">>" if e is latest else "  "
        print(f"{marker} f{e['frame']}  {e['addr']} "
              f"{e['old']}->{e['new']}  {writer_text(e)}")

    if latest.get("attributed", latest.get("writer_pc") is not None):
        print(f"\nnext steps: python tools/atlas.py "
              f"{latest['writer_pc'].replace('0x', '')}   "
              f"(and structure.py for the listing); for instruction-level "
              f"attribution, force_lle that function's region and re-run "
              f"with DKC1_TRACE_PC.")
    else:
        print("\nlatest change occurred outside a generated-function window; "
              "it is intentionally unattributed. Inspect interpreter, host, "
              "and state-load boundaries instead of escalating the preceding "
              "generated function.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
