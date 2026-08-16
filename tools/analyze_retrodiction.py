#!/usr/bin/env python3
"""Cluster SNESRECOMP_WS_RETRODICT mismatch events.

Each event is a PROVEN wrong margin serve: a generated (ROM-decoded or
guessed) tilemap entry that was shown to the player and later contradicted
by the game's own native stream for the same world tile. Clustering by the
served->actual XOR separates attribute-byte wrongness (palette/priority/
flip decode bugs) from wrong-tile wrongness (world-key or stale-cache
bugs); per-column groupings identify the metatiles to fix.
"""
from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    rows = []
    for line in args.log.read_text(errors="replace").splitlines():
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    if not rows:
        print("no retrodiction mismatches — margins were stream-consistent")
        return 0

    print(f"{len(rows)} mismatches, frames {rows[0]['frame']}.."
          f"{rows[-1]['frame']}")
    attribute_only = sum(
        1 for r in rows if (r["served"] ^ r["actual"]) & 0x03FF == 0)
    print(f"attribute-byte-only (palette/priority/flip/tile-high): "
          f"{attribute_only} ({100 * attribute_only // len(rows)}%)")

    pairs = Counter((r["served"], r["actual"]) for r in rows)
    print("top served->actual pairs:")
    for (served, actual), count in pairs.most_common(12):
        print(f"  {served:#06x} -> {actual:#06x}  x{count}  "
              f"(xor {served ^ actual:#06x})")

    columns = defaultdict(int)
    for r in rows:
        columns[(r["layer"], r["tile_x"])] += 1
    worst = sorted(columns.items(), key=lambda kv: -kv[1])[:8]
    print("worst columns (layer, world tile x):")
    for (layer, tile_x), count in worst:
        print(f"  layer {layer} tx={tile_x}  x{count}")

    if args.json_out:
        args.json_out.write_text(json.dumps({
            "events": len(rows),
            "attribute_only": attribute_only,
            "top_pairs": [
                {"served": s, "actual": a, "count": c}
                for (s, a), c in pairs.most_common(20)],
            "worst_columns": [
                {"layer": layer, "tile_x": tx, "count": c}
                for (layer, tx), c in worst],
        }, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
