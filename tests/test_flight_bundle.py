import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.verify_flight_bundle import verify_bundle


class FlightBundleTests(unittest.TestCase):
    def make_bundle(self, root: Path):
        payloads = {
            "anchor.snapshot": b"anchor",
            "current.snapshot": b"current",
            "inputs.txt": b"001\n080\n",
            "final.wram.bin": bytes(0x20000),
            "final.vram.bin": bytes(0x10000),
            "final.cgram.bin": bytes(0x200),
            "final.wram-oam.bin": bytes(544),
            "final.ppu-oam.bin": bytes(544),
        }
        for name, data in payloads.items():
            (root / name).write_bytes(data)
        manifest = {
            "schema": "dkc1.flight-recorder.v1",
            "anchor_frame": 10,
            "current_frame": 12,
            "snes_frame": 99,
            "replay_frames": 2,
            "scene": {"mode": 1, "level": 2, "entrance": 3},
            "rom_sha256": "0" * 64,
            "files": {name: hashlib.sha256(data).hexdigest()
                      for name, data in payloads.items()},
        }
        (root / "manifest.json").write_text(json.dumps(manifest))

    def test_valid_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_bundle(root)
            self.assertEqual(verify_bundle(root)["replay_frames"], 2)

    def test_hash_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_bundle(root)
            (root / "inputs.txt").write_text("000\n000\n")
            with self.assertRaisesRegex(ValueError, "hash-mismatched"):
                verify_bundle(root)

    def test_frame_interval_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_bundle(root)
            manifest_path = root / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["replay_frames"] = 3
            manifest_path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, "covered interval"):
                verify_bundle(root)


if __name__ == "__main__":
    unittest.main()
