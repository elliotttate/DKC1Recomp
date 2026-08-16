#!/usr/bin/env python3
"""Verify that a native state preserves DKC1 host widescreen history.

The oracle is a split run versus an uninterrupted run from the same input
snapshot.  At ``split`` the runtime writes a v9 state containing guest state,
the sparse world-keyed BG shadow, and placed-object/stream phase state.  A
fresh process loads that state and completes the remaining frames.  Final
frame/machine hashes, renderer state, and cumulative shadow counters must be
byte-for-byte equal. Version 9 also preserves hidden PPU data-port/latch state,
preventing valid character-DMA source bytes from being permuted after load.

This deliberately starts from an existing snapshot instead of a clean boot:
playtester states are the workflow this feature exists to protect.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from typing import Iterable


RTL_MAGIC = 0x52544C53
RTL_PRESENTATION_VERSION = 9
COMPARE_KEYS = (
    "video_state",
    "shadow_stats",
    "frame_sha256",
    "wram_sha256",
    "vram_sha256",
    "cgram_sha256",
    "oam_sha256",
    "oam_source_sha256",
)
ENV_REMOVE = (
    "DKC1_SCRIPT",
    "DKC1_INPUT_PLAYBACK",
    "DKC1_ROUTE_RESULT",
    "DKC1_ROUTE_FRAME_LIMIT",
    "DKC1_ROUTE_AUTOCLOSE_MS",
    "DKC1_SAVESTATE_INPUT",
    "DKC1_SAVESTATE_OUTPUT",
    "DKC1_SAVESTATE_SAVE_AT",
    "DKC1_SUPERZSNES_STATE",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_last(lines: Iterable[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in lines:
        for key in COMPARE_KEYS:
            if line.startswith(key + "=") or line.startswith(key + " "):
                result[key] = line.strip()
    return result


def run_case(runner: Path, rom: Path, snapshot: Path, frames: int,
             *, output_state: Path | None = None,
             save_at: int | None = None) -> tuple[list[str], dict[str, str]]:
    env = os.environ.copy()
    for key in ENV_REMOVE:
        env.pop(key, None)
    env.update({
        "DKC1_WIDESCREEN": "1",
        "DKC1_WIDESCREEN_PRESET": "16:9",
        "DKC1_SAVESTATE_INPUT": str(snapshot.resolve()),
    })
    if output_state is not None:
        env["DKC1_SAVESTATE_OUTPUT"] = str(output_state.resolve())
        env["DKC1_SAVESTATE_SAVE_AT"] = str(save_at)
    completed = subprocess.run(
        [str(runner.resolve()), str(rom.resolve()), str(frames)],
        cwd=runner.resolve().parent.parent,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    lines = completed.stdout.splitlines()
    if completed.returncode:
        raise RuntimeError(
            f"runner failed with {completed.returncode}:\n" +
            "\n".join(lines[-80:]))
    parsed = parse_last(lines)
    missing = [key for key in COMPARE_KEYS if key not in parsed]
    if missing:
        raise RuntimeError(f"runner output lacks {missing}:\n" +
                           "\n".join(lines[-80:]))
    return lines, parsed


def verify(args: argparse.Namespace) -> dict:
    runner = args.runner.resolve()
    rom = args.rom.resolve()
    snapshot = args.snapshot.resolve()
    output_dir = args.output.resolve()
    if args.frames <= 1 or args.split <= 0 or args.split >= args.frames:
        raise ValueError("require frames > 1 and 0 < split < frames")
    for path, label in ((runner, "runner"), (rom, "ROM"),
                        (snapshot, "snapshot")):
        if not path.is_file():
            raise FileNotFoundError(f"{label} not found: {path}")
    output_dir.mkdir(parents=True, exist_ok=True)
    split_state = output_dir / "split-v9.state"

    continuous_lines, continuous = run_case(
        runner, rom, snapshot, args.frames,
        output_state=split_state, save_at=args.split)
    restored_lines, restored = run_case(
        runner, rom, split_state, args.frames - args.split)
    (output_dir / "continuous.txt").write_text(
        "\n".join(continuous_lines) + "\n", encoding="utf-8")
    (output_dir / "restored.txt").write_text(
        "\n".join(restored_lines) + "\n", encoding="utf-8")

    header = split_state.read_bytes()[:8]
    if len(header) != 8:
        raise RuntimeError("split state is truncated")
    magic, version = struct.unpack("<II", header)
    comparisons = {
        key: {
            "match": continuous[key] == restored[key],
            "continuous": continuous[key],
            "restored": restored[key],
        }
        for key in COMPARE_KEYS
    }
    passed = (magic == RTL_MAGIC and version == RTL_PRESENTATION_VERSION and
              all(item["match"] for item in comparisons.values()))
    report = {
        "schema": "dkc1.widescreen-savestate-roundtrip.v1",
        "passed": passed,
        "runner": str(runner),
        "runner_sha256": sha256(runner),
        "rom": str(rom),
        "rom_sha256": sha256(rom),
        "input_snapshot": str(snapshot),
        "input_snapshot_sha256": sha256(snapshot),
        "frames": args.frames,
        "split": args.split,
        "split_state": str(split_state),
        "split_state_size": split_state.stat().st_size,
        "split_state_sha256": sha256(split_state),
        "state_magic": f"{magic:08X}",
        "state_version": version,
        "comparisons": comparisons,
    }
    (output_dir / "report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path,
                        default=Path("build/dkc1_headless_tools.exe"))
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--output", type=Path,
                        default=Path("build/widescreen-savestate-roundtrip"))
    parser.add_argument("--frames", type=int, default=60)
    parser.add_argument("--split", type=int, default=30)
    args = parser.parse_args()
    try:
        report = verify(args)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({
        "passed": report["passed"],
        "state_version": report["state_version"],
        "state_size": report["split_state_size"],
        "report": str((args.output / "report.json").resolve()),
    }, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
