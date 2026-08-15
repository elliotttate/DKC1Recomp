#!/usr/bin/env python3
"""Validate, compile, and optionally run a deterministic DKC1 route recipe."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


SCHEMA = "dkc1.route.v1"
OPS = {"eq": "==", "ne": "!=", "gt": ">", "ge": ">=",
       "lt": "<", "le": "<=", "any_set": "&", "all_clear": "!&"}
SAFE_NAME = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$")


def _hex(value: object, digits: int, field: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(
            rf"[0-9A-Fa-f]{{1,{digits}}}", value):
        raise ValueError(f"{field} must be 1..{digits} hexadecimal digits")
    return f"{int(value, 16):0{digits}X}"


def compile_recipe(recipe: dict) -> tuple[str, int, dict]:
    if not isinstance(recipe, dict) or recipe.get("schema") != SCHEMA:
        raise ValueError(f"recipe schema must be {SCHEMA}")
    name = recipe.get("name")
    steps = recipe.get("steps")
    if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
        raise ValueError("recipe name must be a safe 1..64 character name")
    if not isinstance(steps, list) or not steps:
        raise ValueError("steps must be a non-empty array")

    lines = [f"# compiled from {name} ({SCHEMA})"]
    maximum_frames = 1  # permits trailing boundary-only checkpoints
    checkpoint_names: set[str] = set()
    for index, step in enumerate(steps):
        field = f"steps[{index}]"
        if not isinstance(step, dict):
            raise ValueError(f"{field} must be an object")
        kind = step.get("type")
        if kind == "input":
            mask = _hex(step.get("input", "0"), 6, f"{field}.input")
            frames = step.get("frames")
            if not isinstance(frames, int) or not 1 <= frames <= 1_000_000:
                raise ValueError(f"{field}.frames must be 1..1000000")
            lines.append(f"{mask} * {frames}")
            maximum_frames += frames
        elif kind == "wait_wram":
            address = _hex(step.get("address"), 5, f"{field}.address")
            width = step.get("width", 2)
            if width not in (1, 2, 4):
                raise ValueError(f"{field}.width must be 1, 2, or 4")
            op = step.get("op")
            if op not in OPS:
                raise ValueError(f"{field}.op must be one of {sorted(OPS)}")
            value = _hex(step.get("value"), 8, f"{field}.value")
            timeout = step.get("timeout", 3600)
            shift = step.get("shift", 0)
            signed = step.get("signed", False)
            if not isinstance(timeout, int) or not 1 <= timeout <= 1_000_000:
                raise ValueError(f"{field}.timeout must be 1..1000000")
            if not isinstance(shift, int) or not 0 <= shift < width * 8:
                raise ValueError(f"{field}.shift is outside the value width")
            if not isinstance(signed, bool):
                raise ValueError(f"{field}.signed must be boolean")
            address_value = int(address, 16)
            width_mask = (1 << (width * 8)) - 1
            mask_value = width_mask
            if "mask" in step:
                mask_value = int(_hex(step["mask"], 8,
                                      f"{field}.mask"), 16)
            value_value = int(value, 16)
            if (address_value + width > 0x20000 or
                    mask_value > width_mask or
                    value_value > (mask_value >> shift) or
                    (signed and mask_value == 0)):
                raise ValueError(
                    f"{field} value/mask/shift exceeds its WRAM width")
            prefix = "wait"
            if "input" in step:
                prefix = "hold " + _hex(step["input"], 6, f"{field}.input")
            options = [f"width {width}"]
            if "mask" in step:
                options.append(f"mask {mask_value:08X}")
            if shift:
                options.append(f"shift {shift}")
            if signed:
                options.append("signed")
            options.append(f"timeout {timeout}")
            lines.append(
                f"{prefix} {address} {OPS[op]} {value} " + " ".join(options))
            maximum_frames += timeout
        elif kind == "checkpoint":
            checkpoint = step.get("name")
            if not isinstance(checkpoint, str) or not SAFE_NAME.fullmatch(
                    checkpoint):
                raise ValueError(f"{field}.name is not a safe checkpoint name")
            if checkpoint in checkpoint_names:
                raise ValueError(f"duplicate checkpoint name {checkpoint!r}")
            checkpoint_names.add(checkpoint)
            lines.append(f"checkpoint {checkpoint}")
        else:
            raise ValueError(f"{field}.type is unsupported")
    normalized = {"schema": SCHEMA, "name": name,
                  "maximum_frames": maximum_frames,
                  "checkpoint_count": len(checkpoint_names),
                  "step_count": len(steps)}
    return "\n".join(lines) + "\n", maximum_frames, normalized


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_recipe(recipe_path: Path, rom: Path, runner: Path,
               session_dir: Path, snapshot_input: Path | None,
               widescreen: bool) -> tuple[int, dict]:
    recipe_bytes = recipe_path.read_bytes()
    recipe = json.loads(recipe_bytes)
    script, maximum_frames, normalized = compile_recipe(recipe)
    if session_dir.exists() and any(session_dir.iterdir()):
        raise ValueError(f"session directory is not empty: {session_dir}")
    session_dir.mkdir(parents=True, exist_ok=True)
    script_path = session_dir / "route.script"
    stdout_path = session_dir / "stdout.txt"
    stderr_path = session_dir / "stderr.txt"
    script_path.write_text(script, encoding="utf-8", newline="\n")

    env = os.environ.copy()
    env["DKC1_SCRIPT"] = str(script_path)
    env["DKC1_SESSION_DIR"] = str(session_dir / "checkpoints")
    env["DKC1_WIDESCREEN"] = "1" if widescreen else "0"
    if snapshot_input:
        env["DKC1_SAVESTATE_INPUT"] = str(snapshot_input)
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        completed = subprocess.run(
            [str(runner), str(rom), str(maximum_frames)], env=env,
            stdout=stdout, stderr=stderr, check=False)

    manifest = {
        **normalized,
        "exit_code": completed.returncode,
        "widescreen": widescreen,
        "inputs": {
            "recipe": {"name": recipe_path.name,
                       "sha256": hashlib.sha256(recipe_bytes).hexdigest()},
            "rom": {"name": rom.name, "sha256": _sha256(rom)},
            "runner": {"name": runner.name, "sha256": _sha256(runner)},
            "snapshot": (None if snapshot_input is None else
                         {"name": snapshot_input.name,
                          "sha256": _sha256(snapshot_input)}),
        },
        "outputs": {"script": script_path.name,
                    "stdout": stdout_path.name, "stderr": stderr_path.name},
    }
    (session_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return completed.returncode, manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recipe", type=Path)
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--runner", type=Path,
                        default=Path("build/dkc1_snesrecomp_headless.exe"))
    parser.add_argument("--session-dir", type=Path)
    parser.add_argument("--snapshot-input", type=Path)
    parser.add_argument("--native", action="store_true",
                        help="run the native-width presentation")
    parser.add_argument("--script-out", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
        script, _, normalized = compile_recipe(recipe)
        if args.script_out:
            args.script_out.write_text(script, encoding="utf-8", newline="\n")
        if args.validate_only:
            print(json.dumps({**normalized, "validated": True}, indent=2))
            return 0
        if not args.rom or not args.session_dir:
            raise ValueError("--rom and --session-dir are required to run")
        code, manifest = run_recipe(
            args.recipe, args.rom, args.runner, args.session_dir,
            args.snapshot_input, not args.native)
        print(json.dumps(manifest, indent=2))
        return code
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"run_route_recipe: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
