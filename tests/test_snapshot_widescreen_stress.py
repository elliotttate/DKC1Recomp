import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "snapshot_widescreen_stress",
    ROOT / "tools" / "snapshot_widescreen_stress.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class SnapshotWidescreenStressTests(unittest.TestCase):
    def test_fixed_and_pattern_segments_cover_exact_frame_count(self):
        for action in list(MODULE.MASKS) + list(MODULE.PATTERNS):
            for frames in (1, 59, 60, 61, 419):
                segments = MODULE.build_segments(action, frames)
                self.assertEqual(sum(count for _, count in segments), frames)
                self.assertTrue(all(count > 0 for _, count in segments))

    def test_action_parser_rejects_unknown_and_duplicate_names(self):
        self.assertEqual(MODULE.parse_actions("neutral,right_y"),
                         ["neutral", "right_y"])
        with self.assertRaisesRegex(ValueError, "unknown actions"):
            MODULE.parse_actions("neutral,teleport")
        with self.assertRaisesRegex(ValueError, "duplicate actions"):
            MODULE.parse_actions("neutral,neutral")

    def test_blank_signature_retains_causal_fields(self):
        rows = [{"frame": 28, "kind": "full_flat_gameplay",
                 "suspect_columns": 86, "first_x": 0, "width": 342,
                 "color": "ff000000"}]
        self.assertEqual(MODULE.blank_signature(rows),
                         [(28, "full_flat_gameplay", 86, 0, 342)])

    def test_trigger_window_converts_one_based_detector_frame(self):
        self.assertEqual(MODULE.trigger_window(1, 10), (0, 2))
        self.assertEqual(MODULE.trigger_window(5, 10), (2, 6))
        self.assertEqual(MODULE.trigger_window(10, 10), (7, 9))
        with self.assertRaises(ValueError):
            MODULE.trigger_window(0, 10)

    def test_passing_strict_grade_cannot_create_a_trigger(self):
        run = {"blank_events": [],
               "widescreen_grade": {"status": "pass"},
               "artifacts": {}}
        self.assertIsNone(MODULE.first_failure_frame(run))


if __name__ == "__main__":
    unittest.main()
