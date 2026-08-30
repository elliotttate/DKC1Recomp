#!/usr/bin/env python3
"""Summarize native macOS CAMetalDisplayLink physical-presentation traces."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import Counter
from pathlib import Path


def percentile(values: list[float], probability: float) -> float:
    if not values:
        return math.nan
    ordered = sorted(values)
    index = (len(ordered) - 1) * probability
    lower = math.floor(index)
    upper = math.ceil(index)
    if lower == upper:
        return ordered[lower]
    fraction = index - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument(
        "--warmup", type=int, default=120,
        help="physical presentations to ignore (default: 120)")
    args = parser.parse_args()

    records: list[dict[str, object]] = []
    header: dict[str, object] | None = None
    with args.trace.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                item = json.loads(line)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{args.trace}:{line_number}: {error}")
            if "schema" in item:
                header = item
            elif "presented_time" in item:
                records.append(item)

    steady_all = records[max(0, args.warmup):]
    skipped_drawables = sum(
        float(item["presented_time"]) <= 0.0 for item in steady_all)
    steady = [
        item for item in steady_all if float(item["presented_time"]) > 0.0
    ]
    intervals = [
        float(item["scanout_interval_ms"])
        for item in steady
        if float(item["scanout_interval_ms"]) > 0.0
    ]
    if not intervals:
        raise SystemExit("no physical presentation intervals after warmup")

    source_runs: list[int] = []
    last_frame: int | None = None
    run_length = 0
    source_frames: list[int] = []
    source_transition_times: list[float] = []
    for item in steady:
        frame = int(item["host_frame"])
        if frame != last_frame:
            if run_length:
                source_runs.append(run_length)
            source_frames.append(frame)
            source_transition_times.append(float(item["presented_time"]))
            last_frame = frame
            run_length = 1
        else:
            run_length += 1
    if run_length:
        source_runs.append(run_length)

    missing_source_frames = sum(
        max(0, current - previous - 1)
        for previous, current in zip(source_frames, source_frames[1:])
        if current > previous
    )
    backward_source_transitions = sum(
        current < previous
        for previous, current in zip(source_frames, source_frames[1:])
    )
    source_transition_intervals = [
        (current - previous) * 1000.0
        for previous, current in zip(
            source_transition_times, source_transition_times[1:])
    ]
    coalesced_source_transitions = sum(
        interval <= 0.001 for interval in source_transition_intervals)
    positive_source_intervals = [
        interval for interval in source_transition_intervals
        if interval > 0.001
    ]
    camera_transitions = 0
    large_camera_steps = 0
    previous_record: dict[str, object] | None = None
    for item in steady:
        if previous_record is not None and (
                int(item["host_frame"]) !=
                int(previous_record["host_frame"])):
            camera_transitions += 1
            dx = (int(item["camera_x"]) -
                  int(previous_record["camera_x"])) & 0xFFFF
            if dx >= 0x8000:
                dx -= 0x10000
            if abs(dx) > 2:
                large_camera_steps += 1
        previous_record = item

    header_text = header or {}
    print(
        f"schema={header_text.get('schema', 'unknown')} "
        f"requested_display_hz={header_text.get('requested_display_hz', '?')} "
        f"presentations={len(steady)} skipped_drawables={skipped_drawables}")
    print(
        "scanout_interval_ms "
        f"p50={percentile(intervals, 0.50):.6f} "
        f"p95={percentile(intervals, 0.95):.6f} "
        f"p99={percentile(intervals, 0.99):.6f} "
        f"max={max(intervals):.6f} "
        f"stdev={statistics.pstdev(intervals):.6f}")
    print(f"physical_hz={1000.0 / statistics.mean(intervals):.6f}")
    print(f"source_repeats={dict(sorted(Counter(source_runs).items()))}")
    if positive_source_intervals:
        print(
            "source_transition_interval_ms "
            f"p50={percentile(positive_source_intervals, 0.50):.6f} "
            f"p95={percentile(positive_source_intervals, 0.95):.6f} "
            f"p99={percentile(positive_source_intervals, 0.99):.6f} "
            f"max={max(positive_source_intervals):.6f} "
            f"coalesced={coalesced_source_transitions}")
    print(
        f"source_transitions={max(0, len(source_frames) - 1)} "
        f"missing_source_frames={missing_source_frames} "
        f"backward_source_transitions={backward_source_transitions}")
    print(
        f"camera_transitions={camera_transitions} "
        f"camera_steps_over_2px={large_camera_steps}")
    first = steady_all[0]
    tail = steady_all[-1]
    print(
        f"producer_drops={int(tail.get('producer_drops', 0)) - int(first.get('producer_drops', 0))} "
        f"consumer_skips={int(tail.get('consumer_skips', 0)) - int(first.get('consumer_skips', 0))} "
        f"starved_callbacks={int(tail.get('starved_callbacks', 0)) - int(first.get('starved_callbacks', 0))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
