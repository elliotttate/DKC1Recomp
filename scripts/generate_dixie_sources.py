#!/usr/bin/env python3
"""Generate the private Dixie Kong Country runtime sources on any platform.

The variant executable synthesizes the modded image in memory from the user's
verified clean ROM (runner/dkc1_dixie_mod.c). Generation needs that same
image as the analysis oracle, so this script applies the embedded IPS patch
(runner/dkc1_dixie_patch.inc) with the loader's exact semantics, checks the
pinned SHA-256, and runs generate_snesrecomp.py over recomp/dixie into
generated/snesrecomp_dixie, including the same fail-closed widescreen
override pass as stock (all 34 anchors match the variant). Nothing derived
from the ROM is written outside the ignored generated/ tree; the temporary
image is removed afterwards.
"""
import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLEAN_SHA256 = "fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15"
MOD_SHA256 = "2769b72a8a2050000336f5dd6dea1a45385f4f35ee710dafb0c0a3592295643b"
CLEAN_SIZE, MOD_SIZE = 0x400000, 0x600000


def embedded_ips() -> bytes:
    text = (ROOT / "runner/dkc1_dixie_patch.inc").read_text(encoding="utf-8")
    body = text[text.index("{", text.index("kDkc1DixiePatchIps[]")) + 1:]
    body = body[:body.index("}")]
    return bytes(int(value) for value in re.findall(r"\d+", body))


def apply_ips(ips: bytes, image: bytearray) -> None:
    if ips[:5] != b"PATCH":
        raise ValueError("embedded patch is not IPS")
    pos = 5
    while pos + 3 <= len(ips):
        if ips[pos:pos + 3] == b"EOF":
            return
        offset = int.from_bytes(ips[pos:pos + 3], "big")
        pos += 3
        length = int.from_bytes(ips[pos:pos + 2], "big")
        pos += 2
        if length == 0:  # run-length record
            count = int.from_bytes(ips[pos:pos + 2], "big")
            value = ips[pos + 2]
            pos += 3
            if offset + count > len(image):
                raise ValueError("IPS run past the image")
            image[offset:offset + count] = bytes([value]) * count
        else:
            if pos + length > len(ips) or offset + length > len(image):
                raise ValueError("IPS record past the image")
            image[offset:offset + length] = ips[pos:pos + length]
            pos += length
    raise ValueError("IPS patch has no EOF")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path, help="verified clean DKC1 USA v1.0 ROM")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "generated/snesrecomp_dixie")
    parser.add_argument("--analysis-backend", default="python")
    parser.add_argument("--no-widescreen-overrides", action="store_true",
                        help="skip the fail-closed presentation-widescreen pass "
                             "(the v0.0.13 native-only variant)")
    args = parser.parse_args()
    data = args.rom.read_bytes()
    if len(data) % 1024 == 512:
        data = data[512:]
    if len(data) != CLEAN_SIZE or hashlib.sha256(data).hexdigest() != CLEAN_SHA256:
        parser.error("the ROM is not the supported clean DKC1 USA v1.0 image")
    image = bytearray(MOD_SIZE)
    image[:CLEAN_SIZE] = data
    apply_ips(embedded_ips(), image)
    if hashlib.sha256(image).hexdigest() != MOD_SHA256:
        parser.error("synthesized image does not match the pinned Dixie identity")
    with tempfile.TemporaryDirectory(prefix="dkc1-dixie-generate-") as directory:
        rom = Path(directory) / "dixie.sfc"
        rom.write_bytes(image)
        subprocess.run([
            sys.executable, str(ROOT / "scripts/generate_snesrecomp.py"),
            "--rom", str(rom), "--config-dir", str(ROOT / "recomp/dixie"),
            "--output-dir", str(args.output_dir),
            "--analysis-backend", args.analysis_backend]
            + (["--no-widescreen-overrides"] if args.no_widescreen_overrides else []),
            check=True)
    print(f"Generated private Dixie sources in {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
