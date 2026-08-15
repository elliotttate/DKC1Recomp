#!/usr/bin/env python3
"""Sweep routes and grade widescreen health per scene from the WS trace.

The emulator effort's biggest admitted gap was a "walk every level and diff
against stock" harness. The recomp runs uncapped, so sweeping is cheap:
for every route in a directory, run wide with DKC1_WS_TRACE (and optionally
a stock twin), then grade each visited scene identity:

  - calibration: fraction of widened frames with a locked layout;
  - raw fallbacks: WsShadow margin reads served from wrapped VRAM (the
    dangerous source) — should be zero;
  - centered fallbacks inside gameplay (flip-flopping into pillarbox);
  - margin instability: margin-hash changes on frames whose VRAM and OAM
    hashes did not change (open issue 1's signature);
  - completion: script waits must not time out.

Routes are .dks scripts in the routes directory (default recipes/). Add a
route per level as they are recorded; the sweep grades whatever exists and
lists coverage so missing levels are visible, not silently skipped.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
from collections import defaultdict
from pathlib import Path


def run_route(exe: Path, rom: Path, script: Path, frames: int,
              work: Path) -> tuple[int, Path]:
    trace = work / f"{script.stem}_ws.jsonl"
    env = os.environ.copy()
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env["DKC1_WIDESCREEN"] = "1"
    env["DKC1_SCRIPT"] = str(script.resolve())
    env["DKC1_WS_TRACE"] = str(trace.resolve())
    env["DKC1_SESSION_DIR"] = str((work / f"{script.stem}_session").resolve())
    result = subprocess.run([str(exe), str(rom), str(frames)],
                            cwd=str(work), env=env,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    return result.returncode, trace


def grade(trace_path: Path) -> dict:
    scenes: dict[tuple, dict] = defaultdict(lambda: {
        "frames": 0, "widened": 0, "calibrated": 0, "raw_fallbacks": 0,
        "centered_in_gameplay": 0, "unstable_margin_frames": 0,
    })
    previous = None
    for line in trace_path.read_text(errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        scene_key = (record.get("scene", {}).get("mode"),
                     record.get("scene", {}).get("entrance"),
                     record.get("source", {}).get("bank"),
                     record.get("source", {}).get("map"))
        scene = scenes[scene_key]
        scene["frames"] += 1
        decision = record.get("decision", {})
        ppu = record.get("ppu", {})
        calibration = record.get("calibration", {})
        widened = bool(ppu.get("wide_mask")) and \
            not decision.get("centered_fallback")
        if widened:
            scene["widened"] += 1
            if calibration.get("selected"):
                scene["calibrated"] += 1
            for delta in record.get("shadow_delta", []):
                scene["raw_fallbacks"] += delta.get("west_raw", 0)
                scene["raw_fallbacks"] += delta.get("east_raw", 0)
        elif decision.get("centered_fallback") and \
                record.get("camera", {}).get("upper", 0) > 0x100:
            scene["centered_in_gameplay"] += 1
        hashes = record.get("hash", {})
        if previous is not None and widened:
            same_inputs = (hashes.get("vram") == previous.get("vram") and
                           hashes.get("ppu_oam") == previous.get("ppu_oam"))
            margins_changed = (
                hashes.get("left") != previous.get("left") or
                hashes.get("right") != previous.get("right"))
            center_same = hashes.get("center") == previous.get("center")
            if same_inputs and center_same and margins_changed:
                scene["unstable_margin_frames"] += 1
        previous = hashes
    return {str(k): v for k, v in scenes.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path,
                        default=Path("build/dkc1_headless_tools.exe"))
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--routes", type=Path, default=Path("recipes"))
    parser.add_argument("--frames", type=int, default=20000)
    parser.add_argument("--work", type=Path, default=Path("build/sweep"))
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    args.work.mkdir(parents=True, exist_ok=True)
    routes = sorted(args.routes.glob("*.dks"))
    if not routes:
        print(f"no .dks routes in {args.routes} — record some first")
        return 2

    report = {"routes": {}, "coverage_note":
              f"{len(routes)} routes swept; levels without a route are NOT "
              "covered — absence here is not a pass"}
    failures = 0
    for script in routes:
        code, trace = run_route(args.exe.resolve(), args.rom.resolve(),
                                script, args.frames, args.work)
        entry = {"exit_code": code}
        if trace.exists():
            entry["scenes"] = grade(trace)
            for scene, stats in entry["scenes"].items():
                bad = (stats["raw_fallbacks"] or
                       stats["unstable_margin_frames"] or
                       stats["centered_in_gameplay"])
                if bad:
                    failures += 1
        if code != 0:
            failures += 1
        report["routes"][script.name] = entry
        print(f"{script.name}: rc={code}")
        for scene, stats in entry.get("scenes", {}).items():
            flag = ""
            if stats["raw_fallbacks"]:
                flag += " RAW-FALLBACK"
            if stats["unstable_margin_frames"]:
                flag += " UNSTABLE-MARGIN"
            if stats["centered_in_gameplay"]:
                flag += " PILLARBOX-IN-GAMEPLAY"
            print(f"  scene {scene}: {stats['frames']}f "
                  f"widened={stats['widened']} "
                  f"calibrated={stats['calibrated']}{flag}")

    text = json.dumps(report, indent=1)
    if args.json_out:
        args.json_out.write_text(text)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
