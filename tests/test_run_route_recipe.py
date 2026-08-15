import importlib.util
from pathlib import Path
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "run_route_recipe.py")
SPEC = importlib.util.spec_from_file_location("run_route_recipe", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class RouteRecipeTests(unittest.TestCase):
    def test_compiles_inputs_full_predicate_and_checkpoint(self):
        recipe = {
            "schema": MODULE.SCHEMA, "name": "route-one", "steps": [
                {"type": "input", "input": "4100", "frames": 12},
                {"type": "wait_wram", "address": "1e07", "width": 2,
                 "op": "ge", "value": "48", "mask": "ff", "shift": 0,
                 "signed": False, "timeout": 300, "input": "100"},
                {"type": "checkpoint", "name": "section-48"},
            ]}
        script, budget, summary = MODULE.compile_recipe(recipe)
        self.assertIn("004100 * 12", script)
        self.assertIn(
            "hold 000100 01E07 >= 00000048 width 2 mask 000000FF timeout 300",
            script)
        self.assertTrue(script.endswith("checkpoint section-48\n"))
        self.assertEqual(budget, 313)
        self.assertEqual(summary["checkpoint_count"], 1)

    def test_rejects_unsafe_or_duplicate_checkpoint_names(self):
        base = {"schema": MODULE.SCHEMA, "name": "safe", "steps": [
            {"type": "checkpoint", "name": "../escape"}]}
        with self.assertRaisesRegex(ValueError, "safe checkpoint"):
            MODULE.compile_recipe(base)
        base["steps"] = [{"type": "checkpoint", "name": "same"},
                         {"type": "checkpoint", "name": "same"}]
        with self.assertRaisesRegex(ValueError, "duplicate"):
            MODULE.compile_recipe(base)

    def test_rejects_invalid_width_shift_and_input(self):
        for step in (
            {"type": "wait_wram", "address": "0", "width": 3,
             "op": "eq", "value": "0"},
            {"type": "wait_wram", "address": "0", "width": 1,
             "op": "eq", "value": "0", "shift": 8},
            {"type": "wait_wram", "address": "1ffff", "width": 2,
             "op": "eq", "value": "0"},
            {"type": "wait_wram", "address": "0", "width": 1,
             "op": "eq", "value": "100"},
            {"type": "input", "input": "xyz", "frames": 1},
        ):
            with self.subTest(step=step):
                with self.assertRaises(ValueError):
                    MODULE.compile_recipe({"schema": MODULE.SCHEMA,
                                           "name": "bad", "steps": [step]})


if __name__ == "__main__":
    unittest.main()
