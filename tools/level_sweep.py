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
              work: Path) -> tuple[int, Path, Path, Path]:
    trace = work / f"{script.stem}_ws.jsonl"
    cache_log = work / f"{script.stem}_cache.jsonl"
    oam_log = work / f"{script.stem}_oam"
    # The engine opens the cache log lazily on the FIRST event; a clean run
    # writes nothing, which must not leave a previous run's events behind.
    cache_log.unlink(missing_ok=True)
    env = os.environ.copy()
    env.pop("SNESRECOMP_INPUT_PLAY", None)
    env["DKC1_WIDESCREEN"] = "1"
    env["DKC1_SCRIPT"] = str(script.resolve())
    env["DKC1_WS_TRACE"] = str(trace.resolve())
    env["SNESRECOMP_WS_CACHE_LOG"] = str(cache_log.resolve())
    env["DKC1_OAM_LOG"] = str(oam_log.resolve())
    env["DKC1_SESSION_DIR"] = str((work / f"{script.stem}_session").resolve())
    result = subprocess.run([str(exe), str(rom), str(frames)],
                            cwd=str(work), env=env,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    return result.returncode, trace, cache_log, oam_log


def grade_cache_log(cache_log: Path) -> dict:
    """Scene-local cache-window events: out-of-range keys are content loss
    (the bonus-stage class); rebases are informational."""
    stats = {"oob_read": 0, "oob_write": 0, "rebase": 0, "first_oob": None}
    if not cache_log.exists():
        return stats
    for line in cache_log.read_text(errors="replace").splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        event = record.get("event", "")
        if event in ("oob_read", "oob_write"):
            stats[event] += 1
            if stats["first_oob"] is None:
                stats["first_oob"] = record
        elif event == "rebase":
            stats["rebase"] += 1
    return stats


def grade_oam_wrap(oam_bin: Path) -> dict:
    """OAM X-high wrap heuristic. The real lost-high-bit bug renders art
    intended for the right margin ([256, 256+extra), high bit set) at the
    far LEFT with the high bit clear. A sprite crossing x=255 into the
    signed-negative range is NOT flagged — that is the normal 9-bit
    boundary the wide renderer already understands. Non-gameplay frames
    (menus, fades) are skipped via the jsonl sidecar."""
    result = {"frames": 0, "wrap_suspects": 0, "first_suspect": None}
    if not oam_bin.exists():
        return result
    gameplay_frames = set()
    sidecar = Path(str(oam_bin)[:-4] + ".jsonl")
    if sidecar.exists():
        for line in sidecar.read_text(errors="replace").splitlines():
            try:
                meta = json.loads(line)
            except json.JSONDecodeError:
                continue
            if meta.get("gameplay") and not meta.get("forced_blank"):
                gameplay_frames.add(meta.get("frame"))
    data = oam_bin.read_bytes()
    record_size = 4 + 544 + 544
    previous = None
    offset = 0
    while offset + record_size <= len(data):
        frame = int.from_bytes(data[offset:offset + 4], "little")
        ppu = data[offset + 4 + 544:offset + record_size]
        sprites = []
        for i in range(128):
            x_low = ppu[i * 4]
            y = ppu[i * 4 + 1]
            high = (ppu[512 + i // 4] >> ((i % 4) * 2)) & 0x3
            sprites.append((x_low, high & 1, y))
        in_gameplay = not gameplay_frames or frame in gameplay_frames
        if previous is not None and in_gameplay and \
                previous[0] == frame - 1:
            for i, ((x_low, high, y), (px_low, phigh, py)) in enumerate(
                    zip(sprites, previous[1])):
                if y >= 0xF0 or py >= 0xF0:
                    continue  # offscreen parking rows
                # Right-margin art is x in [256, 256+extra): high bit SET,
                # low byte small. Losing the high bit keeps the low byte
                # continuous while teleporting the sprite to the far left —
                # that pair, same row, is the signature. Sprites crossing
                # x=0 or x=255 change the low byte too and are not flagged.
                if phigh and not high and px_low < 64 \
                        and abs(x_low - px_low) <= 8 and abs(y - py) <= 2:
                    result["wrap_suspects"] += 1
                    if result["first_suspect"] is None:
                        result["first_suspect"] = {
                            "frame": frame, "oam_index": i,
                            "previous_x": px_low | 256, "x": x_low, "y": y}
        previous = (frame, sprites)
        result["frames"] += 1
        offset += record_size
    return result


def grade(trace_path: Path) -> dict:
    scenes: dict[tuple, dict] = defaultdict(lambda: {
        "frames": 0, "widened": 0, "calibrated": 0, "raw_fallbacks": 0,
        "blank_serves": 0, "centered_in_gameplay": 0,
        "unstable_margin_frames": 0,
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
                scene["blank_serves"] += delta.get("west_blank", 0)
                scene["blank_serves"] += delta.get("east_blank", 0)
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
        first_op = next(
            (line.strip() for line in script.read_text().splitlines()
             if line.strip() and not line.strip().startswith("#")), "")
        if first_op.startswith("state_load"):
            report["routes"][script.name] = {"skipped": "dependent leg "
                                             "(needs a seeded state)"}
            print(f"{script.name}: skipped (dependent quickload leg)")
            continue
        code, trace, cache_log, oam_log = run_route(
            args.exe.resolve(), args.rom.resolve(),
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
        entry["cache"] = grade_cache_log(cache_log)
        if entry["cache"]["oob_read"] or entry["cache"]["oob_write"]:
            failures += 1
        entry["oam_wrap"] = grade_oam_wrap(
            Path(str(oam_log) + ".bin"))
        if entry["oam_wrap"]["wrap_suspects"]:
            failures += 1
        if code != 0:
            failures += 1
        report["routes"][script.name] = entry
        cache = entry["cache"]
        wrap = entry["oam_wrap"]
        route_flags = ""
        if cache["oob_read"] or cache["oob_write"]:
            route_flags += (f" CACHE-OOB(r={cache['oob_read']},"
                            f"w={cache['oob_write']})")
        if wrap["wrap_suspects"]:
            route_flags += f" OAM-XWRAP({wrap['wrap_suspects']})"
        print(f"{script.name}: rc={code} rebases={cache['rebase']}"
              f"{route_flags}")
        for scene, stats in entry.get("scenes", {}).items():
            flag = ""
            if stats["raw_fallbacks"]:
                flag += " RAW-FALLBACK"
            if stats["blank_serves"]:
                flag += f" BLANK({stats['blank_serves']})"
            if stats["unstable_margin_frames"]:
                flag += " UNSTABLE-MARGIN"
            if stats["centered_in_gameplay"]:
                flag += " PILLARBOX-IN-GAMEPLAY"
            print(f"  scene {scene}: {stats['frames']}f "
                  f"widened={stats['widened']} "
                  f"calibrated={stats['calibrated']}{flag}")
        # Actor-lifecycle divergence needs input-aligned stock/wide twins;
        # run tools/first_divergence.py on any flagged route for that leg.

    text = json.dumps(report, indent=1)
    if args.json_out:
        args.json_out.write_text(text)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
