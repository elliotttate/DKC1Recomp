#!/usr/bin/env python3
"""Apply a small, hash-locked RGBA patch to an opaque fixed-room BGRA plate."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile


def _sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest()


def apply_fixed_patch(plate_path: Path, manifest_path: Path) -> str:
    manifest_path = manifest_path.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("version") != 1:
        raise ValueError("unsupported fixed-patch manifest version")

    plate_info = manifest["plate"]
    patch_info = manifest["patch"]
    if plate_info.get("format") != "BGRA8":
        raise ValueError("fixed plate must use BGRA8")
    if patch_info.get("format") != "RGBA8":
        raise ValueError("fixed patch must use RGBA8")
    if patch_info.get("blend") != "source-over-srgb-round-nearest":
        raise ValueError("unsupported fixed-patch blend mode")

    plate_width = int(plate_info["width"])
    plate_height = int(plate_info["height"])
    patch_width = int(patch_info["width"])
    patch_height = int(patch_info["height"])
    patch_x = int(patch_info["x"])
    patch_y = int(patch_info["y"])
    if min(plate_width, plate_height, patch_width, patch_height) <= 0:
        raise ValueError("fixed-patch dimensions must be positive")
    if patch_x < 0 or patch_y < 0:
        raise ValueError("fixed-patch origin must be nonnegative")
    if patch_x + patch_width > plate_width or patch_y + patch_height > plate_height:
        raise ValueError("fixed patch extends outside the plate")

    plate_path = plate_path.resolve()
    plate = bytearray(plate_path.read_bytes())
    if len(plate) != plate_width * plate_height * 4:
        raise ValueError("fixed plate byte length does not match its manifest")

    current_hash = _sha256(plate)
    output_hash = str(plate_info["output_sha256"])
    if current_hash == output_hash:
        return output_hash
    if current_hash != str(plate_info["base_sha256"]):
        raise ValueError(f"unexpected fixed plate SHA-256: {current_hash}")
    if any(alpha != 255 for alpha in plate[3::4]):
        raise ValueError("fixed plate must be fully opaque")

    patch_path = (manifest_path.parent / str(patch_info["file"])).resolve()
    patch = patch_path.read_bytes()
    if len(patch) != patch_width * patch_height * 4:
        raise ValueError("fixed patch byte length does not match its manifest")
    patch_hash = _sha256(patch)
    if patch_hash != str(patch_info["sha256"]):
        raise ValueError(f"unexpected fixed patch SHA-256: {patch_hash}")

    for patch_row in range(patch_height):
        for patch_column in range(patch_width):
            patch_offset = (patch_row * patch_width + patch_column) * 4
            plate_offset = (
                (patch_y + patch_row) * plate_width + patch_x + patch_column
            ) * 4
            alpha = patch[patch_offset + 3]
            if alpha == 0:
                continue

            inverse_alpha = 255 - alpha
            for plate_channel, patch_channel in ((0, 2), (1, 1), (2, 0)):
                source = patch[patch_offset + patch_channel]
                destination = plate[plate_offset + plate_channel]
                plate[plate_offset + plate_channel] = (
                    source * alpha + destination * inverse_alpha + 127
                ) // 255
            plate[plate_offset + 3] = 255

    actual_output_hash = _sha256(plate)
    if actual_output_hash != output_hash:
        raise ValueError(
            "fixed-patch output SHA-256 mismatch: "
            f"expected {output_hash}, got {actual_output_hash}"
        )

    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{plate_path.name}.", dir=plate_path.parent, delete=False
        ) as temporary:
            temporary.write(plate)
            temporary.flush()
            os.fsync(temporary.fileno())
            temporary_name = temporary.name
        os.replace(temporary_name, plate_path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)

    return actual_output_hash


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plate", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        output_hash = apply_fixed_patch(arguments.plate, arguments.manifest)
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(f"HD_FIXED_PATCH_OK {arguments.plate} {output_hash}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
