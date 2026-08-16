#!/usr/bin/env python3
"""Verify high-world DKC1 widescreen shadow localization from WS JSONL."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def load_frames(path: Path) -> list[dict[str, Any]]:
    frames = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("schema") != "dkc1.ws.frame.v1":
            raise ValueError(f"{path}:{line_no}: unexpected schema")
        frames.append(row)
    if not frames:
        raise ValueError(f"{path}: no frames")
    return frames


def verify(path: Path) -> dict[str, Any]:
    frames = load_frames(path)
    high_world = 0
    checked = 0
    terrain_hits = 0
    identities: dict[str, list[tuple[int, int]]] = {}
    for row in frames:
        if not row.get("decision", {}).get("edge_extension"):
            continue
        checked += 1
        identity = str(row["identity"]["hash"])
        origins = row.get("shadow_origin")
        worlds = row.get("world")
        if not isinstance(origins, list) or len(origins) != 2:
            raise ValueError(f"frame {row['frame']}: missing per-layer origins")
        identities.setdefault(identity, [])
        current = []
        for layer in range(2):
            world = worlds[layer]
            origin = origins[layer]
            if not world.get("valid"):
                continue
            if not origin.get("valid"):
                raise ValueError(f"frame {row['frame']} layer {layer}: invalid origin")
            sx, sy = int(world["shadow_x"]), int(world["shadow_y"])
            wx, wy = int(world["x"]), int(world["y"])
            ox, oy = int(origin["x"]), int(origin["y"])
            if (ox & 0x1FF) or (oy & 0xFF):
                raise ValueError(f"frame {row['frame']} layer {layer}: unsafe alignment")
            if wx - ox != sx or wy - oy != sy:
                raise ValueError(f"frame {row['frame']} layer {layer}: projection mismatch")
            if sx + 256 + 64 >= 4096 * 8 or sy + 224 + 8 >= 512 * 8:
                raise ValueError(f"frame {row['frame']} layer {layer}: local key out of bounds")
            if ((wx // 256) & 1) != ((sx // 256) & 1):
                raise ValueError(f"frame {row['frame']} layer {layer}: X parity changed")
            if ((wy // 8) & 31) != ((sy // 8) & 31):
                raise ValueError(f"frame {row['frame']} layer {layer}: Y row changed")
            if wx // 8 >= 4096 or wy // 8 >= 512:
                high_world += 1
            current.append((ox, oy))
        if identities[identity] and identities[identity][-1] != tuple(current):
            raise ValueError(f"frame {row['frame']}: origin drift within identity {identity}")
        if not identities[identity]:
            identities[identity].append(tuple(current))
        terrain = int(row["ppu"]["terrain_layer"])
        if terrain in (0, 1):
            delta = row["shadow_delta"][terrain]
            terrain_hits += int(delta["west_hit"]) + int(delta["east_hit"])
            if int(delta["west_miss"]) or int(delta["east_miss"]):
                raise ValueError(f"frame {row['frame']}: terrain margin miss")
    if checked == 0:
        raise ValueError("no extended frames")
    if high_world == 0:
        raise ValueError("trace does not exercise a formerly out-of-range world key")
    if terrain_hits == 0:
        raise ValueError("no terrain margin hits")
    return {
        "schema": "dkc1.shadow-localization-contract.v1",
        "status": "pass",
        "trace": str(path.resolve()),
        "frames": len(frames),
        "extended_frames": checked,
        "high_world_layer_samples": high_world,
        "terrain_margin_hits": terrain_hits,
        "identity_count": len(identities),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
