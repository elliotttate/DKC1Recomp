#!/usr/bin/env python3
"""Audit simulation-neutral widescreen actor-prefetch transactions.

Compare two byte-exact wide-mode WRAM timelines made from the same snapshot
and inputs: transaction disabled versus enabled. Differences are expected in
normal-actor state, object bookmarks, and presentation queues. Any difference
outside those domains is an escaped side effect and blocks acceptance.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from first_divergence import contiguous_ranges, load_wram_frames


DOMAINS = {
    "oam": ((0x00200, 0x00420),),
    "actor_state": ((0x00AE5, 0x01699),),
    "graphics_dma": ((0x0170F, 0x0178F),),
    "object_bookkeeping": ((0x0192B, 0x01A2B),),
    "sprite_palette": ((0x01A8F, 0x01B23),),
}


def domain_for(offset: int) -> str | None:
    for name, ranges in DOMAINS.items():
        if any(first <= offset < last for first, last in ranges):
            return name
    return None


def analyze_frames(baseline: dict[int, bytes], candidate: dict[int, bytes],
                   first: int | None = None,
                   last: int | None = None) -> dict:
    frames = sorted(frame for frame in set(baseline) & set(candidate)
                    if (first is None or frame >= first) and
                    (last is None or frame <= last))
    if not frames:
        raise ValueError("no shared WRAM frames")
    if set(baseline) != set(candidate):
        raise ValueError("baseline/candidate frame sets differ")

    domain_totals = {name: 0 for name in DOMAINS}
    unexpected_union: set[int] = set()
    rows = []
    first_unexpected = None
    for frame in frames:
        left = baseline[frame]
        right = candidate[frame]
        if len(left) != len(right):
            raise ValueError(f"frame {frame} payload lengths differ")
        changed = [index for index, pair in enumerate(zip(left, right))
                   if pair[0] != pair[1]]
        counts = {name: 0 for name in DOMAINS}
        unexpected = []
        for offset in changed:
            domain = domain_for(offset)
            if domain is None:
                unexpected.append(offset)
                unexpected_union.add(offset)
            else:
                counts[domain] += 1
                domain_totals[domain] += 1
        if unexpected and first_unexpected is None:
            first_unexpected = {
                "frame": frame,
                "ranges": contiguous_ranges(unexpected),
            }
        if changed:
            rows.append({
                "frame": frame,
                "changed_bytes": len(changed),
                "domains": {key: value for key, value in counts.items()
                            if value},
                "unexpected_bytes": len(unexpected),
                "unexpected_ranges": contiguous_ranges(unexpected),
            })

    return {
        "schema": "dkc1.prefetch-transaction-audit.v1",
        "frames": [frames[0], frames[-1]],
        "frames_compared": len(frames),
        "frames_with_differences": len(rows),
        "domain_difference_samples": domain_totals,
        "unexpected_union": contiguous_ranges(sorted(unexpected_union)),
        "first_unexpected": first_unexpected,
        "accepted": first_unexpected is None,
        "changed_frames": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path,
                        help="raw dump prefix or .bin path, transaction off")
    parser.add_argument("candidate", type=Path,
                        help="raw dump prefix or .bin path, transaction on")
    parser.add_argument("--first", type=int)
    parser.add_argument("--last", type=int)
    parser.add_argument("--json-out", type=Path, required=True)
    args = parser.parse_args()

    def prefix(path: Path) -> Path:
        path = path.expanduser().resolve()
        return path.with_suffix("") if path.suffix == ".bin" else path

    result = analyze_frames(load_wram_frames(prefix(args.baseline)),
                            load_wram_frames(prefix(args.candidate)),
                            args.first, args.last)
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(result, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps({key: result[key] for key in (
        "frames_compared", "frames_with_differences", "first_unexpected",
        "accepted")}, indent=2))
    return 0 if result["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
