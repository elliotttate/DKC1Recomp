#!/usr/bin/env python3
"""Evidence-generated per-scene widescreen capability manifest.

Aggregates the sweep report and per-route widescreen traces into one
manifest keyed by scene identity (mode, entrance, source bank/map):
what layout the scene runs, whether host widescreen is PROVEN there,
fallback/blank counts, and the routes providing the evidence. Runtime policy
and humans alike should consult proven capabilities instead of guessing from
which shared routine executed.

Statuses are strictly evidence-based:
  proven    — a successful route observed widened frames; every widened frame
              was calibrated; and there were zero raw fallbacks, blank serves,
              gameplay pillarbox frames, or unstable-margin frames
  degraded  — widened evidence exists but any proven gate above failed
  centered  — scene only ever ran pillarboxed
  unproven  — no successful route supplies evidence for this scene

The sweep's aggregate ``blank_serves`` counter does not preserve the tile-level
proof that a blank was authored/transparent. Consequently the only defensible
promotion gate at this layer is zero. A future evidence format may relax that
gate only by carrying an explicit verified-blank oracle, not a rate heuristic.

usage: python tools/capability_manifest.py [--out docs/CAPABILITIES.json]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

METRICS = (
    "frames", "widened", "calibrated", "raw_fallbacks", "blank_serves",
    "centered_in_gameplay", "unstable_margin_frames",
)


def _count(stats: dict, key: str) -> int:
    value = stats.get(key, 0)
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ValueError(f"scene metric {key!r} must be a non-negative integer")
    return value


def aggregate_scenes(sweep: dict) -> dict[str, dict]:
    """Aggregate only successful route evidence, retaining rejected sources."""
    scenes: dict[str, dict] = {}
    routes = sweep.get("routes", {})
    if not isinstance(routes, dict):
        raise ValueError("sweep report routes must be an object")
    for route, entry in routes.items():
        if not isinstance(entry, dict):
            raise ValueError(f"route {route!r} must be an object")
        route_ok = entry.get("exit_code") == 0
        route_scenes = entry.get("scenes", {})
        if not isinstance(route_scenes, dict):
            raise ValueError(f"route {route!r} scenes must be an object")
        for scene_key, stats in route_scenes.items():
            if not isinstance(stats, dict):
                raise ValueError(
                    f"route {route!r} scene {scene_key!r} must be an object")
            scene = scenes.setdefault(scene_key, {
                "scene": scene_key,
                **{metric: 0 for metric in METRICS},
                "routes": [],
                "rejected_routes": [],
            })
            # Validate rejected evidence too: malformed counters must never be
            # hidden merely because the producing route also failed.
            counts = {metric: _count(stats, metric) for metric in METRICS}
            if not route_ok:
                scene["rejected_routes"].append({
                    "route": route,
                    "exit_code": entry.get("exit_code"),
                })
                continue
            for metric, value in counts.items():
                scene[metric] += value
            scene["routes"].append(route)
    return scenes


def classify_scene(scene: dict) -> tuple[str, list[str]]:
    """Return a fail-closed status and explicit promotion blockers."""
    if not scene["routes"]:
        return "unproven", ["no_successful_route_evidence"]
    if scene["widened"] == 0:
        return "centered", ["no_widened_frames"]

    blockers = []
    if scene["calibrated"] != scene["widened"]:
        blockers.append("not_all_widened_frames_calibrated")
    if scene["raw_fallbacks"]:
        blockers.append("raw_fallbacks")
    if scene["blank_serves"]:
        blockers.append("blank_serves_without_verified_blank_oracle")
    if scene["centered_in_gameplay"]:
        blockers.append("pillarbox_in_gameplay")
    if scene["unstable_margin_frames"]:
        blockers.append("unstable_margins")
    return ("degraded", blockers) if blockers else ("proven", [])


def build_manifest(sweep: dict) -> dict:
    scenes = aggregate_scenes(sweep)
    manifest = []
    for key, scene in sorted(scenes.items()):
        status, blockers = classify_scene(scene)
        manifest.append({
            "scene": key,  # (mode, entrance, source bank, source map)
            "host_widescreen": status,
            "observed_frames": scene["frames"],
            "widened_frames": scene["widened"],
            "calibrated_frames": scene["calibrated"],
            "raw_fallbacks": scene["raw_fallbacks"],
            "blank_serves": scene["blank_serves"],
            "pillarbox_in_gameplay_frames": scene["centered_in_gameplay"],
            "unstable_margin_frames": scene["unstable_margin_frames"],
            "promotion_blockers": blockers,
            "evidence_routes": sorted(set(scene["routes"])),
            "rejected_evidence_routes": sorted(
                scene["rejected_routes"], key=lambda item: item["route"]),
        })
    return {
        "schema": "dkc1.capabilities.v1",
        "note": (
            "Evidence-based only. PROVEN requires a successful route, fully "
            "calibrated widened frames, and zero raw fallback, blank serve, "
            "gameplay pillarbox, or unstable-margin evidence. Aggregate blank "
            "counts have no verified-blank oracle, so any nonzero count blocks "
            "promotion. Scenes absent here are UNPROVEN."
        ),
        "scenes": manifest,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sweep", type=Path,
                        default=REPO / "build/sweep/report.json")
    parser.add_argument("--out", type=Path,
                        default=REPO / "docs/CAPABILITIES.json")
    args = parser.parse_args()

    try:
        sweep = json.loads(args.sweep.read_text())
    except OSError:
        raise SystemExit(f"no sweep report at {args.sweep}; run "
                         "tools/level_sweep.py first")

    output = build_manifest(sweep)
    manifest = output["scenes"]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(output, indent=1) + "\n",
                        encoding="utf-8")
    proven = sum(1 for s in manifest if s["host_widescreen"] == "proven")
    print(f"{len(manifest)} scenes -> {args.out} "
          f"({proven} proven, "
          f"{sum(1 for s in manifest if s['host_widescreen'] == 'degraded')}"
          f" degraded, "
          f"{sum(1 for s in manifest if s['host_widescreen'] == 'centered')}"
          f" centered-only)")
    for s in manifest:
        if s["widened_frames"]:
            print(f"  {s['scene']}: {s['host_widescreen']} "
                  f"({s['widened_frames']} widened, "
                  f"{s['raw_fallbacks']} raw, {s['blank_serves']} blank) "
                  f"via {', '.join(s['evidence_routes'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
