#!/usr/bin/env python3
"""Verify that a forced widescreen fallback preserves actor phase gating.

Each input directory must contain ``lifecycle.jsonl`` and ``ws-trace.jsonl``
from the same deterministic route.  The verifier is deliberately concerned
with transitions rather than an endpoint dump: a margin-prefetched actor must
be suppressed before the fallback, remain held while presentation is centered,
and release only after its authored X enters the reconstructed stock window.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


PHASE_SCHEMA = "dkc1.prefetch-phase.v1"
WS_SCHEMA = "dkc1.ws.frame.v1"


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{line_number}: {error}") from error
            if not isinstance(row, dict):
                raise ValueError(f"{path}:{line_number}: expected object")
            rows.append(row)
    if not rows:
        raise ValueError(f"{path}: trace is empty")
    return rows


def canonical_hash(rows: list[dict[str, Any]]) -> str:
    payload = json.dumps(
        rows, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")
    return hashlib.sha256(payload).hexdigest().upper()


def actor_key(row: dict[str, Any]) -> tuple[int, int, int]:
    return (
        int(row.get("actor_index", -1)),
        int(row.get("id", -1)),
        int(row.get("source", -1)),
    )


def verify_run(run_dir: Path) -> dict[str, Any]:
    lifecycle_path = run_dir / "lifecycle.jsonl"
    ws_path = run_dir / "ws-trace.jsonl"
    lifecycle = load_jsonl(lifecycle_path)
    ws_rows = load_jsonl(ws_path)
    phases = [row for row in lifecycle if row.get("schema") == PHASE_SCHEMA]
    ws_frames = [row for row in ws_rows if row.get("schema") == WS_SCHEMA]
    if not phases:
        raise ValueError(f"{lifecycle_path}: no {PHASE_SCHEMA} rows")
    if not ws_frames:
        raise ValueError(f"{ws_path}: no {WS_SCHEMA} rows")

    forced = [
        row for row in ws_frames
        if row.get("decision", {}).get("debug_forced_fallback") == 1
    ]
    if len(forced) != 1:
        raise ValueError(
            f"{ws_path}: expected one forced fallback, found {len(forced)}"
        )
    forced_row = forced[0]
    forced_frame = int(forced_row["frame"])
    decision = forced_row.get("decision", {})
    if decision.get("centered_fallback") != 1:
        raise ValueError(f"{ws_path}: forced frame was not centered")
    if decision.get("edge_extension") != 0:
        raise ValueError(f"{ws_path}: forced frame retained edge extension")

    resets_after_start = [
        row for row in phases
        if row.get("event") == "context_reset"
        and int(row.get("frame", -1)) >= forced_frame
    ]
    if resets_after_start:
        raise ValueError(
            f"{lifecycle_path}: gameplay context reset during fallback"
        )

    held = [row for row in phases if row.get("event") == "soft_fallback_held"]
    if not held:
        raise ValueError(f"{lifecycle_path}: no soft_fallback_held event")
    verified_actors: list[dict[str, Any]] = []
    for row in held:
        held_frame = int(row.get("frame", -1))
        if held_frame not in (forced_frame, forced_frame + 1):
            raise ValueError(
                f"{lifecycle_path}: hold at unexpected frame {held_frame}"
            )
        if row.get("terrain_ready") is not False:
            raise ValueError(f"{lifecycle_path}: hold did not observe fallback")
        key = actor_key(row)
        candidates = [
            item for item in phases
            if item.get("event") == "prefetch_candidate"
            and actor_key(item) == key
            and int(item.get("frame", -1)) < held_frame
        ]
        suppressed = [
            item for item in phases
            if item.get("event") == "prefetch_suppressed"
            and actor_key(item) == key
            and int(item.get("frame", -1)) < held_frame
        ]
        released = [
            item for item in phases
            if item.get("event") == "prefetch_released"
            and actor_key(item) == key
            and int(item.get("frame", -1)) > held_frame
        ]
        if len(candidates) != 1 or len(suppressed) != 1 or len(released) != 1:
            raise ValueError(
                f"{lifecycle_path}: actor {key} lacks exactly one "
                "candidate/suppression/release transition"
            )
        release = released[0]
        source_x = int(release["source_x"])
        stock_left, stock_right = map(int, release["stock_window"])
        if not stock_left <= source_x <= stock_right:
            raise ValueError(
                f"{lifecycle_path}: actor {key} released outside stock window"
            )
        verified_actors.append({
            "actor_index": key[0],
            "id": key[1],
            "source": key[2],
            "candidate_frame": int(candidates[0]["frame"]),
            "held_frame": held_frame,
            "release_frame": int(release["frame"]),
        })

    return {
        "run_dir": str(run_dir.resolve()),
        "forced_frame": forced_frame,
        "verified_actors": verified_actors,
        "phase_hash": canonical_hash(phases),
        "ws_hash": canonical_hash(ws_frames),
        "phase_events": len(phases),
        "ws_frames": len(ws_frames),
    }


def verify_runs(run_dirs: list[Path]) -> dict[str, Any]:
    if len(run_dirs) < 3:
        raise ValueError("at least three independent runs are required")
    runs = [verify_run(path) for path in run_dirs]
    phase_hashes = {run["phase_hash"] for run in runs}
    ws_hashes = {run["ws_hash"] for run in runs}
    if len(phase_hashes) != 1 or len(ws_hashes) != 1:
        raise ValueError("runs are not byte-semantically deterministic")
    return {
        "schema": "dkc1.prefetch-soft-fallback-verification.v1",
        "status": "pass",
        "repeat_count": len(runs),
        "forced_frame": runs[0]["forced_frame"],
        "phase_hash": runs[0]["phase_hash"],
        "ws_hash": runs[0]["ws_hash"],
        "verified_actors": runs[0]["verified_actors"],
        "runs": runs,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dirs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = verify_runs(args.run_dirs)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
