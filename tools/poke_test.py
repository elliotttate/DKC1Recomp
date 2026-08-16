#!/usr/bin/env python3
"""WRAM fault injection: patch a snapshot, replay, assert the reaction.

Formalizes the technique that split the contact-damage bug: locate the
WRAM block inside a native snapshot by content fingerprint, patch chosen
bytes, replay N frames, and evaluate expectations against the resulting
WRAM. This proves how DOWNSTREAM code reacts to a value — it does NOT
prove the game naturally produces that value (pair with a watchpoint run
for the producer side).

usage:
  python tools/poke_test.py --state overlap.state --rom <rom> \\
      --set 1597=40 --run 120 --expect "102B==19" --expect "11A3>0"

Sets/expects use WRAM hex offsets; values hex. Expect operators:
== != > < >= <=  (16-bit little-endian reads; use addr.b for 8-bit).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
WRAM_SIZE = 0x20000


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def validate_wram_range(addr: int, width: int, label: str) -> None:
    if addr < 0 or width < 1 or addr + width > WRAM_SIZE:
        raise ValueError(
            f"{label} WRAM range ${addr:X}..${addr + width - 1:X} "
            f"is outside $00000..${WRAM_SIZE - 1:05X}")


def parse_setting(setting: str) -> tuple[int, bytes]:
    match = re.fullmatch(r"([0-9A-Fa-f]+)=([0-9A-Fa-f]+)",
                         setting.strip())
    if not match:
        raise ValueError(f"bad --set {setting!r}; expected ADDR=HEXBYTES")
    addr = int(match.group(1), 16)
    value_text = match.group(2)
    data = bytes.fromhex(value_text if len(value_text) % 2 == 0
                         else "0" + value_text)
    validate_wram_range(addr, len(data), "--set")
    return addr, data


def run_frames(exe: Path, rom: Path, state: Path, frames: int,
               work: Path, widescreen: bool) -> bytes:
    wram_out = work / "poke_out.wram.bin"
    env = os.environ.copy()
    env["DKC1_WIDESCREEN"] = "1" if widescreen else "0"
    env["DKC1_SAVESTATE_INPUT"] = str(state)
    env["DKC1_WRAM_OUTPUT"] = str(wram_out)
    env.pop("DKC1_SCRIPT", None)
    result = subprocess.run([str(exe), str(rom), str(frames)],
                            cwd=str(work), env=env, capture_output=True,
                            text=True)
    if result.returncode != 0:
        sys.exit(f"replay failed rc={result.returncode}: "
                 f"{result.stderr[-400:]}")
    if not wram_out.exists():
        sys.exit("replay succeeded but produced no WRAM output")
    data = wram_out.read_bytes()
    if len(data) != WRAM_SIZE:
        sys.exit(f"WRAM output has {len(data)} bytes; expected {WRAM_SIZE}")
    return data


def locate_wram(state: bytes, reference_wram: bytes) -> int:
    """Find the embedded WRAM block via unique content fingerprints."""
    for probe in (0x1B00, 0x0D40, 0x0500, 0x10000, 0x1F00):
        signature = bytes(reference_wram[probe:probe + 48])
        at = state.find(signature)
        if at < 0 or state.find(signature, at + 1) >= 0:
            continue
        base = at - probe
        if base < 0 or base + WRAM_SIZE > len(state):
            continue
        # The snapshot stores WRAM as one contiguous block. A short second
        # signature is not enough evidence for a destructive fault-injection
        # edit; require the entire 128 KiB candidate to equal the zero-frame
        # replay oracle before accepting the offset.
        if state[base:base + WRAM_SIZE] == bytes(reference_wram):
            return base
    sys.exit("could not locate WRAM inside the snapshot")


def parse_expect(expr: str):
    match = re.fullmatch(
        r"([0-9A-Fa-f]+)(\.b)?\s*(==|!=|>=|<=|>|<)\s*([0-9A-Fa-f]+)",
        expr.strip())
    if not match:
        sys.exit(f"bad --expect {expr!r}")
    addr = int(match.group(1), 16)
    width = 1 if match.group(2) else 2
    validate_wram_range(addr, width, "--expect")
    return addr, width, match.group(3), int(match.group(4), 16)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--exe", type=Path,
                        default=REPO / "build/dkc1_headless_tools.exe")
    parser.add_argument("--set", action="append", default=[],
                        metavar="ADDR=HEXBYTES",
                        help="e.g. 1597=40 or 1597=4000 (byte sequence)")
    parser.add_argument("--run", type=int, default=60)
    parser.add_argument("--expect", action="append", default=[],
                        metavar="EXPR", help='e.g. "102B==19" "11A3.b>0"')
    parser.add_argument("--widescreen", action="store_true")
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()

    if args.run < 0:
        sys.exit("--run must be non-negative")
    for label, path in (("--state", args.state), ("--rom", args.rom),
                        ("--exe", args.exe)):
        if not path.is_file():
            sys.exit(f"{label} file does not exist: {path}")

    work = args.work or Path(tempfile.mkdtemp(prefix="poke_",
                                              dir=str(REPO / "build")))
    work.mkdir(parents=True, exist_ok=True)

    # A zero-frame replay exposes the snapshot's exact WRAM image. Advancing
    # even one frame before fingerprinting makes rapidly changing states
    # impossible to locate reliably and can select a coincidental block.
    reference = run_frames(args.exe.resolve(), args.rom.resolve(),
                           args.state.resolve(), 0, work, args.widescreen)
    original_state = args.state.read_bytes()
    state = bytearray(original_state)
    base = locate_wram(state, reference)
    print(f"WRAM located at snapshot offset {base:#x}")

    applied_settings = []
    for setting in args.set:
        try:
            addr, data = parse_setting(setting)
        except ValueError as exc:
            sys.exit(str(exc))
        old = bytes(state[base + addr:base + addr + len(data)])
        state[base + addr:base + addr + len(data)] = data
        print(f"  set ${addr:04X}: {old.hex()} -> {data.hex()}")
        applied_settings.append({"address": addr, "old": old.hex(),
                                 "new": data.hex()})

    patched = work / "poke_patched.state"
    patched.write_bytes(state)
    after = run_frames(args.exe.resolve(), args.rom.resolve(), patched,
                       args.run, work, args.widescreen)

    failures = 0
    expectation_results = []
    for expr in args.expect:
        try:
            addr, width, op, want = parse_expect(expr)
        except ValueError as exc:
            sys.exit(str(exc))
        actual = int.from_bytes(after[addr:addr + width], "little")
        ok = {"==": actual == want, "!=": actual != want,
              ">": actual > want, "<": actual < want,
              ">=": actual >= want, "<=": actual <= want}[op]
        print(f"  {'ok  ' if ok else 'FAIL'} ${addr:04X}"
              f"{'.b' if width == 1 else ''} = {actual:#x} {op} {want:#x}")
        failures += 0 if ok else 1
        expectation_results.append({"expression": expr, "address": addr,
                                    "width": width, "actual": actual,
                                    "expected": want, "operator": op,
                                    "passed": ok})
    if not args.expect:
        print("(no --expect given; patched replay completed)")
    manifest = {
        "schema": "dkc1.poke-test.v1",
        "rom": str(args.rom.resolve()),
        "rom_sha256": sha256_bytes(args.rom.read_bytes()),
        "source_state": str(args.state.resolve()),
        "source_state_sha256": sha256_bytes(original_state),
        "patched_state": str(patched.resolve()),
        "patched_state_sha256": sha256_bytes(bytes(state)),
        "wram_snapshot_offset": base,
        "frames": args.run,
        "widescreen": args.widescreen,
        "settings": applied_settings,
        "expectations": expectation_results,
        "output_wram_sha256": sha256_bytes(after),
        "passed": failures == 0,
    }
    (work / "poke_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
