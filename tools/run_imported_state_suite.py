#!/usr/bin/env python3
"""Replay imported SuperZSNES states through native and wide DKC1 hosts.

The suite is intentionally evidence-oriented: every process log and OAM index
is retained, every input is hashed, all core output domains must repeat
byte-identically, and known off-rails/dispatch diagnostics fail the run even
when the process happened to return zero.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime, timezone
from typing import Iterable


HASH_FIELDS = (
    "frame_sha256",
    "wram_sha256",
    "vram_sha256",
    "cgram_sha256",
    "oam_sha256",
    "oam_source_sha256",
)
FAIL_PATTERNS = (
    "off-rails",
    "dispatch_oob",
    "unresolved-abandon",
    "unresolved dispatch",
    "lle stopped",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        relative = path.relative_to(root).as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "little"))
        digest.update(relative)
        size = path.stat().st_size
        digest.update(size.to_bytes(8, "little"))
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def _field(text: str, name: str) -> str | None:
    match = re.search(rf"(?:^|\s){re.escape(name)}=([^\s]+)", text)
    return match.group(1) if match else None


def parse_log(text: str) -> dict[str, object]:
    hashes = {name: _field(text, name) for name in HASH_FIELDS}
    stats_match = re.search(r"(?:^|\n)run_stats\s+(.+?)(?:\n|$)", text)
    stats: dict[str, int | str] = {}
    if stats_match:
        for name, value in re.findall(r"([a-z0-9_]+)=([^\s]+)",
                                      stats_match.group(1)):
            stats[name] = int(value) if value.isdigit() else value
    result = _field(text, "result")
    failures = [pattern for pattern in FAIL_PATTERNS
                if pattern in text.lower()]
    missing_hashes = [name for name, value in hashes.items() if not value]
    if missing_hashes:
        failures.append("missing hashes: " + ",".join(missing_hashes))
    if result != "completed":
        failures.append(f"result={result!r}")
    return {
        "hashes": hashes,
        "run_stats": stats,
        "result": result,
        "failures": failures,
    }


def deterministic(repeats: Iterable[dict[str, object]]) -> dict[str, bool]:
    rows = list(repeats)
    result: dict[str, bool] = {}
    for name in HASH_FIELDS:
        values = {
            str(row["parsed"]["hashes"].get(name))  # type: ignore[index]
            for row in rows
        }
        result[name] = len(values) == 1 and "None" not in values
    return result


def load_oam_index(path: Path, expected_frames: int) -> tuple[list[dict], list[str]]:
    failures: list[str] = []
    rows: list[dict] = []
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                rows.append(json.loads(line))
    except (OSError, json.JSONDecodeError) as exc:
        return [], [f"invalid OAM index: {exc}"]
    if len(rows) != expected_frames:
        failures.append(
            f"OAM frame count {len(rows)} != expected {expected_frames}")
    for index, row in enumerate(rows, 1):
        if row.get("frame") != index:
            failures.append(f"OAM sequence break at row {index}")
        for field in ("obj_range_over", "obj_time_over"):
            if not isinstance(row.get(field), bool):
                failures.append(f"OAM row {index} missing boolean {field}")
    return rows, failures


def run_repeat(runner: Path, rom: Path, state_dir: Path, output: Path,
               frames: int, wide: bool) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["DKC1_SUPERZSNES_STATE"] = str(state_dir)
    env["DKC1_WIDESCREEN"] = "1" if wide else "0"
    env["DKC1_FRAME_PPM"] = str(output / "frame.ppm")
    env["DKC1_OAM_LOG"] = str(output / "oam")
    completed = subprocess.run(
        [str(runner), str(rom), str(frames)],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    log_path = output / "run.log"
    log_path.write_text(completed.stdout, encoding="utf-8", newline="\n")
    parsed = parse_log(completed.stdout)
    oam_rows, oam_failures = load_oam_index(output / "oam.jsonl", frames)
    failures = list(parsed["failures"]) + oam_failures
    if completed.returncode:
        failures.append(f"exit={completed.returncode}")
    return {
        "exit_code": completed.returncode,
        "parsed": parsed,
        "oam": {
            "frames": len(oam_rows),
            "range_over_frames": sum(bool(row.get("obj_range_over"))
                                     for row in oam_rows),
            "time_over_frames": sum(bool(row.get("obj_time_over"))
                                    for row in oam_rows),
        },
        "failures": failures,
        "artifacts": {
            "log": str(log_path),
            "frame": str(output / "frame.ppm"),
            "oam_bin": str(output / "oam.bin"),
            "oam_index": str(output / "oam.jsonl"),
        },
    }


def parse_states(text: str) -> list[int]:
    result = [int(part.strip(), 0) for part in text.split(",")
              if part.strip()]
    if not result or len(result) != len(set(result)):
        raise argparse.ArgumentTypeError("states must be a unique CSV list")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--states-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--states", type=parse_states,
                        default=parse_states("0,1,2,3,5,11,12"))
    parser.add_argument("--frames", type=int, default=3)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.frames < 1 or args.repeats < 2:
        parser.error("frames must be >=1 and repeats must be >=2")
    for path, label in ((args.runner, "runner"), (args.rom, "ROM"),
                        (args.states_root, "states root")):
        if not path.exists():
            parser.error(f"{label} does not exist: {path}")

    args.output.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, object] = {
        "schema": "dkc1.imported-state-suite.v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "runner": str(args.runner.resolve()),
            "runner_sha256": sha256_file(args.runner),
            "rom": str(args.rom.resolve()),
            "rom_sha256": sha256_file(args.rom),
            "states_root": str(args.states_root.resolve()),
        },
        "config": {"frames": args.frames, "repeats": args.repeats,
                   "states": args.states, "widths": ["native", "wide"]},
        "states": [],
        "failures": [],
    }
    failures: list[str] = manifest["failures"]  # type: ignore[assignment]
    state_results: list[dict[str, object]] = manifest["states"]  # type: ignore[assignment]
    for state in args.states:
        state_dir = args.states_root / f"state{state}"
        if not state_dir.is_dir():
            failures.append(f"state{state}: missing bundle {state_dir}")
            continue
        state_result: dict[str, object] = {
            "state": state,
            "bundle": str(state_dir.resolve()),
            "bundle_sha256": sha256_tree(state_dir),
            "widths": {},
        }
        widths: dict[str, object] = state_result["widths"]  # type: ignore[assignment]
        for width_name, wide in (("native", False), ("wide", True)):
            repeats = []
            for repeat in range(1, args.repeats + 1):
                run_dir = (args.output / f"state{state}" / width_name /
                           f"repeat{repeat}")
                row = run_repeat(args.runner, args.rom, state_dir, run_dir,
                                 args.frames, wide)
                repeats.append(row)
                for failure in row["failures"]:  # type: ignore[index]
                    failures.append(
                        f"state{state}/{width_name}/repeat{repeat}: {failure}")
            stable = deterministic(repeats)
            for domain, passed in stable.items():
                if not passed:
                    failures.append(
                        f"state{state}/{width_name}: nondeterministic {domain}")
            widths[width_name] = {
                "repeats": repeats,
                "deterministic": stable,
                "passed": all(stable.values()) and
                          all(not row["failures"] for row in repeats),
            }
        native = widths["native"]["repeats"][0]  # type: ignore[index]
        wide = widths["wide"]["repeats"][0]  # type: ignore[index]
        state_result["wide_vs_native"] = {
            name: native["parsed"]["hashes"][name] ==  # type: ignore[index]
                  wide["parsed"]["hashes"][name]       # type: ignore[index]
            for name in HASH_FIELDS
        }
        state_results.append(state_result)

    manifest["passed"] = not failures
    manifest_path = args.output / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) +
                             "\n", encoding="utf-8", newline="\n")
    print(json.dumps({
        "manifest": str(manifest_path.resolve()),
        "states": len(state_results),
        "failures": len(failures),
        "passed": not failures,
    }, indent=2))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
