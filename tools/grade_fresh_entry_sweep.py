#!/usr/bin/env python3
"""Grade a world-map fresh-entry sweep using raw widescreen trace evidence.

The terrain layer is authoritative.  Parallax layers are allowed to use the
explicit continuation/fold policies, but a terrain miss, raw-margin fallback,
or a gameplay entrance that never leaves centered 4:3 is a release failure.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Iterable


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            if not line.strip():
                continue
            value = json.loads(line)
            if value.get("schema") != "dkc1.ws.frame.v1":
                raise ValueError(f"{path}:{line_no}: unexpected trace schema")
            rows.append(value)
    if not rows:
        raise ValueError(f"empty trace: {path}")
    return rows


def _terrain_layer(row: dict[str, Any]) -> int:
    ppu = row.get("ppu", {})
    explicit = int(ppu.get("terrain_layer", -1))
    if 0 <= explicit <= 1:
        return explicit
    stream = int(row.get("source", {}).get("stream_vram", -1)) & 0x7FFF
    bgsc = ppu.get("bgsc", [])
    wide_mask = int(ppu.get("wide_mask", 0))
    for layer in range(min(2, len(bgsc))):
        if wide_mask & (1 << layer):
            map_base = (int(bgsc[layer]) & 0xFC) << 8
            if map_base == stream:
                return layer
    return -1


def _classify_centered(rows: list[dict[str, Any]]) -> str:
    bounded = [r for r in rows if r.get("decision", {}).get("bounds_ready")]
    if not bounded:
        return "centered_static_or_unready_bounds"
    last = bounded[-1]
    terrain = _terrain_layer(last)
    bgmode = int(last.get("ppu", {}).get("bgmode", 0))
    if 0 <= terrain <= 1 and bgmode & (0x10 << terrain):
        return "centered_big_tile_unsupported"
    source = last.get("source", {})
    if int(source.get("map", 0)) == 0 and int(source.get("metatiles", 0)) == 0:
        return "centered_missing_decoder_source"
    calibration = last.get("calibration", {})
    if any(int(v[1]) for v in (calibration.get("horizontal", [0, 0]),
                               calibration.get("vertical", [0, 0]))):
        return "centered_calibration_rejected"
    return "centered_unknown"


def grade_repeat(trace_path: Path) -> dict[str, Any]:
    rows = _read_jsonl(trace_path)
    gameplay = [r for r in rows if int(r.get("scene", {}).get("mode", -1)) != 3
                or int(r.get("scene", {}).get("level", -1)) != 0x25
                or int(r.get("camera", {}).get("upper", 0)) !=
                   int(r.get("camera", {}).get("lower", 0))]
    extended = [r for r in gameplay if r.get("decision", {}).get("edge_extension")]
    raw = 0
    terrain_misses = 0
    terrain_hits = 0
    for row in gameplay:
        for delta in row.get("shadow_delta", []):
            raw += int(delta.get("west_raw", 0)) + int(delta.get("east_raw", 0))
        terrain = _terrain_layer(row)
        deltas = row.get("shadow_delta", [])
        if 0 <= terrain < len(deltas) and row.get("decision", {}).get("shadow_commit"):
            delta = deltas[terrain]
            terrain_misses += int(delta.get("west_miss", 0)) + int(delta.get("east_miss", 0))
            terrain_hits += int(delta.get("west_hit", 0)) + int(delta.get("east_hit", 0))
    centered_reason = None if extended else _classify_centered(gameplay)
    failures: list[str] = []
    if not extended:
        failures.append(centered_reason or "centered_unknown")
    if raw:
        failures.append("raw_margin_fallback")
    if terrain_misses:
        failures.append("terrain_margin_miss")
    return {
        "trace": str(trace_path.resolve()),
        "frames": len(rows),
        "gameplay_frames": len(gameplay),
        "extended_frames": len(extended),
        "terrain_hits": terrain_hits,
        "terrain_misses": terrain_misses,
        "raw_margin_pixels": raw,
        "centered_reason": centered_reason,
        "status": "pass" if not failures else "fail",
        "failures": failures,
    }


def grade_report(report_path: Path) -> dict[str, Any]:
    source = json.loads(report_path.read_text(encoding="utf-8"))
    if source.get("schema") != "dkc1.world-map-fresh-entry-sweep.v1":
        raise ValueError("unexpected sweep schema")
    entries: list[dict[str, Any]] = []
    for entry in source.get("fresh_entries", []):
        repeats = [grade_repeat(Path(r["paths"]["trace"])) for r in entry.get("repeats", [])]
        name = entry.get("map_state", {}).get("entrance_name", "unknown")
        failures = sorted({failure for repeat in repeats for failure in repeat["failures"]})
        entries.append({
            "entrance": int(entry.get("map_state", {}).get("entrance", -1)),
            "name": name,
            "deterministic": bool(entry.get("deterministic", False)),
            "status": "pass" if entry.get("deterministic") and not failures else "fail",
            "failures": failures + ([] if entry.get("deterministic") else ["nondeterministic"]),
            "repeats": repeats,
        })
    failed = [entry for entry in entries if entry["status"] != "pass"]
    return {
        "schema": "dkc1.fresh-entry-widescreen-grade.v1",
        "source_report": str(report_path.resolve()),
        "status": "pass" if not failed else "fail",
        "counts": {"entries": len(entries), "passed": len(entries) - len(failed), "failed": len(failed)},
        "failure_classes": sorted({failure for entry in failed for failure in entry["failures"]}),
        "entries": entries,
    }


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        result = grade_report(args.report)
        encoded = json.dumps(result, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
        print(encoded, end="")
        return 0 if result["status"] == "pass" else 1
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(json.dumps({"schema": "dkc1.fresh-entry-widescreen-grade.v1", "status": "error", "error": str(exc)}))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
