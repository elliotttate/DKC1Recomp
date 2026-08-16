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
import json
import os
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PC24_RE = re.compile(r"^0x[0-9A-Fa-f]{6}$")


def first_operation(script: Path) -> str:
    return next(
        (line.strip() for line in script.read_text(encoding="utf-8").splitlines()
         if line.strip() and not line.strip().startswith("#")), "")


def purge_profiles(directory: Path) -> int:
    """Remove published or interrupted outputs owned by this corpus."""
    removed = 0
    for pattern in ("*.profile.jsonl", ".*.profile.jsonl.tmp"):
        for path in directory.glob(pattern):
            if path.is_file():
                path.unlink()
                removed += 1
    return removed


def validate_profile(path: Path) -> int:
    """Require a nonempty, wholly parseable profiler artifact."""
    if not path.is_file():
        raise ValueError("profile was not created")
    rows = 0
    for line_number, line in enumerate(
            path.read_text(encoding="utf-8", errors="strict").splitlines(), 1):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ValueError(f"invalid JSON at line {line_number}") from exc
        if not isinstance(row, dict) or not PC24_RE.fullmatch(
                str(row.get("pc24", ""))):
            raise ValueError(f"invalid pc24 at line {line_number}")
        calls = row.get("calls")
        if isinstance(calls, bool) or not isinstance(calls, int) or calls <= 0:
            raise ValueError(f"invalid calls at line {line_number}")
        rows += 1
    if rows == 0:
        raise ValueError("profile is empty")
    return rows


def build_corpus(args: argparse.Namespace) -> int:
    args.out.mkdir(parents=True, exist_ok=True)
    removed = purge_profiles(args.out)
    if removed:
        print(f"removed {removed} stale profile artifact(s)")

    if not args.rom.is_file():
        print(f"missing ROM: {args.rom}")
        return 2
    if not args.exe.is_file():
        print(f"missing trace host: {args.exe}")
        return 2
    if not args.routes.is_dir():
        print(f"missing routes directory: {args.routes}")
        return 2

    scripts = sorted(args.routes.glob("*.dks"))
    if not scripts:
        print(f"no .dks routes in {args.routes}")
        return 2
    standalone = [script for script in scripts
                  if not first_operation(script).startswith("state_load")]
    if not standalone:
        print(f"no standalone .dks routes in {args.routes}")
        return 2

    built = 0
    failures: list[str] = []
    staged: list[tuple[Path, Path]] = []
    for script in scripts:
        if first_operation(script).startswith("state_load"):
            print(f"{script.name}: skipped (dependent leg)")
            continue
        profile = args.out / f"{script.stem}.profile.jsonl"
        temporary = args.out / f".{script.stem}.profile.jsonl.tmp"
        temporary.unlink(missing_ok=True)
        env = os.environ.copy()
        env.pop("SNESRECOMP_INPUT_PLAY", None)
        env["DKC1_WIDESCREEN"] = "1"
        env["DKC1_SCRIPT"] = str(script.resolve())
        env["SNESRECOMP_FUNC_PROFILE"] = str(temporary.resolve())
        env["SNESRECOMP_PROFILE_CONTEXT_ADDR"] = "0032"
        try:
            result = subprocess.run(
                [str(args.exe.resolve()), str(args.rom.resolve()),
                 str(args.frames)],
                cwd=str(args.out), env=env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        except OSError as exc:
            temporary.unlink(missing_ok=True)
            failures.append(f"{script.name}: could not run trace host: {exc}")
            print(f"{script.name}: FAILED (could not run trace host: {exc})")
            continue
        functions = 0
        error = None
        if result.returncode != 0:
            error = f"runner exited {result.returncode}"
        else:
            try:
                functions = validate_profile(temporary)
            except (OSError, UnicodeError, ValueError) as exc:
                error = str(exc)
        if error is not None:
            temporary.unlink(missing_ok=True)
            failures.append(f"{script.name}: {error}")
            print(f"{script.name}: FAILED ({error})")
            continue
        staged.append((temporary, profile))
        print(f"{script.name}: rc=0 functions={functions}")
        built += 1

    if failures:
        # A partial corpus is as misleading as a stale one. Publish nothing
        # unless every standalone route produced a valid current profile.
        purge_profiles(args.out)
        print("corpus FAILED; no profiles published:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    for temporary, profile in staged:
        temporary.replace(profile)
    print(f"corpus: {built}/{len(standalone)} profiles in {args.out}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--exe", type=Path,
                        default=REPO / "build/dkc1_headless_trace.exe")
    parser.add_argument("--routes", type=Path, default=REPO / "recipes")
    parser.add_argument("--out", type=Path, default=REPO / "build/profiles")
    parser.add_argument("--frames", type=int, default=20000)
    args = parser.parse_args()

    return build_corpus(args)


if __name__ == "__main__":
    raise SystemExit(main())
