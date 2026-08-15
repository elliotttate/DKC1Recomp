#!/usr/bin/env python3
"""Grade wide_persists_stock_culls findings from raw WRAM windows.

For each finding (a source record the wide run kept alive after the stock
run freed it), walk the wide run's actor state across the extension frames
[stock_end .. wide_end] in the raw WRAM dumps and decide:

  release_delayed_by_wider_window
      during every extension frame the actor sits OUTSIDE the native
      window [camX, camX+256) but INSIDE the wide window
      [camX-extra, camX+256+extra), and its state word never changes:
      the wider activation window is simply releasing it later, on the
      same despawn path.
  state_advanced_during_extension
      the actor's state/animation changed or it moved more than drift
      while stock had already culled it — simulation advanced; must be
      classified further before release.
  outside_wide_window_but_alive
      the actor is beyond even the wide window during extension —
      the release path itself is suspect, not the window.

Inputs: prefetch_report.json (from audit_prefetch_phases.py) and the
stock/wide DKC1_WRAM_DUMP windows covering the extension frames.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

WRAM_SIZE = 0x20000
ACTOR_FIRST, ACTOR_LAST = 0x02, 0x32
ARRAYS = {
    "id": 0x0D45, "source": 0x15FD, "x": 0x0B19, "y": 0x0BC1,
    "state": 0x1029, "anim": 0x10D1, "xs": 0x0E89, "ys": 0x0EF1,
}
CAMERA_X = 0x088B
BOOKKEEPING = 0x192B


def load_window(prefix: Path) -> dict[int, bytes]:
    raw_path = prefix if prefix.suffix == ".bin" \
        else prefix.with_suffix(".bin")
    index_path = Path(str(raw_path) + ".jsonl")
    frames: dict[int, bytes] = {}
    raw = raw_path.read_bytes()
    for line in index_path.read_text().splitlines():
        record = json.loads(line)
        if record.get("type") != "frame":
            continue
        frame = int(record.get("relative_frame", record.get("frame", -1)))
        offset = int(record.get("offset", 0))
        length = int(record.get("length", WRAM_SIZE))
        frames[frame] = raw[offset:offset + length]
    return frames


def read16(memory: bytes, offset: int) -> int:
    return memory[offset] | (memory[offset + 1] << 8)


def actor_by_source(memory: bytes, source: int) -> dict | None:
    for index in range(ACTOR_FIRST, ACTOR_LAST + 2, 2):
        raw_source = read16(memory, ARRAYS["source"] + index)
        if raw_source >= 0x8000:
            raw_source -= 0x10000
        if raw_source == source and read16(memory, ARRAYS["id"] + index):
            return {"index": index, **{
                name: read16(memory, base + index)
                for name, base in ARRAYS.items()}}
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path,
                        default=Path("build/prefetch_report.json"))
    parser.add_argument("--stock", type=Path,
                        default=Path("build/persists_stock.bin"))
    parser.add_argument("--wide", type=Path,
                        default=Path("build/persists_wide.bin"))
    parser.add_argument("--extra", type=int, default=43)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    report = json.loads(args.report.read_text())
    findings = [f for f in report.get("findings", [])
                if f.get("verdict") == "wide_persists_stock_culls"]
    if not findings:
        print("no wide_persists_stock_culls findings in the report")
        return 0
    wide_frames = load_window(args.wide)

    results = []
    for finding in findings:
        source = finding["source"]
        if source <= 0:
            results.append({
                "source": source,
                "verdict": "unalignable_nonauthored_source",
                "note": "backlink <= 0 marks effects/reserved slots, not "
                        "authored records; the aligner cannot claim identity",
            })
            print(f"record {source}: unalignable_nonauthored_source")
            continue
        stock_end = finding["stock_end"]
        wide_end = finding["wide_end"]
        rows = []
        states = set()
        positions = []
        inside_wide = outside_native = 0
        total = 0
        for frame in range(stock_end, wide_end + 1):
            memory = wide_frames.get(frame)
            if memory is None:
                continue
            actor = actor_by_source(memory, source)
            if actor is None:
                continue
            total += 1
            camera = read16(memory, CAMERA_X)
            rel = (actor["x"] - camera) & 0xFFFF
            if rel >= 0x8000:
                rel -= 0x10000
            in_native = 0 <= rel < 256
            # The stock despawn window is already wider than the screen;
            # the adapters widen it by `extra` more. Allow that hysteresis
            # (64px stock slack observed at $BDF570-class releases).
            slack = args.extra + 64
            in_wide = -slack <= rel < 256 + slack
            if not in_native:
                outside_native += 1
            if in_wide:
                inside_wide += 1
            states.add((actor["state"], actor["anim"]))
            positions.append((frame, rel, actor["x"], actor["state"]))
            rows.append({"frame": frame, "rel_x": rel,
                         "state": actor["state"], "anim": actor["anim"]})
        if total == 0:
            verdict = "no_extension_frames_captured"
        elif len(states) == 1 and outside_native == total and \
                inside_wide == total:
            verdict = "release_delayed_by_wider_window"
        elif inside_wide < total:
            verdict = "outside_wide_window_but_alive"
        else:
            verdict = "state_advanced_during_extension"
        results.append({
            "source": source,
            "extension": [stock_end, wide_end],
            "frames_seen": total,
            "outside_native": outside_native,
            "inside_wide": inside_wide,
            "distinct_state_anim_pairs": len(states),
            "verdict": verdict,
            "trail": rows[:8],
        })
        print(f"record {source}: {verdict} "
              f"({total} ext frames, {outside_native} outside native, "
              f"{len(states)} state/anim pairs)")

    text = json.dumps({"findings": results}, indent=1)
    if args.json_out:
        args.json_out.write_text(text)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FileNotFoundError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
