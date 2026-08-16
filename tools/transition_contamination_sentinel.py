#!/usr/bin/env python3
"""Automatically audit hard route transitions for retained wide history.

The sentinel first replays a flight-recorder bundle once with DKC1_WS_TRACE.
It treats scene/source/identity changes and explicit shadow reset decisions as
hard transition boundaries.  At each boundary (plus requested follow-up
offsets), it saves the exact native snapshot and renders every presentation
surface twice:

  1. with the serialized retained widescreen history; and
  2. with DKC1_WS_COLD_STATE_LOAD=1, which discards only host history.

WRAM, VRAM, CGRAM, PPU OAM, and WRAM OAM must remain byte-identical.  BG1,
BG2, BG3, OBJ, and composite are compared independently, with left/native/
right hashes.  A native-center mismatch fails closed instead of being called
margin contamination.  The first failure retains the full snapshot, raw
state, isolated layers, traces, logs, JSON report, and an HTML timeline.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Iterable

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from bisect_transition_contamination import (
    DEFAULT_EXTRA,
    DEFAULT_HEIGHT,
    DEFAULT_NATIVE_WIDTH,
    ROM_SHA256,
    changed_pixels,
    clean_environment,
    parse_runner_hashes,
    read_ppm,
    region,
    run_process,
    sha256,
)
from verify_flight_bundle import verify_bundle


SCHEMA = "dkc1.transition-sentinel.v1"
SURFACES = ("bg1", "bg2", "bg3", "obj", "composite")


def read_jsonl(path: Path) -> list[dict]:
    records: list[dict] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        record = json.loads(line)
        if record.get("schema") != "dkc1.ws.frame.v1":
            raise ValueError(f"{path}:{line_no}: unexpected trace schema")
        records.append(record)
    if not records:
        raise ValueError(f"{path}: no widescreen trace records")
    return records


def identity_key(record: dict) -> tuple:
    scene = record.get("scene", {})
    source = record.get("source", {})
    identity = record.get("identity", {})
    return (
        scene.get("mode"), scene.get("level"), scene.get("entrance"),
        source.get("bank"), source.get("map"), source.get("metatiles"),
        source.get("stream_vram"), identity.get("hash"),
    )


def discover_transitions(records: list[dict]) -> list[dict]:
    transitions: list[dict] = []
    previous: dict | None = None
    for relative, record in enumerate(records, 1):
        reasons: list[str] = []
        decision = record.get("decision", {})
        if previous is None:
            reasons.append("route_start")
        else:
            before, after = identity_key(previous), identity_key(record)
            labels = ("mode", "level", "entrance", "source_bank", "map",
                      "metatiles", "stream_vram", "identity_hash")
            reasons.extend(name for name, left, right in zip(labels, before, after)
                           if left != right)
        for name in ("reset", "source_reset", "identity_reset", "cold_start"):
            if decision.get(name):
                reasons.append(name)
        if record.get("identity", {}).get("change_mask") not in (None, 0):
            reasons.append("identity_change_mask")
        if reasons:
            transitions.append({
                "relative_frame": relative,
                "trace_frame": record.get("frame"),
                "reasons": sorted(set(reasons)),
                "scene": record.get("scene", {}),
                "source": record.get("source", {}),
            })
        previous = record
    return transitions


def scheduled_samples(transitions: list[dict], total_frames: int,
                      offsets: Iterable[int]) -> list[dict]:
    samples: dict[int, dict] = {}
    for transition in transitions:
        origin = transition["relative_frame"]
        for offset in offsets:
            frame = origin + offset
            if not 1 <= frame <= total_frames:
                continue
            item = samples.setdefault(frame, {
                "relative_frame": frame,
                "origins": [],
            })
            item["origins"].append({
                "transition_frame": origin,
                "offset": offset,
                "reasons": transition["reasons"],
            })
    return [samples[key] for key in sorted(samples)]


def compare_surface(retained: Path, cold: Path, extra: int) -> dict:
    rw, rh, retained_pixels = read_ppm(retained)
    cw, ch, cold_pixels = read_ppm(cold)
    expected = DEFAULT_NATIVE_WIDTH + extra * 2
    if (rw, rh) != (cw, ch) or (rw, rh) != (expected, DEFAULT_HEIGHT):
        raise ValueError(
            f"layer geometry mismatch retained={rw}x{rh} cold={cw}x{ch}")
    output: dict[str, object] = {}
    for name, x0, x1 in (
            ("left", 0, extra),
            ("center", extra, extra + DEFAULT_NATIVE_WIDTH),
            ("right", extra + DEFAULT_NATIVE_WIDTH, expected)):
        before = region(retained_pixels, rw, rh, x0, x1)
        after = region(cold_pixels, cw, ch, x0, x1)
        count, linear = changed_pixels(before, after)
        bounds = None
        if linear is not None:
            width = x1 - x0
            first, last = linear
            bounds = [first % width + x0, first // width,
                      last % width + x0, last // width]
        output[name] = {
            "retained_sha256": hashlib.sha256(before).hexdigest(),
            "cold_sha256": hashlib.sha256(after).hexdigest(),
            "changed_pixels": count,
            "bounds": bounds,
        }
    return output


def classify_layers(raw: dict, layers: dict) -> str:
    if not all(item["exact"] for item in raw.values()):
        return "machine_state_mismatch"
    if any(layer["center"]["changed_pixels"] for layer in layers.values()):
        return "native_center_mismatch"
    if any(layer[side]["changed_pixels"]
           for layer in layers.values() for side in ("left", "right")):
        return "retained_layer_contamination"
    return "identical"


def last_trace_record(path: Path) -> dict:
    return read_jsonl(path)[-1]


class Sentinel:
    def __init__(self, args: argparse.Namespace, manifest: dict):
        self.args = args
        self.manifest = manifest
        self.bundle = args.bundle.resolve()
        self.output = args.output.resolve()

    def route_environment(self) -> dict[str, str]:
        env = clean_environment()
        env.update({
            "DKC1_WIDESCREEN": "1",
            "DKC1_SAVESTATE_INPUT": str(self.bundle / "anchor.snapshot"),
            "SNESRECOMP_INPUT_PLAY": str(self.bundle / "inputs.txt"),
        })
        return env

    def baseline(self) -> list[dict]:
        root = self.output / "baseline"
        root.mkdir(parents=True)
        env = self.route_environment()
        trace = root / "ws-trace.jsonl"
        env["DKC1_WS_TRACE"] = str(trace)
        run_process(
            [str(self.args.runner), str(self.args.rom),
             str(self.manifest["replay_frames"])],
            env, root / "stdout.txt", root / "stderr.txt")
        return read_jsonl(trace)

    def capture_layers(self, snapshot: Path, outdir: Path, cold: bool) -> None:
        outdir.mkdir(parents=True)
        env = clean_environment()
        env["DKC1_WIDESCREEN"] = "1"
        if cold:
            env["DKC1_WS_COLD_STATE_LOAD"] = "1"
        run_process(
            [str(self.args.layer_capture), str(self.args.rom),
             str(snapshot), str(outdir)],
            env, outdir / "stdout.txt", outdir / "stderr.txt")

    def evaluate(self, sample: dict) -> dict:
        frame = sample["relative_frame"]
        root = self.output / f"frame-{frame:08d}"
        root.mkdir(parents=True)
        snapshot = root / "frame.snapshot"

        retained_env = self.route_environment()
        retained_env.update({
            "DKC1_SAVESTATE_OUTPUT": str(snapshot),
            "DKC1_SAVESTATE_SAVE_AT": str(frame),
            "DKC1_FRAME_PPM": str(root / "retained-frame.ppm"),
            "DKC1_WRAM_OUTPUT": str(root / "retained.wram"),
            "DKC1_VRAM_OUTPUT": str(root / "retained.vram"),
            "DKC1_WS_TRACE": str(root / "retained-trace.jsonl"),
        })
        run_process([str(self.args.runner), str(self.args.rom), str(frame)],
                    retained_env, root / "retained-stdout.txt",
                    root / "retained-stderr.txt")

        cold_env = clean_environment()
        cold_env.update({
            "DKC1_WIDESCREEN": "1",
            "DKC1_WS_COLD_STATE_LOAD": "1",
            "DKC1_SAVESTATE_INPUT": str(snapshot),
            "DKC1_FRAME_PPM": str(root / "cold-frame.ppm"),
            "DKC1_WRAM_OUTPUT": str(root / "cold.wram"),
            "DKC1_VRAM_OUTPUT": str(root / "cold.vram"),
            "DKC1_WS_TRACE": str(root / "cold-trace.jsonl"),
        })
        run_process([str(self.args.runner), str(self.args.rom), "0"], cold_env,
                    root / "cold-stdout.txt", root / "cold-stderr.txt")

        retained_layers, cold_layers = root / "retained", root / "cold"
        self.capture_layers(snapshot, retained_layers, False)
        self.capture_layers(snapshot, cold_layers, True)

        raw: dict[str, dict] = {}
        for name in ("wram", "vram"):
            left, right = root / f"retained.{name}", root / f"cold.{name}"
            left_hash, right_hash = sha256(left), sha256(right)
            raw[name] = {"retained_sha256": left_hash,
                         "cold_sha256": right_hash,
                         "exact": left_hash == right_hash}
        retained_hashes = parse_runner_hashes(root / "retained-stdout.txt")
        cold_hashes = parse_runner_hashes(root / "cold-stdout.txt")
        for key in ("cgram_sha256", "oam_sha256", "oam_source_sha256"):
            name = key.removesuffix("_sha256")
            raw[name] = {
                "retained_sha256": retained_hashes[key],
                "cold_sha256": cold_hashes[key],
                "exact": retained_hashes[key] == cold_hashes[key],
            }

        layers = {
            name: compare_surface(retained_layers / f"{name}.ppm",
                                  cold_layers / f"{name}.ppm",
                                  self.args.extra)
            for name in SURFACES
        }
        result = {
            **sample,
            "classification": classify_layers(raw, layers),
            "directory": str(root),
            "snapshot_sha256": sha256(snapshot),
            "raw": raw,
            "layers": layers,
            "decision_proof": {
                "retained": last_trace_record(root / "retained-trace.jsonl"),
                "cold": last_trace_record(root / "cold-trace.jsonl"),
            },
            "capture_target_exact": {
                "retained_composite": (
                    sha256(root / "retained-frame.ppm") ==
                    sha256(retained_layers / "composite.ppm")),
                "cold_composite": (
                    sha256(root / "cold-frame.ppm") ==
                    sha256(cold_layers / "composite.ppm")),
            },
        }
        (root / "comparison.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8")
        return result


def write_html(report: dict, path: Path) -> None:
    rows = []
    for sample in report["samples"]:
        reasons = ", ".join(
            "+".join(origin["reasons"]) for origin in sample["origins"])
        rows.append(
            "<tr><td>{}</td><td>{}</td><td>{}</td><td><a href='{}'>evidence</a></td></tr>".format(
                sample["relative_frame"], html.escape(reasons),
                html.escape(sample["classification"]),
                html.escape(Path(sample["directory"]).name + "/comparison.json")))
    first = report.get("first_failure")
    status = "PASS" if first is None else "FAIL at frame " + str(first)
    path.write_text(f"""<!doctype html><meta charset='utf-8'>
<title>DKC1 transition contamination sentinel</title>
<style>body{{font:16px system-ui;max-width:1100px;margin:2rem auto;background:#111;color:#eee}}
table{{border-collapse:collapse;width:100%}}th,td{{border:1px solid #555;padding:.5rem;text-align:left}}
.pass{{color:#7ee787}}.fail{{color:#ff7b72}}a{{color:#79c0ff}}</style>
<h1>Transition contamination sentinel</h1>
<p class='{"pass" if first is None else "fail"}'><strong>{html.escape(status)}</strong></p>
<p>{len(report['transitions'])} hard transitions; {len(report['samples'])} exact-state samples.</p>
<table><thead><tr><th>Relative frame</th><th>Origin/reason</th><th>Classification</th><th>Bundle</th></tr></thead>
<tbody>{''.join(rows)}</tbody></table>\n""", encoding="utf-8")


def parse_offsets(value: str) -> list[int]:
    try:
        offsets = sorted(set(int(part.strip()) for part in value.split(",")))
    except ValueError as error:
        raise argparse.ArgumentTypeError("offsets must be comma-separated integers") from error
    if not offsets or offsets[0] < 0 or offsets[-1] > 300:
        raise argparse.ArgumentTypeError("offsets must be in 0..300")
    return offsets


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--layer-capture", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--extra", type=int, default=DEFAULT_EXTRA)
    parser.add_argument("--offsets", type=parse_offsets,
                        default=parse_offsets("0,1,2,4,8,16,32"))
    parser.add_argument("--max-samples", type=int, default=64)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args(argv)
    try:
        for path in (args.runner, args.layer_capture, args.rom):
            if not path.is_file():
                raise ValueError(f"missing file: {path}")
        if not args.bundle.is_dir():
            raise ValueError(f"missing bundle: {args.bundle}")
        if sha256(args.rom) != ROM_SHA256:
            raise ValueError("ROM is not verified headerless DKC USA v1.0")
        if not 1 <= args.max_samples <= 512:
            raise ValueError("max-samples must be 1..512")
        manifest = verify_bundle(args.bundle)
        if args.validate_only:
            print(json.dumps({"valid": True, "schema": SCHEMA,
                              "replay_frames": manifest["replay_frames"]}))
            return 0
        if args.output.exists() and any(args.output.iterdir()):
            raise ValueError(f"output directory is not empty: {args.output}")
        args.output.mkdir(parents=True, exist_ok=True)
        sentinel = Sentinel(args, manifest)
        records = sentinel.baseline()
        transitions = discover_transitions(records)
        plan = scheduled_samples(transitions, manifest["replay_frames"], args.offsets)
        plan = plan[:args.max_samples]
        samples = []
        first_failure = None
        for index, sample in enumerate(plan, 1):
            print(f"[{index}/{len(plan)}] frame {sample['relative_frame']}", flush=True)
            result = sentinel.evaluate(sample)
            samples.append(result)
            if result["classification"] != "identical":
                first_failure = result["relative_frame"]
                break
        report = {
            "schema": SCHEMA,
            "inputs": {
                "runner": str(args.runner.resolve()),
                "runner_sha256": sha256(args.runner),
                "layer_capture": str(args.layer_capture.resolve()),
                "layer_capture_sha256": sha256(args.layer_capture),
                "rom": str(args.rom.resolve()),
                "rom_sha256": sha256(args.rom),
                "bundle": str(args.bundle.resolve()),
                "bundle_manifest_sha256": sha256(args.bundle / "manifest.json"),
            },
            "offsets": args.offsets,
            "transitions": transitions,
            "samples": samples,
            "first_failure": first_failure,
            "passed": first_failure is None,
            "truncated": len(plan) >= args.max_samples,
        }
        (args.output / "report.json").write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf-8")
        write_html(report, args.output / "report.html")
        print(json.dumps({"passed": report["passed"],
                          "transitions": len(transitions),
                          "samples": len(samples),
                          "first_failure": first_failure}))
        return 0 if report["passed"] else 1
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"transition_contamination_sentinel: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
