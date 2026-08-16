#!/usr/bin/env python3
"""Locate retained widescreen-margin contamination across a route transition.

For each sampled route frame this tool saves an exact native snapshot and the
frame that was presented with the route's live widescreen history.  It then
loads that snapshot in a fresh process and asks the headless host for a
zero-frame render.  WRAM/VRAM/OAM/PPU state is therefore held constant while
the host-only widescreen shadow is rebuilt from a clean history.

A margin-only pixel difference with byte-identical machine memories is strong
evidence of retained transition state.  A native-center difference or a raw
machine-state difference is rejected instead of being mislabeled.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Callable


SCHEMA = "dkc1.transition-contamination.v1"
ROM_SHA256 = "fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15"
DEFAULT_EXTRA = 43
DEFAULT_NATIVE_WIDTH = 256
DEFAULT_HEIGHT = 224

_ENV_KEYS = {
    "DKC1_AUDIO_PCM", "DKC1_FRAME_PPM", "DKC1_FRAME_PPM_PREFIX",
    "DKC1_FRAME_PPM_START", "DKC1_FRAME_PPM_END", "DKC1_FRAME_PPM_STEP",
    "DKC1_SAVESTATE_INPUT", "DKC1_SAVESTATE_OUTPUT",
    "DKC1_SAVESTATE_SAVE_AT", "DKC1_SUPERZSNES_STATE", "DKC1_SCRIPT",
    "DKC1_WRAM_OUTPUT", "DKC1_VRAM_OUTPUT", "DKC1_WS_TRACE",
    "DKC1_WS_COLD_STATE_LOAD",
    "SNESRECOMP_INPUT_PLAY",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise ValueError(f"{path}: expected P6 PPM")
    pos = 2
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while pos < len(data) and chr(data[pos]).isspace():
            pos += 1
        if pos < len(data) and data[pos] == ord("#"):
            while pos < len(data) and data[pos] not in b"\r\n":
                pos += 1
            continue
        start = pos
        while pos < len(data) and not chr(data[pos]).isspace():
            pos += 1
        if start == pos:
            raise ValueError(f"{path}: truncated PPM header")
        tokens.append(data[start:pos])
    try:
        width, height, maximum = map(int, tokens)
    except ValueError as error:
        raise ValueError(f"{path}: invalid PPM header") from error
    if maximum != 255 or width <= 0 or height <= 0:
        raise ValueError(f"{path}: unsupported PPM geometry/range")
    if pos >= len(data) or not chr(data[pos]).isspace():
        raise ValueError(f"{path}: missing PPM raster separator")
    pos += 2 if data[pos:pos + 2] == b"\r\n" else 1
    raster = data[pos:]
    expected = width * height * 3
    if len(raster) != expected:
        raise ValueError(
            f"{path}: raster is {len(raster)} bytes, expected {expected}")
    return width, height, raster


def region(raster: bytes, width: int, height: int,
           x0: int, x1: int) -> bytes:
    output = bytearray()
    for y in range(height):
        start = (y * width + x0) * 3
        output.extend(raster[start:start + (x1 - x0) * 3])
    return bytes(output)


def changed_pixels(before: bytes, after: bytes) -> tuple[int, list[int] | None]:
    if len(before) != len(after) or len(before) % 3:
        raise ValueError("pixel regions differ in length")
    changed = 0
    first = None
    last = None
    for pixel in range(len(before) // 3):
        start = pixel * 3
        if before[start:start + 3] != after[start:start + 3]:
            changed += 1
            if first is None:
                first = pixel
            last = pixel
    return changed, None if first is None else [first, last]


def classify_pair(path_ppm: Path, fresh_ppm: Path,
                  path_wram: Path, fresh_wram: Path,
                  path_vram: Path, fresh_vram: Path,
                  extra: int = DEFAULT_EXTRA) -> dict:
    pw, ph, path_raster = read_ppm(path_ppm)
    fw, fh, fresh_raster = read_ppm(fresh_ppm)
    expected_width = DEFAULT_NATIVE_WIDTH + 2 * extra
    if (pw, ph) != (fw, fh) or (pw, ph) != (expected_width, DEFAULT_HEIGHT):
        raise ValueError(
            f"unexpected frame geometry path={pw}x{ph} fresh={fw}x{fh}")

    raw = {}
    for name, before, after in (
            ("wram", path_wram, fresh_wram),
            ("vram", path_vram, fresh_vram)):
        before_hash, after_hash = sha256(before), sha256(after)
        raw[name] = {"path_sha256": before_hash,
                     "fresh_sha256": after_hash,
                     "exact": before_hash == after_hash}

    regions = {}
    ranges = {
        "left": (0, extra),
        "center": (extra, extra + DEFAULT_NATIVE_WIDTH),
        "right": (extra + DEFAULT_NATIVE_WIDTH, expected_width),
    }
    for name, (x0, x1) in ranges.items():
        before = region(path_raster, pw, ph, x0, x1)
        after = region(fresh_raster, fw, fh, x0, x1)
        count, linear_bounds = changed_pixels(before, after)
        bounds = None
        if linear_bounds is not None:
            region_width = x1 - x0
            first, last = linear_bounds
            bounds = [first % region_width + x0, first // region_width,
                      last % region_width + x0, last // region_width]
        regions[name] = {
            "path_sha256": hashlib.sha256(before).hexdigest(),
            "fresh_sha256": hashlib.sha256(after).hexdigest(),
            "changed_pixels": count,
            "bounds": bounds,
        }

    raw_exact = all(value["exact"] for value in raw.values())
    center_exact = regions["center"]["changed_pixels"] == 0
    margin_changed = (regions["left"]["changed_pixels"] > 0 or
                      regions["right"]["changed_pixels"] > 0)
    if not raw_exact:
        classification = "machine_state_mismatch"
    elif not center_exact:
        classification = "native_center_mismatch"
    elif margin_changed:
        classification = "retained_margin_contamination"
    else:
        classification = "identical"
    return {"classification": classification, "raw": raw,
            "regions": regions, "center_exact": center_exact,
            "margin_changed": margin_changed}


def locate_boundary(good_frame: int, bad_frame: int,
                    evaluate: Callable[[int], str]) -> tuple[int | None, list[dict]]:
    if good_frame < 1 or bad_frame <= good_frame:
        raise ValueError("require 1 <= good_frame < bad_frame")
    samples: list[dict] = []

    def sample(frame: int) -> str:
        classification = evaluate(frame)
        samples.append({"frame": frame, "classification": classification})
        return classification

    good = sample(good_frame)
    bad = sample(bad_frame)
    if good != "identical":
        raise ValueError(
            f"good endpoint {good_frame} is {good}, expected identical")
    if bad == "identical":
        return None, samples
    if bad != "retained_margin_contamination":
        raise ValueError(f"bad endpoint {bad_frame} is not margin contamination: {bad}")

    low, high = good_frame, bad_frame
    while high - low > 1:
        middle = (low + high) // 2
        classification = sample(middle)
        if classification == "identical":
            low = middle
        elif classification == "retained_margin_contamination":
            high = middle
        else:
            raise ValueError(
                f"frame {middle} produced invalid classification {classification}")
    return high, samples


def clean_environment() -> dict[str, str]:
    env = os.environ.copy()
    for key in _ENV_KEYS:
        env.pop(key, None)
    return env


def run_process(command: list[str], env: dict[str, str],
                stdout_path: Path, stderr_path: Path) -> None:
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        result = subprocess.run(command, env=env, stdout=stdout, stderr=stderr,
                                check=False)
    if result.returncode:
        raise RuntimeError(
            f"runner exited {result.returncode}; see {stderr_path}")


def parse_runner_hashes(path: Path) -> dict[str, str]:
    wanted = {"cgram_sha256", "oam_sha256", "oam_source_sha256"}
    found: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator and key in wanted and len(value.strip()) == 64:
            found[key] = value.strip().lower()
    missing = wanted - found.keys()
    if missing:
        raise ValueError(f"{path}: missing runner hashes {sorted(missing)}")
    return found


class Evaluator:
    def __init__(self, *, runner: Path, rom: Path, output: Path,
                 snapshot: Path | None, superzsnes_state: Path | None,
                 input_play: Path | None, script: Path | None,
                 extra: int):
        self.runner = runner
        self.rom = rom
        self.output = output
        self.snapshot = snapshot
        self.superzsnes_state = superzsnes_state
        self.input_play = input_play
        self.script = script
        self.extra = extra
        self.cache: dict[tuple[int, int], dict] = {}

    def _root_environment(self) -> dict[str, str]:
        env = clean_environment()
        env["DKC1_WIDESCREEN"] = "1"
        if self.snapshot:
            env["DKC1_SAVESTATE_INPUT"] = str(self.snapshot)
        if self.superzsnes_state:
            env["DKC1_SUPERZSNES_STATE"] = str(self.superzsnes_state)
        if self.input_play:
            env["SNESRECOMP_INPUT_PLAY"] = str(self.input_play)
        if self.script:
            env["DKC1_SCRIPT"] = str(self.script)
        return env

    def evaluate(self, frame: int, repeat: int = 1) -> dict:
        key = (frame, repeat)
        if key in self.cache:
            return self.cache[key]
        root = self.output / f"frame-{frame:08d}" / f"repeat-{repeat}"
        root.mkdir(parents=True, exist_ok=False)
        snapshot = root / "route.snapshot"
        path_ppm, fresh_ppm = root / "path.ppm", root / "fresh.ppm"
        path_wram, fresh_wram = root / "path.wram", root / "fresh.wram"
        path_vram, fresh_vram = root / "path.vram", root / "fresh.vram"

        path_env = self._root_environment()
        path_env.update({
            "DKC1_FRAME_PPM": str(path_ppm),
            "DKC1_WRAM_OUTPUT": str(path_wram),
            "DKC1_VRAM_OUTPUT": str(path_vram),
            "DKC1_SAVESTATE_OUTPUT": str(snapshot),
            "DKC1_SAVESTATE_SAVE_AT": str(frame),
            "DKC1_WS_TRACE": str(root / "path-trace.jsonl"),
        })
        run_process([str(self.runner), str(self.rom), str(frame)], path_env,
                    root / "path-stdout.txt", root / "path-stderr.txt")

        fresh_env = clean_environment()
        fresh_env.update({
            "DKC1_WIDESCREEN": "1",
            "DKC1_WS_COLD_STATE_LOAD": "1",
            "DKC1_SAVESTATE_INPUT": str(snapshot),
            "DKC1_FRAME_PPM": str(fresh_ppm),
            "DKC1_WRAM_OUTPUT": str(fresh_wram),
            "DKC1_VRAM_OUTPUT": str(fresh_vram),
            "DKC1_WS_TRACE": str(root / "fresh-trace.jsonl"),
        })
        run_process([str(self.runner), str(self.rom), "0"], fresh_env,
                    root / "fresh-stdout.txt", root / "fresh-stderr.txt")

        result = classify_pair(path_ppm, fresh_ppm, path_wram, fresh_wram,
                               path_vram, fresh_vram, self.extra)
        path_hashes = parse_runner_hashes(root / "path-stdout.txt")
        fresh_hashes = parse_runner_hashes(root / "fresh-stdout.txt")
        for hash_key in ("cgram_sha256", "oam_sha256",
                         "oam_source_sha256"):
            name = hash_key.removesuffix("_sha256")
            result["raw"][name] = {
                "path_sha256": path_hashes[hash_key],
                "fresh_sha256": fresh_hashes[hash_key],
                "exact": path_hashes[hash_key] == fresh_hashes[hash_key],
            }
        if not all(item["exact"] for item in result["raw"].values()):
            result["classification"] = "machine_state_mismatch"
        result.update({"frame": frame, "repeat": repeat,
                       "directory": str(root)})
        (root / "comparison.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8")
        self.cache[key] = result
        return result


def build_report(args: argparse.Namespace, evaluator: Evaluator) -> dict:
    boundary, samples = locate_boundary(
        args.good_frame, args.bad_frame,
        lambda frame: evaluator.evaluate(frame)["classification"])
    confirmations: list[dict] = []
    if boundary is not None:
        for repeat in range(1, args.repeats + 1):
            confirmations.append(evaluator.evaluate(boundary, repeat))
        previous = evaluator.evaluate(boundary - 1)
        if previous["classification"] != "identical":
            raise ValueError(
                f"boundary predecessor {boundary - 1} is not identical")
        signatures = {
            (item["classification"],
             item["regions"]["left"]["path_sha256"],
             item["regions"]["right"]["path_sha256"],
             item["regions"]["left"]["fresh_sha256"],
             item["regions"]["right"]["fresh_sha256"])
            for item in confirmations
        }
        deterministic = len(signatures) == 1
    else:
        deterministic = True

    inputs = {
        "runner": str(args.runner), "runner_sha256": sha256(args.runner),
        "rom": str(args.rom), "rom_sha256": sha256(args.rom),
        "snapshot": str(args.snapshot) if args.snapshot else None,
        "snapshot_sha256": sha256(args.snapshot) if args.snapshot else None,
        "superzsnes_state": (str(args.superzsnes_state)
                              if args.superzsnes_state else None),
        "input_play": str(args.input_play) if args.input_play else None,
        "input_play_sha256": sha256(args.input_play) if args.input_play else None,
        "script": str(args.script) if args.script else None,
        "script_sha256": sha256(args.script) if args.script else None,
    }
    return {
        "schema": SCHEMA,
        "inputs": inputs,
        "window": {"good_frame": args.good_frame,
                   "bad_frame": args.bad_frame,
                   "bisection_assumes_monotonic_within_window": True},
        "boundary_frame": boundary,
        "classification": ("no_contamination_at_bad_endpoint"
                           if boundary is None
                           else "retained_margin_contamination"),
        "deterministic": deterministic,
        "analysis_valid": deterministic,
        "passed": boundary is None and deterministic,
        "samples": samples,
        "confirmations": confirmations,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    root = parser.add_mutually_exclusive_group()
    root.add_argument("--snapshot", type=Path)
    root.add_argument("--superzsnes-state", type=Path)
    route = parser.add_mutually_exclusive_group()
    route.add_argument("--input-play", type=Path)
    route.add_argument("--script", type=Path)
    parser.add_argument("--good-frame", type=int, required=True)
    parser.add_argument("--bad-frame", type=int, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--extra", type=int, default=DEFAULT_EXTRA)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        for path in (args.runner, args.rom, args.snapshot,
                     args.input_play, args.script):
            if path is not None and not path.is_file():
                raise ValueError(f"missing file: {path}")
        if args.superzsnes_state is not None and not args.superzsnes_state.is_dir():
            raise ValueError(f"missing SuperZSNES bundle: {args.superzsnes_state}")
        if sha256(args.rom) != ROM_SHA256:
            raise ValueError("ROM is not verified headerless DKC USA v1.0")
        if not 1 <= args.repeats <= 10:
            raise ValueError("repeats must be 1..10")
        if args.extra <= 0:
            raise ValueError("extra must be positive")
        if args.output.exists() and any(args.output.iterdir()):
            raise ValueError(f"output directory is not empty: {args.output}")
        args.output.mkdir(parents=True, exist_ok=True)
        evaluator = Evaluator(
            runner=args.runner, rom=args.rom, output=args.output,
            snapshot=args.snapshot, superzsnes_state=args.superzsnes_state,
            input_play=args.input_play, script=args.script, extra=args.extra)
        report = build_report(args, evaluator)
        (args.output / "report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"classification": report["classification"],
                          "boundary_frame": report["boundary_frame"],
                          "deterministic": report["deterministic"]}))
        return 0 if report["passed"] else 1
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"transition contamination bisector: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
