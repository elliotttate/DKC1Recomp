import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("grader", ROOT / "tools" / "grade_fresh_entry_sweep.py")
GRADER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(GRADER)


def row(*, extended=True, terrain=0, miss=0, raw=0, source=(1, 2), bounds=True):
    return {
        "schema": "dkc1.ws.frame.v1", "frame": 1,
        "scene": {"mode": 1, "level": 1},
        "source": {"map": source[0], "metatiles": source[1], "stream_vram": 0x7800},
        "camera": {"lower": 0, "upper": 0x400},
        "ppu": {"terrain_layer": terrain, "wide_mask": 1, "bgsc": [0x79, 0], "bgmode": 1},
        "decision": {"bounds_ready": int(bounds), "edge_extension": int(extended), "shadow_commit": int(extended)},
        "calibration": {"horizontal": [200, 224], "vertical": [20, 224]},
        "shadow_delta": [
            {"west_hit": 10, "east_hit": 10, "west_miss": miss, "east_miss": 0,
             "west_raw": raw, "east_raw": 0},
            {"west_hit": 0, "east_hit": 0, "west_miss": 999, "east_miss": 999,
             "west_raw": 0, "east_raw": 0},
        ],
    }


class GradeFreshEntryTests(unittest.TestCase):
    def write_trace(self, rows):
        temp = tempfile.TemporaryDirectory()
        path = Path(temp.name) / "trace.jsonl"
        path.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
        self.addCleanup(temp.cleanup)
        return path

    def test_parallax_misses_do_not_fail_terrain(self):
        result = GRADER.grade_repeat(self.write_trace([row()]))
        self.assertEqual("pass", result["status"])
        self.assertEqual(0, result["terrain_misses"])

    def test_terrain_miss_and_raw_fail(self):
        result = GRADER.grade_repeat(self.write_trace([row(miss=2, raw=3)]))
        self.assertEqual({"terrain_margin_miss", "raw_margin_fallback"}, set(result["failures"]))

    def test_missing_source_centered_is_classified(self):
        result = GRADER.grade_repeat(self.write_trace([row(extended=False, source=(0, 0))]))
        self.assertEqual("centered_missing_decoder_source", result["centered_reason"])

    def test_calibration_rejection_is_classified(self):
        result = GRADER.grade_repeat(self.write_trace([row(extended=False)]))
        self.assertEqual("centered_calibration_rejected", result["centered_reason"])


if __name__ == "__main__":
    unittest.main()
