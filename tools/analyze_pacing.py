#!/usr/bin/env python3
"""Summarize a DKC1_PACING_LOG capture after an optional warm-up."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import sys


SUPPORTED_SCHEMAS = {
    "dkc1.pacing.v1", "dkc1.pacing.v2", "dkc1.pacing.v3", "dkc1.pacing.v4",
    "dkc1.pacing.v5",
}
# Schemas whose cadence measurement is the submit timestamp (v1 only had
# the completion timestamp), whose rows carry CPU/audio phases, and whose
# rows carry the 120 Hz midpoint fields.
SUBMIT_SCHEMAS = {"dkc1.pacing.v2", "dkc1.pacing.v3", "dkc1.pacing.v4",
                  "dkc1.pacing.v5"}
CPU_SCHEMAS = {"dkc1.pacing.v3", "dkc1.pacing.v4", "dkc1.pacing.v5"}
MID_SCHEMAS = {"dkc1.pacing.v4", "dkc1.pacing.v5"}


def scanout_summary(header: dict, steady: list[dict]) -> dict:
    """Summarize the DXGI frame statistics carried by v5 rows.

    ``stat_present_count`` / ``stat_present_refresh`` describe the last image
    the display actually showed and the refresh it landed on, so consecutive
    rows give refreshes per displayed present.  With an integer display
    divisor every present should occupy exactly ``divisor`` refreshes (half
    that for each image of a 120 Hz frame-generation pair); any excess is a
    repeated refresh the host's own timers cannot see.
    """
    rows = [item for item in steady if item.get("stat_valid")]
    divisor = int(header.get("present_divisor") or 0) or 1
    expected: int | None = divisor
    if header.get("framegen_extra_refresh") and divisor >= 2 and divisor % 2 == 0:
        expected = divisor // 2
    if header.get("pacing") == "timer":
        # Free-running presents use sync interval 0: the newest queued image
        # wins each refresh, so refreshes per present is descriptive only.
        expected = None
    result = {
        "presents": 0,
        "statistics_rows": len(rows),
        "missing_statistics_rows": len(steady)-len(rows),
        "expected_refreshes_per_present": expected,
        "repeated_refreshes": 0,
        "early_refreshes": 0,
        "disjoint": sum(int(item.get("stat_disjoint", 0)) for item in steady),
        "stat_lag_presents_max": max(
            (int(item.get("stat_lag_presents", 0)) for item in steady),
            default=0),
        "measured_refresh_hz": None,
        "refreshes_per_present": None,
    }
    if len(rows) < 2:
        return result
    per_present: list[float] = []
    previous = rows[0]
    for item in rows[1:]:
        presents = (int(item["stat_present_count"])
                    - int(previous["stat_present_count"]))
        refreshes = (int(item["stat_present_refresh"])
                     - int(previous["stat_present_refresh"]))
        if presents > 0:
            result["presents"] += presents
            per_present.append(refreshes / presents)
            if expected is not None:
                result["repeated_refreshes"] += max(
                    0, refreshes - presents * expected)
                result["early_refreshes"] += max(
                    0, presents * expected - refreshes)
        previous = item
    refresh_delta = (int(rows[-1]["stat_sync_refresh"])
                     - int(rows[0]["stat_sync_refresh"]))
    ms_delta = (float(rows[-1]["stat_sync_qpc_ms"])
                - float(rows[0]["stat_sync_qpc_ms"]))
    if refresh_delta > 0 and ms_delta > 0:
        result["measured_refresh_hz"] = refresh_delta * 1000.0 / ms_delta
    if per_present:
        result["refreshes_per_present"] = metric(per_present)
    return result


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
    if header.get('log_mode') == 'async':
        status_path = Path(str(path) + '.status.json')
        if not status_path.exists():
            raise ValueError(f'{path}: asynchronous log has not closed cleanly')
        status = json.loads(status_path.read_text())
        if (status.get('schema') != 'dkc1.pacing-log-status.v1'
                or status.get('dropped') != 0 or status.get('io_errors') != 0
                or status.get('records') != len(frames)+1):
            raise ValueError(f'{path}: incomplete asynchronous log: {status}')
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
                      if schema in SUBMIT_SCHEMAS
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
    if schema in SUBMIT_SCHEMAS:
        summary["submit_error_ms"] = metric(
            [abs(float(item["submit_error_ms"])) for item in steady])
        summary["present_ms"] = metric(
            [float(item["present_ms"]) for item in steady])
        summary["completion_interval_ms"] = metric(
            [float(item["present_interval_ms"]) for item in steady
             if float(item["present_interval_ms"]) > 0.0])
    if schema in CPU_SCHEMAS:
        for field in ("setup_ms", "emulation_ms", "render_ms",
                      "diagnostics_ms", "audio_ms",
                      "audio_queued_frames"):
            summary[field] = metric(
                [float(item[field]) for item in steady])
        first_starvations = int(
            frames[warmup - 1]["audio_starvations"]) if warmup else 0
        first_drops = int(frames[warmup - 1]["audio_drops"]) if warmup else 0
        summary["steady_audio_starvations"] = (
            int(steady[-1]["audio_starvations"]) - first_starvations)
        summary["steady_audio_drops"] = (
            int(steady[-1]["audio_drops"]) - first_drops)
        if all("audio_ring_frames" in item for item in steady):
            summary["audio_ring_frames"] = metric(
                [float(item["audio_ring_frames"]) for item in steady])
        if all("audio_internal_underflows" in item for item in frames):
            first_internal_underflows = int(
                frames[warmup - 1]["audio_internal_underflows"]
            ) if warmup else 0
            summary["steady_audio_internal_underflows"] = (
                int(steady[-1]["audio_internal_underflows"])
                - first_internal_underflows)
    if schema in MID_SCHEMAS:
        mid = [item for item in steady if item.get("mid_presented")]
        summary["mid_presented"] = len(mid)
        first_skips = int(frames[warmup-1]["mid_skips"]) if warmup else 0
        summary["steady_mid_skips"] = int(steady[-1]["mid_skips"]) - first_skips
        if mid:
            for field in ("real_to_mid_ms", "mid_to_real_ms", "mid_submit_error_ms"):
                summary[field] = metric([float(item[field]) for item in mid])
        if all('pose_source_frame' in item for item in steady) and header.get('framegen'):
            sources=[int(item['pose_source_frame']) for item in steady]
            summary['frame_order']={
                'host_sequence_errors':sum(b['frame']!=a['frame']+1 for a,b in zip(steady,steady[1:])),
                'missing_source_identity':sum(s<=0 for s in sources),
                'repeated_or_backward_sources':sum(b<=a for a,b in zip(sources,sources[1:])),
                'skipped_sources':sum(b>a+1 for a,b in zip(sources,sources[1:])),
            }
    if schema == "dkc1.pacing.v5":
        summary["pacing"] = header.get("pacing")
        summary["presenter"] = header.get("presenter")
        summary["steady_wait_timeouts"] = sum(
            int(item.get("wait_timeout", 0)) for item in steady)
        summary["scanout"] = scanout_summary(header, steady)
        wake = [float(item["wake_interval_ms"]) for item in steady
                if float(item.get("wake_interval_ms", 0.0)) > 0.0]
        if wake:
            # Wait-first modes: present-call spacing absorbs work variance,
            # so the slot cadence is the spacing of the wait returns.
            summary["wake_interval_ms"] = metric(wake)
        if all("interp_steps" in item for item in steady):
            summary["interp_steps"] = metric(
                [float(item["interp_steps"]) for item in steady])
            summary["steady_tier_hits"] = sum(
                int(item.get("tier_hits", 0)) for item in steady)
        if all("gap_ms" in item for item in steady):
            for field in ("gap_ms", "pump_ms", "stats_ms", "previous_log_ms"):
                summary[field] = metric(
                    [float(item.get(field, 0.0)) for item in steady])
            slow = sorted(
                ((float(item.get("slow_msg_ms", 0.0)), int(item["frame"]),
                  int(item.get("slow_msg", 0))) for item in steady
                 if float(item.get("slow_msg_ms", 0.0)) > 5.0),
                reverse=True)[:5]
            summary["slow_messages"] = [
                {"frame": frame, "message": message, "ms": ms}
                for ms, frame, message in slow]
        if (header.get('pacing') != 'timer' and not header.get('framegen_extra_refresh')
                and all('submit_qpc_ms' in item for item in steady)):
            summary['hitches'] = []
            for index, item in enumerate(frames):
                if index < warmup or item['submit_interval_ms'] < 25:
                    continue
                previous = frames[index-1] if index else {}
                # For wait-first pacing this accounts for the entire interval,
                # including previous-frame statistics and diagnostic logging.
                accounted = (previous.get('present_ms', 0) + item.get('gap_ms', 0)
                             + item['wait_ms'] + item['work_ms'])
                summary['hitches'].append({
                    **{key: item.get(key, 0) for key in (
                        'frame', 'submit_qpc_ms', 'submit_interval_ms', 'work_ms',
                        'wait_ms', 'gap_ms', 'pump_ms', 'slow_msg', 'slow_msg_ms',
                        'emulation_ms', 'render_ms', 'interp_ms', 'audio_ms',
                        'audio_mix_ms', 'audio_submit_ms')},
                    'previous_stats_ms': previous.get('stats_ms', 0),
                    'previous_log_ms': item.get('previous_log_ms', 0),
                    'unaccounted_ms': item['submit_interval_ms']-accounted})
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
    if "scanout" in summary:
        print(f"pacing {summary['pacing']} on the {summary['presenter']} "
              f"presenter; steady wait timeouts "
              f"{summary['steady_wait_timeouts']}")
        scan = summary["scanout"]
        if scan["presents"]:
            per = scan["refreshes_per_present"]
            hz = scan["measured_refresh_hz"]
            print(f"scanout (DXGI statistics): {scan['presents']} presents "
                  f"displayed; refreshes/present p50 {per['p50']:.3f}, "
                  f"max {per['max']:.3f} (expected "
                  f"{scan['expected_refreshes_per_present']}); repeated "
                  f"refreshes {scan['repeated_refreshes']}; disjoint "
                  f"{scan['disjoint']}; statistics lag max "
                  f"{scan['stat_lag_presents_max']}"
                  + (f"; measured refresh {hz:.4f} Hz" if hz else ""))
        else:
            print("scanout (DXGI statistics): unavailable (GDI presenter or "
                  "no displayed-frame statistics)")
        if "wake_interval_ms" in summary:
            wake = summary["wake_interval_ms"]
            print(f"slot interval (wait return to wait return) p50 "
                  f"{wake['p50']:.4f}, p99 {wake['p99']:.4f}, "
                  f"max {wake['max']:.4f} ms")
        if "gap_ms" in summary:
            gap = summary["gap_ms"]
            print(f"loop gap outside work/wait/present p99 {gap['p99']:.4f}, "
                  f"max {gap['max']:.4f} ms; message pump max "
                  f"{summary['pump_ms']['max']:.4f} ms; statistics query max "
                  f"{summary['stats_ms']['max']:.4f} ms")
            for slow in summary["slow_messages"]:
                print(f"  slow window message 0x{slow['message']:04X} at "
                      f"frame {slow['frame']}: {slow['ms']:.2f} ms")
        if "interp_steps" in summary:
            steps = summary["interp_steps"]
            print(f"interpreted opcodes per frame p50 {steps['p50']:.0f}, "
                  f"p99 {steps['p99']:.0f}, max {steps['max']:.0f}; "
                  f"dispatch tier-downs {summary['steady_tier_hits']}")
    if "submit_error_ms" in summary:
        error = summary["submit_error_ms"]
        present = summary["present_ms"]
        print(f"absolute submit error p99 {error['p99']:.4f} ms; "
              f"host present p99 {present['p99']:.4f} ms")
    if "steady_audio_starvations" in summary:
        queued = summary["audio_queued_frames"]
        print(f"audio queue p50 {queued['p50']:.0f}, "
              f"min {queued['min']:.0f} frames; "
              f"steady starvations {summary['steady_audio_starvations']}, "
              f"drops {summary['steady_audio_drops']}")
        if ("steady_audio_internal_underflows" in summary and
                "audio_ring_frames" in summary):
            ring = summary["audio_ring_frames"]
            print(f"engine audio ring p50 {ring['p50']:.0f}, "
                  f"min {ring['min']:.0f} native frames; "
                  f"steady internal underflows "
                  f"{summary['steady_audio_internal_underflows']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
