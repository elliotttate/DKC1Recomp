#!/usr/bin/env python3
"""Summarize DKC1_WS_TRACE and locate evidence-worthy transition frames."""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sys


SCHEMA = "dkc1.ws.frame.v1"
REQUIRED_TOP = {
    "schema", "frame", "scene", "source", "camera", "ppu",
    "calibration", "decision", "world", "margin_tiles", "shadow_delta",
    "hash",
}
REQUIRED_HASHES = {
    "left", "center", "right", "bg1_left", "bg1_right", "bg2_left",
    "bg2_right", "vram", "ppu_oam", "wram_oam",
}
OPTIONAL_HASHES = {"cgram"}


def validate_record(record: dict, location: str) -> None:
    missing = REQUIRED_TOP.difference(record)
    if missing:
        raise ValueError(f"{location}: missing fields {sorted(missing)}")
    missing_hashes = REQUIRED_HASHES.difference(record["hash"])
    if missing_hashes:
        raise ValueError(
            f"{location}: missing hashes {sorted(missing_hashes)}")
    if len(record["world"]) != 2 or len(record["shadow_delta"]) != 2:
        raise ValueError(f"{location}: expected exactly two BG layers")
    for name in REQUIRED_HASHES.union(
            OPTIONAL_HASHES.intersection(record["hash"])):
        value = record["hash"][name]
        if not isinstance(value, str) or len(value) != 16 or any(
                char not in "0123456789abcdef" for char in value):
            raise ValueError(f"{location}: invalid {name} hash")


def load_trace(path: Path) -> list[dict]:
    frames: list[dict] = []
    epoch = 0
    previous_frame: int | None = None
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
            if record.get("schema") != SCHEMA:
                raise ValueError(
                    f"{path}:{line_number}: unsupported schema "
                    f"{record.get('schema')!r}")
            validate_record(record, f"{path}:{line_number}")
            frame = record.get("frame")
            if not isinstance(frame, int):
                raise ValueError(f"{path}:{line_number}: frame is not an int")
            # DKC resets/rewinds the PPU frame counter during some scene
            # transitions.  File order is the authoritative presentation
            # order, so retain both it and a monotonically increasing epoch
            # instead of rejecting the exact transition we need to inspect.
            if previous_frame is not None and frame <= previous_frame:
                epoch += 1
            record["_sequence"] = len(frames)
            record["_epoch"] = epoch
            frames.append(record)
            previous_frame = frame
    if not frames:
        raise ValueError(f"{path}: trace is empty")
    return frames


def fnv1a_zero_bytes(size: int) -> str:
    """Return the exact renderer FNV-1a hash for ``size`` zero bytes."""
    if size < 0:
        raise ValueError("zero-byte hash size must not be negative")
    value = 1469598103934665603
    for _ in range(size):
        value ^= 0
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def analyze(frames: list[dict], max_findings: int = 200,
            extra: int = 43) -> dict:
    if extra <= 0:
        raise ValueError("widescreen margin width must be positive")
    black_margin_hash = fnv1a_zero_bytes(extra * 4 * 224)
    decisions: Counter[str] = Counter()
    raw_fallback_frames: list[dict] = []
    refresh_frames: list[dict] = []
    stable_input_margin_changes: list[dict] = []
    stable_input_unproven_margin_changes: list[dict] = []
    identity_transitions: list[dict] = []
    policy_violations: list[dict] = []
    centered_nonblack_margin_frames: list[dict] = []
    frame_counter_resets: list[dict] = []
    previous: dict | None = None

    for record in frames:
        decision = record["decision"]
        if previous is not None and record.get("_epoch", 0) != \
                previous.get("_epoch", 0):
            frame_counter_resets.append({
                "sequence": record.get("_sequence"),
                "previous_frame": previous["frame"],
                "frame": record["frame"],
                "epoch": record.get("_epoch"),
            })
        for key, value in decision.items():
            if value:
                decisions[key] += 1
        if decision.get("identity_reset") and \
                len(identity_transitions) < max_findings:
            identity = record.get("identity", {})
            identity_transitions.append({
                "frame": record["frame"],
                "hash": identity.get("hash"),
                "change_mask": identity.get("change_mask"),
                "scene": record["scene"],
            })
        violations = []
        if decision.get("centered_fallback") and \
                decision.get("shadow_commit"):
            violations.append("centered_frame_committed_shadow")
        if decision.get("identity_reset") and decision.get("grace_accepted"):
            violations.append("new_identity_used_old_grace")
        if decision.get("shadow_commit") and not decision.get(
                "bounds_ready", True):
            violations.append("shadow_committed_before_camera_bounds")
        if bool(decision.get("shadow_frame")) != \
                bool(decision.get("shadow_commit")):
            violations.append("shadow_frame_commit_disagree")
        if decision.get("centered_fallback"):
            hashes = record["hash"]
            bad_sides = [side for side in ("left", "right")
                         if hashes.get(side) != black_margin_hash]
            if bad_sides:
                violations.append("centered_margin_not_black")
                if len(centered_nonblack_margin_frames) < max_findings:
                    centered_nonblack_margin_frames.append({
                        "frame": record["frame"],
                        "sides": bad_sides,
                        "expected": black_margin_hash,
                        "left": hashes.get("left"),
                        "right": hashes.get("right"),
                    })
        if violations and len(policy_violations) < max_findings:
            policy_violations.append({"frame": record["frame"],
                                      "violations": violations})
        raw = sum(layer["west_raw"] + layer["east_raw"]
                  for layer in record["shadow_delta"])
        refresh = sum(layer["prefill_refresh"]
                      for layer in record["shadow_delta"])
        if raw and len(raw_fallback_frames) < max_findings:
            raw_fallback_frames.append({"frame": record["frame"],
                                        "count": raw})
        if refresh and len(refresh_frames) < max_findings:
            refresh_frames.append({"frame": record["frame"],
                                   "count": refresh})

        if previous is not None:
            old_hash = previous["hash"]
            new_hash = record["hash"]
            has_full_hashes = all(
                key in old_hash and key in new_hash
                for key in ("wram", "cgram"))
            machine_hashes_same = has_full_hashes and all(
                old_hash.get(key) == new_hash.get(key)
                for key in ("wram", "vram", "cgram", "ppu_oam",
                            "wram_oam"))
            ppu_keys = ("mode", "bgmode", "inidisp", "main", "sub",
                        "bgsc", "h", "v", "wide_mask", "repeat_mask",
                        "terrain_layer")
            ppu_same = all(previous["ppu"].get(key) ==
                           record["ppu"].get(key) for key in ppu_keys)
            camera_keys = ("x", "y", "lower", "upper",
                           "presentation_bias")
            camera_same = all(previous["camera"].get(key) ==
                              record["camera"].get(key)
                              for key in camera_keys)
            world_same = previous["world"] == record["world"]
            identity_same = (previous.get("identity", {}).get("hash") ==
                             record.get("identity", {}).get("hash"))
            reset_boundary = any(record.get("decision", {}).get(key)
                                 for key in ("reset", "cold_start",
                                             "source_reset",
                                             "identity_reset"))
            center_same = old_hash.get("center") == new_hash.get("center")
            inputs_same = (machine_hashes_same and ppu_same and camera_same and
                           world_same and identity_same and not reset_boundary and
                           center_same)
            margin_changed = any(old_hash.get(key) != new_hash.get(key)
                                 for key in ("left", "right", "bg1_left",
                                             "bg1_right", "bg2_left",
                                             "bg2_right"))
            if inputs_same and margin_changed and \
                    len(stable_input_margin_changes) < max_findings:
                stable_input_margin_changes.append({
                    "frame": record["frame"],
                    "previous_frame": previous["frame"],
                    "camera": record["camera"],
                    "calibration": record["calibration"],
                    "decision": record["decision"],
                    "changed_hashes": [
                        key for key in ("left", "right", "bg1_left",
                                        "bg1_right", "bg2_left", "bg2_right")
                        if old_hash.get(key) != new_hash.get(key)
                    ],
                })
            elif margin_changed and center_same and not has_full_hashes and \
                    len(stable_input_unproven_margin_changes) < max_findings:
                # Legacy v1 traces did not include CGRAM. Preserve the lead,
                # but do not call it stable-input evidence: palette changes
                # can alter only transparent/edge pixels in the margins.
                stable_input_unproven_margin_changes.append({
                    "frame": record["frame"],
                    "previous_frame": previous["frame"],
                    "reason": "trace_missing_full_machine_hash",
                })
        previous = record

    return {
        "schema": "dkc1.ws.trace-summary.v1",
        "source_schema": SCHEMA,
        "frames": len(frames),
        "frame_range": [frames[0]["frame"], frames[-1]["frame"]],
        "sequence_range": [frames[0].get("_sequence", 0),
                           frames[-1].get("_sequence", len(frames) - 1)],
        "frame_epochs": 1 + max(
            (record.get("_epoch", 0) for record in frames), default=0),
        "frame_counter_resets": frame_counter_resets,
        "decision_counts": dict(sorted(decisions.items())),
        "raw_fallback_frames": raw_fallback_frames,
        "prefill_refresh_frames": refresh_frames,
        "identity_transitions": identity_transitions,
        "policy_violations": policy_violations,
        "black_margin_hash": black_margin_hash,
        "centered_nonblack_margin_frames": centered_nonblack_margin_frames,
        "stable_input_margin_changes": stable_input_margin_changes,
        "stable_input_unproven_margin_changes":
            stable_input_unproven_margin_changes,
        "truncated": any(len(items) >= max_findings for items in
                         (raw_fallback_frames, refresh_frames,
                          stable_input_margin_changes,
                          stable_input_unproven_margin_changes,
                          identity_transitions,
                          policy_violations,
                          centered_nonblack_margin_frames)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--max-findings", type=int, default=200)
    parser.add_argument(
        "--extra", type=int, default=43,
        help="added pixels on each side (43 for 342x224, 71 for 398x224)")
    args = parser.parse_args()
    try:
        summary = analyze(load_trace(args.trace), args.max_findings,
                          args.extra)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    rendered = json.dumps(summary, indent=2, sort_keys=True)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
