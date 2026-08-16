#!/usr/bin/env python3
"""Compile byte-grounded presentation-proxy candidates from DKC source.

The write-set analyzer proves an exact (mode, level, entrance, source, sprite)
tuple safe to project.  This tool resolves that tuple back to DATA_BD8000 and
its authored external record, verifies the initializer really selects the
observed sprite ID, then emits JSON and a small C include.  Nothing is inferred
from pool slot numbers and unsupported records are omitted fail-closed.
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path


SCHEMA = "dkc1.margin-proxy-manifest.v1"


@dataclass(frozen=True)
class Record:
    source: int
    record_type: int
    x: int
    y: int
    initializer: str


def locate(lines: list[str], label: str) -> int:
    needle = label + ":"
    for index, line in enumerate(lines):
        if line.strip() == needle:
            return index
    raise ValueError(f"label not found: {label}")


def parse_word(token: str) -> int:
    match = re.fullmatch(r"\$([0-9A-Fa-f]{1,4})", token.strip())
    if not match:
        raise ValueError(f"not a word: {token}")
    return int(match.group(1), 16)


def entrance_table(lines: list[str]) -> list[str]:
    result: list[str] = []
    for line in lines[locate(lines, "DATA_BD8000") + 1:]:
        stripped = line.strip()
        if stripped.startswith("DATA_") and stripped.endswith(":"):
            break
        if stripped.startswith("dw "):
            result.extend(part.strip() for part in stripped[3:].split(","))
    if len(result) != 0xE6:
        raise ValueError(f"expected 230 entrance pointers, found {len(result)}")
    return result


def records_at(lines: list[str], label: str) -> list[Record]:
    result: list[Record] = []
    external = False
    for line in lines[locate(lines, label) + 1:]:
        stripped = line.strip()
        if stripped == "else":
            external = True
            continue
        if not external or not stripped.startswith("dw "):
            continue
        parts = [part.strip() for part in stripped[3:].split(",")]
        if len(parts) < 4 or not all(
                re.fullmatch(r"\$[0-9A-Fa-f]{1,4}", part)
                for part in parts[:3]):
            continue
        record_type = parse_word(parts[0])
        if record_type == 0:
            break
        result.append(Record(len(result), record_type, parse_word(parts[1]),
                             parse_word(parts[2]), parts[3]))
    return result


def blocks(lines: list[str]) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    current: str | None = None
    for line in lines:
        match = re.fullmatch(r"(DATA_[0-9A-Fa-f]{6}):", line.strip())
        if match:
            current = match.group(1)
            result[current] = []
        elif current is not None:
            result[current].append(line.strip())
    return result


def resolve_sprite(initializer: str,
                   source_blocks: dict[str, list[str]]) -> int | None:
    current = initializer
    visited: set[str] = set()
    pattern = re.compile(
        r"NorSpr_SpriteIDLo\s*,\s*!Define_DKC1_NorSpr([0-9A-Fa-f]{2})_")
    while current in source_blocks and current not in visited:
        visited.add(current)
        body = source_blocks[current]
        for line in body:
            match = pattern.search(line)
            if match:
                return int(match.group(1), 16)
        parent = None
        for line in body:
            match = re.search(r"DKC1_SSS_Op82\((DATA_[0-9A-Fa-f]{6})\)", line)
            if match:
                parent = match.group(1)
                break
        if parent is None:
            return None
        current = parent
    return None


def label_word(label: str) -> int:
    match = re.fullmatch(r"DATA_[0-9A-Fa-f]{2}([0-9A-Fa-f]{4})", label)
    if not match:
        raise ValueError(f"initializer is not a banked data label: {label}")
    return int(match.group(1), 16)


def build_manifest(disassembly: Path, analysis: dict) -> dict:
    lines = disassembly.read_text(encoding="utf-8", errors="replace").splitlines()
    entrances = entrance_table(lines)
    source_blocks = blocks(lines)
    rows = []
    for proof in analysis.get("actors", []):
        if not proof.get("candidate"):
            continue
        entrance = int(proof.get("entrance", -1))
        source = int(proof["source"])
        sprite_id = int(proof["id"])
        if not 0 <= entrance < len(entrances):
            raise ValueError(f"candidate has invalid entrance {entrance}")
        authored = records_at(lines, entrances[entrance])
        if not 0 <= source < len(authored):
            raise ValueError(
                f"entrance ${entrance:02X} has no source ${source:02X}")
        record = authored[source]
        resolved = resolve_sprite(record.initializer, source_blocks)
        if resolved != sprite_id:
            raise ValueError(
                f"entrance ${entrance:02X} source ${source:02X}: "
                f"trace id ${sprite_id:02X}, initializer resolves to {resolved}")
        rows.append({
            "mode": int(proof["mode"]), "level": int(proof["level"]),
            "entrance": entrance, "source": source,
            "record_type": record.record_type, "x": record.x, "y": record.y,
            "initializer": record.initializer,
            "initializer_word": label_word(record.initializer),
            "sprite_id": sprite_id,
            "write_set": proof["domains"],
        })
    rows.sort(key=lambda row: (row["entrance"], row["source"], row["sprite_id"]))
    return {"schema": SCHEMA, "candidates": rows,
            "candidate_count": len(rows)}


def emit_c(manifest: dict) -> str:
    lines = [
        "/* Generated by tools/build_margin_proxy_manifest.py. */",
        "/* Exact candidates only; absence means fail closed. */",
        "static const Dkc1MarginProxyCandidate kDkc1MarginProxyCandidates[] = {",
    ]
    for row in manifest["candidates"]:
        lines.append(
            "  { 0x%04xu, 0x%04xu, 0x%02xu, 0x%02xu, 0x%02xu, "
            "0x%04xu, 0x%04xu, 0x%04xu, 0x%02xu }," % (
                row["mode"], row["level"], row["entrance"], row["source"],
                row["record_type"], row["x"], row["y"],
                row["initializer_word"], row["sprite_id"]))
    lines.extend(["};", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disassembly", type=Path, required=True)
    parser.add_argument("--analysis", type=Path, required=True)
    parser.add_argument("--json-out", type=Path, required=True)
    parser.add_argument("--c-out", type=Path, required=True)
    args = parser.parse_args()
    analysis = json.loads(args.analysis.read_text(encoding="utf-8"))
    manifest = build_manifest(args.disassembly, analysis)
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.c_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")
    args.c_out.write_text(emit_c(manifest), encoding="utf-8", newline="\n")
    print(json.dumps({"candidate_count": manifest["candidate_count"]},
                     indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
