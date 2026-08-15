#!/usr/bin/env python3
"""Classify wide-vs-stock object activation differences by lifecycle episode.

Consumes two DKC1_LIFECYCLE_TRACE captures of the SAME resolved input
schedule (see tools/first_divergence.py stage 0) run under DKC1_WIDESCREEN=0
and =1. Actors are aligned by SOURCE RECORD, never by pool slot — slot
reallocation must not read as a missing enemy (SuperZSNES rule).

Episode = one contiguous allocation of a source record. For each record the
report compares, at the frame the STOCK run first allocates it:

  - whether the wide run had already allocated it (prefetch), and how early;
  - identity, position, motion, state, and animation of the wide actor at
    that exact stock-allocation frame (from the nearest wide keyframe or
    transition at/before the frame);
  - the honest verdict vocabulary from the SuperZSNES auditor:
      matched                     same episode timing, no differences
      harmless_visual_prefetch    early alloc, but state matches at stock t0
      behavior_phase_advancement  early alloc AND state/position differs
      wide_persists_stock_culls   wide kept an actor stock released
                                  (disposition: indeterminate, NOT harmless)
      stock_only / wide_only      allocation missing on one side
      indeterminate_without_stock_allocation
                                  wide allocated, stock never did (within
                                  the compared window)

For byte-exact field comparison beyond the trace fields, re-run the wide
route with DKC1_WRAM_DUMP around the reported frames and inspect the actor
arrays directly; this tool reports which frames need that pass.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


def load_events(path: Path) -> list[dict]:
    events = []
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(record, dict) and "event" in record:
            events.append(record)
    return events


class Episode:
    def __init__(self, source: int, start: int, alloc: dict):
        self.source = source
        self.start = start
        self.end: int | None = None
        self.alloc = alloc
        self.states: list[dict] = [alloc]

    def state_at(self, frame: int) -> dict | None:
        best = None
        for state in self.states:
            if state["frame"] <= frame:
                best = state
            else:
                break
        return best


def build_episodes(events: list[dict]) -> dict[int, list[Episode]]:
    open_by_index: dict[int, Episode] = {}
    episodes: dict[int, list[Episode]] = defaultdict(list)
    for event in events:
        kind = event["event"]
        if kind in ("actor_alloc", "slot_alloc", "actor_retype",
                    "slot_retype"):
            index = event.get("actor_index", event.get("slot"))
            source = event.get("source")
            if index is None or source is None:
                continue
            previous = open_by_index.pop(index, None)
            if previous is not None:
                previous.end = event["frame"]
            episode = Episode(source, event["frame"], event)
            open_by_index[index] = episode
            episodes[source].append(episode)
        elif kind in ("actor_state", "slot_state"):
            index = event.get("actor_index", event.get("slot"))
            episode = open_by_index.get(index)
            if episode is not None:
                episode.states.append(event)
        elif kind in ("actor_free", "slot_free"):
            index = event.get("actor_index", event.get("slot"))
            episode = open_by_index.pop(index, None)
            if episode is not None:
                episode.end = event["frame"]
        elif kind == "gameplay_exit":
            for episode in open_by_index.values():
                episode.end = event["frame"]
            open_by_index.clear()
    return episodes


COMPARE_FIELDS = ("id", "state", "anim", "pose", "x", "y", "xs", "ys")


def compare_states(stock: dict, wide: dict) -> dict:
    return {
        field: {"stock": stock.get(field), "wide": wide.get(field)}
        for field in COMPARE_FIELDS
        if stock.get(field) != wide.get(field)
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path)
    parser.add_argument("wide", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    stock_eps = build_episodes(load_events(args.stock))
    wide_eps = build_episodes(load_events(args.wide))

    records = sorted(set(stock_eps) | set(wide_eps))
    findings = []
    needs_wram_pass: set[int] = set()
    for source in records:
        stock_list = stock_eps.get(source, [])
        wide_list = wide_eps.get(source, [])
        count = max(len(stock_list), len(wide_list))
        for ordinal in range(count):
            stock_ep = stock_list[ordinal] if ordinal < len(stock_list) \
                else None
            wide_ep = wide_list[ordinal] if ordinal < len(wide_list) else None
            finding = {"source": source, "episode": ordinal}
            if stock_ep is None and wide_ep is not None:
                finding["verdict"] = "indeterminate_without_stock_allocation"
                finding["wide_start"] = wide_ep.start
            elif wide_ep is None and stock_ep is not None:
                finding["verdict"] = "stock_only"
                finding["stock_start"] = stock_ep.start
            else:
                lead = stock_ep.start - wide_ep.start
                finding["stock_start"] = stock_ep.start
                finding["wide_start"] = wide_ep.start
                finding["wide_lead_frames"] = lead
                wide_state = wide_ep.state_at(stock_ep.start)
                differences = compare_states(stock_ep.alloc,
                                             wide_state or wide_ep.alloc)
                # release/persistence comparison
                stock_end = stock_ep.end
                wide_end = wide_ep.end
                persists = (stock_end is not None and
                            (wide_end is None or wide_end > stock_end + 2))
                if lead == 0 and not differences and not persists:
                    finding["verdict"] = "matched"
                elif persists:
                    finding["verdict"] = "wide_persists_stock_culls"
                    finding["disposition"] = "indeterminate"
                    finding["stock_end"] = stock_end
                    finding["wide_end"] = wide_end
                    needs_wram_pass.add(stock_end or stock_ep.start)
                elif lead > 0 and not differences:
                    finding["verdict"] = "harmless_visual_prefetch"
                elif lead > 0:
                    finding["verdict"] = "behavior_phase_advancement"
                    finding["differences_at_stock_alloc"] = differences
                    needs_wram_pass.add(stock_ep.start)
                elif differences:
                    finding["verdict"] = "behavior_phase_difference"
                    finding["differences_at_stock_alloc"] = differences
                    needs_wram_pass.add(stock_ep.start)
                else:
                    finding["verdict"] = "matched_late" if lead < 0 \
                        else "matched"
                    if lead < 0:
                        needs_wram_pass.add(stock_ep.start)
            findings.append(finding)

    verdicts = defaultdict(int)
    for finding in findings:
        verdicts[finding["verdict"]] += 1
    report = {
        "records": len(records),
        "episodes": len(findings),
        "verdicts": dict(verdicts),
        "wram_pass_frames": sorted(needs_wram_pass)[:32],
        "findings": [f for f in findings if f["verdict"] != "matched"][:64],
    }
    text = json.dumps(report, indent=1)
    print(text)
    if args.json_out:
        args.json_out.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
