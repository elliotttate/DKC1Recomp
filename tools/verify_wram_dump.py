#!/usr/bin/env python3
"""Verify a DKC1 headless WRAM payload and its checksum-indexed JSONL."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


SCHEMA = "dkc1.wram.dump.v1"


def _hex_address(value: object, source: str) -> int:
    if not isinstance(value, str) or len(value) != 5:
        raise ValueError(f"{source}: WRAM address must be five hex digits")
    try:
        result = int(value, 16)
    except ValueError as error:
        raise ValueError(f"{source}: invalid WRAM address {value!r}") from error
    if result < 0 or result >= 0x20000:
        raise ValueError(f"{source}: WRAM address out of range")
    return result


def verify(raw_path: Path, index_path: Path | None = None) -> dict:
    if index_path is None:
        index_path = Path(f"{raw_path}.jsonl")
    rows: list[dict] = []
    with index_path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{index_path}:{line_number}: invalid JSON") from error
            if not isinstance(row, dict) or row.get("schema") != SCHEMA:
                raise ValueError(
                    f"{index_path}:{line_number}: unsupported schema")
            rows.append(row)
    if not rows or rows[0].get("type") != "manifest":
        raise ValueError(f"{index_path}: first record must be a manifest")
    if any(row.get("type") != "frame" for row in rows[1:]):
        raise ValueError(f"{index_path}: records after manifest must be frames")

    manifest = rows[0]
    first = manifest.get("first_frame")
    last = manifest.get("last_frame")
    payload_size = manifest.get("payload_size")
    ranges = manifest.get("ranges")
    if (not isinstance(first, int) or not isinstance(last, int) or first < 1
            or last < first or not isinstance(payload_size, int)
            or payload_size < 1 or not isinstance(ranges, list) or not ranges):
        raise ValueError(f"{index_path}: invalid manifest fields")

    calculated_size = 0
    previous_last = -1
    normalized_ranges: list[list[str]] = []
    for index, item in enumerate(ranges):
        if not isinstance(item, list) or len(item) != 2:
            raise ValueError(f"{index_path}: range {index} is malformed")
        start = _hex_address(item[0], str(index_path))
        stop = _hex_address(item[1], str(index_path))
        if start > stop or start <= previous_last:
            raise ValueError(f"{index_path}: ranges must be sorted/disjoint")
        calculated_size += stop - start + 1
        previous_last = stop
        normalized_ranges.append([f"{start:05x}", f"{stop:05x}"])
    if calculated_size != payload_size:
        raise ValueError(
            f"{index_path}: payload_size disagrees with ranges")

    raw = raw_path.read_bytes()
    expected_frames = last - first + 1
    frames = rows[1:]
    if len(frames) != expected_frames:
        raise ValueError(
            f"{index_path}: has {len(frames)} frame records, "
            f"expected {expected_frames}")
    if len(raw) != expected_frames * payload_size:
        raise ValueError(
            f"{raw_path}: has {len(raw)} bytes, "
            f"expected {expected_frames * payload_size}")

    emulator_frames: list[int] = []
    for index, row in enumerate(frames):
        relative = first + index
        offset = index * payload_size
        if (row.get("relative_frame") != relative or
                row.get("offset") != offset or
                row.get("length") != payload_size or
                not isinstance(row.get("emulator_frame"), int)):
            raise ValueError(
                f"{index_path}: frame record {index + 1} has invalid metadata")
        payload = raw[offset:offset + payload_size]
        digest = hashlib.sha256(payload).hexdigest()
        if row.get("sha256") != digest:
            raise ValueError(
                f"{index_path}: frame {relative} checksum mismatch")
        emulator_frames.append(row["emulator_frame"])

    return {
        "schema": SCHEMA,
        "verified": True,
        "raw": str(raw_path),
        "index": str(index_path),
        "ranges": normalized_ranges,
        "payload_size": payload_size,
        "frame_count": expected_frames,
        "relative_frames": [first, last],
        "emulator_frames": [emulator_frames[0], emulator_frames[-1]],
        "raw_sha256": hashlib.sha256(raw).hexdigest(),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", type=Path)
    parser.add_argument("--index", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)
    try:
        report = verify(args.raw, args.index)
        encoded = json.dumps(report, indent=2) + "\n"
        if args.json_out:
            args.json_out.write_text(encoded, encoding="utf-8")
        print(encoded, end="")
        return 0
    except (OSError, ValueError) as error:
        print(f"verify_wram_dump: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
