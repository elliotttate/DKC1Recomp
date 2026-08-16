import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_widescreen_capability_floor",
    ROOT / "tools" / "check_widescreen_capability_floor.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def contract():
    return {
        "schema": "dkc1.widescreen-capability-floor.v1",
        "source_report_schema": "dkc1.fresh-entry-stress-sweep.v1",
        "minimum_frames": 180,
        "minimum_entry_settle_frames": 360,
        "minimum_repeats": 1,
        "required_action": "neutral",
        "expected_entrances": ["0001", "00E1"],
        "centered_fixed_camera_entrances": ["00E1"],
    }


def grade(extended=100, reason=None):
    return {
        "status": "pass", "failures": [], "extended_frames": extended,
        "centered_reason": reason, "raw_margin_pixels": 0,
        "terrain_misses": 0, "strict_summary": {"policy_violations": []},
    }


def report():
    return {
        "schema": "dkc1.fresh-entry-stress-sweep.v1",
        "config": {"frames": 180, "entry_settle_frames": 360,
                   "repeats": 1, "actions": ["neutral"]},
        "branches": [
            {"entrance": 1, "action": "neutral",
             "deterministic": {"native": True, "wide": True},
             "wide_runs": [{"exit_code": 0, "widescreen_grade": grade()}]},
            {"entrance": 0xE1, "action": "neutral",
             "deterministic": {"native": True, "wide": True},
             "wide_runs": [{"exit_code": 0, "widescreen_grade": grade(
                 0, "centered_fixed_camera_arena")}]}],
    }


class WidescreenCapabilityFloorTests(unittest.TestCase):
    def test_accepts_complete_floor(self):
        self.assertEqual([], MODULE.check(report(), contract()))

    def test_rejects_missing_entrance(self):
        value = report()
        value["branches"].pop()
        self.assertTrue(any("missing entrances" in error
                            for error in MODULE.check(value, contract())))

    def test_rejects_pillarboxed_gameplay(self):
        value = report()
        value["branches"][0]["wide_runs"][0]["widescreen_grade"][
            "extended_frames"] = 0
        self.assertTrue(any("no extended gameplay frames" in error
                            for error in MODULE.check(value, contract())))

    def test_rejects_visual_fallback_or_nondeterminism(self):
        value = report()
        branch = value["branches"][0]
        branch["deterministic"]["wide"] = False
        branch["wide_runs"][0]["widescreen_grade"]["raw_margin_pixels"] = 4
        errors = MODULE.check(value, contract())
        self.assertTrue(any("not deterministic" in error for error in errors))
        self.assertTrue(any("raw margin pixels" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
