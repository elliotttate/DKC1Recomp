#!/usr/bin/env python3
"""Closure-contract regression runner for DKC1Recomp routes.

A contract is JSON describing a deterministic route and the evidence it must
produce (modeled on the SuperZSNES closure contracts):

{
  "name": "jungle-entry",
  "script": "recipes/route_jungle.dks",
  "frames": 16000,
  "widescreen": true,
  "repeats": 3,
  "checkpoints": {
    "level_entry": {
      "expect": [
        {"addr": "0x003E", "op": "==", "value": "0x0016"},
        {"addr": "0x1B25", "op": ">=", "value": "0x0100"}
      ]
    },
    "camera_0200": {"expect": [
        {"addr": "0x088B", "op": ">=", "value": "0x0200"}]}
  }
}

Gates enforced:
  - the run must complete (script waits may not time out);
  - every declared checkpoint must be recorded;
  - every expect predicate is evaluated against the checkpoint's full WRAM
    dump (raw bytes, not the live process);
  - `repeats` runs must produce byte-identical WRAM/VRAM hashes at every
    checkpoint (the 3x byte-identical rule). Any disagreement fails the
    contract as nondeterministic — never averaged away.
  - contracts must not contain WRAM writes; there is deliberately no
    mechanism for them here.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

WRAM_SIZE = 0x20000


def parse_int(value) -> int:
    if isinstance(value, int):
        return value
    return int(str(value), 0)


def evaluate(expect: dict, wram: bytes) -> tuple[bool, str]:
    address = parse_int(expect["addr"])
    width = parse_int(expect.get("width", 2))
    value = parse_int(expect["value"])
    op = expect.get("op", "==")
    actual = int.from_bytes(wram[address:address + width], "little")
    mask = parse_int(expect.get("mask", (1 << (8 * width)) - 1))
    actual &= mask
    passed = {
        "==": actual == value, "!=": actual != value,
        ">=": actual >= value, "<=": actual <= value,
        ">": actual > value, "<": actual < value,
        "&": (actual & value) != 0, "!&": (actual & value) == 0,
    }.get(op)
    if passed is None:
        return False, f"unknown operator {op!r}"
    detail = (f"[{address:#06x}]&{mask:#x} = {actual:#06x} {op} "
              f"{value:#06x} -> {'ok' if passed else 'FAIL'}")
    return passed, detail


def run_once(contract: dict, exe: Path, rom: Path, session: Path,
             base: Path) -> dict[str, dict]:
    if session.exists():
        shutil.rmtree(session)
    session.mkdir(parents=True)
    env = os.environ.copy()
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env["DKC1_WIDESCREEN"] = "1" if contract.get("widescreen", True) else "0"
    env["DKC1_SCRIPT"] = str((base / contract["script"]).resolve())
    env["DKC1_SESSION_DIR"] = str(session)
    result = subprocess.run(
        [str(exe), str(rom), str(contract["frames"])],
        cwd=str(session), env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"route failed rc={result.returncode}: {result.stderr[-800:]}")
    checkpoints = {}
    index = session / "checkpoints.jsonl"
    if index.exists():
        for line in index.read_text().splitlines():
            record = json.loads(line)
            checkpoints[record["name"]] = record
    return checkpoints


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("contracts", nargs="+", type=Path)
    parser.add_argument("--exe", type=Path,
                        default=Path("build/dkc1_snesrecomp_headless.exe"))
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--work", type=Path, default=Path("build/regression"))
    args = parser.parse_args()

    exe = args.exe.resolve()
    rom = args.rom.resolve()
    overall_failed = 0

    for contract_path in args.contracts:
        contract = json.loads(contract_path.read_text())
        if "write_wram" in json.dumps(contract):
            print(f"{contract['name']}: REJECTED (contains write_wram)")
            overall_failed += 1
            continue
        base = contract_path.resolve().parent
        repeats = int(contract.get("repeats", 3))
        name = contract["name"]
        print(f"=== {name} ({repeats} repeats) ===")
        failures: list[str] = []
        reference: dict[str, dict] | None = None
        reference_session: Path | None = None

        for attempt in range(repeats):
            session = (args.work / name / f"repeat-{attempt}").resolve()
            try:
                checkpoints = run_once(contract, exe, rom, session, base)
            except RuntimeError as error:
                failures.append(f"repeat {attempt}: {error}")
                break
            for cp_name in contract.get("checkpoints", {}):
                if cp_name not in checkpoints:
                    failures.append(
                        f"repeat {attempt}: checkpoint {cp_name} missing")
            if failures:
                break
            if reference is None:
                reference = checkpoints
                reference_session = session
                # evaluate expects once, against the reference dumps
                for cp_name, spec in contract.get("checkpoints", {}).items():
                    dump = session / f"{cp_name}.wram.bin"
                    wram = dump.read_bytes()
                    if len(wram) != WRAM_SIZE:
                        failures.append(f"{cp_name}: short WRAM dump")
                        continue
                    for expect in spec.get("expect", []):
                        ok, detail = evaluate(expect, wram)
                        print(f"  {cp_name}: {detail}")
                        if not ok:
                            failures.append(f"{cp_name}: {detail}")
            else:
                for cp_name, record in reference.items():
                    other = checkpoints.get(cp_name, {})
                    for key in ("wram", "vram", "oam_shadow"):
                        if record.get(key) != other.get(key):
                            failures.append(
                                f"repeat {attempt}: {cp_name}.{key} differs "
                                f"from repeat 0 — nondeterministic route")
        if failures:
            overall_failed += 1
            for failure in failures:
                print(f"  FAIL: {failure}")
        else:
            print(f"  PASS: {len(reference or {})} checkpoints x "
                  f"{repeats} byte-identical repeats")
    return 1 if overall_failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
