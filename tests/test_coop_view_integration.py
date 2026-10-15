from pathlib import Path
import tempfile
import unittest

import test_coop_mod

ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests/coop_view_integration_harness.c"
VIEW = ROOT / "runner/dkc1_coop_view.c"


class CoopViewIntegrationTests(unittest.TestCase):
    def run_fixture(self, case, implementation=VIEW):
        # Includes the whole .c rather than lifting helpers by neighboring text.
        source = '#define COOP_VIEW_IMPLEMENTATION "' + implementation.as_posix() + '"\n'
        source += f"#define COOP_VIEW_CASE {case}\n" + HARNESS.read_text(encoding="utf-8")
        test_coop_mod.CoopModTests().compile_harness(
            source, ["snesrecomp/runner/src/snes/interp816.c"])

    def rejects_mutation(self, case, before, after, witness):
        source = VIEW.read_text(encoding="utf-8")
        self.assertEqual(source.count(before), 1, "mutation anchor must be unique")
        with tempfile.TemporaryDirectory() as tmp:
            mutated = Path(tmp) / "dkc1_coop_view_mutated.c"
            mutated.write_text(source.replace(before, after), encoding="utf-8")
            with self.assertRaises(AssertionError) as rejected:
                self.run_fixture(case, mutated)
            # A compile failure is not a mutation witness. It must reach a C
            # runtime assertion on an observed table/state/resource contract.
            self.assertIn("Assertion", str(rejected.exception))
            self.assertIn(witness, str(rejected.exception))

    def test_two_view_composition_and_redraw_restore_guest_state(self):
        self.run_fixture(1)
        self.rejects_mutation(1,
            "  memcpy(g_ram, s_backup, sizeof s_backup);\n  Dkc1CoopClockRestore(&s_clock);",
            "  /* mutation: omit final WRAM restore */\n  Dkc1CoopClockRestore(&s_clock);",
            "!memcmp(g_ram, original_ram, sizeof original_ram)")

    def test_light_fog_and_ship_keep_their_hdma_ownership(self):
        self.run_fixture(2)
        self.rejects_mutation(2,
            "if (!fixed_room && protocol.owner == kCoopOrdinaryParallax) {",
            "if (!fixed_room) {",
            "Dkc1WramU16(observed[0].tables, 1) == 0x123")

    def test_bubble_cast_and_rope_compose_borrowed_resources_then_restore(self):
        self.run_fixture(3)

    def test_failed_preparation_is_closed_and_restores_guest_state(self):
        self.run_fixture(4)


if __name__ == "__main__":
    unittest.main()
