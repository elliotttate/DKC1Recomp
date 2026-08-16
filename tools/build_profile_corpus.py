#!/usr/bin/env python3
"""Build the per-route function-profile corpus (build/profiles/).

Runs every standalone recipe under the trace-hook host with
SNESRECOMP_FUNC_PROFILE armed, producing one profile per route. The
corpus powers impact.py ("which routes exercise this function?") and
profile_diff.py coverage/behavioral queries. Rebuild after recording new
routes or regenerating the recomp.
"""
from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--exe", type=Path,
                        default=REPO / "build/dkc1_headless_trace.exe")
    parser.add_argument("--routes", type=Path, default=REPO / "recipes")
    parser.add_argument("--out", type=Path, default=REPO / "build/profiles")
    parser.add_argument("--frames", type=int, default=20000)
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    built = 0
    for script in sorted(args.routes.glob("*.dks")):
        first_op = next(
            (line.strip() for line in script.read_text().splitlines()
             if line.strip() and not line.strip().startswith("#")), "")
        if first_op.startswith("state_load"):
            print(f"{script.name}: skipped (dependent leg)")
            continue
        profile = args.out / f"{script.stem}.profile.jsonl"
        env = os.environ.copy()
        env.pop("SNESRECOMP_INPUT_PLAY", None)
        env["DKC1_WIDESCREEN"] = "1"
        env["DKC1_SCRIPT"] = str(script.resolve())
        env["SNESRECOMP_FUNC_PROFILE"] = str(profile.resolve())
        env["SNESRECOMP_PROFILE_CONTEXT_ADDR"] = "0032"
        result = subprocess.run(
            [str(args.exe.resolve()), str(args.rom.resolve()),
             str(args.frames)],
            cwd=str(args.out), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        functions = sum(1 for _ in profile.open()) if profile.exists() else 0
        print(f"{script.name}: rc={result.returncode} "
              f"functions={functions}")
        built += 1 if functions else 0
    print(f"corpus: {built} profiles in {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
