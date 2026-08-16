import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "run_imported_state_suite",
    ROOT / "tools" / "run_imported_state_suite.py")
SUITE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(SUITE)


def log(frame="a", wram="b", result="completed", extra=""):
    return "\n".join((
        f"frame_sha256={frame}",
        f"wram_sha256={wram}",
        "vram_sha256=c",
        "cgram_sha256=d",
        "oam_sha256=e",
        "oam_source_sha256=f",
        "run_stats video_active_frames=3 blank_frames=0 "
        "obj_range_over_frames=1 obj_time_over_frames=2",
        extra,
        f"result={result} frames=3",
    ))


class ImportedStateSuiteTests(unittest.TestCase):
    def test_parse_complete_log_and_stats(self):
        parsed = SUITE.parse_log(log())
        self.assertEqual(parsed["failures"], [])
        self.assertEqual(parsed["hashes"]["frame_sha256"], "a")
        self.assertEqual(parsed["run_stats"]["obj_time_over_frames"], 2)

    def test_diagnostic_text_fails_even_with_completed_result(self):
        parsed = SUITE.parse_log(log(extra="dispatch_oob pc=$123456"))
        self.assertIn("dispatch_oob", parsed["failures"])

    def test_determinism_is_per_domain(self):
        first = {"parsed": SUITE.parse_log(log(frame="a", wram="b"))}
        second = {"parsed": SUITE.parse_log(log(frame="x", wram="b"))}
        result = SUITE.deterministic([first, second])
        self.assertFalse(result["frame_sha256"])
        self.assertTrue(result["wram_sha256"])

    def test_oam_index_requires_latches_and_sequence(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "oam.jsonl"
            path.write_text(
                '{"frame":1,"obj_range_over":false,'
                '"obj_time_over":true}\n', encoding="utf-8")
            rows, failures = SUITE.load_oam_index(path, 1)
            self.assertEqual(len(rows), 1)
            self.assertEqual(failures, [])

            path.write_text('{"frame":2}\n', encoding="utf-8")
            _, failures = SUITE.load_oam_index(path, 1)
            self.assertTrue(any("sequence break" in item for item in failures))
            self.assertTrue(any("obj_range_over" in item
                                for item in failures))

    def test_tree_digest_is_path_and_content_sensitive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "a").write_bytes(b"one")
            first = SUITE.sha256_tree(root)
            (root / "a").write_bytes(b"two")
            self.assertNotEqual(first, SUITE.sha256_tree(root))


if __name__ == "__main__":
    unittest.main()
