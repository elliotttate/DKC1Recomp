#!/usr/bin/env python3
"""Promote a flight-recorder bundle into a local regression asset.

A playtester capture becomes a first-class route + contract ONLY after
it earns it:
  1. manifest schema + supported-ROM sha + file hashes verified;
  2. the replay (anchor snapshot + recorded inputs) reproduces N times
     with byte-identical end-of-run WRAM;
  3. by default the replay must also match the bundle's own
     final.wram.bin hash (--allow-capture-drift downgrades that to a
     recorded warning, e.g. across build-identity changes).

On pass it emits:
  recipes/promoted/<name>/   anchor.snapshot + inputs.txt + replay.dks
                             (state_load + run-length MASK lines +
                             final checkpoint)
  contracts/promoted/<name>.json   scene-identity checkpoint expects,
                             zero budgets, provenance block

Both directories are LOCAL-ONLY (gitignored): snapshots are never
committed, and replay.dks embeds an absolute anchor path.

usage: python tools/promote_bundle.py BUNDLE --rom R [--name jungle-x]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPO = TOOLS.parent
SUPPORTED_ROM_SHA = ("fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6"
                     "cf9e1db30d5a36a469f74d15")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fail(message: str) -> None:
    sys.exit(f"promotion REFUSED: {message}")


def validate_name(name: str) -> str:
    """Return a path-safe local promotion slug or refuse it."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,95}", name):
        fail("--name must be a 1-96 character slug containing only "
             "letters, numbers, underscores, and hyphens")
    return name


def verify_manifest_files(bundle: Path, files: object) -> dict[str, str]:
    """Verify every declared artifact and all promotion prerequisites."""
    if not isinstance(files, dict):
        fail("manifest files must be an object")
    required = {"anchor.snapshot", "inputs.txt", "final.wram.bin"}
    missing = sorted(required - set(files))
    if missing:
        fail("manifest omits required file hash(es): " + ", ".join(missing))
    bundle_root = bundle.resolve()
    verified: dict[str, str] = {}
    for raw_name, raw_digest in files.items():
        if not isinstance(raw_name, str) or not isinstance(raw_digest, str):
            fail("manifest file names and hashes must be strings")
        relative = Path(raw_name)
        if relative.is_absolute() or ".." in relative.parts:
            fail(f"unsafe manifest file path {raw_name!r}")
        digest = raw_digest.lower()
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            fail(f"invalid sha256 for {raw_name}")
        path = (bundle_root / relative).resolve()
        try:
            path.relative_to(bundle_root)
        except ValueError:
            fail(f"manifest file escapes bundle root: {raw_name!r}")
        if not path.is_file():
            fail(f"missing manifested file {path}")
        if sha256(path) != digest:
            fail(f"{raw_name} hash differs from manifest (corrupt bundle)")
        verified[raw_name] = digest
    return verified


def masks_to_dks(inputs: list[str], anchor_abs: Path) -> str:
    lines = [f"state_load {anchor_abs}"]
    run_mask, run_len = None, 0
    for raw in inputs + [None]:
        mask = raw.strip().lstrip("0").upper() or "0" \
            if raw is not None else None
        if mask == run_mask:
            run_len += 1
            continue
        if run_mask is not None:
            lines.append(run_mask if run_len == 1
                         else f"{run_mask} * {run_len}")
        run_mask, run_len = mask, 1
    lines.append("checkpoint promoted_end")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--exe", type=Path,
                        default=REPO / "build/dkc1_headless_tools.exe")
    parser.add_argument("--name")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--no-widescreen", action="store_true")
    parser.add_argument("--allow-capture-drift", action="store_true")
    parser.add_argument("--work", type=Path,
                        default=REPO / "build" / "promote")
    args = parser.parse_args()

    if args.repeats < 1:
        fail("--repeats must be at least 1")

    manifest_path = args.bundle / "manifest.json"
    anchor = args.bundle / "anchor.snapshot"
    inputs_path = args.bundle / "inputs.txt"
    for required in (manifest_path, anchor, inputs_path):
        if not required.exists():
            fail(f"missing {required}")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != "dkc1.flight-recorder.v1":
        fail(f"unknown schema {manifest.get('schema')!r}")
    if manifest.get("rom_sha256") != SUPPORTED_ROM_SHA:
        fail("bundle was captured against an unsupported ROM")
    if sha256(args.rom) != SUPPORTED_ROM_SHA:
        fail(f"{args.rom} is not the supported ROM")
    files = verify_manifest_files(args.bundle, manifest.get("files"))

    inputs = inputs_path.read_text().splitlines()
    replay_frames = manifest.get("replay_frames", len(inputs))
    if abs(replay_frames - len(inputs)) > 1:
        fail(f"inputs.txt has {len(inputs)} frames, manifest says "
             f"{replay_frames}")

    scene = manifest.get("scene", {})
    default_name = (f"promoted-e{scene.get('entrance', 0):02x}"
                    f"-f{manifest.get('current_frame', 0)}")
    name = validate_name(args.name or default_name)

    # reproduce gate
    args.work.mkdir(parents=True, exist_ok=True)
    hashes = []
    for attempt in range(args.repeats):
        wram_out = args.work / f"{name}.rep{attempt}.wram"
        wram_out.unlink(missing_ok=True)
        env = os.environ.copy()
        env.pop("DKC1_SCRIPT", None)
        env["DKC1_SAVESTATE_INPUT"] = str(anchor.resolve())
        env["SNESRECOMP_INPUT_PLAY"] = str(inputs_path.resolve())
        env["DKC1_WRAM_OUTPUT"] = str(wram_out.resolve())
        env["DKC1_WIDESCREEN"] = "0" if args.no_widescreen else "1"
        result = subprocess.run(
            [str(args.exe.resolve()), str(args.rom.resolve()),
             str(len(inputs))],
            cwd=str(args.work), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            fail(f"replay {attempt + 1} exited rc={result.returncode}: "
                 f"{result.stderr[-300:]}")
        if not wram_out.exists():
            fail(f"replay {attempt + 1} produced no WRAM dump "
                 "(host lacks DKC1_WRAM_OUTPUT?)")
        hashes.append(sha256(wram_out))
    if len(set(hashes)) != 1:
        fail(f"replays are NOT byte-identical across {args.repeats} "
             f"runs: {hashes}")
    drift = None
    captured = files["final.wram.bin"]
    if captured and hashes[0] != captured:
        if not args.allow_capture_drift:
            fail("replay end WRAM differs from the bundle's own "
                 "final.wram.bin — the capture does not reproduce on "
                 "this build. Re-capture, or pass --allow-capture-drift "
                 "to promote the (still deterministic) replay anyway.")
        drift = {"bundle_final_wram": captured, "replay_wram": hashes[0]}

    # emit assets
    asset_dir = REPO / "recipes" / "promoted" / name
    asset_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(anchor, asset_dir / "anchor.snapshot")
    shutil.copy2(inputs_path, asset_dir / "inputs.txt")
    shutil.copy2(manifest_path, asset_dir / "bundle-manifest.json")
    replay = asset_dir / "replay.dks"
    replay.write_text(masks_to_dks(
        inputs, (asset_dir / "anchor.snapshot").resolve()))

    contract_dir = REPO / "contracts" / "promoted"
    contract_dir.mkdir(parents=True, exist_ok=True)
    contract = {
        "name": name,
        "script": f"../../recipes/promoted/{name}/replay.dks",
        "frames": len(inputs) + 240,
        "widescreen": not args.no_widescreen,
        "repeats": args.repeats,
        "budgets": {"cache_oob": 0, "retrodict": 0},
        "checkpoints": {
            "promoted_end": {"expect": [
                {"addr": "0x003E", "op": "==",
                 "value": f"0x{scene.get('entrance', 0):04X}"},
                {"addr": "0x0032", "op": "==",
                 "value": f"0x{scene.get('mode', 0):04X}"},
            ]},
        },
        "provenance": {
            "bundle": str(args.bundle.resolve()),
            "captured_build": manifest.get("build"),
            "replay_wram_sha256": hashes[0],
            "capture_drift": drift,
            "local_only": "snapshots are never committed; this asset "
                          "lives outside version control",
        },
    }
    contract_path = contract_dir / f"{name}.json"
    contract_path.write_text(json.dumps(contract, indent=2))

    print(f"PROMOTED {name}")
    print(f"  {args.repeats}x byte-identical replay "
          f"(wram {hashes[0][:16]}...)"
          + (" with capture drift recorded" if drift else
             ", matches the bundle's own capture"))
    print(f"  route:    {replay}")
    print(f"  contract: {contract_path}")
    print(f"  run it:   python tools/run_regression.py --rom {args.rom} "
          f"{contract_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
