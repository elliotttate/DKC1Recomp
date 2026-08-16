import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import state_catalog  # noqa: E402
import sync_names  # noqa: E402

FIXTURE = json.loads((ROOT / "tests" / "fixtures" /
                      "understanding_tools.json").read_text())
LIFECYCLE = ROOT / "tests" / "fixtures" / "understanding_lifecycle.jsonl"


class StateCatalogTests(unittest.TestCase):
    def code_names(self):
        names = {int(entry["ea"], 16): entry
                 for entry in FIXTURE["curated"]["code"]}
        derived = sync_names.derive_names(
            FIXTURE["rows"], names, FIXTURE["dispatches"])
        names.update((int(key, 16), value)
                     for key, value in derived.items())
        return names

    def test_catalog_preserves_ordinal_and_renders_observed_evidence(self):
        observed = state_catalog.load_observed_states(LIFECYCLE)
        rendered, machines = state_catalog.render_catalog(
            FIXTURE["rows"], self.code_names(), FIXTURE["dispatches"],
            observed)

        self.assertEqual(machines, 1)
        state0 = rendered.index("$00: FixtureMachine_State0")
        state1 = rendered.index("$01: FixtureMachine_State1")
        state2 = rendered.index("$02: FixtureCuratedHandler")
        self.assertLess(state0, state1)
        self.assertLess(state1, state2)
        self.assertIn("$00: FixtureMachine_State0 [table-derived] | "
                      "(observed: sprite $2A)", rendered)
        self.assertIn("$01: FixtureMachine_State1 [table-derived] | -",
                      rendered)
        self.assertIn("$02: FixtureCuratedHandler [curated] | "
                      "(observed: sprite $2A)", rendered)

    def test_lifecycle_loader_rejects_non_actor_lookalikes(self):
        observed = state_catalog.load_observed_states(LIFECYCLE)
        self.assertEqual(observed, {0x2A: {0, 2}})

    def test_accumulator_clobber_prevents_false_immediate_store(self):
        body = [row for row in FIXTURE["rows"]
                if row["function"] == "CODE_80A000"]
        mined = state_catalog.mine_function(body)
        self.assertEqual(mined["state_stores"], set())


if __name__ == "__main__":
    unittest.main()
