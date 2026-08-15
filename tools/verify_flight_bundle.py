#!/usr/bin/env python3
"""Verify a DKC1 visible-host rolling repro bundle and optional replay."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


SCHEMA = "dkc1.flight-recorder.v1"
EXPECTED_SIZES = {
    "final.wram.bin": 0x20000,
    "final.vram.bin": 0x10000,
    "final.cgram.bin": 0x200,
    "final.wram-oam.bin": 544,
    "final.ppu-oam.bin": 544,
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_bundle(bundle: Path) -> dict:
    manifest_path = bundle / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA:
        raise ValueError(f"manifest schema must be {SCHEMA}")
    anchor = manifest.get("anchor_frame")
    current = manifest.get("current_frame")
    replay = manifest.get("replay_frames")
    if not all(isinstance(value, int) and value >= 0
               for value in (anchor, current, replay)):
        raise ValueError("frame fields must be nonnegative integers")
    if current - anchor != replay or replay > 3600:
        raise ValueError("replay_frames does not match the covered interval")
    files = manifest.get("files")
    if not isinstance(files, dict) or set(files) != {
            "anchor.snapshot", "current.snapshot", "inputs.txt",
            *EXPECTED_SIZES}:
        raise ValueError("manifest has an incomplete or unexpected file set")
    for name, expected_hash in files.items():
        if not re.fullmatch(r"[0-9a-f]{64}", str(expected_hash)):
            raise ValueError(f"invalid SHA-256 for {name}")
        path = bundle / name
        if not path.is_file() or sha256(path) != expected_hash:
            raise ValueError(f"missing or hash-mismatched file: {name}")
        if name in EXPECTED_SIZES and path.stat().st_size != EXPECTED_SIZES[name]:
            raise ValueError(f"wrong byte length for {name}")
    inputs = (bundle / "inputs.txt").read_text(encoding="ascii").splitlines()
    if len(inputs) != replay or any(
            not re.fullmatch(r"[0-9A-F]{3}", line) for line in inputs):
        raise ValueError("inputs.txt does not contain one 12-bit mask per frame")
    if (bundle / "anchor.snapshot").stat().st_size == 0 or \
            (bundle / "current.snapshot").stat().st_size == 0:
        raise ValueError("native snapshots must be nonempty")
    return manifest


def replay_bundle(bundle: Path, manifest: dict,
                  runner: Path, rom: Path) -> dict:
    if sha256(rom) != manifest.get("rom_sha256"):
        raise ValueError("replay ROM hash does not match the bundle")
    replay_frames = manifest["replay_frames"]
    if replay_frames == 0:
        return {"performed": False, "reason": "zero-frame bundle"}
    env = os.environ.copy()
    for name in ("DKC1_SCRIPT", "DKC1_WRAM_DUMP", "DKC1_WRAM_DUMP_PATH",
                 "DKC1_WRAM_DUMP_RANGES", "DKC1_WS_TRACE"):
        env.pop(name, None)
    env["DKC1_SAVESTATE_INPUT"] = str((bundle / "anchor.snapshot").resolve())
    env["SNESRECOMP_INPUT_PLAY"] = str((bundle / "inputs.txt").resolve())
    env["DKC1_WIDESCREEN"] = "1"
    with tempfile.TemporaryDirectory(prefix="dkc1-flight-replay-") as temp:
        raw = Path(temp) / "final.wram.bin"
        env["DKC1_WRAM_DUMP"] = f"{replay_frames}-{replay_frames}"
        env["DKC1_WRAM_DUMP_PATH"] = str(raw)
        completed = subprocess.run(
            [str(runner.resolve()), str(rom.resolve()), str(replay_frames)],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False)
        if completed.returncode != 0:
            raise ValueError(
                f"replay runner failed ({completed.returncode}): "
                f"{completed.stderr[-800:]}")
        replay_hash = sha256(raw)
    expected = manifest["files"]["final.wram.bin"]
    if replay_hash != expected:
        raise ValueError(
            f"replay WRAM differs: expected {expected}, got {replay_hash}")
    return {"performed": True, "wram_sha256": replay_hash,
            "frames": replay_frames}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--rom", type=Path)
    args = parser.parse_args(argv)
    try:
        manifest = verify_bundle(args.bundle)
        replay = {"performed": False}
        if bool(args.runner) != bool(args.rom):
            raise ValueError("--runner and --rom must be supplied together")
        if args.runner:
            replay = replay_bundle(args.bundle, manifest, args.runner, args.rom)
        print(json.dumps({"valid": True, "bundle": str(args.bundle),
                          "replay": replay}, indent=2))
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"verify_flight_bundle: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
