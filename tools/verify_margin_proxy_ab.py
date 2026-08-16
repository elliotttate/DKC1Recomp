#!/usr/bin/env python3
"""Verify a presentation-only DKC1 margin-proxy A/B pair.

The proxy is expected to change OAM, sprite-upload presentation state, and
edge pixels.  It must not change audio or any gameplay-owned WRAM byte.  This
tool keeps those claims separate instead of treating a whole-WRAM hash change
as either automatically harmless or automatically fatal.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


SCHEMA = "dkc1.margin-proxy-ab.v1"
RENDERER_SCRATCH = frozenset(
    [0x0057, 0x005A, 0x005B, 0x005C, 0x0064, 0x006E, 0x008E,
     0x0090, 0x0091, 0x0092, 0x0093]
)
PRESENTATION_RANGES = (
    (0x0200, 0x041F, "oam_shadow"),
    (0x170F, 0x1AF2, "sprite_upload_queue"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(br"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not match or int(match.group(3)) != 255:
        raise ValueError(f"unsupported P6 image: {path}")
    width, height = int(match.group(1)), int(match.group(2))
    pixels = data[match.end():]
    if len(pixels) != width * height * 3:
        raise ValueError(f"truncated P6 image: {path}")
    return width, height, pixels


def parse_stdout(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.endswith("_sha256"):
            values[key] = value.strip()
    audio = re.search(r"\baudio_fnv1a=([0-9a-f]+)",
                      path.read_text(encoding="utf-8"))
    if audio:
        values["audio_fnv1a"] = audio.group(1)
    return values


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(
        encoding="utf-8").splitlines() if line.strip()]


def classify_offset(offset: int) -> str | None:
    if offset in RENDERER_SCRATCH:
        return "renderer_scratch"
    for first, last, name in PRESENTATION_RANGES:
        if first <= offset <= last:
            return name
    return None


def contiguous_ranges(offsets: list[int]) -> list[list[int]]:
    ranges: list[list[int]] = []
    for offset in offsets:
        if not ranges or offset != ranges[-1][1] + 1:
            ranges.append([offset, offset])
        else:
            ranges[-1][1] = offset
    return ranges


def verify(on_dir: Path, off_dir: Path, *, extra: int,
           center_overlap: int) -> dict:
    required = ("frame.ppm", "wram.bin", "audio.pcm", "stdout.log")
    for directory in (on_dir, off_dir):
        for name in required:
            if not (directory / name).is_file():
                raise ValueError(f"missing {directory / name}")

    width, height, on_pixels = read_ppm(on_dir / "frame.ppm")
    off_width, off_height, off_pixels = read_ppm(off_dir / "frame.ppm")
    if (width, height) != (off_width, off_height):
        raise ValueError("A/B framebuffer geometry differs")
    if width != 256 + 2 * extra:
        raise ValueError("framebuffer width does not match native+2*extra")

    changed_pixels: list[tuple[int, int]] = []
    for pixel in range(width * height):
        start = pixel * 3
        if on_pixels[start:start + 3] != off_pixels[start:start + 3]:
            changed_pixels.append((pixel % width, pixel // width))
    bbox = None
    if changed_pixels:
        xs = [point[0] for point in changed_pixels]
        ys = [point[1] for point in changed_pixels]
        bbox = [min(xs), min(ys), max(xs) + 1, max(ys) + 1]
    protected_left = extra + center_overlap
    protected_right = extra + 256 - center_overlap
    deep_center_pixels = [
        [x, y] for x, y in changed_pixels
        if protected_left <= x < protected_right
    ]

    on_wram = (on_dir / "wram.bin").read_bytes()
    off_wram = (off_dir / "wram.bin").read_bytes()
    if len(on_wram) != 0x20000 or len(off_wram) != 0x20000:
        raise ValueError("WRAM evidence must be exactly 128 KiB")
    changed_offsets = [
        offset for offset, pair in enumerate(zip(on_wram, off_wram))
        if pair[0] != pair[1]
    ]
    domains: dict[str, list[int]] = {}
    unexpected: list[int] = []
    for offset in changed_offsets:
        domain = classify_offset(offset)
        if domain is None:
            unexpected.append(offset)
        else:
            domains.setdefault(domain, []).append(offset)

    on_stdout = parse_stdout(on_dir / "stdout.log")
    off_stdout = parse_stdout(off_dir / "stdout.log")
    events = read_jsonl(on_dir / "proxy.jsonl")
    injected = [event for event in events if event.get("event") == "inject"]
    restored = [event for event in events if event.get("event") == "restore"]
    renderer_advanced = any(
        int(after.get("global_oam_index", 0)) >
        int(before.get("global_oam_index", 0)) and
        int(after.get("proxy_displayed_pose", 0)) != 0
        for before, after in zip(injected, restored)
    )

    audio_equal = sha256(on_dir / "audio.pcm") == sha256(
        off_dir / "audio.pcm")
    ppu_oam_changed = (on_stdout.get("oam_sha256") !=
                       off_stdout.get("oam_sha256"))
    shadow_oam_changed = (on_stdout.get("oam_source_sha256") !=
                          off_stdout.get("oam_source_sha256"))
    status = "pass" if all((
        changed_pixels,
        not deep_center_pixels,
        not unexpected,
        audio_equal,
        renderer_advanced,
        ppu_oam_changed,
        shadow_oam_changed,
    )) else "fail"

    return {
        "schema": SCHEMA,
        "status": status,
        "inputs": {
            "on": str(on_dir.resolve()),
            "off": str(off_dir.resolve()),
            "on_frame_sha256": sha256(on_dir / "frame.ppm"),
            "off_frame_sha256": sha256(off_dir / "frame.ppm"),
            "on_wram_sha256": sha256(on_dir / "wram.bin"),
            "off_wram_sha256": sha256(off_dir / "wram.bin"),
        },
        "frame": {
            "width": width,
            "height": height,
            "changed_pixels": len(changed_pixels),
            "bbox": bbox,
            "protected_center": [protected_left, protected_right],
            "deep_center_changed_pixels": len(deep_center_pixels),
        },
        "wram": {
            "changed_bytes": len(changed_offsets),
            "changed_ranges": contiguous_ranges(changed_offsets),
            "domains": {
                name: {
                    "changed_bytes": len(offsets),
                    "ranges": contiguous_ranges(offsets),
                }
                for name, offsets in sorted(domains.items())
            },
            "unexpected_offsets": unexpected,
        },
        "presentation": {
            "proxy_events": len(events),
            "inject_events": len(injected),
            "restore_events": len(restored),
            "renderer_advanced_oam": renderer_advanced,
            "ppu_oam_changed": ppu_oam_changed,
            "shadow_oam_changed": shadow_oam_changed,
            "vram_changed": (on_stdout.get("vram_sha256") !=
                             off_stdout.get("vram_sha256")),
            "cgram_changed": (on_stdout.get("cgram_sha256") !=
                              off_stdout.get("cgram_sha256")),
        },
        "audio": {
            "pcm_equal": audio_equal,
            "on_sha256": sha256(on_dir / "audio.pcm"),
            "off_sha256": sha256(off_dir / "audio.pcm"),
            "fnv_equal": (on_stdout.get("audio_fnv1a") ==
                          off_stdout.get("audio_fnv1a")),
        },
        "interpretation": (
            "Only named presentation scratch/OAM/upload domains may differ; "
            "audio and protected center pixels must remain identical."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--on-dir", required=True, type=Path)
    parser.add_argument("--off-dir", required=True, type=Path)
    parser.add_argument("--extra", type=int, default=43)
    parser.add_argument("--center-overlap", type=int, default=16)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    report = verify(args.on_dir, args.off_dir, extra=args.extra,
                    center_overlap=args.center_overlap)
    encoded = json.dumps(report, indent=2) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(encoded, end="")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
