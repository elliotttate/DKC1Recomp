#!/usr/bin/env python3
"""Reorganize a DKC1_LIFECYCLE_TRACE by authored SOURCE RECORD.

Actor slots are mutable scratch: the same enemy can occupy different slots
across activations, and slot-keyed reading misattributes lifecycle events.
This tool re-keys every event by the authored source record (the $15FD
backlink) and emits, per source: the alloc/state/retype/free timeline with
world positions and camera context, reallocation churn, slot migration,
and two suspicion flags:

  freed_in_view      the actor was released while its last known world X
                     was inside the (wide) viewport and away from level
                     bounds — the wrongful-cull signature;
  thrash             alloc/free cycling faster than the activation window
                     plausibly explains (>= --thrash-limit cycles).

Effects/reserved slots (source <= 0) are summarized separately and never
attributed to authored records.
"""
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--wide-extra", type=int, default=43,
                        help="margin pixels per side for the in-view test")
    parser.add_argument("--edge-slack", type=int, default=96,
                        help="distance from camera bounds treated as the "
                             "level edge (legitimate cull region)")
    parser.add_argument("--thrash-limit", type=int, default=6)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    sources: dict[int, dict] = defaultdict(lambda: {
        "events": 0, "allocs": 0, "frees": 0, "slots": set(),
        "first_frame": None, "last_frame": None, "last_alloc": None,
        "timeline": [], "freed_in_view": [], "ids": set(),
    })
    nonauthored = {"events": 0, "allocs": 0, "frees": 0}
    bounds = [0, 0]

    for line in args.trace.read_text(errors="replace").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        kind = event.get("event")
        if kind == "gameplay_enter":
            bounds = event.get("bounds", [0, 0])
            continue
        if kind not in ("actor_alloc", "actor_free", "actor_retype",
                        "actor_state", "actor_sample"):
            continue
        source = int(event.get("source", event.get("prev_source", 0)))
        if source <= 0:
            nonauthored["events"] += 1
            if kind == "actor_alloc":
                nonauthored["allocs"] += 1
            elif kind == "actor_free":
                nonauthored["frees"] += 1
            continue
        record = sources[source]
        record["events"] += 1
        frame = event.get("frame")
        if record["first_frame"] is None:
            record["first_frame"] = frame
        record["last_frame"] = frame
        record["slots"].add(event.get("pool_ordinal"))
        if "id" in event:
            record["ids"].add(event["id"])
        if kind == "actor_alloc":
            record["allocs"] += 1
            record["last_alloc"] = event
            record["timeline"].append(
                {"frame": frame, "event": "alloc",
                 "slot": event.get("pool_ordinal"),
                 "x": event.get("x"), "y": event.get("y"),
                 "camera": event.get("camera")})
        elif kind == "actor_free":
            record["frees"] += 1
            last = record["last_alloc"] or {}
            # the free event has no position; the last known position and
            # camera from this source's own preceding events stand in.
            prior = [t for t in record["timeline"]
                     if t.get("x") is not None]
            known = prior[-1] if prior else {}
            record["timeline"].append(
                {"frame": frame, "event": "free",
                 "slot": event.get("pool_ordinal"),
                 "last_known_x": known.get("x"),
                 "last_camera": known.get("camera")})
            camera = known.get("camera")
            x = known.get("x")
            if camera and x is not None:
                rel = (x - camera[0]) & 0xFFFF
                if rel >= 0x8000:
                    rel -= 0x10000
                in_wide = -args.wide_extra <= rel < 256 + args.wide_extra
                near_edge = (
                    camera[0] <= bounds[0] + args.edge_slack or
                    camera[0] >= max(0, bounds[1] - args.edge_slack))
                if in_wide and not near_edge:
                    record["freed_in_view"].append(
                        {"frame": frame, "rel_x": rel, "x": x,
                         "camera_x": camera[0]})
        else:
            record["timeline"].append(
                {"frame": frame, "event": kind.replace("actor_", ""),
                 "slot": event.get("pool_ordinal"),
                 "x": event.get("x"), "y": event.get("y"),
                 "state": event.get("state"),
                 "camera": event.get("camera")})

    report = {"sources": {}, "nonauthored": nonauthored}
    flagged = 0
    for source, record in sorted(sources.items()):
        summary = {
            "events": record["events"],
            "allocs": record["allocs"],
            "frees": record["frees"],
            "slots_used": sorted(s for s in record["slots"]
                                 if s is not None),
            "ids": sorted(record["ids"]),
            "first_frame": record["first_frame"],
            "last_frame": record["last_frame"],
            "freed_in_view": record["freed_in_view"][:8],
            "thrash": record["allocs"] >= args.thrash_limit,
            "timeline_tail": record["timeline"][-12:],
        }
        report["sources"][str(source)] = summary
        flags = []
        if summary["freed_in_view"]:
            flags.append(
                f"FREED-IN-VIEW x{len(record['freed_in_view'])}")
            flagged += 1
        if summary["thrash"]:
            flags.append(f"THRASH({record['allocs']} allocs)")
            flagged += 1
        if flags or record["allocs"] > 1:
            print(f"source {source}: allocs={record['allocs']} "
                  f"frees={record['frees']} "
                  f"slots={summary['slots_used']} {' '.join(flags)}")

    print(f"{len(sources)} authored sources, "
          f"{nonauthored['events']} non-authored events, "
          f"{flagged} flagged")
    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
