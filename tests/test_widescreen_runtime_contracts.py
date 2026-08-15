from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WidescreenRuntimeContractTests(unittest.TestCase):
    def test_type5_retry_uses_native_16_bit_push_order(self):
        source = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        body = source.split(
            "bool Dkc1VideoPrepareType5ChildRetry", 1)[1].split(
                "static const uint8_t *s_rom", 1)[0]

        native_push = re.compile(
            r"cpu->S\s*=\s*\(uint16_t\)\(cpu->S - 1u\);\s*"
            r"cpu_write16\(cpu, 0x00, cpu->S, parent_index\);\s*"
            r"cpu->S\s*=\s*\(uint16_t\)\(cpu->S - 1u\);",
            re.MULTILINE)
        self.assertRegex(body, native_push)
        self.assertNotIn(
            "cpu_write16(cpu, 0x00, cpu->S, parent_index);\n"
            "  cpu->S = (uint16_t)(cpu->S - 2u);",
            body)

    def test_presentation_bias_moves_backgrounds_and_oam_together(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        ppu = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
               "ppu.c").read_text(encoding="utf-8")

        self.assertIn("Dkc1WidescreenPresentationBias", game)
        self.assertIn("g_ppu->hScroll[layer] + presentation_bias", game)
        self.assertIn("g_ppu->hScroll[layer] - presentation_bias", game)
        self.assertIn(
            "PpuSetWidescreenPresentationXBias(g_ppu, presentation_bias)",
            game)
        self.assertIn("return x - ppu->wsPresentationXBias;", ppu)

    def test_presentation_bias_does_not_write_logical_camera_or_bounds(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        function = game.split(
            "static int Dkc1WidescreenPresentationBias", 1)[1].split(
                "static uint16_t Dkc1RollingMapWord", 1)[0]
        self.assertNotIn("g_ram[", function)
        self.assertNotIn("Dkc1Write", function)


if __name__ == "__main__":
    unittest.main()
