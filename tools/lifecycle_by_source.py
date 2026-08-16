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
    parser.add_argument("--html-out", type=Path,
                        help="per-object visibility timeline (swimlanes)")
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
    if args.html_out:
        write_timeline_html(args.html_out, sources)
        print(f"timeline: {args.html_out}")
    return 0


def write_timeline_html(out, sources):
    """Object visibility timeline: one swimlane per authored source.
    Alloc..free episodes are bars; state changes are ticks; freed-in-view
    releases are red markers. Hover any element for exact frames."""
    frames = [t["frame"] for rec in sources.values()
              for t in rec["timeline"] if t.get("frame") is not None]
    if not frames:
        out.write_text("<p>no lifecycle events</p>")
        return
    lo, hi = min(frames), max(frames)
    span = max(1, hi - lo)

    def pct(frame):
        return 100.0 * (frame - lo) / span

    rows = []
    for source, rec in sorted(sources.items()):
        episodes = []
        open_frame = None
        for t in rec["timeline"]:
            if t["event"] == "alloc":
                open_frame = t["frame"]
            elif t["event"] == "free":
                episodes.append((open_frame if open_frame is not None
                                 else lo, t["frame"]))
                open_frame = None
        if open_frame is not None:
            episodes.append((open_frame, hi))
        bars = "".join(
            f'<div class="ep" style="left:{pct(a):.2f}%;'
            f'width:{max(0.15, pct(b) - pct(a)):.2f}%"'
            f' title="alloc f{a} .. free f{b}"></div>'
            for a, b in episodes)
        ticks = "".join(
            f'<div class="tick" style="left:{pct(t["frame"]):.2f}%"'
            f' title="{t["event"]} f{t["frame"]}"></div>'
            for t in rec["timeline"] if t["event"] == "state")
        marks = "".join(
            f'<div class="bad" style="left:{pct(m["frame"]):.2f}%"'
            f' title="freed in view f{m["frame"]} rel_x={m["rel_x"]}">'
            "</div>"
            for m in rec["freed_in_view"])
        label = (f"src {source} (ids {','.join(map(str, sorted(rec['ids'])))}"
                 f", slots {sorted(s for s in rec['slots'] if s is not None)})")
        rows.append(f'<div class="lane"><span class="name">{label}</span>'
                    f'<div class="track">{bars}{ticks}{marks}</div></div>')

    html = ("<!-- generated by lifecycle_by_source.py --html-out -->\n"
            "<meta charset='utf-8'><title>DKC1 object visibility"
            " timeline</title><style>"
            "body{background:#121519;color:#dee6ee;font:13px monospace}"
            ".lane{display:flex;align-items:center;margin:3px 0}"
            ".name{width:340px;flex:none;overflow:hidden;"
            "text-overflow:ellipsis;white-space:nowrap}"
            ".track{position:relative;height:16px;flex:1;"
            "background:#1a1e24;border-radius:3px}"
            ".ep{position:absolute;top:2px;bottom:2px;background:#2f6f3f;"
            "border-radius:2px}"
            ".tick{position:absolute;top:4px;bottom:4px;width:1px;"
            "background:#8fb8ff}"
            ".bad{position:absolute;top:0;bottom:0;width:3px;"
            "background:#e5484d}"
            "</style>"
            f"<h3>Object visibility timeline — frames {lo}..{hi}</h3>"
            "<p>green = allocated episode; blue tick = state change; "
            "red = freed while inside the wide viewport (triage)</p>"
            + "".join(rows))
    out.write_text(html, encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
