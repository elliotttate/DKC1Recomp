#!/usr/bin/env python3
"""Turn a flight-recorder repro bundle into a minimal reproduction.

A bundle (F9 export) contains anchor.snapshot plus the exact per-frame
inputs from the anchor to the moment of export. Given a failure predicate
on final WRAM, this driver hands both to macro_minimize, which ddmin-
shrinks the input suffix while replaying from the anchor snapshot — the
player's "I saw a bug" report becomes the shortest input sequence that
still reproduces it, with the minimizer's own 3x-consistency soundness
gates intact.

usage:
  python tools/minimize_bundle.py <bundle-dir> --rom <rom>
      --predicate '{"addr": "0x0578", "op": "==", "value": "0x0001"}'
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--predicate", required=True)
    parser.add_argument("--exe", type=Path,
                        default=Path("build/dkc1_headless_tools.exe"))
    parser.add_argument("--confirm", type=int, default=3)
    parser.add_argument("--settle", type=int, default=60)
    parser.add_argument("--preserve-transitions", action="store_true",
                        default=True)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    anchor = args.bundle / "anchor.snapshot"
    inputs = args.bundle / "inputs.txt"
    manifest_path = args.bundle / "manifest.json"
    for required in (anchor, inputs):
        if not required.exists():
            print(f"error: {required} missing — not a flight-recorder "
                  "bundle?", file=sys.stderr)
            return 2
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        print(f"bundle: frames {manifest.get('anchor_frame')}.."
              f"{manifest.get('current_frame')}, "
              f"build {manifest.get('build', 'unknown')}")

    out = args.out or (args.bundle / "minimal_inputs.txt")
    command = [
        sys.executable,
        str(Path(__file__).resolve().parent / "macro_minimize.py"),
        str(inputs),
        "--exe", str(args.exe),
        "--rom", str(args.rom),
        "--predicate", args.predicate,
        "--snapshot-input", str(anchor),
        "--confirm", str(args.confirm),
        "--settle", str(args.settle),
        "--work", str(args.bundle / "minimize-work"),
        "--out", str(out),
    ]
    if args.preserve_transitions:
        command.append("--preserve-transitions")
    result = subprocess.run(command)
    if result.returncode == 0:
        print(f"minimal repro inputs: {out} (replay with "
              f"DKC1_SAVESTATE_INPUT={anchor} "
              f"SNESRECOMP_INPUT_PLAY={out})")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
