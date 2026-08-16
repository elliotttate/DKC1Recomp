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
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from analyze_ws_trace import analyze as analyze_ws_trace
from analyze_ws_trace import load_trace

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


RUN_HASH_KEYS = ("frame_sha256", "wram_sha256", "vram_sha256",
                 "cgram_sha256", "oam_sha256", "oam_source_sha256")


def parse_run_hashes(stdout: str) -> dict:
    """End-of-run hashes from the headless host, including the rolling
    framebuffer hash — the renderer-buffer leg of the 3x-identical gate
    (raw state alone can agree while presentation diverges)."""
    hashes = {}
    for line in stdout.splitlines():
        for key in RUN_HASH_KEYS:
            prefix = key + "="
            if line.startswith(prefix):
                hashes[key] = line[len(prefix):].strip()
        if "audio_fnv1a=" in line:
            hashes["audio_fnv1a"] = \
                line.split("audio_fnv1a=", 1)[1].split()[0]
    return hashes


def run_once(contract: dict, exe: Path, rom: Path, session: Path,
             base: Path, seed_state: Path | None = None,
             seed_name: str | None = None) -> dict:
    if session.exists():
        shutil.rmtree(session)
    session.mkdir(parents=True)
    if seed_state is not None:
        shutil.copyfile(seed_state, session / (seed_name or seed_state.name))
    env = os.environ.copy()
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env["DKC1_WIDESCREEN"] = "1" if contract.get("widescreen", True) else "0"
    env["DKC1_SCRIPT"] = str((base / contract["script"]).resolve())
    env["DKC1_SESSION_DIR"] = str(session)
    trace_spec = contract.get("trace")
    trace_path = session / "ws-trace.jsonl"
    if trace_spec is not None:
        env["DKC1_WS_TRACE"] = str(trace_path)
    # Always-on integrity taps: scene-local cache-bound events and stream
    # retrodiction mismatches, gated by the contract's "budgets".
    cache_log = session / "cache-events.jsonl"
    retrodict_log = session / "retrodict-events.jsonl"
    env["SNESRECOMP_WS_CACHE_LOG"] = str(cache_log)
    env["SNESRECOMP_WS_RETRODICT"] = str(retrodict_log)
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
    def count_events(path: Path) -> int:
        if not path.exists():
            return 0
        return sum(1 for line in path.read_text(
            errors="replace").splitlines() if line.strip())

    evidence = {"checkpoints": checkpoints,
                "run_hashes": parse_run_hashes(result.stdout),
                "integrity_events": {
                    "cache_oob": count_events(cache_log),
                    "retrodict": count_events(retrodict_log)}}
    if trace_spec is not None:
        frames = load_trace(trace_path)
        if trace_spec.get("require_cgram", True) and any(
                "cgram" not in frame["hash"] for frame in frames):
            raise RuntimeError("widescreen trace lacks CGRAM hashes")
        summary = analyze_ws_trace(
            frames, extra=int(trace_spec.get("extra", 43)))
        (session / "ws-trace-summary.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        evidence["trace_summary"] = summary
        evidence["trace_sha256"] = hashlib.sha256(
            trace_path.read_bytes()).hexdigest()
    return evidence


def evaluate_trace(trace_spec: dict, summary: dict) -> list[str]:
    """Return contract failures for one analyzed widescreen trace."""
    failures: list[str] = []
    for key in trace_spec.get("expect_empty", []):
        value = summary.get(key)
        if value != []:
            failures.append(
                f"trace.{key}: expected empty list, got "
                f"{len(value) if isinstance(value, list) else value!r}")
    decisions = summary.get("decision_counts", {})
    for key, minimum in trace_spec.get(
            "minimum_decision_counts", {}).items():
        actual = int(decisions.get(key, 0))
        if actual < int(minimum):
            failures.append(
                f"trace.decision_counts.{key}: {actual} < {minimum}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("contracts", nargs="+", type=Path)
    parser.add_argument("--exe", type=Path,
                        default=Path("build/dkc1_snesrecomp_headless.exe"))
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--work", type=Path, default=Path("build/regression"))
    parser.add_argument("--json-out", type=Path,
                        help="structured results (consumed by the "
                             "regression dashboard)")
    args = parser.parse_args()

    exe = args.exe.resolve()
    rom = args.rom.resolve()
    overall_failed = 0
    results: dict[str, dict] = {}

    for contract_path in args.contracts:
        contract = json.loads(contract_path.read_text())
        if "write_wram" in json.dumps(contract):
            print(f"{contract['name']}: REJECTED (contains write_wram)")
            overall_failed += 1
            continue
        base = contract_path.resolve().parent
        name = contract["name"]

        failures, entry_reference = execute_phase(
            f"{name}/entry", contract, exe, rom, args.work / name, base)

        # Fresh entry exercises initialization; a quickload leg exercises
        # reconstruction from a mid-level state WITHOUT retained history.
        # The state is produced by the entry route itself (state_save in
        # the recipe), never committed to the repository.
        quickload = contract.get("quickload")
        if quickload is not None and not failures:
            state_name = quickload["state_from"]
            seed = (entry_reference / state_name) if entry_reference else None
            if seed is None or not seed.exists():
                failures.append(
                    f"quickload: entry run did not produce {state_name} "
                    "(add 'state_save' to the entry recipe)")
            else:
                phase_contract = dict(quickload)
                phase_contract.setdefault(
                    "widescreen", contract.get("widescreen", True))
                phase_contract.setdefault(
                    "repeats", contract.get("repeats", 3))
                quick_failures, _ = execute_phase(
                    f"{name}/quickload", phase_contract, exe, rom,
                    args.work / name / "quickload", base,
                    seed_state=seed, seed_name=state_name)
                failures.extend(quick_failures)

        if failures:
            overall_failed += 1
            for failure in failures:
                print(f"  FAIL: {failure}")
        else:
            legs = "entry+quickload" if quickload is not None else "entry"
            print(f"  PASS ({legs})")
        results[name] = {
            "passed": not failures,
            "failures": failures,
            "legs": ["entry"] + (["quickload"] if quickload else []),
            "contract": str(contract_path),
            "evidence": str((args.work / name).resolve()),
        }
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(
            {"schema": "dkc1.regression-results.v1", "results": results},
            indent=1))
    return 1 if overall_failed else 0


def execute_phase(label: str, contract: dict, exe: Path, rom: Path,
                  work: Path, base: Path, seed_state: Path | None = None,
                  seed_name: str | None = None
                  ) -> tuple[list[str], Path | None]:
    repeats = int(contract.get("repeats", 3))
    print(f"=== {label} ({repeats} repeats) ===")
    failures: list[str] = []
    reference: dict[str, dict] | None = None
    reference_trace_sha256: str | None = None
    reference_run_hashes: dict | None = None
    reference_integrity: dict | None = None
    reference_session: Path | None = None

    for attempt in range(repeats):
        session = (work / f"repeat-{attempt}").resolve()
        try:
            evidence = run_once(contract, exe, rom, session, base,
                                seed_state=seed_state, seed_name=seed_name)
        except RuntimeError as error:
            failures.append(f"{label} repeat {attempt}: {error}")
            break
        checkpoints = evidence["checkpoints"]
        for cp_name in contract.get("checkpoints", {}):
            if cp_name not in checkpoints:
                failures.append(
                    f"{label} repeat {attempt}: checkpoint {cp_name} missing")
        if failures:
            break
        if reference is None:
            reference = checkpoints
            reference_trace_sha256 = evidence.get("trace_sha256")
            reference_run_hashes = evidence.get("run_hashes")
            reference_integrity = evidence.get("integrity_events")
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
            trace_spec = contract.get("trace")
            if trace_spec is not None:
                for failure in evaluate_trace(
                        trace_spec, evidence["trace_summary"]):
                    failures.append(failure)
            # Integrity budgets are RATCHETS: the recorded maximum for a
            # known issue, tightened to zero when it is fixed. Exceeding
            # one means a regression, never a new baseline.
            budgets = contract.get("budgets", {})
            events = evidence.get("integrity_events", {})
            for key, actual in events.items():
                allowed = int(budgets.get(key, 0))
                detail = f"integrity.{key}: {actual} (budget {allowed})"
                print(f"  {detail}")
                if actual > allowed:
                    failures.append(f"{label}: {detail} EXCEEDED")
        else:
            for cp_name, record in reference.items():
                other = checkpoints.get(cp_name, {})
                for key in ("wram", "vram", "oam_shadow"):
                    if record.get(key) != other.get(key):
                        failures.append(
                            f"{label} repeat {attempt}: {cp_name}.{key} "
                            "differs from repeat 0 — nondeterministic route")
            if reference_trace_sha256 != evidence.get("trace_sha256"):
                failures.append(
                    f"{label} repeat {attempt}: widescreen trace differs "
                    "from repeat 0 — nondeterministic presentation")
            if reference_run_hashes != evidence.get("run_hashes"):
                failures.append(
                    f"{label} repeat {attempt}: end-of-run hashes differ "
                    f"from repeat 0 (renderer/state buffers) — "
                    f"{reference_run_hashes} vs {evidence.get('run_hashes')}")
            if reference_integrity != evidence.get("integrity_events"):
                failures.append(
                    f"{label} repeat {attempt}: integrity event counts "
                    f"differ from repeat 0 — nondeterministic")
    if not failures:
        print(f"  {label}: {len(reference or {})} checkpoints x "
              f"{repeats} byte-identical repeats "
              f"(incl. framebuffer/audio hashes)")
    return failures, reference_session


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
