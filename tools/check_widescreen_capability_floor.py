#!/usr/bin/env python3
"""Fail closed when the whole-game fresh-entry widescreen floor regresses."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def _entrance(value: Any) -> int:
    if isinstance(value, int) and not isinstance(value, bool):
        return value
    if isinstance(value, str):
        return int(value, 16)
    raise ValueError(f"invalid entrance value {value!r}")


def check(report: dict[str, Any], contract: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    expected_schema = contract.get("source_report_schema")
    if report.get("schema") != expected_schema:
        errors.append(
            f"report schema {report.get('schema')!r} != {expected_schema!r}")
    if contract.get("schema") != "dkc1.widescreen-capability-floor.v1":
        errors.append("unsupported capability-floor contract schema")

    config = report.get("config")
    if not isinstance(config, dict):
        errors.append("report config is missing")
        config = {}
    for key, contract_key in (
        ("frames", "minimum_frames"),
        ("entry_settle_frames", "minimum_entry_settle_frames"),
        ("repeats", "minimum_repeats"),
    ):
        actual = config.get(key)
        minimum = contract.get(contract_key)
        if not isinstance(actual, int) or not isinstance(minimum, int) or actual < minimum:
            errors.append(f"config {key}={actual!r} is below required {minimum!r}")
    required_action = contract.get("required_action")
    actions = config.get("actions")
    if not isinstance(actions, list) or required_action not in actions:
        errors.append(f"required action {required_action!r} was not exercised")

    try:
        expected = {_entrance(value) for value in contract["expected_entrances"]}
        centered = {
            _entrance(value)
            for value in contract.get("centered_fixed_camera_entrances", [])
        }
    except (KeyError, TypeError, ValueError) as exc:
        return errors + [f"invalid contract entrances: {exc}"]

    branches = report.get("branches")
    if not isinstance(branches, list):
        return errors + ["report branches are missing"]
    by_entrance: dict[int, list[dict[str, Any]]] = {}
    for index, branch in enumerate(branches):
        if not isinstance(branch, dict):
            errors.append(f"branch {index} is not an object")
            continue
        try:
            entrance = _entrance(branch.get("entrance"))
        except ValueError as exc:
            errors.append(f"branch {index}: {exc}")
            continue
        by_entrance.setdefault(entrance, []).append(branch)

    missing = sorted(expected - set(by_entrance))
    unexpected = sorted(set(by_entrance) - expected)
    if missing:
        errors.append("missing entrances: " + ", ".join(f"{x:04X}" for x in missing))
    if unexpected:
        errors.append("unexpected entrances: " + ", ".join(f"{x:04X}" for x in unexpected))

    for entrance in sorted(expected & set(by_entrance)):
        candidates = by_entrance[entrance]
        if len(candidates) != 1:
            errors.append(f"{entrance:04X}: expected one branch, found {len(candidates)}")
            continue
        branch = candidates[0]
        if branch.get("action") != required_action:
            errors.append(f"{entrance:04X}: wrong action {branch.get('action')!r}")
        deterministic = branch.get("deterministic")
        if not isinstance(deterministic, dict) or not all(
                deterministic.get(side) is True for side in ("native", "wide")):
            errors.append(f"{entrance:04X}: native/wide run is not deterministic")
        wide_runs = branch.get("wide_runs")
        if not isinstance(wide_runs, list) or not wide_runs:
            errors.append(f"{entrance:04X}: no wide run")
            continue
        for repeat, run in enumerate(wide_runs, 1):
            if not isinstance(run, dict) or run.get("exit_code") != 0:
                errors.append(f"{entrance:04X}: wide repeat {repeat} did not complete")
                continue
            grade = run.get("widescreen_grade")
            if not isinstance(grade, dict):
                errors.append(f"{entrance:04X}: wide repeat {repeat} has no grade")
                continue
            if grade.get("status") != "pass":
                errors.append(
                    f"{entrance:04X}: widescreen grade {grade.get('status')!r}: "
                    + ", ".join(grade.get("failures") or []))
            extended = grade.get("extended_frames")
            reason = grade.get("centered_reason")
            if entrance in centered:
                if extended != 0 or reason != "centered_fixed_camera_arena":
                    errors.append(
                        f"{entrance:04X}: fixed arena contract changed "
                        f"(extended={extended!r}, reason={reason!r})")
            elif not isinstance(extended, int) or extended <= 0:
                errors.append(f"{entrance:04X}: no extended gameplay frames")
            if grade.get("raw_margin_pixels") != 0:
                errors.append(f"{entrance:04X}: raw margin pixels were served")
            if grade.get("terrain_misses") != 0:
                errors.append(f"{entrance:04X}: terrain misses were served")
            strict = grade.get("strict_summary")
            if not isinstance(strict, dict):
                errors.append(f"{entrance:04X}: strict trace summary is missing")
            elif strict.get("policy_violations"):
                errors.append(f"{entrance:04X}: strict trace policy violations")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument(
        "--contract", type=Path,
        default=Path(__file__).resolve().parents[1]
        / "contracts" / "widescreen-capability-floor.json")
    args = parser.parse_args()
    try:
        errors = check(_load(args.report), _load(args.contract))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"widescreen capability floor: ERROR: {exc}")
        return 2
    if errors:
        print("widescreen capability floor: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1
    print("widescreen capability floor: PASS (40/40 entrances)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
