#!/usr/bin/env python3
"""Audit isolated PPM planes for old-256px clipping or edge repetition.

The runtime blank detector is deliberately conservative and only catches
nearly-flat columns.  This offline companion targets the two non-flat failure
shapes seen during DKC1 widescreen development:

* a layer becomes empty outside the centered 256-pixel viewport; and
* a side margin is copied from the opposite native edge.

It also measures whether the transitions at the two legacy boundaries are
outliers compared with nearby column transitions.  Repeat/seam findings are
diagnostic leads, not automatic renderer failures: authored periodic layers
such as BG3 can legitimately look similar after wrapping.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
import statistics
import sys
from typing import Iterable


SCHEMA = "dkc1.legacy-width-cull.v1"


@dataclass(frozen=True)
class Ppm:
    width: int
    height: int
    pixels: bytes

    def pixel(self, x: int, y: int) -> bytes:
        offset = (y * self.width + x) * 3
        return self.pixels[offset:offset + 3]


@dataclass(frozen=True)
class Pgm:
    width: int
    height: int
    pixels: bytes

    def occupied(self, x: int, y: int) -> bool:
        return self.pixels[y * self.width + x] != 0


def _tokens(raw: bytes) -> tuple[list[bytes], int]:
    tokens: list[bytes] = []
    cursor = 0
    while len(tokens) < 4:
        while cursor < len(raw) and raw[cursor] in b" \t\r\n":
            cursor += 1
        if cursor < len(raw) and raw[cursor] == ord("#"):
            while cursor < len(raw) and raw[cursor] not in b"\r\n":
                cursor += 1
            continue
        start = cursor
        while cursor < len(raw) and raw[cursor] not in b" \t\r\n":
            cursor += 1
        if start == cursor:
            raise ValueError("truncated PPM header")
        tokens.append(raw[start:cursor])
    while cursor < len(raw) and raw[cursor] in b" \t\r\n":
        cursor += 1
    return tokens, cursor


def read_ppm(path: Path) -> Ppm:
    raw = path.read_bytes()
    tokens, body = _tokens(raw)
    if tokens[0] != b"P6":
        raise ValueError(f"{path}: only binary P6 PPM is supported")
    width, height, maximum = map(int, tokens[1:])
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: invalid PPM dimensions/range")
    expected = width * height * 3
    if len(raw) - body != expected:
        raise ValueError(
            f"{path}: expected {expected} pixel bytes, got {len(raw) - body}")
    return Ppm(width, height, raw[body:])


def read_pgm(path: Path) -> Pgm:
    raw = path.read_bytes()
    tokens, body = _tokens(raw)
    if tokens[0] != b"P5":
        raise ValueError(f"{path}: only binary P5 PGM is supported")
    width, height, maximum = map(int, tokens[1:])
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: invalid PGM dimensions/range")
    expected = width * height
    if len(raw) - body != expected:
        raise ValueError(
            f"{path}: expected {expected} mask bytes, got {len(raw) - body}")
    return Pgm(width, height, raw[body:])


def is_content(pixel: bytes, threshold: int) -> bool:
    return any(channel > threshold for channel in pixel)


def region_pixels(image: Ppm, x0: int, x1: int) -> Iterable[bytes]:
    for y in range(image.height):
        for x in range(x0, x1):
            yield image.pixel(x, y)


def occupied(image: Ppm, mask: Pgm | None, x: int, y: int,
             threshold: int) -> bool:
    return (mask.occupied(x, y) if mask is not None
            else is_content(image.pixel(x, y), threshold))


def coverage(image: Ppm, mask: Pgm | None, x0: int, x1: int,
             threshold: int, y0: int = 0, y1: int | None = None) -> float:
    y1 = image.height if y1 is None else y1
    total = (x1 - x0) * (y1 - y0)
    present = sum(occupied(image, mask, x, y, threshold)
                  for y in range(y0, y1) for x in range(x0, x1))
    return present / total


def region_match(image: Ppm, mask: Pgm | None, ax: int, bx: int, width: int,
                 threshold: int, y0: int = 0,
                 y1: int | None = None) -> dict[str, float]:
    y1 = image.height if y1 is None else y1
    exact = 0
    content_exact = 0
    content_union = 0
    total = width * (y1 - y0)
    for y in range(y0, y1):
        for dx in range(width):
            a = image.pixel(ax + dx, y)
            b = image.pixel(bx + dx, y)
            a_content = occupied(image, mask, ax + dx, y, threshold)
            b_content = occupied(image, mask, bx + dx, y, threshold)
            same = a_content == b_content and (not a_content or a == b)
            exact += same
            if a_content or b_content:
                content_union += 1
                content_exact += same
    return {
        "exact_ratio": exact / total,
        "content_match_ratio": (
            content_exact / content_union if content_union else 1.0),
        "content_union_ratio": content_union / total,
    }


def transition_ratio(image: Ppm, mask: Pgm | None, x: int,
                     threshold: int, y0: int = 0,
                     y1: int | None = None) -> float:
    """Fraction of rows differing across the boundary between x-1 and x."""
    y1 = image.height if y1 is None else y1
    changed = 0
    for y in range(y0, y1):
        left_present = occupied(image, mask, x - 1, y, threshold)
        right_present = occupied(image, mask, x, y, threshold)
        if left_present != right_present or (left_present and
                image.pixel(x - 1, y) != image.pixel(x, y)):
            changed += 1
    return changed / (y1 - y0)


def seam_metrics(image: Ppm, mask: Pgm | None, boundary: int,
                 threshold: int, y0: int = 0,
                 y1: int | None = None) -> dict[str, float]:
    y1 = image.height if y1 is None else y1
    seam = transition_ratio(image, mask, boundary, threshold, y0, y1)
    nearby = [transition_ratio(image, mask, x, threshold, y0, y1)
              for x in range(max(1, boundary - 12),
                             min(image.width, boundary + 13))
              if x != boundary]
    baseline = statistics.median(nearby) if nearby else 0.0
    floor = 1.0 / (y1 - y0)
    return {
        "difference_ratio": seam,
        "nearby_median": baseline,
        "excess_ratio": seam / max(baseline, floor),
    }


def audit_sides(image: Ppm, mask: Pgm | None, native_width: int,
                threshold: int, y0: int, y1: int) -> dict[str, dict]:
    extra = (image.width - native_width) // 2
    right_native = extra + native_width
    adjacent_band = min(extra, 16)
    sides = {}
    for name, margin, adjacent, opposite, boundary in (
        ("left", (0, extra), (extra, extra + adjacent_band),
         (right_native - extra, right_native), extra),
        ("right", (right_native, image.width),
         (right_native - adjacent_band, right_native),
         (extra, extra + extra), right_native),
    ):
        margin_coverage = coverage(
            image, mask, *margin, threshold, y0, y1)
        adjacent_coverage = coverage(
            image, mask, *adjacent, threshold, y0, y1)
        repeat = region_match(
            image, mask, margin[0], opposite[0], extra, threshold, y0, y1)
        seam = seam_metrics(image, mask, boundary, threshold, y0, y1)
        hard_cull = (margin_coverage <= 0.03 and
                     adjacent_coverage >= 0.15 and
                     seam["difference_ratio"] >= 0.10)
        repeat_candidate = (
            repeat["content_match_ratio"] >= 0.98 and
            repeat["content_union_ratio"] >= 0.05)
        seam_candidate = (
            seam["difference_ratio"] >= 0.35 and
            seam["excess_ratio"] >= 2.5)
        sides[name] = {
            "margin": [margin[0], margin[1]],
            "legacy_boundary_x": boundary,
            "margin_content_ratio": margin_coverage,
            "adjacent_content_ratio": adjacent_coverage,
            "opposite_native_edge_match": repeat,
            "seam": seam,
            "hard_empty_cull": hard_cull,
            "opposite_edge_repeat_candidate": repeat_candidate,
            "legacy_seam_candidate": seam_candidate,
        }
    return sides


def audit(path: Path, native_width: int = 256,
          threshold: int = 8) -> dict:
    image = read_ppm(path)
    mask_path = path.with_name(path.stem + ".mask.pgm")
    mask = read_pgm(mask_path) if mask_path.is_file() else None
    if mask is not None and (mask.width != image.width or
                             mask.height != image.height):
        raise ValueError(f"{mask_path}: mask geometry differs from image")
    if image.width <= native_width or (image.width - native_width) % 2:
        raise ValueError(
            f"{path}: width must be centered and greater than {native_width}")
    extra = (image.width - native_width) // 2
    sides = audit_sides(
        image, mask, native_width, threshold, 0, image.height)

    band_findings = []
    for y0 in range(0, image.height, 16):
        y1 = min(image.height, y0 + 16)
        band_sides = audit_sides(
            image, mask, native_width, threshold, y0, y1)
        for side, row in band_sides.items():
            kinds = []
            if row["hard_empty_cull"]:
                kinds.append("hard_empty_cull")
            if row["opposite_edge_repeat_candidate"]:
                kinds.append("opposite_edge_repeat")
            if row["legacy_seam_candidate"]:
                kinds.append("legacy_boundary_seam")
            if kinds:
                band_findings.append({
                    "y": [y0, y1], "side": side, "kinds": kinds,
                    "metrics": row,
                })

    hard = [side for side, row in sides.items() if row["hard_empty_cull"]]
    hard_bands = [row for row in band_findings
                  if "hard_empty_cull" in row["kinds"]]
    leads = [
        {"side": side, "kind": kind}
        for side, row in sides.items()
        for kind, key in (
            ("opposite_edge_repeat", "opposite_edge_repeat_candidate"),
            ("legacy_boundary_seam", "legacy_seam_candidate"),
        ) if row[key]
    ]
    return {
        "schema": SCHEMA,
        "input": str(path.resolve()),
        "occupancy_mask": str(mask_path.resolve()) if mask else None,
        "geometry": {"width": image.width, "height": image.height,
                     "native_width": native_width, "extra_each_side": extra},
        "threshold": threshold,
        "status": "hard_failure" if hard or hard_bands else (
            "investigate" if leads or band_findings else "clean"),
        "hard_failure_sides": hard,
        "diagnostic_leads": leads,
        "band_height": 16,
        "band_findings": band_findings,
        "sides": sides,
        "interpretation": (
            "hard_empty_cull is release-blocking for a layer expected to fill "
            "the margin; repeat/seam candidates require provenance or a raw "
            "tilemap oracle because authored periodic layers may be valid"),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", type=Path)
    parser.add_argument("--native-width", type=int, default=256)
    parser.add_argument("--black-threshold", type=int, default=8)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args(argv)
    try:
        reports = [audit(path, args.native_width, args.black_threshold)
                   for path in args.images]
        output = {"schema": "dkc1.legacy-width-cull.batch.v1",
                  "reports": reports,
                  "status": ("hard_failure" if any(
                      row["status"] == "hard_failure" for row in reports)
                      else "investigate" if any(
                          row["status"] == "investigate" for row in reports)
                      else "clean")}
        text = json.dumps(output, indent=2)
        if args.json_out:
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(text + "\n", encoding="utf-8")
        print(text)
        return 2 if output["status"] == "hard_failure" else 0
    except (OSError, ValueError) as error:
        print(f"detect_legacy_width_cull: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
