#!/usr/bin/env python3
"""Stress an arbitrary DKC1 native snapshot for widescreen presentation faults.

Unlike the fresh-entry sweep, this starts every branch from the exact supplied
snapshot.  A first pass records strict widescreen/blank/lifecycle/OAM evidence.
If a blank-margin detector fires deterministically, a second pass saves the
exact trigger state, five surrounding frames, and same-frame isolated layers.
The visible desktop process is never contacted.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from grade_fresh_entry_sweep import grade_repeat  # noqa: E402


SCHEMA = "dkc1.snapshot-widescreen-stress.v1"
MASKS = {
    "neutral": 0x0000,
    "right_y": 0x0082,
    "left_y": 0x0042,
    "up_y": 0x0012,
    "down_y": 0x0022,
    "up_right_y": 0x0092,
    "up_left_y": 0x0052,
    "down_right_y": 0x00A2,
    "down_left_y": 0x0062,
    "right_b": 0x0081,
    "left_b": 0x0041,
}
PATTERNS = {
    "horizontal_sweep": ((0x0082, 60), (0x0042, 60)),
    "vertical_sweep": ((0x0012, 60), (0x0022, 60)),
    "box_y": ((0x0092, 45), (0x00A2, 45),
              (0x0062, 45), (0x0052, 45)),
}
DEFAULT_ACTIONS = (
    "neutral,right_y,left_y,up_y,down_y,up_right_y,up_left_y,"
    "down_right_y,down_left_y,horizontal_sweep,vertical_sweep,box_y"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def parse_actions(value: str) -> list[str]:
    actions = [item.strip() for item in value.split(",") if item.strip()]
    known = set(MASKS) | set(PATTERNS)
    unknown = sorted(set(actions) - known)
    if unknown:
        raise ValueError(f"unknown actions: {', '.join(unknown)}")
    if not actions:
        raise ValueError("at least one action is required")
    if len(set(actions)) != len(actions):
        raise ValueError("duplicate actions are not allowed")
    return actions


def build_segments(action: str, frames: int) -> list[tuple[int, int]]:
    if frames < 1:
        raise ValueError("frames must be positive")
    if action in MASKS:
        return [(MASKS[action], frames)]
    pattern = PATTERNS[action]
    result: list[tuple[int, int]] = []
    remaining = frames
    cursor = 0
    while remaining:
        mask, requested = pattern[cursor % len(pattern)]
        count = min(requested, remaining)
        if result and result[-1][0] == mask:
            result[-1] = (mask, result[-1][1] + count)
        else:
            result.append((mask, count))
        remaining -= count
        cursor += 1
    return result


def write_script(path: Path, action: str, frames: int) -> None:
    lines = ["# generated controller-only snapshot stress"]
    lines.extend(f"{mask:X} * {count}"
                 for mask, count in build_segments(action, frames))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def parse_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    rows: list[dict[str, Any]] = []
    for line_no, line in enumerate(
            path.read_text(encoding="utf-8", errors="strict").splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{line_no}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise ValueError(f"{path}:{line_no}: expected JSON object")
        rows.append(row)
    return rows


def blank_signature(rows: list[dict[str, Any]]) -> list[tuple[Any, ...]]:
    return [(row.get("frame"), row.get("kind"),
             row.get("suspect_columns"), row.get("first_x"),
             row.get("width")) for row in rows]


def artifact_hash(run: dict[str, Any], name: str) -> Any:
    return run.get("artifacts", {}).get(name, {}).get("sha256")


def run_signature(run: dict[str, Any]) -> tuple[Any, ...]:
    return (run.get("exit_code"), artifact_hash(run, "final.wram.bin"),
            artifact_hash(run, "final.snapshot"),
            artifact_hash(run, "final.ppm"),
            artifact_hash(run, "ws-trace.jsonl"),
            artifact_hash(run, "lifecycle.jsonl"),
            artifact_hash(run, "oam.bin"),
            artifact_hash(run, "oam.jsonl"))


def first_failure_frame(run: dict[str, Any]) -> int | None:
    blanks = run.get("blank_events", [])
    if blanks:
        return int(blanks[0]["frame"])
    grade = run.get("widescreen_grade", {})
    if grade.get("status") == "pass":
        return None
    trace_path = Path(run["artifacts"]["ws-trace.jsonl"]["path"])
    rows = parse_jsonl(trace_path)
    strict = grade.get("strict_summary") or {}
    global_frames: set[int] = set()
    for key in ("policy_violations", "centered_nonblack_margin_frames",
                "stable_input_margin_changes",
                "stable_input_unproven_margin_changes"):
        for finding in strict.get(key, []):
            if "frame" in finding:
                global_frames.add(int(finding["frame"]))
    for index, row in enumerate(rows, 1):
        if int(row.get("frame", -1)) in global_frames:
            return index
        for delta in row.get("shadow_delta", []):
            if any(int(delta.get(key, 0)) for key in
                   ("west_raw", "east_raw", "west_miss", "east_miss")):
                return index
    return None


def trigger_window(event_frame: int, total_frames: int) -> tuple[int, int]:
    if not 1 <= event_frame <= total_frames:
        raise ValueError("event frame outside run")
    # Frame-sequence indexes are zero-based; detector frames are one-based.
    center = event_frame - 1
    return max(0, center - 2), min(total_frames - 1, center + 2)


def clean_environment() -> dict[str, str]:
    env = os.environ.copy()
    for key in tuple(env):
        if key.startswith("DKC1_") or key.startswith("SNESRECOMP_"):
            env.pop(key, None)
    return env


def run_branch(*, runner: Path, rom: Path, snapshot: Path, output: Path,
               action: str, frames: int, prefetch_guard: bool,
               trigger_frame: int | None = None) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=False)
    output = output.resolve()
    script = output / "input.dks"
    write_script(script, action, frames)
    env = clean_environment()
    final_snapshot = output / "final.snapshot"
    env.update({
        "DKC1_SAVESTATE_INPUT": str(snapshot),
        "DKC1_SAVESTATE_OUTPUT": str(final_snapshot),
        "DKC1_SCRIPT": str(script),
        "DKC1_WIDESCREEN": "1",
        "DKC1_PREFETCH_PHASE_GUARD": "1" if prefetch_guard else "0",
        "DKC1_WS_TRACE": str(output / "ws-trace.jsonl"),
        "DKC1_BLANK_SCAN": str(output / "blank-scan.jsonl"),
        "DKC1_LIFECYCLE_TRACE": str(output / "lifecycle.jsonl"),
        "DKC1_OAM_LOG": str(output / "oam"),
        "DKC1_WRAM_DUMP": f"{frames}-{frames}",
        "DKC1_WRAM_DUMP_PATH": str(output / "final.wram.bin"),
        "DKC1_FRAME_PPM": str(output / "final.ppm"),
    })
    if trigger_frame is not None:
        start, end = trigger_window(trigger_frame, frames)
        env["DKC1_SAVESTATE_SAVE_AT"] = str(trigger_frame)
        env["DKC1_SAVESTATE_OUTPUT"] = str(output / "trigger.snapshot")
        env["DKC1_FRAME_PPM_PREFIX"] = str(output / "frame")
        env["DKC1_FRAME_PPM_START"] = str(start)
        env["DKC1_FRAME_PPM_END"] = str(end)
        env["DKC1_FRAME_PPM_STEP"] = "1"
    log = output / "run.log"
    with log.open("wb") as handle:
        completed = subprocess.run(
            [str(runner), str(rom), str(frames)], cwd=output, env=env,
            stdout=handle, stderr=subprocess.STDOUT, check=False)
    blanks = parse_jsonl(output / "blank-scan.jsonl")
    grade = grade_repeat(output / "ws-trace.jsonl", strict=True)
    artifacts = {item.name: {"path": str(item.resolve()),
                             "size": item.stat().st_size,
                             "sha256": sha256(item)}
                 for item in sorted(output.iterdir()) if item.is_file()}
    return {
        "exit_code": completed.returncode,
        "blank_events": blanks,
        "blank_signature": blank_signature(blanks),
        "widescreen_grade": grade,
        "artifacts": artifacts,
    }


def capture_layers(layer_capture: Path, rom: Path, snapshot: Path,
                   output: Path) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=False)
    output = output.resolve()
    log = output / "layer-capture.log"
    with log.open("wb") as handle:
        completed = subprocess.run(
            [str(layer_capture), str(rom), str(snapshot), str(output)],
            cwd=output.parent, stdout=handle, stderr=subprocess.STDOUT,
            check=False)
    return {
        "exit_code": completed.returncode,
        "artifacts": {
            item.name: {"path": str(item.resolve()),
                        "size": item.stat().st_size,
                        "sha256": sha256(item)}
            for item in sorted(output.iterdir()) if item.is_file()
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--snapshot", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--actions", default=DEFAULT_ACTIONS)
    parser.add_argument("--layer-capture", type=Path)
    parser.add_argument("--prefetch-phase-guard", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    try:
        actions = parse_actions(args.actions)
        if args.frames < 1:
            raise ValueError("--frames must be positive")
        if not 1 <= args.repeats <= 10:
            raise ValueError("--repeats must be within 1..10")
        for path in (args.runner, args.rom, args.snapshot):
            if not path.is_file():
                raise ValueError(f"missing input file: {path}")
        if args.layer_capture and not args.layer_capture.is_file():
            raise ValueError(f"missing layer capture executable: {args.layer_capture}")
        if args.output.exists():
            raise ValueError(f"output already exists: {args.output}")
    except ValueError as error:
        parser.error(str(error))

    inputs = {
        name: {"path": str(path.resolve()), "sha256": sha256(path),
               "size": path.stat().st_size}
        for name, path in (("runner", args.runner), ("rom", args.rom),
                           ("snapshot", args.snapshot))
    }
    if args.validate_only:
        print(json.dumps({"schema": SCHEMA, "valid": True, "inputs": inputs,
                          "actions": actions}, indent=2))
        return 0

    args.output.mkdir(parents=True)
    report: dict[str, Any] = {
        "schema": SCHEMA,
        "status": "running",
        "inputs": inputs,
        "settings": {"frames": args.frames, "repeats": args.repeats,
                     "actions": actions,
                     "prefetch_phase_guard": args.prefetch_phase_guard},
        "actions": [],
    }
    failures = 0
    deterministic_triggers = 0
    strict_failure_actions = 0
    for action in actions:
        action_root = args.output / action
        repeats = []
        for repeat in range(1, args.repeats + 1):
            run = run_branch(
                runner=args.runner.resolve(), rom=args.rom.resolve(),
                snapshot=args.snapshot.resolve(),
                output=action_root / f"repeat-{repeat}", action=action,
                frames=args.frames, prefetch_guard=args.prefetch_phase_guard)
            repeats.append(run)
            failures += int(run["exit_code"] != 0)
        blank_signatures = [run["blank_signature"] for run in repeats]
        machine_signatures = [run_signature(run) for run in repeats]
        blank_deterministic = all(signature == blank_signatures[0]
                                  for signature in blank_signatures[1:])
        machine_deterministic = all(signature == machine_signatures[0]
                                    for signature in machine_signatures[1:])
        deterministic = blank_deterministic and machine_deterministic
        action_failed = any(run["exit_code"] != 0 for run in repeats)
        grade_failures = sorted({failure for run in repeats
                                 for failure in
                                 run["widescreen_grade"].get("failures", [])})
        if grade_failures:
            strict_failure_actions += 1
        entry: dict[str, Any] = {
            "action": action, "deterministic": deterministic,
            "machine_deterministic": machine_deterministic,
            "blank_deterministic": blank_deterministic,
            "blank_event_count": (len(blank_signatures[0])
                                  if blank_deterministic else None),
            "widescreen_failures": grade_failures,
            "repeats": repeats,
        }
        trigger_frame = first_failure_frame(repeats[0])
        if deterministic and trigger_frame is not None and not action_failed:
            deterministic_triggers += 1
            trigger = run_branch(
                runner=args.runner.resolve(), rom=args.rom.resolve(),
                snapshot=args.snapshot.resolve(), output=action_root / "trigger",
                action=action, frames=args.frames,
                prefetch_guard=args.prefetch_phase_guard,
                trigger_frame=trigger_frame)
            entry["trigger_frame"] = trigger_frame
            entry["trigger"] = trigger
            failures += int(trigger["exit_code"] != 0)
            trigger_snapshot = action_root / "trigger" / "trigger.snapshot"
            if args.layer_capture and trigger_snapshot.is_file():
                entry["layers"] = capture_layers(
                    args.layer_capture.resolve(), args.rom.resolve(),
                    trigger_snapshot.resolve(), action_root / "trigger-layers")
                failures += int(entry["layers"]["exit_code"] != 0)
        report["actions"].append(entry)

    report["counts"] = {
        "actions": len(actions), "process_failures": failures,
        "deterministic_trigger_actions": deterministic_triggers,
        "strict_failure_actions": strict_failure_actions,
    }
    report["status"] = ("error" if failures else
                        "detected" if deterministic_triggers else "clean")
    report_path = args.output / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n",
                           encoding="utf-8", newline="\n")
    print(json.dumps({"status": report["status"],
                      "report": str(report_path.resolve()),
                      **report["counts"]}, indent=2))
    return 2 if failures else 1 if deterministic_triggers else 0


if __name__ == "__main__":
    raise SystemExit(main())
