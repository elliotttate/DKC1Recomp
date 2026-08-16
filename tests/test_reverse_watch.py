import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "reverse_watch_under_test", ROOT / "tools" / "reverse_watch.py")
REVERSE_WATCH = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = REVERSE_WATCH
SPEC.loader.exec_module(REVERSE_WATCH)


class WatchSpecTests(unittest.TestCase):
    def test_accepts_bounded_nonoverlapping_hex_ranges(self):
        self.assertEqual(
            REVERSE_WATCH.validate_watch_spec("0028:2, 1595:34"),
            [(0x28, 0x2), (0x1595, 0x34)])

    def test_rejects_malformed_or_unsafe_specs(self):
        rejected = [
            "", "0028:", "0028:0", "0028:2,", "0028:2,0029:1",
            "1FFFF:2", "20000:1", "xyz:1", "0:1001",
            ",".join(f"{address:x}:1" for address in range(17)),
        ]
        for spec in rejected:
            with self.subTest(spec=spec), self.assertRaises(ValueError):
                REVERSE_WATCH.validate_watch_spec(spec)


class WatchLogTests(unittest.TestCase):
    def test_separates_changes_and_fail_closed_truncation_markers(self):
        rows = [
            {"type": "watch_change", "frame": 2, "addr": "0x00028",
             "old": "0x01", "new": "0x02", "attributed": False,
             "writer_pc": None,
             "writer": "host/outside-function-window"},
            {"type": "watch_truncated", "frame": 3, "limit": 256},
            {"frame": 1, "addr": "0x00029", "old": "0x00",
             "new": "0x01", "writer_pc": "0x123456",
             "writer": "legacy"},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "watch.jsonl"
            path.write_text(
                "\n".join(json.dumps(row) for row in rows) + "\nnot-json\n",
                encoding="utf-8")
            changes, truncations = REVERSE_WATCH.load_watch_events(path)

        self.assertEqual([row["frame"] for row in changes], [2, 1])
        self.assertEqual([row["frame"] for row in truncations], [3])
        self.assertEqual(
            REVERSE_WATCH.writer_text(changes[0]),
            "host/outside-function-window (unattributed)")
        self.assertEqual(
            REVERSE_WATCH.writer_text(changes[1]),
            "legacy (0x123456)")
        self.assertEqual(REVERSE_WATCH.attribution_counts(changes), (1, 1))
        self.assertEqual(
            REVERSE_WATCH.attribution_summary(changes, 1, 2),
            "2 observed changes total (1 attributed, 1 unattributed); "
            "1 observed at or before frame 2")


if __name__ == "__main__":
    unittest.main()
