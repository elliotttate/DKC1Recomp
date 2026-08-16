#!/usr/bin/env python3
"""Inspect DKC1_OAM_LOG captures: WRAM shadow vs PPU OAM, 9-bit X decode,
and margin-wrap detection.

Record format (dkc1_debug_dump.c): u32 host frame, 544 bytes WRAM OAM shadow
($0200-$041F), 544 bytes PPU OAM (512 low + 32 high).

Rules baked in from the SuperZSNES worklog:
- the WRAM shadow and PPU OAM legitimately disagree by one frame (VBlank DMA
  lag). A single-frame mismatch is lag; a persistent one is a bug. The
  report tracks mismatch streaks, not single frames.
- the classic widescreen defect is a sprite whose 9-bit X lost its high bit:
  art intended for [256, 256+extra) renders at [0, extra). Flag entries in
  that window, and entries at [512-64, 512) (negative/left-margin art)
  whose size makes them visible.
"""
from __future__ import annotations

import argparse
from collections import deque
import json
import struct
import sys
from pathlib import Path

RECORD = 4 + 544 + 544


def decode_entries(oam: bytes) -> list[dict]:
    low, high = oam[:512], oam[512:544]
    entries = []
    for i in range(128):
        x_low, y, tile, attr = low[i * 4:i * 4 + 4]
        extra = (high[i // 4] >> ((i % 4) * 2)) & 3
        x = x_low | ((extra & 1) << 8)
        big = (extra >> 1) & 1
        entries.append({
            "index": i, "x": x, "y": y, "tile": tile, "attr": attr,
            "big": big,
        })
    return entries


def visible(entry: dict) -> bool:
    # Power-on/cleared OAM is all zero in this runtime. It is not authored
    # sprite evidence even though Y=0 is technically inside the scanout.
    return (entry["y"] < 0xF0 and entry["tile"] != 0xFF and
            any(entry[key] for key in ("x", "y", "tile", "attr", "big")))


def load_records(path: Path):
    data = path.read_bytes()
    for offset in range(0, len(data) - RECORD + 1, RECORD):
        frame = struct.unpack_from("<I", data, offset)[0]
        shadow = data[offset + 4:offset + 4 + 544]
        ppu = data[offset + 4 + 544:offset + RECORD]
        yield frame, shadow, ppu


def load_metadata(path: Path) -> dict[int, dict]:
    index = path.with_suffix(".jsonl")
    if not index.exists():
        return {}
    result = {}
    for line in index.read_text(errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(record, dict) and isinstance(record.get("frame"), int):
            result[record["frame"]] = record
    return result


def analyze_oam_pipeline(path: Path, *, extra: int = 43) -> dict:
    """Return the evidence-grade OAM pipeline/wrap verdict.

    Unlike the retired frame-to-frame slot heuristic, this compares a PPU
    entry with the current/recent WRAM shadows and requires matching low X,
    Y, tile, and attributes before calling a missing X-high bit. DKC's OAM
    allocator freely reuses an index for a different object on the next
    frame, so index continuity alone is not object identity.
    """
    metadata = load_metadata(path)
    shadow_history = deque(maxlen=4)
    mismatch_streak = 0
    max_streak = 0
    streak_start = None
    worst_streak = None
    active_grace = 2
    frames = 0
    active_frames = 0
    excluded_forced_blank = 0
    excluded_outside_gameplay = 0
    right_margin_entries = 0
    left_margin_entries = 0
    xhigh_loss_suspects: list[dict] = []

    for frame, shadow, ppu in load_records(path):
        frames += 1
        meta = metadata.get(frame)
        forced_blank = bool(meta and meta.get("forced_blank"))
        outside_gameplay = bool(meta and not meta.get("gameplay"))
        if forced_blank or outside_gameplay:
            excluded_forced_blank += int(forced_blank)
            excluded_outside_gameplay += int(outside_gameplay)
            active_grace = 2
            shadow_history.append(shadow)
            mismatch_streak = 0
            continue

        active_frames += 1
        normal_pipeline = ppu == shadow or ppu in shadow_history
        if active_grace:
            active_grace -= 1
            mismatch_streak = 0
        elif not normal_pipeline:
            if mismatch_streak == 0:
                streak_start = frame
            mismatch_streak += 1
            if mismatch_streak > max_streak:
                max_streak = mismatch_streak
                worst_streak = (streak_start, frame)
        else:
            mismatch_streak = 0

        candidates = [decode_entries(shadow)]
        candidates.extend(decode_entries(blob) for blob in shadow_history)
        for entry in decode_entries(ppu):
            if not visible(entry):
                continue
            if 256 <= entry["x"] < 256 + extra:
                right_margin_entries += 1
            elif 512 - extra <= entry["x"] < 512:
                left_margin_entries += 1
            if 0 <= entry["x"] < extra:
                for candidate_set in candidates:
                    candidate = candidate_set[entry["index"]]
                    if (candidate["x"] >= 256 and
                            (candidate["x"] & 0xFF) == entry["x"] and
                            all(candidate[key] == entry[key]
                                for key in ("y", "tile", "attr"))):
                        xhigh_loss_suspects.append({
                            "frame": frame, "index": entry["index"],
                            "x": entry["x"], "shadow_x": candidate["x"],
                            "y": entry["y"], "tile": entry["tile"],
                        })
                        break
        shadow_history.append(shadow)

    return {
        "frames": frames,
        "active_frames": active_frames,
        "forced_blank_frames_excluded": excluded_forced_blank,
        "outside_gameplay_frames_excluded": excluded_outside_gameplay,
        "metadata_available": bool(metadata),
        "max_shadow_ppu_mismatch_streak": max_streak,
        "worst_streak_frames": worst_streak,
        "verdict": ("persistent_mismatch" if max_streak > 1 else
                    "pipeline_lag_only" if max_streak == 1 else "clean"),
        "right_margin_entries": right_margin_entries,
        "left_margin_entries": left_margin_entries,
        "xhigh_loss_suspects": len(xhigh_loss_suspects),
        "samples": xhigh_loss_suspects[:40],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="DKC1_OAM_LOG prefix or .bin")
    parser.add_argument("--extra", type=int, default=43,
                        help="widescreen margin pixels per side")
    parser.add_argument("--frame", type=int,
                        help="dump full entry table for one frame")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    path = args.log if args.log.suffix == ".bin" \
        else args.log.with_suffix(".bin")
    if not path.exists():
        print(f"missing {path}", file=sys.stderr)
        return 2

    metadata = load_metadata(path)

    wrap_suspects = []
    mismatch_streak = 0
    max_streak = 0
    streak_start = None
    worst_streak = None
    frames = 0
    shadow_history = deque(maxlen=4)
    active_frames = 0
    excluded_forced_blank = 0
    excluded_outside_gameplay = 0
    active_grace = 2

    for frame, shadow, ppu in load_records(path):
        frames += 1
        meta = metadata.get(frame)
        forced_blank = bool(meta and meta.get("forced_blank"))
        outside_gameplay = bool(meta and not meta.get("gameplay"))
        if forced_blank or outside_gameplay:
            if forced_blank:
                excluded_forced_blank += 1
            if outside_gameplay:
                excluded_outside_gameplay += 1
            active_grace = 2
            shadow_history.append(shadow)
            mismatch_streak = 0
            continue

        active_frames += 1
        # A forced-blank interval may intentionally leave the PPU OAM stale.
        # Give the normal DMA pipeline two displayed frames to catch up, then
        # compare against a four-frame shadow history rather than assuming a
        # fixed one-frame delay.
        normal_pipeline = ppu == shadow or ppu in shadow_history
        if active_grace:
            active_grace -= 1
            mismatch_streak = 0
        elif not normal_pipeline:
            if mismatch_streak == 0:
                streak_start = frame
            mismatch_streak += 1
            if mismatch_streak > max_streak:
                max_streak = mismatch_streak
                worst_streak = (streak_start, frame)
        else:
            mismatch_streak = 0
        shadow_history.append(shadow)

        shadow_candidates = [decode_entries(shadow)]
        shadow_candidates.extend(decode_entries(blob)
                                 for blob in shadow_history)
        for entry in decode_entries(ppu):
            if not visible(entry):
                continue
            # A low X value is not itself suspicious: legitimate sprites
            # cross the native left edge constantly. Require direct evidence
            # that the same logical OAM entry (same low byte/Y/tile/attr) had
            # X-high set in the current/recent WRAM shadow.
            if 0 <= entry["x"] < args.extra:
                for candidate_set in shadow_candidates:
                    candidate = candidate_set[entry["index"]]
                    if (candidate["x"] >= 256 and
                            (candidate["x"] & 0xFF) == entry["x"] and
                            all(candidate[key] == entry[key]
                                for key in ("y", "tile", "attr"))):
                        wrap_suspects.append({
                            "frame": frame, "index": entry["index"],
                            "x": entry["x"], "shadow_x": candidate["x"],
                            "y": entry["y"], "tile": entry["tile"],
                            "suspect": "xhigh_lost_shadow_to_ppu",
                        })
                        break
            elif 256 <= entry["x"] < 256 + args.extra:
                wrap_suspects.append({
                    "frame": frame, "index": entry["index"],
                    "x": entry["x"], "y": entry["y"],
                    "tile": entry["tile"],
                    "suspect": "right_margin_entry",
                })
            elif 512 - args.extra <= entry["x"] < 512:
                wrap_suspects.append({
                    "frame": frame, "index": entry["index"],
                    "x": entry["x"], "y": entry["y"],
                    "tile": entry["tile"],
                    "suspect": "left_margin_entry",
                })

        if args.frame is not None and frame == args.frame:
            print(f"frame {frame} shadow==ppu: {shadow == ppu}")
            for label, blob in (("shadow", shadow), ("ppu", ppu)):
                live = [e for e in decode_entries(blob) if visible(e)]
                print(f"  {label}: {len(live)} visible")
                for e in live:
                    print(f"    #{e['index']:3d} x={e['x']:3d} "
                          f"y={e['y']:3d} tile=${e['tile']:02X} "
                          f"attr=${e['attr']:02X} big={e['big']}")

    report = {
        "frames": frames,
        "active_frames": active_frames,
        "forced_blank_frames_excluded": excluded_forced_blank,
        "outside_gameplay_frames_excluded": excluded_outside_gameplay,
        "metadata_available": bool(metadata),
        "max_shadow_ppu_mismatch_streak": max_streak,
        "worst_streak_frames": worst_streak,
        "verdict": ("persistent_mismatch" if max_streak > 1 else
                    "pipeline_lag_only" if max_streak == 1 else "clean"),
        "right_margin_entries": len(
            [s for s in wrap_suspects
             if s["suspect"] == "right_margin_entry"]),
        "left_margin_entries": len(
            [s for s in wrap_suspects
             if s["suspect"] == "left_margin_entry"]),
        "xhigh_loss_suspects": len(
            [s for s in wrap_suspects
             if s["suspect"] == "xhigh_lost_shadow_to_ppu"]),
        "samples": wrap_suspects[:40],
    }
    text = json.dumps(report, indent=1)
    print(text)
    if args.json_out:
        args.json_out.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
