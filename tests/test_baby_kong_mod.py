from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BabyKongModTests(unittest.TestCase):
    def test_frame_map_is_metadata_only_and_complete(self):
        frame_map = (ROOT / "runner" / "dkc1_baby_kong_frames.inc").read_text(
            encoding="utf-8")
        rows = re.findall(
            r'^\s*\{\s*"(Kiddy_[A-Za-z0-9_]+)", '
            r'0x([0-9A-Fa-f]{6})u, 0x([0-9A-Fa-f]{4})u\s*\},\s*$',
            frame_map,
            flags=re.MULTILINE,
        )
        self.assertEqual(354, len(rows))
        self.assertEqual(354, len({name for name, _, _ in rows}))
        self.assertEqual(354, len({offset for _, offset, _ in rows}))
        self.assertTrue(any(name.startswith("Kiddy_Walk") for name, _, _ in rows))
        self.assertTrue(any(name.startswith("Kiddy_Jump") for name, _, _ in rows))
        self.assertTrue(any(name.startswith("Kiddy_Roll") for name, _, _ in rows))
        self.assertTrue(any(name.startswith("Kiddy_Swim") for name, _, _ in rows))
        self.assertNotIn("incbin", frame_map.lower())
        self.assertNotIn(".bin", frame_map.lower())

    def test_movement_tuning_public_contract(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("a C compiler is required for the movement contract")

        harness = textwrap.dedent(
            r"""
            #include "dkc1_baby_kong_movement.h"
            #include <stdint.h>

            static int check(uint16_t held, uint16_t pressed, int grounded,
                             int16_t x0, int16_t y0,
                             int16_t want_x, int16_t want_y) {
              int16_t x = x0;
              int16_t y = y0;
              Dkc1BabyKongTuneVelocity(held, pressed, grounded != 0, &x, &y);
              return x == want_x && y == want_y;
            }

            int main(void) {
              if (!check(0x4000, 0, 1, 0x0400, 0, 0x0480, 0)) return 1;
              if (!check(0, 0, 1, 0x0400, 0, 0x0400, 0)) return 2;
              if (!check(0, 0x8000, 0, 0x0200, -0x0400,
                         0x0200, -0x0368)) return 3;
              if (!check(0, 0, 0, 0, 0x05f8, 0, 0x0600)) return 4;
              if (!check(0x4000, 0, 1, 0x0500, 0, 0x0500, 0)) return 5;
              return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            harness_path = directory / "baby_kong_movement_test.c"
            executable = directory / "baby_kong_movement_test"
            harness_path.write_text(harness, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "runner"),
                    str(harness_path),
                    str(ROOT / "runner" / "dkc1_baby_kong_movement.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_sprite_layout_and_wrapped_oam_envelope_contract(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("a C compiler is required for the layout contract")

        harness = textwrap.dedent(
            r"""
            #include "dkc1_baby_kong_layout.h"

            int main(void) {
              unsigned source = 99;
              if (!Dkc1BabyKongResolveTile(15, 16, 10, 14, &source) ||
                  source != 14) return 1;
              if (Dkc1BabyKongResolveTile(15, 16, 10, 15, &source)) return 2;
              if (!Dkc1BabyKongResolveTile(15, 16, 10, 16, &source) ||
                  source != 15) return 3;
              if (!Dkc1BabyKongResolveTile(15, 16, 10, 25, &source) ||
                  source != 24) return 4;

              if (Dkc1BabyKongLargeTile(0, 0, 0) != 0) return 5;
              if (Dkc1BabyKongLargeTile(0, 1, 0) != 1) return 6;
              if (Dkc1BabyKongLargeTile(0, 0, 1) != 16) return 7;
              if (Dkc1BabyKongLargeTile(7, 1, 1) != 31) return 8;
              if (Dkc1BabyKongLargeTile(8, 0, 0) != 32) return 9;

              /* Airborne OAM may wrap through Y=255; identity comes from the
               * validated contiguous attribute/X run, not a grounded Y. */
              if (!Dkc1BabyKongOamXMatches(-20)) return 10;
              if (!Dkc1BabyKongOamXMatches(72)) return 11;
              if (Dkc1BabyKongOamXMatches(73)) return 12;
              if (Dkc1BabyKongAnchorFromOpaqueBottom(121, 7) != 114)
                return 13;
              return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            harness_path = directory / "baby_kong_layout_test.c"
            executable = directory / "baby_kong_layout_test"
            harness_path.write_text(harness, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "runner"),
                    str(harness_path),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_mod_is_fail_closed_behind_verified_rom(self):
        source = (ROOT / "runner" / "dkc1_baby_kong.c").read_text(
            encoding="utf-8")
        self.assertIn("kExpectedDkc3Sha256", source)
        self.assertIn("memcmp(actual_hash, kExpectedDkc3Sha256", source)
        self.assertIn("enabled && Dkc1BabyKongReady()", source)
        self.assertIn("kPpuOverlayFlag_RemoveFromGame", source)
        self.assertIn(
            "Dkc1WramU16(wram, DKC1_WRAM_Player_CurrentKongLo) != 1u",
            source,
        )


if __name__ == "__main__":
    unittest.main()
