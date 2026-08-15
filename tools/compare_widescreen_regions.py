#!/usr/bin/env python3
"""Byte-exact left/center/right comparison for DKC1 P6 frame captures."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


SCHEMA = "dkc1.ws.regions.v1"
TRACE_INPUT_HASHES = ("vram", "ppu_oam", "wram_oam")


def _tokens(data: bytes):
    pos = 0
    while pos < len(data):
        while pos < len(data) and chr(data[pos]).isspace():
            pos += 1
        if pos < len(data) and data[pos] == ord("#"):
            while pos < len(data) and data[pos] not in b"\r\n":
                pos += 1
            continue
        if pos >= len(data):
            return
        start = pos
        while (pos < len(data) and not chr(data[pos]).isspace()
               and data[pos] != ord("#")):
            pos += 1
        yield data[start:pos], pos


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    token_iter = _tokens(data)
    header: list[bytes] = []
    end = 0
    for _ in range(4):
        try:
            token, end = next(token_iter)
        except StopIteration as error:
            raise ValueError(f"{path}: truncated PPM header") from error
        header.append(token)
    if header[0] != b"P6":
        raise ValueError(f"{path}: expected binary P6 PPM")
    try:
        width, height, maximum = map(int, header[1:])
    except ValueError as error:
        raise ValueError(f"{path}: invalid PPM dimensions") from error
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: unsupported PPM geometry/range")
    if end >= len(data) or not chr(data[end]).isspace():
        raise ValueError(f"{path}: missing PPM raster separator")
    raster_start = end + 1
    if data[end:end + 2] == b"\r\n":
        raster_start = end + 2
    raster = data[raster_start:]
    expected = width * height * 3
    if len(raster) != expected:
        raise ValueError(
            f"{path}: raster is {len(raster)} bytes, expected {expected}")
    return width, height, raster


def _region(raster: bytes, width: int, height: int,
            x0: int, x1: int) -> bytes:
    result = bytearray()
    for y in range(height):
        start = (y * width + x0) * 3
        result.extend(raster[start:start + (x1 - x0) * 3])
    return bytes(result)


def _hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _diff_stats(a: bytes, b: bytes, width: int, height: int) -> dict:
    if len(a) != len(b) or len(a) != width * height * 3:
        raise ValueError("comparison regions have different geometry")
    changed = 0
    max_delta = 0
    bounds: list[int] | None = None
    for pixel in range(width * height):
        base = pixel * 3
        delta = max(abs(a[base + channel] - b[base + channel])
                    for channel in range(3))
        if delta:
            changed += 1
            max_delta = max(max_delta, delta)
            x, y = pixel % width, pixel // width
            if bounds is None:
                bounds = [x, y, x, y]
            else:
                bounds[0] = min(bounds[0], x)
                bounds[1] = min(bounds[1], y)
                bounds[2] = max(bounds[2], x)
                bounds[3] = max(bounds[3], y)
    return {"pixels": width * height, "changed_pixels": changed,
            "max_channel_delta": max_delta, "bounds": bounds}


def load_trace_record(path: Path, frame: int | None) -> dict:
    selected = None
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            record = json.loads(line)
            if record.get("schema") != "dkc1.ws.frame.v1":
                raise ValueError(f"{path}:{line_number}: unsupported trace")
            if frame is None or record.get("frame") == frame:
                selected = record
            if frame is not None and record.get("frame") == frame:
                break
    if selected is None:
        raise ValueError(f"{path}: frame {frame!r} not found")
    return selected


def compare(reference_path: Path, candidate_path: Path, extra: int,
            reference_trace: Path | None = None,
            candidate_trace: Path | None = None,
            frame: int | None = None) -> tuple[dict, bytes]:
    rw, rh, reference = read_ppm(reference_path)
    cw, ch, candidate = read_ppm(candidate_path)
    if rh != ch:
        raise ValueError("reference and candidate heights differ")
    native_width = cw - extra * 2
    if extra < 0 or native_width <= 0:
        raise ValueError("invalid candidate width/extra")
    if rw == native_width:
        ref_center = reference
        reference_layout = "native"
    elif rw == cw:
        ref_center = _region(reference, rw, rh, extra, extra + native_width)
        reference_layout = "wide"
    else:
        raise ValueError(
            f"reference width must be {native_width} or {cw}, got {rw}")

    cand_left = _region(candidate, cw, ch, 0, extra)
    cand_center = _region(candidate, cw, ch, extra, extra + native_width)
    cand_right = _region(candidate, cw, ch, extra + native_width, cw)
    center_stats = _diff_stats(ref_center, cand_center, native_width, ch)

    regions = {
        "left": {"width": extra, "sha256": _hash(cand_left)},
        "center": {"width": native_width, "sha256": _hash(cand_center),
                   **center_stats},
        "right": {"width": extra, "sha256": _hash(cand_right)},
    }
    if reference_layout == "wide":
        for name, x0, x1 in (
                ("left", 0, extra),
                ("right", extra + native_width, cw)):
            ref = _region(reference, rw, rh, x0, x1)
            cand = cand_left if name == "left" else cand_right
            regions[name].update(_diff_stats(ref, cand, extra, ch))
            regions[name]["reference_sha256"] = _hash(ref)

    input_gate = {"provided": False, "match": None, "hashes": {}}
    if bool(reference_trace) != bool(candidate_trace):
        raise ValueError("both trace paths are required for the raw-input gate")
    if reference_trace and candidate_trace:
        rr = load_trace_record(reference_trace, frame)
        cr = load_trace_record(candidate_trace, frame)
        input_gate["provided"] = True
        input_gate["frame"] = [rr.get("frame"), cr.get("frame")]
        matches = []
        for key in TRACE_INPUT_HASHES:
            before = rr.get("hash", {}).get(key)
            after = cr.get("hash", {}).get(key)
            same = before is not None and before == after
            matches.append(same)
            input_gate["hashes"][key] = {
                "reference": before, "candidate": after, "match": same}
        input_gate["match"] = all(matches)

    report = {
        "schema": SCHEMA,
        "reference": str(reference_path),
        "candidate": str(candidate_path),
        "geometry": {"candidate": [cw, ch], "native_width": native_width,
                     "extra_per_side": extra,
                     "reference_layout": reference_layout},
        "input_gate": input_gate,
        "regions": regions,
        "center_exact": center_stats["changed_pixels"] == 0,
    }

    diff = bytearray(candidate)
    for y in range(ch):
        for x in range(native_width):
            rb = (y * native_width + x) * 3
            cb = (y * cw + x + extra) * 3
            if ref_center[rb:rb + 3] != candidate[cb:cb + 3]:
                diff[cb:cb + 3] = b"\xff\x00\x00"
            else:
                gray = sum(candidate[cb:cb + 3]) // 9
                diff[cb:cb + 3] = bytes((gray, gray, gray))
    return report, bytes(diff)


def write_ppm(path: Path, width: int, height: int, raster: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + raster)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--extra", type=int, default=43)
    parser.add_argument("--reference-trace", type=Path)
    parser.add_argument("--candidate-trace", type=Path)
    parser.add_argument("--frame", type=int)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--diff-out", type=Path)
    parser.add_argument("--allow-center-diff", action="store_true")
    args = parser.parse_args(argv)
    try:
        report, diff = compare(
            args.reference, args.candidate, args.extra,
            args.reference_trace, args.candidate_trace, args.frame)
        if args.json_out:
            args.json_out.write_text(json.dumps(report, indent=2) + "\n",
                                     encoding="utf-8")
        if args.diff_out:
            width, height, _ = read_ppm(args.candidate)
            write_ppm(args.diff_out, width, height, diff)
        print(json.dumps(report, indent=2))
        if report["input_gate"]["provided"] and not report["input_gate"]["match"]:
            return 3
        if not args.allow_center_diff and not report["center_exact"]:
            return 1
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"compare_widescreen_regions: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
