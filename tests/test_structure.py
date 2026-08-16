import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import structure  # noqa: E402

FIXTURE = json.loads((ROOT / "tests" / "fixtures" /
                      "understanding_tools.json").read_text())
CODE_NAMES = {int(entry["ea"], 16): entry
              for entry in FIXTURE["curated"]["code"]}
DEFINES = {
    0x0001: ["MusicID_CaveDwellerConcert", "SoundID_TireBounce"],
    0x0020: ["SoundID_Unknown20", "EntranceID_OrangutanGang_EnterBonus1"],
    0x0040: ["SoundID_DKTooCloseToEdge", "EntranceID_OilDrumAlley_Main"],
    0x00C0: ["EntranceID_CoralCapers_CheckpointBarrel",
             "AnimationID_Diddy_TurnWhileCrawling"],
}


class StructureTests(unittest.TestCase):
    def test_player_hit_events_omits_cross_namespace_constant_guesses(self):
        rendered = structure.render_listing(
            "CODE_BFA0F7", FIXTURE["rows"], CODE_NAMES, DEFINES, {})

        self.assertIn("Player_HandleHitEvents", rendered)
        for misleading in (
                "MusicID_CaveDwellerConcert", "SoundID_TireBounce",
                "SoundID_DKTooCloseToEdge", "EntranceID_OilDrumAlley_Main",
                "EntranceID_CoralCapers_CheckpointBarrel",
                "AnimationID_Diddy_TurnWhileCrawling"):
            self.assertNotIn(misleading, rendered)

    def test_explicit_operand_context_selects_one_namespace(self):
        rendered = structure.render_listing(
            "CODE_80C000", FIXTURE["rows"], CODE_NAMES, DEFINES, {})

        self.assertIn("; EntranceID_OilDrumAlley_Main", rendered)
        self.assertNotIn("SoundID_DKTooCloseToEdge", rendered)

    def test_listing_is_flat_and_keeps_exact_assembly_cross_reference(self):
        rendered = structure.render_listing(
            "CODE_BFA0F7", FIXTURE["rows"], CODE_NAMES, DEFINES, {})
        lines = rendered.splitlines()
        instruction_lines = [line for line in lines
                             if "; BF:" in line]

        self.assertTrue(all(line.startswith("    ")
                            for line in instruction_lines))
        self.assertIn("if (cpu.Z) goto .L0", rendered)
        self.assertIn("BEQ.b CODE_BFA115", rendered)
        self.assertIn("local labels are cross-references, not reconstructed "
                      "blocks", rendered)


if __name__ == "__main__":
    unittest.main()
