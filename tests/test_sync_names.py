import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import sync_names  # noqa: E402

FIXTURE = json.loads((ROOT / "tests" / "fixtures" /
                      "understanding_tools.json").read_text())


class SyncNamesTests(unittest.TestCase):
    def setUp(self):
        self.rows = FIXTURE["rows"]
        self.code_names = {
            int(entry["ea"], 16): entry
            for entry in FIXTURE["curated"]["code"]
        }

    def test_names_follow_dispatch_ordinal_not_target_address(self):
        derived = sync_names.derive_names(
            self.rows, self.code_names, FIXTURE["dispatches"])

        self.assertEqual(derived["0x80A200"]["name"],
                         "FixtureMachine_State0")
        self.assertEqual(derived["0x80A000"]["name"],
                         "FixtureMachine_State1")
        self.assertNotIn("0x80A100", derived)  # curated target wins
        self.assertEqual(derived["0x80A200"]["state_ordinal"], 0)

    def test_duplicate_handler_records_all_ordinals_without_renaming(self):
        dispatch = dict(FIXTURE["dispatches"][0])
        dispatch["targets"] = [0x80A200, 0x80A000, 0x80A200]
        derived = sync_names.derive_names(
            self.rows, self.code_names, [dispatch])

        self.assertEqual(derived["0x80A200"]["name"],
                         "FixtureMachine_State0")
        self.assertEqual(derived["0x80A200"]["state_ordinals"], [0, 2])

    def test_default_output_is_safe_and_reference_is_rejected(self):
        self.assertEqual(sync_names.OUT, ROOT / "docs" /
                         "derived_names.json")
        with self.assertRaises(ValueError):
            sync_names.ensure_safe_output(ROOT / "reference" / "x.json")


if __name__ == "__main__":
    unittest.main()
