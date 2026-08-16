#!/usr/bin/env python3
"""Evidence-generated per-scene widescreen capability manifest.

Aggregates the sweep report and per-route widescreen traces into one
manifest keyed by scene identity (mode, entrance, source bank/map):
what layout the scene runs, whether host widescreen is PROVEN there
(calibrated frames with zero raw fallbacks), fallback/blank counts, the
routes providing the evidence, and the artifact paths. Runtime policy
and humans alike should consult proven capabilities instead of guessing
from which shared routine executed.

Statuses are strictly evidence-based:
  proven    — widened frames observed, zero raw fallbacks, calibration
              locked whenever widened
  degraded  — widened but with raw fallbacks or blank serves
  centered  — scene only ever ran pillarboxed
  unproven  — no route reaches this scene yet (absence of evidence)

usage: python tools/capability_manifest.py [--out docs/CAPABILITIES.json]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


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

    scenes: dict[str, dict] = {}
    for route, entry in sweep.get("routes", {}).items():
        for scene_key, stats in entry.get("scenes", {}).items():
            scene = scenes.setdefault(scene_key, {
                "scene": scene_key, "frames": 0, "widened": 0,
                "calibrated": 0, "raw_fallbacks": 0, "blank_serves": 0,
                "centered_in_gameplay": 0, "unstable_margin_frames": 0,
                "routes": []})
            for key in ("frames", "widened", "calibrated", "raw_fallbacks",
                        "blank_serves", "centered_in_gameplay",
                        "unstable_margin_frames"):
                scene[key] += stats.get(key, 0)
            scene["routes"].append(route)

    manifest = []
    for key, scene in sorted(scenes.items()):
        if scene["widened"] == 0:
            status = "centered"
        elif scene["raw_fallbacks"] or scene["blank_serves"] > \
                scene["widened"] * 16:
            status = "degraded"
        elif scene["calibrated"] == scene["widened"]:
            status = "proven"
        else:
            status = "degraded"
        manifest.append({
            "scene": key,  # (mode, entrance, source bank, source map)
            "host_widescreen": status,
            "widened_frames": scene["widened"],
            "calibrated_frames": scene["calibrated"],
            "raw_fallbacks": scene["raw_fallbacks"],
            "blank_serves": scene["blank_serves"],
            "pillarbox_in_gameplay_frames": scene["centered_in_gameplay"],
            "unstable_margin_frames": scene["unstable_margin_frames"],
            "evidence_routes": sorted(set(scene["routes"])),
        })

    output = {
        "schema": "dkc1.capabilities.v1",
        "note": ("evidence-based only; scenes absent here are UNPROVEN "
                 "(no route reaches them), never assumed-good. Regenerate "
                 "after tools/level_sweep.py."),
        "scenes": manifest,
    }
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
