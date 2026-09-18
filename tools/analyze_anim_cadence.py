#!/usr/bin/env python3
"""Summarize a DKC1_ANIM_CADENCE_LOG capture.

The runtime writes one JSON line per guest frame listing the camera and each
live normal-pool actor as [slot, sprite_id, displayed_pose, anim_frame,
anim_id, world_x, world_y]. This script measures, per sprite id, how many
frames each displayed pose is held, i.e. the effective animation rate the
cartridge produces at 60 Hz, and how fast actors and the camera move per
frame. Both numbers bound what presentation-side interpolation can add:
position interpolation halves every step of two or more pixels, while pose
changes remain at the cartridge's own cadence unless in-between cells exist.
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def percentile(values: list[float], percent: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("empty sample")
    position = (len(ordered) - 1) * percent / 100.0
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def signed16(value: int) -> int:
    value &= 0xFFFF
    return value - 0x10000 if value >= 0x8000 else value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--warmup", type=int, default=0,
                        help="ignore this many leading frames")
    parser.add_argument("--min-samples", type=int, default=20,
                        help="only report sprite ids with at least this many "
                             "pose holds")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    frames: list[dict] = []
    with args.log.open("r", encoding="utf-8") as stream:
        for line in stream:
            line = line.strip()
            if not line:
                continue
            record = json.loads(line)
            if "schema" in record:
                if record["schema"] != "dkc1.anim_cadence.v1":
                    raise SystemExit(f"unsupported schema {record['schema']!r}")
                continue
            frames.append(record)
    frames = frames[args.warmup:]
    if len(frames) < 2:
        raise SystemExit("not enough frames")

    # Episodes: contiguous frames where a slot keeps the same sprite id.
    holds: dict[int, list[int]] = defaultdict(list)     # id -> hold lengths
    speeds: dict[int, list[int]] = defaultdict(list)    # id -> |dx| per frame
    cam_speeds: list[int] = []
    pose_changes_per_frame: list[int] = []
    live_per_frame: list[int] = []
    previous: dict[int, tuple] = {}
    previous_cam = None
    current_hold: dict[int, int] = {}
    for frame in frames:
        cam = frame["cam"]
        if previous_cam is not None:
            cam_speeds.append(abs(signed16(cam[0] - previous_cam[0])))
        previous_cam = cam
        seen = set()
        changes = 0
        live = 0
        for slot, sprite_id, pose, _anim_frame, _anim_id, x, y in frame["actors"]:
            live += 1
            seen.add(slot)
            prior = previous.get(slot)
            if prior and prior[0] == sprite_id:
                speeds[sprite_id].append(abs(signed16(x - prior[2])))
                if pose != prior[1]:
                    holds[sprite_id].append(current_hold[slot])
                    current_hold[slot] = 1
                    changes += 1
                else:
                    current_hold[slot] += 1
            else:
                current_hold[slot] = 1
            previous[slot] = (sprite_id, pose, x, y, cam[0])
        for slot in list(previous):
            if slot not in seen:
                del previous[slot]
                current_hold.pop(slot, None)
        pose_changes_per_frame.append(changes)
        live_per_frame.append(live)

    report = {
        "frames": len(frames),
        "live_actors_mean": statistics.fmean(live_per_frame),
        "pose_changes_per_frame_mean": statistics.fmean(pose_changes_per_frame),
        "camera_px_per_frame": {
            "mean": statistics.fmean(cam_speeds) if cam_speeds else 0.0,
            "moving_frames": sum(1 for s in cam_speeds if s),
            "frames_step_ge_2": sum(1 for s in cam_speeds if s >= 2),
            "histogram": dict(sorted(Counter(cam_speeds).items())),
        },
        "sprites": {},
    }
    all_holds: list[int] = []
    for sprite_id, values in sorted(holds.items()):
        all_holds.extend(values)
        if len(values) < args.min_samples:
            continue
        mean_hold = statistics.fmean(values)
        speed = speeds.get(sprite_id, [])
        report["sprites"][f"0x{sprite_id:04x}"] = {
            "pose_holds": len(values),
            "hold_frames_mean": round(mean_hold, 3),
            "hold_frames_p50": percentile(values, 50),
            "hold_frames_p90": percentile(values, 90),
            "hold_frames_min": min(values),
            "hold_frames_max": max(values),
            "poses_per_second": round(60.0 / mean_hold, 2),
            "hold_histogram": dict(sorted(Counter(values).items())[:12]),
            "world_px_per_frame_mean": round(statistics.fmean(speed), 3) if speed else 0.0,
            "world_frames_step_ge_2": sum(1 for s in speed if s >= 2),
            "world_frames_moving": sum(1 for s in speed if s),
        }
    if all_holds:
        report["all_sprites"] = {
            "pose_holds": len(all_holds),
            "hold_frames_mean": round(statistics.fmean(all_holds), 3),
            "hold_frames_p50": percentile(all_holds, 50),
            "hold_frames_p90": percentile(all_holds, 90),
            "poses_per_second_at_mean_hold": round(60.0 / statistics.fmean(all_holds), 2),
            "holds_of_1_frame": sum(1 for h in all_holds if h == 1),
            "holds_of_2_frames": sum(1 for h in all_holds if h == 2),
            "holds_over_4_frames": sum(1 for h in all_holds if h > 4),
        }

    print(json.dumps(report, indent=2))
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
