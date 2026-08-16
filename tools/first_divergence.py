#!/usr/bin/env python3
"""Stock-vs-wide first-divergence locator for the DKC1 recomp.

Method (ported from the SuperZSNES DKCFirstDivergenceLocator, simplified by
the recomp's determinism):

  pass 1: run the same route under DKC1_WIDESCREEN=0 and =1 with
          DKC1_WRAM_HASH_LOG capturing an ordered per-frame full-WRAM
          fingerprint. Because EVERY frame is fingerprinted, a transient
          divergence that later reconverges cannot be missed.
  pass 2: re-run both modes with the shared wram_dump tap capturing raw
          128 KiB WRAM for a window around the first differing frame, then
          classify the divergence: report BOTH the first raw difference and
          the first difference outside the expected-widescreen profile,
          with named include-groups and actor/bookkeeping decoding.
  confirm: pass 2 re-executes independently; if its window disagrees with
          pass 1's fingerprints the route is nondeterministic and the run
          aborts rather than reporting unstable evidence.

WRAM semantics from the SuperZSNES DkcTraceModel (opcode-verified).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

WRAM_SIZE = 0x20000
SEMANTIC_DUMP_RANGES = (
    "00020-00100,00500-00600,00880-008a0,00ae5-01e0d"
)

NAMED_FIELDS = {
    "game_mode": (0x0032, 2),
    "entrance": (0x003E, 2),
    "fade": (0x1DF1, 2),
    "layer_x": (0x088B, 2),
    "layer_y": (0x0895, 2),
    "camera_lower": (0x1B23, 2),
    "camera_upper": (0x1B25, 2),
    "scanner_window_left": (0x00EF, 2),
    "scanner_window_right": (0x00F1, 2),
    "scanner_cursor_primary": (0x00A0, 1),
    "scanner_cursor_secondary": (0x00A2, 1),
    "scanner_record_index": (0x00A4, 1),
    "section_state": (0x1E03, 2),
    "section_current": (0x1E07, 2),
    "section_pending": (0x1E09, 2),
}

ACTOR_FIRST, ACTOR_LAST = 0x02, 0x32
ACTOR_ARRAYS = {
    "id": 0x0D45, "source": 0x15FD, "x": 0x0B19, "y": 0x0BC1,
    "xs": 0x0E89, "ys": 0x0EF1, "state": 0x1029, "anim": 0x10D1,
    "pose": 0x0AE5, "current_pose": 0x0D11, "graphics": 0x0C69,
}
BOOKKEEPING = (0x192B, 0x1A2B)

# Ranges where wide-vs-stock differences are EXPECTED consequences of the
# presentation system (host camera bias reads, activation-window scratch) and
# must not mask the first real gameplay difference. Everything else that
# differs is unexpected. Deliberately narrow: raw differences are always
# reported alongside.
#
# These built-ins are the fallback; the authoritative copy is the explicit
# profile (contracts/wide-intended-differences.json, or --profile), so
# intended-difference policy is versioned data, not tool internals.
EXPECTED_WIDESCREEN_RANGES = [
    (0x00EF, 0x00F3),   # scanner window (widened activation comparisons)
]

EXPECTED_PRESENTATION_RANGES = [
    (0x0200, 0x0420),   # WRAM OAM shadow (low table + 9-bit high table)
]

DEFAULT_PROFILE = Path(__file__).resolve().parent.parent / \
    "contracts" / "wide-intended-differences.json"


def load_profile(path: Path) -> dict:
    """Replace the intended-difference ranges from an explicit profile."""
    global EXPECTED_WIDESCREEN_RANGES, EXPECTED_PRESENTATION_RANGES
    profile = json.loads(path.read_text())

    def ranges(key):
        return [(int(first, 0), int(last, 0))
                for first, last in profile.get(key, [])]

    if "expected_activation_ranges" in profile:
        EXPECTED_WIDESCREEN_RANGES = ranges("expected_activation_ranges")
    if "expected_presentation_ranges" in profile:
        EXPECTED_PRESENTATION_RANGES = ranges("expected_presentation_ranges")
    return profile

INCLUDE_GROUPS = {
    "core_gameplay": [(0x0020, 0x0100), (0x0500, 0x0600)],
    "actor_pool": [(0x0AE5, 0x1631 + 0x32)],
    "object_bookkeeping": [BOOKKEEPING],
    "scanner": [(0x00A0, 0x00A6), (0x00EF, 0x00F3)],
    "section_controller": [(0x1E03, 0x1E0D)],
    "camera_and_bounds": [(0x088B, 0x0897), (0x1B23, 0x1B27)],
}


def read16(memory: bytes, offset: int) -> int:
    return memory[offset] | (memory[offset + 1] << 8)


def run_host(exe: Path, rom: Path, frames: int, widescreen: bool,
             script: Path | None, extra_env: dict[str, str], cwd: Path,
             *, visible: bool = False, snapshot: Path | None = None,
             autoclose_ms: int = 1500) -> None:
    env = os.environ.copy()
    env["DKC1_WIDESCREEN"] = "1" if widescreen else "0"
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env.pop("DKC1_SCRIPT", None)
    env.pop("DKC1_WRAM_HASH_LOG", None)
    env.pop("DKC1_WRAM_DUMP", None)
    env.pop("DKC1_WRAM_DUMP_PATH", None)
    env.pop("DKC1_WRAM_DUMP_RANGES", None)
    env.pop("DKC1_FRAME_PPM", None)
    env.pop("DKC1_ROUTE_RESULT", None)
    env.pop("DKC1_ROUTE_FRAME_LIMIT", None)
    env.pop("DKC1_ROUTE_AUTOCLOSE_MS", None)
    env.pop("DKC1_SAVESTATE_INPUT", None)
    if script is not None:
        env["DKC1_SCRIPT"] = str(script)
    if snapshot is not None:
        env["DKC1_SAVESTATE_INPUT"] = str(snapshot)
    env.update(extra_env)
    if visible:
        result_path = (cwd / "visible-route-result.json").resolve()
        result_path.unlink(missing_ok=True)
        env["DKC1_ROUTE_RESULT"] = str(result_path)
        env["DKC1_ROUTE_FRAME_LIMIT"] = str(frames)
        env["DKC1_ROUTE_AUTOCLOSE_MS"] = str(max(250, autoclose_ms))
        process = subprocess.Popen([str(exe), str(rom)], cwd=str(cwd), env=env)
        timeout = max(60.0, frames / 30.0 + 45.0)
        try:
            returncode = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
            raise RuntimeError(
                f"visible run timed out after {timeout:.1f}s") from error
        if returncode != 0:
            raise RuntimeError(f"visible run failed (rc={returncode})")
        if not result_path.exists():
            raise RuntimeError("visible run exited without a route result")
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if result.get("status") != "complete":
            raise RuntimeError(
                f"visible route did not complete: {result.get('status')}")
        return

    result = subprocess.run(
        [str(exe), str(rom), str(frames)], cwd=str(cwd), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"headless run failed (rc={result.returncode}):\n"
            f"{result.stderr[-2000:]}")


def load_hash_log(path: Path) -> list[tuple[int, str]]:
    rows = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) == 2:
            rows.append((int(parts[0]), parts[1]))
    return rows


def first_hash_divergence(stock: list[tuple[int, str]],
                          wide: list[tuple[int, str]],
                          start_frame: int = 1) -> int | None:
    """Return the first mismatching relative frame at or after start_frame.

    Fresh-entry routes intentionally use different temporary Layer1 origins
    while the widened cartridge initializer fills extra columns.  Callers
    auditing settled gameplay must be able to retain those frames in the
    determinism logs while beginning divergence classification after the
    initializer/transition boundary.  Frame numbers are authoritative; do
    not assume a zero-based list index or silently align unequal rows.
    """
    for stock_row, wide_row in zip(stock, wide):
        if stock_row[0] != wide_row[0]:
            raise RuntimeError(
                "stock/wide hash logs have different frame numbering")
        if stock_row[0] >= start_frame and stock_row != wide_row:
            return stock_row[0]
    return None


def load_wram_frames(prefix: Path) -> dict[int, bytes]:
    """Read wram_dump.c output: <raw>.bin plus <raw>.bin.jsonl whose frame
    rows carry relative_frame, offset, length, and sha256 (first row is a
    manifest)."""
    frames: dict[int, bytes] = {}
    raw_path = prefix.with_suffix(".bin")
    index_path = Path(str(raw_path) + ".jsonl")
    if not index_path.exists() or not raw_path.exists():
        return frames
    raw = raw_path.read_bytes()
    ranges = [(0, WRAM_SIZE - 1)]
    for line in index_path.read_text().splitlines():
        record = json.loads(line)
        if record.get("type") == "manifest":
            declared = record.get("ranges")
            if declared:
                ranges = [(int(first, 16), int(last, 16))
                          for first, last in declared]
                expected_size = sum(last - first + 1
                                    for first, last in ranges)
                if expected_size != int(record.get("payload_size", -1)):
                    raise RuntimeError(
                        "wram dump manifest range/payload mismatch")
            continue
        if record.get("type") != "frame":
            continue
        frame = record.get("relative_frame", record.get("frame"))
        offset = int(record.get("offset", 0))
        length = int(record.get("length", WRAM_SIZE))
        payload = raw[offset:offset + length]
        if frame is None or len(payload) != length:
            continue
        digest = record.get("sha256")
        if digest and hashlib.sha256(payload).hexdigest() != digest:
            raise RuntimeError(
                f"wram dump index/sha mismatch at frame {frame}")
        expanded = bytearray(WRAM_SIZE)
        cursor = 0
        for first, last in ranges:
            range_length = last - first + 1
            expanded[first:last + 1] = payload[cursor:cursor + range_length]
            cursor += range_length
        if cursor != len(payload):
            raise RuntimeError(
                f"wram dump payload/range mismatch at frame {frame}")
        frames[int(frame)] = bytes(expanded)
    return frames


def in_ranges(offset: int, ranges) -> bool:
    return any(start <= offset < end for start, end in ranges)


def contiguous_ranges(offsets: list[int]) -> list[dict]:
    if not offsets:
        return []
    result = []
    start = previous = offsets[0]
    for offset in offsets[1:]:
        if offset != previous + 1:
            result.append({"first": f"0x{start:05X}",
                           "last": f"0x{previous:05X}",
                           "count": previous - start + 1})
            start = offset
        previous = offset
    result.append({"first": f"0x{start:05X}",
                   "last": f"0x{previous:05X}",
                   "count": previous - start + 1})
    return result


def classify(stock: bytes, wide: bytes) -> dict:
    diffs = [i for i in range(WRAM_SIZE) if stock[i] != wide[i]]
    groups = {}
    for name, ranges in INCLUDE_GROUPS.items():
        hits = [o for o in diffs if in_ranges(o, ranges)]
        if hits:
            groups[name] = {
                "count": len(hits),
                "first": f"0x{hits[0]:05X}",
                "last": f"0x{hits[-1]:05X}",
            }
    fields = {}
    for name, (offset, size) in NAMED_FIELDS.items():
        a = int.from_bytes(stock[offset:offset + size], "little")
        b = int.from_bytes(wide[offset:offset + size], "little")
        if a != b:
            fields[name] = {"stock": f"0x{a:04X}", "wide": f"0x{b:04X}"}
    actors = []
    render_pose_refresh_only = []
    conditional_presentation_offsets: set[int] = set()
    for index in range(ACTOR_FIRST, ACTOR_LAST + 2, 2):
        entry = {}
        for field, base in ACTOR_ARRAYS.items():
            a = read16(stock, base + index)
            b = read16(wide, base + index)
            if a != b:
                entry[field] = {"stock": f"0x{a:04X}", "wide": f"0x{b:04X}"}
        if entry:
            changed_fields = set(entry)
            entry["slot"] = index
            entry["stock_id"] = f"0x{read16(stock, 0x0D45 + index):04X}"
            entry["wide_id"] = f"0x{read16(wide, 0x0D45 + index):04X}"
            actors.append(entry)
            # $0AE5 is the pose pointer most recently submitted by the
            # object's render path. A widened view can refresh it before the
            # stock cull does. If every gameplay/animation field and the
            # desired $0D11 pose agree, classify only those two bytes as a
            # presentation refresh—not actor phase advancement.
            if changed_fields == {"pose"} and (
                    read16(stock, ACTOR_ARRAYS["current_pose"] + index) ==
                    read16(wide, ACTOR_ARRAYS["current_pose"] + index)):
                pose_offset = ACTOR_ARRAYS["pose"] + index
                conditional_presentation_offsets.update(
                    (pose_offset, pose_offset + 1))
                render_pose_refresh_only.append({
                    "slot": index,
                    "id": entry["stock_id"],
                    "source": f"0x{read16(stock, 0x15FD + index):04X}",
                    "stock_pose": entry["pose"]["stock"],
                    "wide_pose": entry["pose"]["wide"],
                    "current_pose":
                        f"0x{read16(stock, 0x0D11 + index):04X}",
                })
    bookmarks = []
    for offset in range(*BOOKKEEPING):
        if stock[offset] != wide[offset]:
            bookmarks.append({
                "record": offset - BOOKKEEPING[0],
                "stock": stock[offset], "wide": wide[offset],
            })
    expected_activation = [
        o for o in diffs if in_ranges(o, EXPECTED_WIDESCREEN_RANGES)]
    presentation = [
        o for o in diffs
        if in_ranges(o, EXPECTED_PRESENTATION_RANGES) or
        o in conditional_presentation_offsets]
    expected = set(expected_activation) | set(presentation)
    unexpected = [o for o in diffs if o not in expected]

    critical_actor_fields = {
        "id", "source", "x", "y", "xs", "ys", "state", "anim"
    }
    gameplay_actor_differences = [
        actor for actor in actors
        if critical_actor_fields.intersection(actor)
    ]
    gameplay_named_fields = {
        name: value for name, value in fields.items()
        if not name.startswith("scanner_")
    }
    actor_bookkeeping_critical = bool(
        gameplay_actor_differences or bookmarks)
    scene_outcome_fields = {
        name: value for name, value in fields.items()
        if name in {"game_mode", "entrance", "fade", "section_state",
                    "section_current", "section_pending"}
    }
    scene_outcome_critical = bool(scene_outcome_fields)
    gameplay_critical = bool(
        actor_bookkeeping_critical or gameplay_named_fields)
    if gameplay_critical:
        divergence_class = "gameplay_state"
    elif any(name.startswith("scanner_") for name in fields):
        divergence_class = "activation_or_presentation"
    elif diffs and not unexpected:
        divergence_class = "presentation_only"
    elif diffs:
        divergence_class = "unclassified_transient_or_scratch"
    else:
        divergence_class = "identical"
    return {
        "raw_diff_bytes": len(diffs),
        "expected_activation_diff_bytes": len(expected_activation),
        "presentation_diff_bytes": len(presentation),
        "unexpected_diff_bytes": len(unexpected),
        "first_raw_offset": f"0x{diffs[0]:05X}" if diffs else None,
        "first_unexpected_offset":
            f"0x{unexpected[0]:05X}" if unexpected else None,
        "groups": groups,
        "unexpected_ranges": contiguous_ranges(unexpected),
        "named_fields": fields,
        "actor_differences": actors[:16],
        "render_pose_refresh_only_actors": render_pose_refresh_only[:16],
        "gameplay_actor_differences": gameplay_actor_differences[:16],
        "bookmark_differences": bookmarks[:32],
        "gameplay_named_fields": gameplay_named_fields,
        "actor_bookkeeping_critical": actor_bookkeeping_critical,
        "scene_outcome_fields": scene_outcome_fields,
        "scene_outcome_critical": scene_outcome_critical,
        "gameplay_critical": gameplay_critical,
        "divergence_class": divergence_class,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, default=None,
                        help="intended-differences profile JSON (defaults "
                             "to contracts/wide-intended-differences.json "
                             "when present)")
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--script", type=Path,
                        help="DKC1_SCRIPT route (recommended) — the same "
                             "route runs in both modes")
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument(
        "--start-frame", type=int, default=1,
        help=("begin stock/wide divergence classification at this relative "
              "frame while still recording and determinism-checking the "
              "complete route (use after a proven initializer boundary)"))
    parser.add_argument("--snapshot-input", type=Path,
                        help="native full-machine anchor loaded before every "
                             "stock/wide pass")
    parser.add_argument("--visible", action="store_true",
                        help="run each pass in the visible desktop debugger")
    parser.add_argument("--autoclose-ms", type=int, default=1500,
                        help="visible paused-inspection time after each pass")
    parser.add_argument("--work", type=Path, default=Path("build/divergence"))
    parser.add_argument("--window", type=int, default=8,
                        help="raw-dump window radius around first divergence")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument(
        "--skip-semantic-scan", action="store_true",
        help="stop after classifying the first raw-difference window")
    parser.add_argument(
        "--reuse-existing-baseline", action="store_true",
        help=("reuse hash/input/raw-window artifacts already present under "
              "--work; every reused artifact is revalidated before the two "
              "semantic passes run"))
    args = parser.parse_args()

    if not 1 <= args.start_frame <= args.frames:
        parser.error("--start-frame must be within 1..--frames")

    profile_path = args.profile or (
        DEFAULT_PROFILE if DEFAULT_PROFILE.exists() else None)
    if profile_path is not None:
        profile = load_profile(profile_path)
        print(f"intended-differences profile: {profile_path} "
              f"({profile.get('note', 'no note')})")

    work = args.work
    work.mkdir(parents=True, exist_ok=True)
    exe = (args.exe or Path("build/dkc1_desktop.exe" if args.visible else
                            "build/dkc1_snesrecomp_headless.exe")).resolve()
    rom = args.rom.resolve()
    script = args.script.resolve() if args.script else None
    snapshot = args.snapshot_input.resolve() if args.snapshot_input else None
    if args.visible and "desktop" not in exe.name.lower():
        raise RuntimeError("--visible requires a desktop host executable")

    # stage 0: resolve the predicate-driven route into an exact-frame input
    # schedule under STOCK, then replay that fixed schedule in both modes.
    # Letting waits re-time themselves after a divergence would silently
    # de-align the two runs.
    inputs_path = (work / "resolved_inputs.txt").resolve()
    if script is not None:
        if not (args.reuse_existing_baseline and inputs_path.exists()):
            inputs_path.unlink(missing_ok=True)
            run_host(exe, rom, args.frames, False, script,
                     {"DKC1_INPUT_RECORD": str(inputs_path),
                      "DKC1_SESSION_DIR": str((work / "stage0").resolve())},
                     work, visible=args.visible, snapshot=snapshot,
                     autoclose_ms=args.autoclose_ms)
        if not inputs_path.exists():
            raise RuntimeError("input recording did not appear")
    elif not inputs_path.exists():
        raise RuntimeError("--script required (no resolved_inputs.txt yet)")

    def playback_env(extra: dict[str, str]) -> dict[str, str]:
        env = {"SNESRECOMP_INPUT_PLAY": str(inputs_path)}
        env.update(extra)
        return env

    # pass 1: fingerprint logs
    logs = {}
    for mode, wide in (("stock", False), ("wide", True)):
        log_path = (work / f"{mode}_hash.log").resolve()
        if not (args.reuse_existing_baseline and log_path.exists()):
            run_host(exe, rom, args.frames, wide, None,
                     playback_env({"DKC1_WRAM_HASH_LOG": str(log_path)}),
                     work, visible=args.visible, snapshot=snapshot,
                     autoclose_ms=args.autoclose_ms)
        logs[mode] = load_hash_log(log_path)
        if not logs[mode]:
            raise RuntimeError(f"empty baseline hash log: {log_path}")

    length = min(len(logs["stock"]), len(logs["wide"]))
    first = first_hash_divergence(
        logs["stock"][:length], logs["wide"][:length], args.start_frame)
    report = {
        "frames_compared": length,
        "comparison_frame_range": [args.start_frame, length],
        "first_divergence_frame": first,
    }
    if first is None:
        report["result"] = "no_divergence"
        print(json.dumps(report, indent=1))
        if args.json_out:
            args.json_out.write_text(json.dumps(report, indent=1))
        return 0

    # pass 2: raw window dumps + independent confirmation
    lo = max(args.start_frame, first - args.window)
    hi = first + args.window
    dumps = {}
    for mode, wide in (("stock", False), ("wide", True)):
        prefix = (work / f"{mode}_wram").resolve()
        hash2_path = (work / f"{mode}_hash2.log").resolve()
        reused_dump = False
        if args.reuse_existing_baseline:
            dumps[mode] = load_wram_frames(prefix)
            reused_dump = (
                all(frame in dumps[mode] for frame in range(lo, hi + 1)) and
                hash2_path.exists())
        if not reused_dump:
            for stale in (prefix.with_suffix(".bin"),
                          Path(str(prefix.with_suffix(".bin")) + ".jsonl")):
                stale.unlink(missing_ok=True)
            run_host(
                exe, rom, hi, wide, None,
                playback_env({
                    "DKC1_WRAM_DUMP": f"{lo}-{hi}",
                    "DKC1_WRAM_DUMP_PATH": str(prefix.with_suffix(".bin")),
                    "DKC1_WRAM_HASH_LOG": str(hash2_path)}),
                work, visible=args.visible, snapshot=snapshot,
                autoclose_ms=args.autoclose_ms)
            dumps[mode] = load_wram_frames(prefix)
        # confirmation: pass-2 fingerprints must reproduce pass 1
        confirm = load_hash_log(hash2_path)
        for i in range(min(len(confirm), hi)):
            if confirm[i] != logs[mode][i]:
                report["result"] = "nondeterministic_route"
                report["mode"] = mode
                report["disagrees_at_frame"] = confirm[i][0]
                print(json.dumps(report, indent=1))
                if args.json_out:
                    args.json_out.write_text(json.dumps(report, indent=1))
                return 3

    if first not in dumps["stock"] or first not in dumps["wide"]:
        report["result"] = "dump_window_missing_frame"
        print(json.dumps(report, indent=1))
        return 2

    report["result"] = "diverged"
    report["window"] = [lo, hi]
    report["at_first_divergence"] = classify(
        dumps["stock"][first], dumps["wide"][first])
    if first - 1 in dumps["stock"] and first - 1 in dumps["wide"]:
        report["frame_before"] = classify(
            dumps["stock"][first - 1], dumps["wide"][first - 1])
    for frame in range(lo, hi + 1):
        if frame not in dumps["stock"] or frame not in dumps["wide"]:
            continue
        frame_classification = classify(
            dumps["stock"][frame], dumps["wide"][frame])
        if frame_classification["gameplay_critical"]:
            report["first_gameplay_critical_frame_in_window"] = frame
            report["first_gameplay_critical_in_window"] = frame_classification
            break
    else:
        report["first_gameplay_critical_frame_in_window"] = None

    # pass 3: a sparse whole-route replay that excludes framebuffer/OAM bulk
    # but retains every actor, bookmark, section, logical-camera and core
    # gameplay field. This finds the first semantically important difference
    # even when the first raw divergence is only a widened draw-cache refresh.
    if not args.skip_semantic_scan:
        semantic_dumps = {}
        report["semantic_scan"] = {
            "frames": [args.start_frame, length],
            "ranges": SEMANTIC_DUMP_RANGES,
        }
        for mode, wide in (("stock", False), ("wide", True)):
            prefix = (work / f"{mode}_semantic_wram").resolve()
            for stale in (prefix.with_suffix(".bin"),
                          Path(str(prefix.with_suffix(".bin")) + ".jsonl")):
                stale.unlink(missing_ok=True)
            semantic_hash = (work / f"{mode}_semantic_hash.log").resolve()
            run_host(
                exe, rom, length, wide, None,
                playback_env({
                    "DKC1_WRAM_DUMP": f"{args.start_frame}-{length}",
                    "DKC1_WRAM_DUMP_PATH": str(prefix.with_suffix(".bin")),
                    "DKC1_WRAM_DUMP_RANGES": SEMANTIC_DUMP_RANGES,
                    "DKC1_WRAM_HASH_LOG": str(semantic_hash),
                }),
                work, visible=args.visible, snapshot=snapshot,
                autoclose_ms=args.autoclose_ms)
            semantic_dumps[mode] = load_wram_frames(prefix)
            confirm = load_hash_log(semantic_hash)
            if len(confirm) != len(logs[mode]):
                report["result"] = "nondeterministic_semantic_replay"
                report["mode"] = mode
                report["reason"] = "frame_count"
                report["expected_frames"] = len(logs[mode])
                report["actual_frames"] = len(confirm)
                print(json.dumps(report, indent=1))
                if args.json_out:
                    args.json_out.write_text(json.dumps(report, indent=1))
                return 3
            for i, row in enumerate(confirm):
                if row != logs[mode][i]:
                    report["result"] = "nondeterministic_semantic_replay"
                    report["mode"] = mode
                    report["disagrees_at_frame"] = row[0]
                    print(json.dumps(report, indent=1))
                    if args.json_out:
                        args.json_out.write_text(json.dumps(report, indent=1))
                    return 3

        first_semantic = None
        first_semantic_classification = None
        first_actor_bookkeeping = None
        first_actor_bookkeeping_classification = None
        first_scene_outcome = None
        first_scene_outcome_classification = None
        for frame in range(args.start_frame, length + 1):
            if (frame not in semantic_dumps["stock"] or
                    frame not in semantic_dumps["wide"]):
                continue
            classification = classify(
                semantic_dumps["stock"][frame],
                semantic_dumps["wide"][frame])
            if (first_actor_bookkeeping is None and
                    classification["actor_bookkeeping_critical"]):
                first_actor_bookkeeping = frame
                first_actor_bookkeeping_classification = classification
            if (first_scene_outcome is None and
                    classification["scene_outcome_critical"]):
                first_scene_outcome = frame
                first_scene_outcome_classification = classification
            if (first_semantic is None and
                    classification["gameplay_critical"]):
                first_semantic = frame
                first_semantic_classification = classification
        report["semantic_scan"]["first_gameplay_critical_frame"] = (
            first_semantic)
        if first_semantic_classification is not None:
            report["semantic_scan"]["first_gameplay_critical"] = (
                first_semantic_classification)
        report["semantic_scan"]["first_actor_bookkeeping_critical_frame"] = (
            first_actor_bookkeeping)
        if first_actor_bookkeeping_classification is not None:
            report["semantic_scan"]["first_actor_bookkeeping_critical"] = (
                first_actor_bookkeeping_classification)
        report["semantic_scan"]["first_scene_outcome_critical_frame"] = (
            first_scene_outcome)
        if first_scene_outcome_classification is not None:
            report["semantic_scan"]["first_scene_outcome_critical"] = (
                first_scene_outcome_classification)

    text = json.dumps(report, indent=1)
    print(text)
    if args.json_out:
        args.json_out.write_text(text)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
