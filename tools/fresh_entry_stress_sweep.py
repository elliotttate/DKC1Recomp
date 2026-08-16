#!/usr/bin/env python3
"""Stress every discovered DKC1 fresh entry with native/wide twin branches.

This complements ``world_map_fresh_entry_sweep.py``. Fresh entry proves level
initialization, but a settled branch must still exercise scrolling, private
object windows, OAM boundaries, and later tile streaming. Each branch starts
from the same immutable native snapshot and retains enough evidence to route a
failure into presentation versus gameplay/lifecycle work.

The tool never writes WRAM and never contacts the visible desktop process.
Native/wide machine-state differences are classified ``investigate`` rather
than silently ignored or treated as renderer failures. Pixel-exact center
comparison is required only when final WRAM/VRAM/CGRAM/OAM inputs match.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Iterable


TOOLS = Path(__file__).resolve().parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from compare_widescreen_regions import compare as compare_regions  # noqa: E402
from grade_fresh_entry_sweep import grade_repeat  # noqa: E402
from level_sweep import grade_cache_log  # noqa: E402
from oam_inspect import analyze_oam_pipeline  # noqa: E402
from world_map_fresh_entry_sweep import state_summary  # noqa: E402


SCHEMA = "dkc1.fresh-entry-stress-sweep.v1"
ACTIONS = {
    "neutral": 0x0000,
    "right_y": 0x0082,
    "left_y": 0x0042,
    "up_y": 0x0012,
    "down_y": 0x0022,
    "right_b": 0x0081,
    "left_b": 0x0041,
}
MACHINE_HASHES = (
    "wram_sha256", "vram_sha256", "cgram_sha256",
    "oam_sha256", "oam_source_sha256",
)
HASH_PATTERN = re.compile(r"^(\w+_sha256)=([0-9a-fA-F]{64})$", re.MULTILINE)
RUN_STATS_PATTERN = re.compile(r"^run_stats\s+(.+)$", re.MULTILINE)
WRAM_SIZE = 0x20000
READY_MATCH_FIELDS = (
    "level", "mode", "entrance", "fade", "active_selector", "actor_id",
    "actor_x", "actor_y", "actor_state", "actor_animation",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def parse_csv_names(value: str, allowed: dict[str, int]) -> list[str]:
    names = [item.strip() for item in value.split(",") if item.strip()]
    unknown = sorted(set(names) - set(allowed))
    if unknown:
        raise ValueError(f"unknown actions: {', '.join(unknown)}")
    if not names:
        raise ValueError("at least one action is required")
    return names


def parse_entrances(value: str | None) -> set[int] | None:
    if not value:
        return None
    return {int(item.strip(), 0) for item in value.split(",") if item.strip()}


def parse_run_log(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8", errors="replace")
    hashes = {match.group(1): match.group(2).upper()
              for match in HASH_PATTERN.finditer(text)}
    stats: dict[str, int | str] = {}
    match = RUN_STATS_PATTERN.search(text)
    if match:
        for token in match.group(1).split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            try:
                stats[key] = int(value, 0)
            except ValueError:
                stats[key] = value
    result = None
    for line in text.splitlines():
        if line.startswith("result="):
            result = line.split("=", 1)[1].split()[0]
    return {"hashes": hashes, "run_stats": stats, "result": result}


def u16(wram: bytes, offset: int) -> int:
    if len(wram) != WRAM_SIZE:
        raise ValueError(
            f"expected {WRAM_SIZE} WRAM bytes, received {len(wram)}")
    return wram[offset] | (wram[offset + 1] << 8)


def gameplay_ready_summary(wram: bytes) -> dict[str, Any]:
    """Return the phase-alignment fields from one final 128 KiB WRAM image."""
    selector = u16(wram, 0x056F)
    actor_index = selector * 2 if 1 <= selector <= 25 else None
    summary: dict[str, Any] = {
        "level": u16(wram, 0x0030),
        "mode": u16(wram, 0x0032),
        "entrance": u16(wram, 0x003E),
        "fade": u16(wram, 0x1DF1),
        "camera_lower": u16(wram, 0x1B23),
        "camera_upper": u16(wram, 0x1B25),
        "active_selector": selector,
        "actor_index": actor_index,
        "actor_id": None,
        "actor_x": None,
        "actor_y": None,
        "actor_state": None,
        "actor_animation": None,
    }
    if actor_index is not None:
        summary.update({
            "actor_id": u16(wram, 0x0D45 + actor_index),
            "actor_x": u16(wram, 0x0B19 + actor_index),
            "actor_y": u16(wram, 0x0BC1 + actor_index),
            "actor_state": u16(wram, 0x1029 + actor_index),
            "actor_animation": u16(wram, 0x10D1 + actor_index),
        })
    camera_published = bool(
        summary["camera_upper"] > summary["camera_lower"] or
        (summary["camera_upper"] == summary["camera_lower"] and
         summary["camera_upper"] != 0))
    summary["gameplay_ready"] = bool(
        camera_published and summary["actor_id"] in (1, 2))
    return summary


def compare_ready_roots(native: dict[str, Any],
                        wide: dict[str, Any]) -> dict[str, Any]:
    """Fail closed unless native/wide roots represent the same game phase."""
    failures: list[str] = []
    investigations: list[str] = []
    for label, run in (("native", native), ("wide", wide)):
        if (run.get("exit_code") != 0 or
                run.get("parsed", {}).get("result") != "completed"):
            failures.append(f"{label}_ready_process_failure")
        state = run.get("ready_state")
        if not state or not state.get("gameplay_ready"):
            failures.append(f"{label}_gameplay_not_ready")
    grade = wide.get("widescreen_grade", {})
    failures.extend(f"wide_entry_{item}"
                    for item in grade.get("failures", []))
    differences: dict[str, dict[str, Any]] = {}
    native_state = native.get("ready_state", {})
    wide_state = wide.get("ready_state", {})
    if native_state and wide_state:
        for field in READY_MATCH_FIELDS:
            if native_state.get(field) != wide_state.get(field):
                differences[field] = {
                    "native": native_state.get(field),
                    "wide": wide_state.get(field),
                }
    if differences:
        investigations.append("gameplay_ready_root_mismatch")
    return {
        "aligned": not failures and not differences,
        "status": "fail" if failures else
                  "investigate" if investigations else "pass",
        "failures": sorted(set(failures)),
        "investigations": investigations,
        "differences": differences,
    }


def oam_budget(index_path: Path) -> dict[str, int]:
    result = {"frames": 0, "range_over_frames": 0, "time_over_frames": 0}
    if not index_path.exists():
        return result
    for line in index_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not row.get("gameplay") or row.get("forced_blank"):
            continue
        result["frames"] += 1
        result["range_over_frames"] += int(bool(row.get("range_over")))
        result["time_over_frames"] += int(bool(row.get("time_over")))
    return result


def write_input_script(path: Path, mask: int, frames: int,
                       entry_settle_frames: int, *,
                       enter_before_stress: bool = True) -> None:
    lines = ["# generated authentic fresh-entry plus controller-only stress"]
    if enter_before_stress:
        lines.append("1 * 1")
    if entry_settle_frames:
        lines.append(f"0 * {entry_settle_frames}")
    lines.append(f"{mask:X} * {frames}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def write_ready_script(path: Path, *, level: int, mode: int, entrance: int,
                       fade: int, timeout: int, stable_frames: int) -> None:
    """Enter a level, then stop at the first conservative gameplay root."""
    path.write_text(
        "# generated fresh-entry gameplay-ready alignment\n"
        "1 * 1\n"
        f"wait 0032 == {mode:04X} timeout {timeout}\n"
        f"wait 0030 == {level:04X} timeout {timeout}\n"
        f"wait 003E == {entrance:04X} timeout {timeout}\n"
        f"wait 1DF1 == {fade:04X} timeout {timeout}\n"
        f"wait 1B25 != 0000 timeout {timeout}\n"
        f"0 * {stable_frames}\n",
        encoding="utf-8", newline="\n")


def run_ready_mode(*, runner: Path, rom: Path, snapshot: Path, output: Path,
                   level: int, mode: int, entrance: int, fade: int,
                   timeout: int, stable_frames: int, wide: bool,
                   prefetch_phase_guard: bool = False,
                   prefetch_transaction_debug: bool = False) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=False)
    script = output / "ready.dks"
    write_ready_script(script, level=level, mode=mode, entrance=entrance,
                       fade=fade, timeout=timeout,
                       stable_frames=stable_frames)
    wram = output / "ready.wram.bin"
    saved = output / "ready.snapshot"
    trace = output / "ws-trace.jsonl"
    lifecycle = output / "lifecycle.jsonl"
    frame = output / "ready.ppm"
    log = output / "run.log"
    # Each wait has its own timeout. This limit is deliberately larger than
    # the sum so an unexpectedly long path fails in the script, not the host.
    frame_limit = 1 + timeout * 5 + stable_frames + 32
    env = os.environ.copy()
    for key in (
            "SNESRECOMP_INPUT_PLAY", "DKC1_SUPERZSNES_STATE",
            "DKC1_PREFETCH_PHASE_GUARD", "DKC1_WS_FORCE_FALLBACK_FRAME",
            "DKC1_WRAM_DUMP", "DKC1_WRAM_DUMP_PATH",
            "DKC1_WRAM_DUMP_RANGES", "DKC1_PREFETCH_TRANSACTION_DEBUG"):
        env.pop(key, None)
    env.update({
        "DKC1_SAVESTATE_INPUT": str(snapshot.resolve()),
        "DKC1_SAVESTATE_OUTPUT": str(saved.resolve()),
        "DKC1_SCRIPT": str(script.resolve()),
        "DKC1_WRAM_OUTPUT": str(wram.resolve()),
        "DKC1_FRAME_PPM": str(frame.resolve()),
        "DKC1_WIDESCREEN": "1" if wide else "0",
        "DKC1_PREFETCH_PHASE_GUARD":
            "1" if prefetch_phase_guard else "0",
        "DKC1_PREFETCH_TRANSACTION_DEBUG":
            "1" if prefetch_transaction_debug else "0",
        "DKC1_WS_TRACE": str(trace.resolve()),
        "DKC1_LIFECYCLE_TRACE": str(lifecycle.resolve()),
    })
    with log.open("wb") as handle:
        completed = subprocess.run(
            [str(runner.resolve()), str(rom.resolve()), str(frame_limit)],
            cwd=str(output), env=env, stdout=handle,
            stderr=subprocess.STDOUT, check=False)
    parsed = parse_run_log(log)
    result: dict[str, Any] = {
        "exit_code": completed.returncode,
        "parsed": parsed,
        "artifacts": {
            "script": str(script.resolve()),
            "frame": str(frame.resolve()),
            "wram": str(wram.resolve()),
            "snapshot": str(saved.resolve()),
            "trace": str(trace.resolve()),
            "lifecycle": str(lifecycle.resolve()),
            "log": str(log.resolve()),
        },
    }
    if wram.exists():
        try:
            result["ready_state"] = gameplay_ready_summary(wram.read_bytes())
        except ValueError as error:
            result["ready_state_error"] = str(error)
    if wide and trace.exists():
        result["widescreen_grade"] = grade_repeat(trace, strict=True)
    return result


def run_mode(*, runner: Path, rom: Path, snapshot: Path, output: Path,
             action: str, frames: int, entry_settle_frames: int,
             wide: bool, prefetch_phase_guard: bool = False,
             prefetch_transaction_debug: bool = False,
             enter_before_stress: bool = True) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=False)
    script = output / "input.dks"
    write_input_script(script, ACTIONS[action], frames, entry_settle_frames,
                       enter_before_stress=enter_before_stress)
    total_frames = (1 if enter_before_stress else 0) + \
        entry_settle_frames + frames
    frame = output / "final.ppm"
    wram = output / "final.wram.bin"
    saved = output / "final.snapshot"
    trace = output / "ws-trace.jsonl"
    cache = output / "cache.jsonl"
    lifecycle = output / "lifecycle.jsonl"
    oam_prefix = output / "oam"
    log = output / "run.log"
    cache.unlink(missing_ok=True)
    env = os.environ.copy()
    for key in (
            "SNESRECOMP_INPUT_PLAY", "DKC1_SUPERZSNES_STATE",
            "DKC1_PREFETCH_PHASE_GUARD", "DKC1_WS_FORCE_FALLBACK_FRAME",
            "DKC1_WRAM_OUTPUT", "DKC1_PREFETCH_TRANSACTION_DEBUG"):
        env.pop(key, None)
    env.update({
        "DKC1_SAVESTATE_INPUT": str(snapshot.resolve()),
        "DKC1_SAVESTATE_OUTPUT": str(saved.resolve()),
        "DKC1_SCRIPT": str(script.resolve()),
        "DKC1_WRAM_DUMP": f"{total_frames}-{total_frames}",
        "DKC1_WRAM_DUMP_PATH": str(wram.resolve()),
        "DKC1_FRAME_PPM": str(frame.resolve()),
        "DKC1_WIDESCREEN": "1" if wide else "0",
        # Never inherit this experimental gameplay switch from the caller.
        # A matrix must say whether it tested the guard, and its report must
        # retain that fact.  It is harmless in native mode because the runtime
        # gates it on widescreen, so use one explicit value for both twins.
        "DKC1_PREFETCH_PHASE_GUARD":
            "1" if prefetch_phase_guard else "0",
        "DKC1_PREFETCH_TRANSACTION_DEBUG":
            "1" if prefetch_transaction_debug else "0",
        "DKC1_WS_TRACE": str(trace.resolve()),
        "SNESRECOMP_WS_CACHE_LOG": str(cache.resolve()),
        "DKC1_OAM_LOG": str(oam_prefix.resolve()),
        "DKC1_LIFECYCLE_TRACE": str(lifecycle.resolve()),
    })
    with log.open("wb") as handle:
        completed = subprocess.run(
            [str(runner.resolve()), str(rom.resolve()), str(total_frames)],
            cwd=str(output), env=env, stdout=handle,
            stderr=subprocess.STDOUT, check=False)
    parsed = parse_run_log(log)
    result: dict[str, Any] = {
        "exit_code": completed.returncode,
        "parsed": parsed,
        "artifacts": {
            "frame": str(frame.resolve()), "wram": str(wram.resolve()),
            "snapshot": str(saved.resolve()), "trace": str(trace.resolve()),
            "cache": str(cache.resolve()),
            "lifecycle": str(lifecycle.resolve()),
            "oam_bin": str(Path(str(oam_prefix) + ".bin").resolve()),
            "oam_index": str(Path(str(oam_prefix) + ".jsonl").resolve()),
            "log": str(log.resolve()),
        },
    }
    if wram.exists():
        result["state"] = state_summary(wram.read_bytes())
    result["cache"] = grade_cache_log(cache)
    result["oam_pipeline"] = analyze_oam_pipeline(
        Path(str(oam_prefix) + ".bin"), extra=51)
    result["oam_budget"] = oam_budget(Path(str(oam_prefix) + ".jsonl"))
    if wide and trace.exists():
        result["widescreen_grade"] = grade_repeat(trace, strict=True)
    return result


def mode_signature(run: dict[str, Any]) -> tuple[Any, ...]:
    hashes = run.get("parsed", {}).get("hashes", {})
    return tuple(hashes.get(key) for key in MACHINE_HASHES) + (
        hashes.get("frame_sha256"), run.get("exit_code"),
        run.get("parsed", {}).get("result"),
    )


def compare_pair(native: dict[str, Any], wide: dict[str, Any],
                 extra: int) -> dict[str, Any]:
    native_hashes = native.get("parsed", {}).get("hashes", {})
    wide_hashes = wide.get("parsed", {}).get("hashes", {})
    machine_match = {
        key: bool(native_hashes.get(key)) and
             native_hashes.get(key) == wide_hashes.get(key)
        for key in MACHINE_HASHES
    }
    all_machine_match = all(machine_match.values())
    center = {"eligible": False, "exact": None}
    if all_machine_match:
        try:
            comparison, _ = compare_regions(
                Path(native["artifacts"]["frame"]),
                Path(wide["artifacts"]["frame"]), extra)
            center = {
                "eligible": True,
                "exact": bool(comparison["center_exact"]),
                "changed_pixels":
                    comparison["regions"]["center"]["changed_pixels"],
            }
        except (OSError, ValueError) as error:
            center = {"eligible": True, "exact": False, "error": str(error)}
    native_budget = native.get("oam_budget", {})
    wide_budget = wide.get("oam_budget", {})
    budget_regression = {
        key: max(0, int(wide_budget.get(key, 0)) -
                    int(native_budget.get(key, 0)))
        for key in ("range_over_frames", "time_over_frames")
    }
    failures: list[str] = []
    investigations: list[str] = []
    for label, run in (("native", native), ("wide", wide)):
        if run.get("exit_code") != 0 or run.get("parsed", {}).get("result") != "completed":
            failures.append(f"{label}_process_failure")
    grade = wide.get("widescreen_grade", {})
    failures.extend(f"wide_{item}" for item in grade.get("failures", []))
    if wide.get("cache", {}).get("oob_read") or wide.get("cache", {}).get("oob_write"):
        failures.append("wide_shadow_cache_oob")
    pipeline = wide.get("oam_pipeline", {})
    if pipeline.get("xhigh_loss_suspects"):
        failures.append("wide_oam_x_wrap")
    if pipeline.get("verdict") == "persistent_mismatch":
        failures.append("wide_oam_pipeline_mismatch")
    if any(budget_regression.values()):
        failures.append("wide_oam_budget_regression")
    if center.get("eligible") and not center.get("exact"):
        failures.append("native_center_pixel_mismatch")
    if not all_machine_match:
        investigations.append("native_wide_machine_state_divergence")
    return {
        "machine_hash_match": machine_match,
        "all_machine_hashes_match": all_machine_match,
        "center_comparison": center,
        "oam_budget_regression": budget_regression,
        "status": "fail" if failures else
                  "investigate" if investigations else "pass",
        "failures": sorted(set(failures)),
        "investigations": investigations,
    }


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--fresh-entry-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=720)
    parser.add_argument("--entry-settle-frames", type=int, default=360)
    parser.add_argument(
        "--align-gameplay-ready", action="store_true",
        help="prepare native/wide roots independently and begin stress only "
             "after their gameplay state matches")
    parser.add_argument("--ready-timeout", type=int, default=1200)
    parser.add_argument(
        "--ready-stable-frames", type=int, default=64,
        help="neutral frames after the coarse level/fade/bounds predicates; "
             "64 clears the observed one-frame-wide entrance-walk skew")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--actions", default="neutral,right_y,left_y")
    parser.add_argument("--entrances",
                        help="optional comma-separated decimal/0x IDs")
    parser.add_argument(
        "--prefetch-phase-guard", action="store_true",
        help="explicitly enable DKC1_PREFETCH_PHASE_GUARD for this matrix; "
             "the setting is recorded in the report and never inherited")
    parser.add_argument(
        "--prefetch-transaction-debug", action="store_true",
        help="with the phase guard, emit one structured rolled-back actor "
             "write set per pool ordinal to lifecycle.jsonl")
    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        actions = parse_csv_names(args.actions, ACTIONS)
        wanted = parse_entrances(args.entrances)
        source = json.loads(args.fresh_entry_report.read_text(encoding="utf-8"))
        if source.get("schema") != "dkc1.world-map-fresh-entry-sweep.v1":
            raise ValueError("unexpected fresh-entry report schema")
        if (args.frames <= 0 or args.repeats <= 0 or
                args.entry_settle_frames < 0 or args.ready_timeout <= 0 or
                args.ready_stable_frames <= 0):
            raise ValueError(
                "frames/repeats/ready values must be positive and settle "
                "nonnegative")
        if args.prefetch_transaction_debug and not args.prefetch_phase_guard:
            raise ValueError(
                "--prefetch-transaction-debug requires --prefetch-phase-guard")
        args.output.mkdir(parents=True, exist_ok=False)
        entries = [entry for entry in source.get("fresh_entries", [])
                   if wanted is None or
                   int(entry.get("map_state", {}).get("entrance", -1)) in wanted]
        report: dict[str, Any] = {
            "schema": SCHEMA,
            "inputs": {
                "runner": str(args.runner.resolve()),
                "runner_sha256": sha256(args.runner),
                "rom": str(args.rom.resolve()), "rom_sha256": sha256(args.rom),
                "fresh_entry_report": str(args.fresh_entry_report.resolve()),
                "fresh_entry_report_sha256": sha256(args.fresh_entry_report),
            },
            "config": {"frames": args.frames,
                       "entry_settle_frames": args.entry_settle_frames,
                       "align_gameplay_ready":
                           bool(args.align_gameplay_ready),
                       "ready_timeout": args.ready_timeout,
                       "ready_stable_frames": args.ready_stable_frames,
                       "repeats": args.repeats,
                       "actions": actions,
                       "prefetch_phase_guard":
                           bool(args.prefetch_phase_guard),
                       "prefetch_transaction_debug":
                           bool(args.prefetch_transaction_debug)},
            "ready_roots": [],
            "branches": [],
        }
        hard_failures = 0
        investigations = 0
        for entry in entries:
            entrance = int(entry["map_state"]["entrance"])
            name = entry["map_state"].get("entrance_name", "Unknown")
            snapshot = Path(entry["pre_entry_snapshot"])
            ready_pairs: list[dict[str, Any]] = []
            ready_deterministic = {"native": True, "wide": True}
            if args.align_gameplay_ready:
                target = entry.get("level_state", {})
                target_level = int(target["level"])
                target_mode = int(target["mode"])
                target_entrance = int(target["entrance"])
                target_fade = int(target["fade"])
                ready_root = args.output / f"{entrance:04x}-{name}" / "ready"
                for repeat in range(1, args.repeats + 1):
                    native_ready = run_ready_mode(
                        runner=args.runner, rom=args.rom, snapshot=snapshot,
                        output=ready_root / "native" / f"repeat-{repeat}",
                        level=target_level, mode=target_mode,
                        entrance=target_entrance, fade=target_fade,
                        timeout=args.ready_timeout,
                        stable_frames=args.ready_stable_frames, wide=False,
                        prefetch_phase_guard=args.prefetch_phase_guard,
                        prefetch_transaction_debug=
                            args.prefetch_transaction_debug)
                    wide_ready = run_ready_mode(
                        runner=args.runner, rom=args.rom, snapshot=snapshot,
                        output=ready_root / "wide" / f"repeat-{repeat}",
                        level=target_level, mode=target_mode,
                        entrance=target_entrance, fade=target_fade,
                        timeout=args.ready_timeout,
                        stable_frames=args.ready_stable_frames, wide=True,
                        prefetch_phase_guard=args.prefetch_phase_guard,
                        prefetch_transaction_debug=
                            args.prefetch_transaction_debug)
                    ready_pairs.append({
                        "repeat": repeat,
                        "alignment": compare_ready_roots(
                            native_ready, wide_ready),
                        "native": native_ready,
                        "wide": wide_ready,
                    })
                for label in ("native", "wide"):
                    signatures = {
                        (item[label].get("exit_code"),
                         item[label].get("parsed", {}).get("result"),
                         json.dumps(item[label].get("ready_state", {}),
                                    sort_keys=True))
                        for item in ready_pairs
                    }
                    ready_deterministic[label] = len(signatures) == 1
                report["ready_roots"].append({
                    "entrance": entrance, "name": name,
                    "target_level": target_level,
                    "target_mode": target_mode,
                    "target_entrance": target_entrance,
                    "target_fade": target_fade,
                    "deterministic": ready_deterministic,
                    "pairs": ready_pairs,
                })
            for action in actions:
                branch_root = args.output / f"{entrance:04x}-{name}" / action
                native_runs = []
                wide_runs = []
                alignment_failures: list[str] = []
                alignment_investigations: list[str] = []
                if args.align_gameplay_ready:
                    alignment_failures = sorted({
                        failure for item in ready_pairs
                        for failure in item["alignment"]["failures"]})
                    alignment_investigations = sorted({
                        finding for item in ready_pairs
                        for finding in item["alignment"]["investigations"]})
                    if not all(ready_deterministic.values()):
                        alignment_failures.append(
                            "nondeterministic_gameplay_ready_root")
                if not alignment_failures and not alignment_investigations:
                    for repeat in range(1, args.repeats + 1):
                        if args.align_gameplay_ready:
                            native_snapshot = Path(
                                ready_pairs[repeat - 1]["native"]
                                ["artifacts"]["snapshot"])
                            wide_snapshot = Path(
                                ready_pairs[repeat - 1]["wide"]
                                ["artifacts"]["snapshot"])
                            settle_frames = 0
                            enter_before_stress = False
                        else:
                            native_snapshot = snapshot
                            wide_snapshot = snapshot
                            settle_frames = args.entry_settle_frames
                            enter_before_stress = True
                        native_runs.append(run_mode(
                            runner=args.runner, rom=args.rom,
                            snapshot=native_snapshot,
                            output=branch_root / "native" / f"repeat-{repeat}",
                            action=action, frames=args.frames,
                            entry_settle_frames=settle_frames, wide=False,
                            prefetch_phase_guard=args.prefetch_phase_guard,
                            prefetch_transaction_debug=
                                args.prefetch_transaction_debug,
                            enter_before_stress=enter_before_stress))
                        wide_runs.append(run_mode(
                            runner=args.runner, rom=args.rom,
                            snapshot=wide_snapshot,
                            output=branch_root / "wide" / f"repeat-{repeat}",
                            action=action, frames=args.frames,
                            entry_settle_frames=settle_frames, wide=True,
                            prefetch_phase_guard=args.prefetch_phase_guard,
                            prefetch_transaction_debug=
                                args.prefetch_transaction_debug,
                            enter_before_stress=enter_before_stress))
                deterministic = {
                    "native": bool(native_runs) and
                              len({mode_signature(run)
                                   for run in native_runs}) == 1,
                    "wide": bool(wide_runs) and
                            len({mode_signature(run)
                                 for run in wide_runs}) == 1,
                }
                pairs = [compare_pair(native, wide, extra=43)
                         for native, wide in zip(native_runs, wide_runs)]
                failures = sorted(set(alignment_failures) | {
                    item for pair in pairs for item in pair["failures"]})
                branch_investigations = sorted(
                    set(alignment_investigations) | {
                        item for pair in pairs
                        for item in pair["investigations"]})
                if native_runs and not all(deterministic.values()):
                    failures.append("nondeterministic_repeat")
                status = "fail" if failures else (
                    "investigate" if branch_investigations else "pass")
                hard_failures += int(status == "fail")
                investigations += int(status == "investigate")
                branch = {
                    "entrance": entrance, "name": name, "action": action,
                    "mask": ACTIONS[action], "snapshot": str(snapshot.resolve()),
                    "snapshot_sha256": sha256(snapshot),
                    "aligned_gameplay_ready":
                        bool(args.align_gameplay_ready),
                    "deterministic": deterministic, "status": status,
                    "failures": failures,
                    "investigations": branch_investigations,
                    "pairs": pairs, "native_runs": native_runs,
                    "wide_runs": wide_runs,
                }
                report["branches"].append(branch)
                print(f"{entrance:04X} {name} {action}: {status}")
        report["summary"] = {
            "entries": len(entries), "branches": len(report["branches"]),
            "hard_failures": hard_failures,
            "investigations": investigations,
            "passes": len(report["branches"]) - hard_failures - investigations,
        }
        report["status"] = "fail" if hard_failures else (
            "investigate" if investigations else "pass")
        manifest = args.output / "report.json"
        manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report["summary"], indent=2))
        return 1 if hard_failures else 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"fresh_entry_stress_sweep: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
