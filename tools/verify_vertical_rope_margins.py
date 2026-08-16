#!/usr/bin/env python3
"""Verify DKC1 vertical-rope OAM behavior across both wide margins."""

from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.oam_inspect import decode_entries, load_metadata, load_records, visible


ROPE_TILE = 0x60
ROPE_ATTR = 0x36


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def rope_entries(oam: bytes) -> list[dict[str, Any]]:
    return [
        entry for entry in decode_entries(oam)
        if visible(entry) and entry["tile"] == ROPE_TILE
        and entry["attr"] == ROPE_ATTR and entry["big"] == 1
    ]


def verify_run(run_dir: Path, side: str, extra: int) -> dict[str, Any]:
    oam_path = run_dir / "oam.bin"
    if not oam_path.exists():
        raise ValueError(f"{run_dir}: missing oam.bin")
    metadata = load_metadata(oam_path)
    if not metadata:
        raise ValueError(f"{run_dir}: missing OAM metadata")
    margin_hits: list[dict[str, int]] = []
    native_hits = 0
    alias_hits: list[dict[str, int]] = []
    max_chain = 0
    mismatch_streak = 0
    max_mismatch_streak = 0
    shadow_history: deque[bytes] = deque(maxlen=4)
    active_frames = 0

    for frame, shadow, ppu in load_records(oam_path):
        meta = metadata.get(frame, {})
        if meta.get("forced_blank") or not meta.get("gameplay", True):
            shadow_history.append(shadow)
            mismatch_streak = 0
            continue
        active_frames += 1
        if ppu == shadow or ppu in shadow_history:
            mismatch_streak = 0
        else:
            mismatch_streak += 1
            max_mismatch_streak = max(max_mismatch_streak, mismatch_streak)
        shadow_history.append(shadow)

        entries = rope_entries(ppu)
        native_hits += sum(0 <= entry["x"] < 256 for entry in entries)
        if side == "right":
            margin = [entry for entry in entries
                      if 256 <= entry["x"] < 256 + extra]
        elif side == "left":
            margin = [entry for entry in entries
                      if 512 - extra <= entry["x"] < 512]
        else:
            raise ValueError(f"unknown side {side!r}")
        if margin:
            grouped: dict[int, list[dict[str, Any]]] = {}
            for entry in margin:
                grouped.setdefault(int(entry["x"]), []).append(entry)
                margin_hits.append({
                    "frame": frame,
                    "index": int(entry["index"]),
                    "x": int(entry["x"]),
                    "y": int(entry["y"]),
                })
                low_x = int(entry["x"]) & 0xff
                aliases = [
                    other for other in entries
                    if other["index"] != entry["index"]
                    and other["x"] == low_x and other["y"] == entry["y"]
                ]
                for other in aliases:
                    alias_hits.append({
                        "frame": frame, "x": low_x,
                        "y": int(entry["y"]),
                        "index": int(other["index"]),
                    })
            max_chain = max(max_chain, *(len(group) for group in grouped.values()))

    if not margin_hits:
        raise ValueError(f"{run_dir}: no {side}-margin rope entries")
    if native_hits == 0:
        raise ValueError(f"{run_dir}: route did not cross the native boundary")
    if max_chain < 4:
        raise ValueError(f"{run_dir}: no complete four-segment rope chain")
    if alias_hits:
        raise ValueError(f"{run_dir}: rope appeared at both 9-bit X aliases")
    if max_mismatch_streak > 1:
        raise ValueError(f"{run_dir}: persistent WRAM/PPU OAM mismatch")

    return {
        "run_dir": str(run_dir.resolve()),
        "side": side,
        "oam_sha256": sha256(oam_path),
        "active_frames": active_frames,
        "native_entries": native_hits,
        "margin_entries": len(margin_hits),
        "first_margin": margin_hits[0],
        "last_margin": margin_hits[-1],
        "max_segments_at_one_x": max_chain,
        "max_shadow_ppu_mismatch_streak": max_mismatch_streak,
        "alias_entries": 0,
    }


def verify_contract(root: Path, repeats: int = 3, extra: int = 43) -> dict:
    if repeats < 3:
        raise ValueError("at least three repeats are required")
    results: dict[str, list[dict[str, Any]]] = {}
    for label, side in (("right-margin", "right"), ("left-margin", "left")):
        runs = [
            verify_run(root / label / f"run{index}", side, extra)
            for index in range(1, repeats + 1)
        ]
        if len({run["oam_sha256"] for run in runs}) != 1:
            raise ValueError(f"{label}: OAM streams are not deterministic")
        results[label] = runs
    return {
        "schema": "dkc1.vertical-rope-margin-contract.v1",
        "status": "pass",
        "repeat_count": repeats,
        "extra_pixels": extra,
        "right_margin": results["right-margin"][0],
        "left_margin": results["left-margin"][0],
        "runs": results,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--extra", type=int, default=43)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = verify_contract(args.root, args.repeats, args.extra)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
