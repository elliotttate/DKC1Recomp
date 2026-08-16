import json
from pathlib import Path
import tempfile
import unittest

from tools.verify_prefetch_soft_fallback import verify_runs


class PrefetchSoftFallbackVerifierTests(unittest.TestCase):
    def make_run(self, root: Path, name: str, release_frame: int = 12) -> Path:
        run = root / name
        run.mkdir()
        phase = [
            {"schema": "dkc1.prefetch-phase.v1", "event": "context_reset",
             "frame": 1},
            {"schema": "dkc1.prefetch-phase.v1", "event": "prefetch_candidate",
             "frame": 8, "actor_index": 2, "id": 77, "source": 13,
             "source_x": 100, "terrain_ready": True,
             "stock_window": [0, 90]},
            {"schema": "dkc1.prefetch-phase.v1", "event": "prefetch_suppressed",
             "frame": 8, "actor_index": 2, "id": 77, "source": 13,
             "source_x": 100, "terrain_ready": True,
             "stock_window": [0, 90]},
            {"schema": "dkc1.prefetch-phase.v1", "event": "soft_fallback_held",
             "frame": 9, "actor_index": 2, "id": 77, "source": 13,
             "source_x": 100, "terrain_ready": False,
             "stock_window": [3, 93]},
            {"schema": "dkc1.prefetch-phase.v1", "event": "prefetch_released",
             "frame": release_frame, "actor_index": 2, "id": 77,
             "source": 13, "source_x": 100, "terrain_ready": True,
             "stock_window": [10, 100]},
        ]
        ws = [
            {"schema": "dkc1.ws.frame.v1", "frame": 8,
             "decision": {"debug_forced_fallback": 0,
                          "centered_fallback": 0, "edge_extension": 1}},
            {"schema": "dkc1.ws.frame.v1", "frame": 9,
             "decision": {"debug_forced_fallback": 1,
                          "centered_fallback": 1, "edge_extension": 0}},
            {"schema": "dkc1.ws.frame.v1", "frame": 10,
             "decision": {"debug_forced_fallback": 0,
                          "centered_fallback": 0, "edge_extension": 1}},
        ]
        (run / "lifecycle.jsonl").write_text(
            "".join(json.dumps(row) + "\n" for row in phase),
            encoding="utf-8")
        (run / "ws-trace.jsonl").write_text(
            "".join(json.dumps(row) + "\n" for row in ws),
            encoding="utf-8")
        return run

    def test_three_deterministic_runs_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runs = [self.make_run(root, f"run{i}") for i in range(3)]
            report = verify_runs(runs)
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["repeat_count"], 3)
            self.assertEqual(report["verified_actors"][0]["release_frame"], 12)

    def test_non_deterministic_release_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runs = [self.make_run(root, "run0"),
                    self.make_run(root, "run1"),
                    self.make_run(root, "run2", release_frame=13)]
            with self.assertRaisesRegex(ValueError, "not byte-semantically"):
                verify_runs(runs)


if __name__ == "__main__":
    unittest.main()
