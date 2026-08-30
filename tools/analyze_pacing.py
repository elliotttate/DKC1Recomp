#!/usr/bin/env python3
"""Summarize a DKC1_PACING_LOG capture after an optional warm-up."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import sys


SUPPORTED_SCHEMAS = {"dkc1.pacing.v1", "dkc1.pacing.v2"}


def percentile(values: list[float], percent: float) -> float:
    """Return a linearly interpolated percentile for a non-empty sample."""
    if not values:
        raise ValueError("cannot calculate a percentile of an empty sample")
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percent / 100.0
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction


def load_log(path: Path) -> tuple[dict, list[dict]]:
    header: dict | None = None
    frames: list[dict] = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
            if header is None:
                schema = record.get("schema")
                if schema not in SUPPORTED_SCHEMAS:
                    raise ValueError(
                        f"{path}:{line_number}: unsupported schema {schema!r}")
                if not isinstance(record.get("refresh_hz"), (int, float)):
                    raise ValueError(
                        f"{path}:{line_number}: refresh_hz is missing")
                header = record
                continue
            for field in ("frame", "work_ms", "wait_ms", "late_ms",
                          "present_interval_ms", "overruns"):
                if not isinstance(record.get(field), (int, float)):
                    raise ValueError(
                        f"{path}:{line_number}: {field} is missing")
            frames.append(record)
    if header is None:
        raise ValueError(f"{path}: pacing log is empty")
    if not frames:
        raise ValueError(f"{path}: pacing log contains no frames")
    return header, frames


def metric(values: list[float]) -> dict:
    return {
        "min": min(values),
        "p50": percentile(values, 50),
        "p95": percentile(values, 95),
        "p99": percentile(values, 99),
        "max": max(values),
        "mean": statistics.fmean(values),
    }


def analyze(header: dict, frames: list[dict], warmup: int = 30) -> dict:
    if warmup < 0:
        raise ValueError("warm-up frame count must not be negative")
    if warmup >= len(frames):
        raise ValueError(
            f"warm-up ({warmup}) leaves no frames from {len(frames)} samples")
    steady = frames[warmup:]
    schema = header["schema"]
    interval_field = ("submit_interval_ms"
                      if schema == "dkc1.pacing.v2"
                      else "present_interval_ms")
    interval_values = [float(item[interval_field]) for item in steady
                       if float(item[interval_field]) > 0.0]
    if not interval_values:
        raise ValueError(f"no usable {interval_field} samples")
    first_overruns = int(frames[warmup - 1]["overruns"]) if warmup else 0
    summary = {
        "schema": schema,
        "refresh_hz": float(header["refresh_hz"]),
        "target_interval_ms": 1000.0 / float(header["refresh_hz"]),
        "captured_frames": len(frames),
        "warmup_frames": warmup,
        "steady_frames": len(steady),
        "interval_source": interval_field,
        "interval_ms": metric(interval_values),
        "work_ms": metric([float(item["work_ms"]) for item in steady]),
        "wait_ms": metric([float(item["wait_ms"]) for item in steady]),
        "late_ms": metric([float(item["late_ms"]) for item in steady]),
        "steady_overruns": int(steady[-1]["overruns"]) - first_overruns,
    }
    if schema == "dkc1.pacing.v2":
        summary["submit_error_ms"] = metric(
            [abs(float(item["submit_error_ms"])) for item in steady])
        summary["present_ms"] = metric(
            [float(item["present_ms"]) for item in steady])
        summary["completion_interval_ms"] = metric(
            [float(item["present_interval_ms"]) for item in steady
             if float(item["present_interval_ms"]) > 0.0])
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--warmup", type=int, default=30)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        header, frames = load_log(args.log)
        summary = analyze(header, frames, args.warmup)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
        return 0
    interval = summary["interval_ms"]
    work = summary["work_ms"]
    print(f"{summary['schema']}: {summary['steady_frames']} steady frames "
          f"after {summary['warmup_frames']} warm-up frames")
    print(f"target {summary['target_interval_ms']:.4f} ms; "
          f"{summary['interval_source']} p50 {interval['p50']:.4f}, "
          f"p95 {interval['p95']:.4f}, p99 {interval['p99']:.4f}, "
          f"max {interval['max']:.4f} ms")
    print(f"work p50 {work['p50']:.4f}, p99 {work['p99']:.4f}, "
          f"max {work['max']:.4f} ms; "
          f"steady overruns {summary['steady_overruns']}")
    if "submit_error_ms" in summary:
        error = summary["submit_error_ms"]
        present = summary["present_ms"]
        print(f"absolute submit error p99 {error['p99']:.4f} ms; "
              f"GDI present p99 {present['p99']:.4f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
