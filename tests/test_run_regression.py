import importlib.util
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "run_regression", ROOT / "tools" / "run_regression.py")
RUNNER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(RUNNER)


class RegressionTraceContractTests(unittest.TestCase):
    def test_trace_contract_accepts_clean_transition(self):
        spec = {
            "expect_empty": ["policy_violations", "raw_fallback_frames"],
            "minimum_decision_counts": {
                "edge_extension": 1, "centered_fallback": 1},
        }
        summary = {
            "policy_violations": [],
            "raw_fallback_frames": [],
            "decision_counts": {
                "edge_extension": 25, "centered_fallback": 4},
        }
        self.assertEqual(RUNNER.evaluate_trace(spec, summary), [])

    def test_trace_contract_rejects_artifact_and_missing_transition(self):
        spec = {
            "expect_empty": ["centered_nonblack_margin_frames"],
            "minimum_decision_counts": {"centered_fallback": 1},
        }
        summary = {
            "centered_nonblack_margin_frames": [{"frame": 10}],
            "decision_counts": {},
        }
        failures = RUNNER.evaluate_trace(spec, summary)
        self.assertEqual(len(failures), 2)
        self.assertIn("expected empty list", failures[0])
        self.assertIn("centered_fallback", failures[1])


if __name__ == "__main__":
    unittest.main()
