#!/usr/bin/env python3
"""Classify isolated placed-actor update write sets for margin proxies.

The native runner emits ``dkc1.prefetch-transaction.v1`` rows immediately
before it rolls a guarded early actor update back.  This tool groups those
rows by authored source record and sprite ID.  A presentation proxy is only a
candidate when every observed write is confined to that actor's own indexed
state or OAM; bookkeeping, other-actor, global, and truncated evidence fail
closed.
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path
from typing import Iterable


SCHEMA = "dkc1.prefetch-transaction.v1"
OUTPUT_SCHEMA = "dkc1.prefetch-write-set-analysis.v1"
DOMAINS = ("own_actor", "other_actor", "oam", "bookkeeping", "scratch",
           "global")


def load_rows(paths: Iterable[Path]) -> list[dict]:
    rows: list[dict] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as handle:
            for line_no, line in enumerate(handle, 1):
                if not line.strip():
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError as exc:
                    raise ValueError(f"{path}:{line_no}: {exc}") from exc
                if row.get("schema") == SCHEMA:
                    rows.append(row)
    return rows


def verdict_for(domains: dict[str, int], truncated: bool,
                samples: int) -> str:
    if samples == 0:
        return "insufficient_evidence"
    if truncated:
        return "truncated_fail_closed"
    if domains.get("other_actor", 0):
        return "cross_actor_side_effects"
    if domains.get("global", 0):
        return "global_gameplay_side_effects"
    if domains.get("bookkeeping", 0):
        return "object_bookkeeping_side_effects"
    if (domains.get("own_actor", 0) or domains.get("oam", 0) or
            domains.get("scratch", 0)):
        return "presentation_proxy_candidate"
    return "no_observed_writes"


def analyze(rows: Iterable[dict]) -> dict:
    groups: dict[tuple[int, int, int, int, int], list[dict]] = defaultdict(list)
    for row in rows:
        if row.get("schema") != SCHEMA or row.get("event") != "write_set":
            continue
        groups[(int(row.get("mode", -1)), int(row.get("level", -1)),
                int(row.get("entrance", -1)), int(row["id"]),
                int(row["source"]))].append(row)

    actors = []
    verdict_counts: dict[str, int] = defaultdict(int)
    for (mode, level, entrance, sprite_id, source), samples in sorted(
            groups.items()):
        totals = {domain: 0 for domain in DOMAINS}
        changed = 0
        truncated = False
        frames = []
        actor_indices = set()
        for sample in samples:
            changed += int(sample.get("changed_bytes", 0))
            truncated |= bool(sample.get("offsets_truncated"))
            frames.append(int(sample.get("frame", -1)))
            actor_indices.add(int(sample.get("actor_index", -1)))
            domains = sample.get("domains", {})
            for domain in DOMAINS:
                totals[domain] += int(domains.get(domain, 0))
        verdict = verdict_for(totals, truncated, len(samples))
        verdict_counts[verdict] += 1
        actors.append({
            "mode": mode,
            "level": level,
            "entrance": entrance,
            "id": sprite_id,
            "source": source,
            "samples": len(samples),
            "frames": sorted(frames),
            "actor_indices": sorted(actor_indices),
            "changed_bytes": changed,
            "domains": totals,
            "offsets_truncated": truncated,
            "verdict": verdict,
            "candidate": verdict == "presentation_proxy_candidate",
        })

    return {
        "schema": OUTPUT_SCHEMA,
        "events": sum(len(samples) for samples in groups.values()),
        "actors": actors,
        "summary": {
            "authored_actor_records": len(actors),
            "presentation_proxy_candidates": sum(
                actor["candidate"] for actor in actors),
            "verdicts": dict(sorted(verdict_counts.items())),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lifecycle", type=Path, nargs="+")
    parser.add_argument("--json-out", type=Path, required=True)
    args = parser.parse_args()

    result = analyze(load_rows(args.lifecycle))
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(result, indent=2) + "\n",
                             encoding="utf-8")
    print(json.dumps(result["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
