#!/usr/bin/env python3
"""Delta-debug a failing input recording to a minimal reproduction.

Input: a resolved per-frame input file (DKC1_INPUT_RECORD output or any
input-playback file) plus a failure predicate evaluated on the final WRAM.
The tool ddmin-shrinks the recording while preserving the predicate.

Soundness rules ported from the SuperZSNES DKCMacroMinimizer:
  - every candidate (not only the final result) is replayed `--confirm`
    times; all replays must agree on the predicate AND on the final
    full-WRAM hash. Any disagreement aborts the run as nondeterministic
    instead of producing an unstable "minimal" macro.
  - button transitions can be preserved (`--preserve-transitions`): removal
    candidates then only shorten constant runs, never delete press edges —
    vital when press semantics (jump buffering) matter.
  - frames are removed, never reordered.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def load_inputs(path: Path) -> list[int]:
    masks = []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].split(";", 1)[0].strip()
        if not line:
            continue
        if "*" in line:
            mask_text, count_text = [t.strip() for t in line.split("*", 1)]
            masks.extend([int(mask_text, 16)] * int(count_text))
        else:
            masks.append(int(line, 16))
    return masks


def write_inputs(path: Path, masks: list[int]) -> None:
    path.write_text("\n".join(f"{m:X}" for m in masks) + "\n")


class Replayer:
    def __init__(self, exe: Path, rom: Path, work: Path, widescreen: bool,
                 settle: int, confirm: int, snapshot: Path | None = None):
        self.exe, self.rom, self.work = exe, rom, work
        self.widescreen = widescreen
        self.settle = settle
        self.confirm = confirm
        self.snapshot = snapshot
        self.replays = 0

    def run(self, masks: list[int]) -> tuple[bytes, str]:
        inputs = self.work / "candidate_inputs.txt"
        write_inputs(inputs, masks)
        wram_out = self.work / "candidate_wram.bin"
        env = os.environ.copy()
        env.pop("DKC1_SCRIPT", None)
        env["DKC1_WIDESCREEN"] = "1" if self.widescreen else "0"
        env["SNESRECOMP_INPUT_PLAY"] = str(inputs)
        env["DKC1_WRAM_OUTPUT"] = str(wram_out)
        if self.snapshot is not None:
            # Replay from a mid-session anchor (flight-recorder bundles)
            # instead of power-on: the recording is the anchor's suffix.
            env["DKC1_SAVESTATE_INPUT"] = str(self.snapshot)
        frames = len(masks) + self.settle
        result = subprocess.run(
            [str(self.exe), str(self.rom), str(frames)],
            cwd=str(self.work), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.replays += 1
        if result.returncode != 0:
            raise RuntimeError(
                f"replay failed rc={result.returncode}: "
                f"{result.stderr[-400:]}")
        wram = wram_out.read_bytes()
        return wram, hashlib.sha256(wram).hexdigest()

    def check(self, masks: list[int], predicate) -> bool:
        """True iff predicate holds, confirmed self-consistently."""
        outcomes = []
        for _ in range(self.confirm):
            wram, digest = self.run(masks)
            outcomes.append((predicate(wram), digest))
        if len(set(outcomes)) != 1:
            raise RuntimeError(
                "nondeterministic candidate: replays disagreed "
                f"({outcomes}) — aborting, evidence would be unstable")
        return outcomes[0][0]


def make_predicate(spec: dict):
    address = int(str(spec["addr"]), 0)
    width = int(spec.get("width", 2))
    value = int(str(spec["value"]), 0)
    op = spec.get("op", "==")

    def predicate(wram: bytes) -> bool:
        actual = int.from_bytes(wram[address:address + width], "little")
        return {
            "==": actual == value, "!=": actual != value,
            ">=": actual >= value, "<=": actual <= value,
        }[op]
    return predicate


def transition_points(masks: list[int]) -> set[int]:
    return {i for i in range(1, len(masks)) if masks[i] != masks[i - 1]}


def ddmin(masks: list[int], check, preserve_transitions: bool,
          log) -> list[int]:
    granularity = 2
    current = masks
    while len(current) >= 2:
        chunk = max(1, len(current) // granularity)
        removed_any = False
        start = 0
        while start < len(current):
            end = min(start + chunk, len(current))
            if preserve_transitions:
                edges = transition_points(current)
                if any(start < e <= end for e in edges):
                    start = end
                    continue
            candidate = current[:start] + current[end:]
            if candidate and check(candidate):
                log(f"  removed frames [{start},{end}) -> "
                    f"{len(candidate)} frames")
                current = candidate
                removed_any = True
            else:
                start = end
        if not removed_any:
            if granularity >= len(current):
                break
            granularity = min(len(current), granularity * 2)
    return current


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path)
    parser.add_argument("--exe", type=Path,
                        default=Path("build/dkc1_snesrecomp_headless.exe"))
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--predicate", required=True,
                        help='JSON, e.g. {"addr":"0x057B","op":">=",'
                             '"value":"0x0003"}')
    parser.add_argument("--snapshot-input", type=Path,
                        help="native snapshot to load before replaying "
                             "(e.g. a flight-recorder bundle anchor)")
    parser.add_argument("--settle", type=int, default=60)
    parser.add_argument("--confirm", type=int, default=3)
    parser.add_argument("--widescreen", action="store_true", default=True)
    parser.add_argument("--preserve-transitions", action="store_true")
    parser.add_argument("--work", type=Path, default=Path("build/minimize"))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    args.work.mkdir(parents=True, exist_ok=True)
    masks = load_inputs(args.inputs)
    predicate = make_predicate(json.loads(args.predicate))
    replayer = Replayer(args.exe.resolve(), args.rom.resolve(),
                        args.work.resolve(), args.widescreen,
                        args.settle, args.confirm,
                        snapshot=args.snapshot_input.resolve()
                        if args.snapshot_input else None)

    print(f"baseline: {len(masks)} frames, confirming x{args.confirm}...")
    if not replayer.check(masks, predicate):
        print("predicate does not hold on the full recording; nothing to "
              "minimize", file=sys.stderr)
        return 2

    minimal = ddmin(masks, lambda m: replayer.check(m, predicate),
                    args.preserve_transitions, print)
    out = args.out or args.inputs.with_suffix(".minimal.txt")
    write_inputs(out, minimal)
    print(f"minimal: {len(minimal)} frames ({replayer.replays} replays) "
          f"-> {out}")
    print("status: 1-minimal-under-policy" if len(minimal) < len(masks)
          else "status: could-not-shrink")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
