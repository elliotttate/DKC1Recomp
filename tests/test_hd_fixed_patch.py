"""Contracts for the tracked Candy portrait patch and fixed-room installer."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets/hd-preview/treehouse-candy-v1.json"
MODULE_SPEC = importlib.util.spec_from_file_location(
    "apply_hd_fixed_patch", ROOT / "scripts/apply_hd_fixed_patch.py"
)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
PATCH_MODULE = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(PATCH_MODULE)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class HdFixedPatchTests(unittest.TestCase):
    def test_tracked_candy_assets_match_manifest(self):
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        patch_info = manifest["patch"]
        patch = MANIFEST.parent / patch_info["file"]
        preview = MANIFEST.parent / patch_info["preview"]

        self.assertEqual(
            patch.stat().st_size,
            patch_info["width"] * patch_info["height"] * 4,
        )
        self.assertEqual(sha256(patch.read_bytes()), patch_info["sha256"])
        self.assertEqual(sha256(preview.read_bytes()), patch_info["preview_sha256"])
        self.assertEqual(manifest["plate"]["output_sha256"],
                         "33ceb4a39a711449004cf6111f547380606b970fafedf1206679624cdfcb787d")

    def test_blends_atomically_and_is_idempotent(self):
        base = bytes((10, 20, 30, 255, 40, 50, 60, 255))
        patch = bytes((110, 120, 130, 128, 200, 100, 50, 0))
        expected = bytes((70, 70, 70, 255, 40, 50, 60, 255))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plate_path = root / "plate.bgra"
            patch_path = root / "patch.rgba"
            manifest_path = root / "patch.json"
            plate_path.write_bytes(base)
            patch_path.write_bytes(patch)
            manifest_path.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "plate": {
                            "width": 2,
                            "height": 1,
                            "format": "BGRA8",
                            "base_sha256": sha256(base),
                            "output_sha256": sha256(expected),
                        },
                        "patch": {
                            "file": patch_path.name,
                            "width": 2,
                            "height": 1,
                            "format": "RGBA8",
                            "x": 0,
                            "y": 0,
                            "blend": "source-over-srgb-round-nearest",
                            "sha256": sha256(patch),
                        },
                    }
                ),
                encoding="utf-8",
            )

            self.assertEqual(
                PATCH_MODULE.apply_fixed_patch(plate_path, manifest_path),
                sha256(expected),
            )
            self.assertEqual(plate_path.read_bytes(), expected)
            self.assertEqual(
                PATCH_MODULE.apply_fixed_patch(plate_path, manifest_path),
                sha256(expected),
            )

    def test_rejects_an_unknown_plate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plate_path = root / "plate.bgra"
            patch_path = root / "patch.rgba"
            manifest_path = root / "patch.json"
            plate_path.write_bytes(bytes((1, 2, 3, 255)))
            patch_path.write_bytes(bytes((4, 5, 6, 255)))
            manifest_path.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "plate": {
                            "width": 1,
                            "height": 1,
                            "format": "BGRA8",
                            "base_sha256": "0" * 64,
                            "output_sha256": "1" * 64,
                        },
                        "patch": {
                            "file": patch_path.name,
                            "width": 1,
                            "height": 1,
                            "format": "RGBA8",
                            "x": 0,
                            "y": 0,
                            "blend": "source-over-srgb-round-nearest",
                            "sha256": sha256(patch_path.read_bytes()),
                        },
                    }
                ),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "unexpected fixed plate SHA-256"):
                PATCH_MODULE.apply_fixed_patch(plate_path, manifest_path)


if __name__ == "__main__":
    unittest.main()
