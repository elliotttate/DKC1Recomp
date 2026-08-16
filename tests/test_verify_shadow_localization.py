import json
from pathlib import Path
import tempfile
import unittest

from tools.verify_shadow_localization import verify


def frame(miss=0, origin_x=0x9400):
    return {
        "schema": "dkc1.ws.frame.v1", "frame": 1,
        "identity": {"hash": "bonus"},
        "decision": {"edge_extension": 1},
        "world": [
            {"valid": 1, "x": 0x9AF9, "y": 0x0112,
             "shadow_x": 0x06F9, "shadow_y": 0x0012},
            {"valid": 1, "x": 0x997C, "y": 0x0044,
             "shadow_x": 0x017C, "shadow_y": 0x0044},
        ],
        "shadow_origin": [
            {"valid": 1, "x": origin_x, "y": 0x0100},
            {"valid": 1, "x": 0x9800, "y": 0x0000},
        ],
        "ppu": {"terrain_layer": 0},
        "shadow_delta": [
            {"west_hit": 10, "east_hit": 10,
             "west_miss": miss, "east_miss": 0},
            {"west_hit": 0, "east_hit": 0,
             "west_miss": 0, "east_miss": 0},
        ],
    }


class ShadowLocalizationVerifierTests(unittest.TestCase):
    def write(self, root: Path, row):
        path = root / "trace.jsonl"
        path.write_text(json.dumps(row) + "\n", encoding="utf-8")
        return path

    def test_high_world_localization_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = verify(self.write(Path(temporary), frame()))
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["high_world_layer_samples"], 2)

    def test_terrain_miss_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "terrain margin miss"):
                verify(self.write(Path(temporary), frame(miss=1)))

    def test_misaligned_origin_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            row = frame(origin_x=0x9401)
            row["world"][0]["shadow_x"] = row["world"][0]["x"] - 0x9401
            with self.assertRaisesRegex(ValueError, "unsafe alignment"):
                verify(self.write(Path(temporary), row))


if __name__ == "__main__":
    unittest.main()
