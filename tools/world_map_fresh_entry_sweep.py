#!/usr/bin/env python3
"""Discover DKC1's unlocked map graph and capture authentic fresh entries.

The widescreen initializer must be tested while a level is entered by the
game.  A save state made after entry can already contain poisoned VRAM and is
not evidence about the initializer.  This tool starts from a native snapshot
on an unlocked map, explores with controller input only, and archives the
snapshot immediately before every B-button entrance plus the resulting
widescreen trace, WRAM, frame, and native snapshot.

No WRAM is written and no game routine is bypassed.  Each probe is a separate
headless process loaded from an immutable parent snapshot, so graph branches
cannot contaminate one another.  The visible desktop process is never opened
or contacted.
"""
from __future__ import annotations

import argparse
from collections import deque
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Iterable


SCHEMA = "dkc1.world-map-fresh-entry-sweep.v1"
MAP_MODE = 0x0003
WORLD_MAP_LEVEL = 0x0025
WRAM_SIZE = 0x20000
ACTIONS = {
    "neutral": 0x0000,
    "up": 0x0010,
    "down": 0x0020,
    "left": 0x0040,
    "right": 0x0080,
    "enter": 0x0001,
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def u16(wram: bytes, offset: int) -> int:
    return wram[offset] | (wram[offset + 1] << 8)


def state_summary(wram: bytes) -> dict:
    if len(wram) != WRAM_SIZE:
        raise ValueError(f"WRAM must be exactly {WRAM_SIZE} bytes")
    return {
        "level": u16(wram, 0x0030),
        "mode": u16(wram, 0x0032),
        "entrance": u16(wram, 0x003E),
        "fade": u16(wram, 0x1DF1),
        "layer1_x": u16(wram, 0x088B),
        "layer1_y": u16(wram, 0x0895),
        "camera_x": u16(wram, 0x1A62),
        "camera_y": u16(wram, 0x1A4C),
        "camera_lower": u16(wram, 0x1B23),
        "camera_upper": u16(wram, 0x1B25),
        "map_actor_x": u16(wram, 0x0B19),
        "map_actor_y": u16(wram, 0x0BC1),
    }


def node_key(summary: dict) -> str:
    return f"{summary['mode']:04X}-{summary['entrance']:04X}"


def is_world_map_state(summary: dict) -> bool:
    # Mode $0003 is shared with underwater levels, and Croctopus Chase also
    # uses level ID $0025.  A settled world-map node has no gameplay camera
    # span; Croctopus initializes $0000..$0700.  All three fields are needed.
    return (summary["mode"] == MAP_MODE and
            summary["level"] == WORLD_MAP_LEVEL and
            summary["camera_lower"] == 0 and
            summary["camera_upper"] == 0)


def parse_entrance_names(path: Path | None) -> dict[int, str]:
    if path is None:
        return {}
    pattern = re.compile(
        r"^!Define_DKC1_EntranceID_([A-Za-z0-9_]+)\s*=\s*\$([0-9A-Fa-f]{4})")
    names: dict[int, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line.strip())
        if match:
            names.setdefault(int(match.group(2), 16), match.group(1))
    return names


def write_script(path: Path, mask: int, action_frames: int,
                 settle_frames: int) -> None:
    lines = ["# controller-only graph probe", f"{mask:X} * {action_frames}"]
    if settle_frames:
        lines.append(f"0 * {settle_frames}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def run_probe(*, runner: Path, rom: Path, snapshot: Path, action: str,
              output: Path, action_frames: int, settle_frames: int,
              widescreen: bool,
              trace: bool) -> dict:
    output.mkdir(parents=True, exist_ok=False)
    script = output / "input.dks"
    write_script(script, ACTIONS[action], action_frames, settle_frames)
    wram_path = output / "final.wram.bin"
    snapshot_path = output / "final.snapshot"
    frame_path = output / "final.ppm"
    trace_path = output / "ws-trace.jsonl"
    stdout_path = output / "stdout.txt"
    stderr_path = output / "stderr.txt"
    frames = settle_frames + action_frames
    env = os.environ.copy()
    for key in ("SNESRECOMP_INPUT_PLAY", "DKC1_SUPERZSNES_STATE"):
        env.pop(key, None)
    env.update({
        "DKC1_SAVESTATE_INPUT": str(snapshot.resolve()),
        "DKC1_SAVESTATE_OUTPUT": str(snapshot_path.resolve()),
        "DKC1_SCRIPT": str(script.resolve()),
        "DKC1_WRAM_DUMP": f"{frames}-{frames}",
        "DKC1_WRAM_DUMP_PATH": str(wram_path.resolve()),
        "DKC1_FRAME_PPM": str(frame_path.resolve()),
        "DKC1_WIDESCREEN": "1" if widescreen else "0",
    })
    if trace:
        env["DKC1_WS_TRACE"] = str(trace_path.resolve())
    else:
        env.pop("DKC1_WS_TRACE", None)
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        completed = subprocess.run(
            [str(runner.resolve()), str(rom.resolve()), str(frames)],
            cwd=str(output), env=env, stdout=stdout, stderr=stderr,
            check=False)
    if completed.returncode != 0:
        raise RuntimeError(
            f"{action} probe failed rc={completed.returncode}; see {stderr_path}")
    wram = wram_path.read_bytes()
    summary = state_summary(wram)
    return {
        "action": action,
        "action_frames": action_frames,
        "widescreen": widescreen,
        "frames": frames,
        "state": summary,
        "wram_sha256": sha256(wram_path),
        "snapshot_sha256": sha256(snapshot_path),
        "paths": {
            "directory": str(output.resolve()),
            "input_script": str(script.resolve()),
            "wram": str(wram_path.resolve()),
            "snapshot": str(snapshot_path.resolve()),
            "frame": str(frame_path.resolve()),
            "trace": str(trace_path.resolve()) if trace else None,
        },
    }


def successful_level_entry(parent: dict, child: dict) -> bool:
    """A B probe left the world-map scene through the game's own path."""
    state = child["state"]
    return (is_world_map_state(parent["state"]) and
            not is_world_map_state(state) and
            state["entrance"] != 0xFFFF)


def _named(summary: dict, names: dict[int, str]) -> dict:
    result = dict(summary)
    result["entrance_name"] = names.get(summary["entrance"], "Unknown")
    return result


def explore(*, runner: Path, rom: Path, root_snapshot: Path,
            output: Path, defines: Path | None, settle_frames: int,
            action_frames: int, max_nodes: int, max_probes: int,
            entry_repeats: int) -> dict:
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    names = parse_entrance_names(defines)

    # Read the root through the runtime rather than reverse-engineering the
    # snapshot container.  Neutral is still controller-only and establishes
    # the same settled boundary used for every child.
    root_probe = run_probe(
        runner=runner, rom=rom, snapshot=root_snapshot, action="neutral",
        output=output / "root-observation", action_frames=1, settle_frames=0,
        widescreen=False, trace=False)
    if not is_world_map_state(root_probe["state"]):
        raise ValueError(
            "root snapshot is not on the world map "
            f"(mode={root_probe['state']['mode']:04X}, "
            f"level={root_probe['state']['level']:04X})")

    # Use the immutable caller-provided root for every discovery branch.
    root_state = root_probe["state"]
    root_key = node_key(root_state)
    nodes: dict[str, dict] = {
        root_key: {
            "key": root_key,
            "state": _named(root_state, names),
            "snapshot": str(root_snapshot.resolve()),
            "snapshot_sha256": sha256(root_snapshot),
            "discovered_from": None,
        }
    }
    queue: deque[str] = deque([root_key])
    edges: list[dict] = []
    entries: list[dict] = []
    probes = 0

    while queue and len(nodes) < max_nodes and probes < max_probes:
        key = queue.popleft()
        node = nodes[key]
        source_snapshot = Path(node["snapshot"])
        for action in ("up", "down", "left", "right", "enter"):
            if probes >= max_probes:
                break
            probe_dir = output / "probes" / f"{probes:04d}-{key}-{action}"
            probe = run_probe(
                runner=runner, rom=rom, snapshot=source_snapshot,
                action=action, output=probe_dir,
                action_frames=1 if action == "enter" else action_frames,
                settle_frames=settle_frames, widescreen=False, trace=False)
            probes += 1
            child_state = probe["state"]
            child_key = node_key(child_state)
            edge = {
                "from": key, "action": action, "to": child_key,
                "state": _named(child_state, names),
                "probe": probe["paths"]["directory"],
            }
            edges.append(edge)

            if is_world_map_state(child_state):
                if child_key not in nodes and len(nodes) < max_nodes:
                    nodes[child_key] = {
                        "key": child_key,
                        "state": _named(child_state, names),
                        "snapshot": probe["paths"]["snapshot"],
                        "snapshot_sha256": probe["snapshot_sha256"],
                        "discovered_from": {"node": key, "action": action},
                    }
                    queue.append(child_key)
                continue

            if action != "enter" or not successful_level_entry(
                    {"state": node["state"]}, probe):
                continue

            # Re-run the transition from the exact pre-entry map snapshot in
            # widescreen mode.  All evidence for initializer correctness is
            # rooted here, not in the exploratory native run above.
            repeats: list[dict] = []
            entry_root = output / "fresh-entries" / key
            for repeat in range(entry_repeats):
                wide = run_probe(
                    runner=runner, rom=rom, snapshot=source_snapshot,
                    action="enter", output=entry_root / f"repeat-{repeat + 1}",
                    action_frames=1, settle_frames=settle_frames,
                    widescreen=True, trace=True)
                repeats.append(wide)
            deterministic = (None if len(repeats) < 2 else
                             len({r["wram_sha256"] for r in repeats}) == 1)
            entries.append({
                "from": key,
                "map_state": node["state"],
                "level_state": _named(repeats[0]["state"], names),
                "pre_entry_snapshot": str(source_snapshot.resolve()),
                "pre_entry_snapshot_sha256": sha256(source_snapshot),
                "deterministic": deterministic,
                "repeats": repeats,
            })

    report = {
        "schema": SCHEMA,
        "status": "pass" if entries and not queue and all(
            item["deterministic"] is True for item in entries)
            else "discovery_only" if entries and not queue
            else "incomplete",
        "inputs": {
            "runner": {"path": str(runner.resolve()), "sha256": sha256(runner)},
            "rom": {"path": str(rom.resolve()), "sha256": sha256(rom)},
            "root_snapshot": {"path": str(root_snapshot.resolve()),
                              "sha256": sha256(root_snapshot)},
            "defines": None if defines is None else str(defines.resolve()),
        },
        "settings": {
            "settle_frames": settle_frames,
            "action_frames": action_frames,
            "max_nodes": max_nodes,
            "max_probes": max_probes,
            "entry_repeats": entry_repeats,
        },
        "counts": {
            "nodes": len(nodes), "edges": len(edges),
            "fresh_entries": len(entries), "probes": probes,
            "queued_unexplored": len(queue),
        },
        "nodes": list(nodes.values()),
        "edges": edges,
        "fresh_entries": entries,
    }
    (output / "report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--root-snapshot", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--defines", type=Path)
    parser.add_argument("--settle-frames", type=int, default=600)
    parser.add_argument("--action-frames", type=int, default=16,
                        help="held frames for each directional map move")
    parser.add_argument("--max-nodes", type=int, default=128)
    parser.add_argument("--max-probes", type=int, default=640)
    parser.add_argument("--entry-repeats", type=int, default=1)
    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        for path in (args.runner, args.rom, args.root_snapshot):
            if not path.is_file():
                raise ValueError(f"required file is missing: {path}")
        if args.defines is not None and not args.defines.is_file():
            raise ValueError(f"defines file is missing: {args.defines}")
        if not 1 <= args.settle_frames <= 10000:
            raise ValueError("--settle-frames must be 1..10000")
        if not 1 <= args.action_frames <= 1000:
            raise ValueError("--action-frames must be 1..1000")
        if not 1 <= args.max_nodes <= 1024:
            raise ValueError("--max-nodes must be 1..1024")
        if not 1 <= args.max_probes <= 10000:
            raise ValueError("--max-probes must be 1..10000")
        if not 1 <= args.entry_repeats <= 10:
            raise ValueError("--entry-repeats must be 1..10")
        report = explore(
            runner=args.runner, rom=args.rom,
            root_snapshot=args.root_snapshot, output=args.output,
            defines=args.defines, settle_frames=args.settle_frames,
            action_frames=args.action_frames,
            max_nodes=args.max_nodes, max_probes=args.max_probes,
            entry_repeats=args.entry_repeats)
        print(json.dumps(report["counts"], indent=2))
        return 0 if report["status"] == "pass" else 1
    except (OSError, ValueError, RuntimeError) as error:
        print(f"world_map_fresh_entry_sweep: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
