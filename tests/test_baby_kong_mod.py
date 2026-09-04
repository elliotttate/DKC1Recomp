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

        animation_source = (
            ROOT / "runner" / "dkc1_baby_kong_animation.c"
        ).read_text(encoding="utf-8")
        mapped_ids = {
            int(value, 16)
            for value in re.findall(
                r"^\s*\[0x([0-9a-f]+)\]\s*=", animation_source,
                flags=re.MULTILINE,
            )
        }
        self.assertEqual(set(range(1, 0x69)), mapped_ids)
        animation_groups = set(re.findall(r'"(Kiddy_[A-Za-z]+)"',
                                          animation_source))
        frame_names = {name for name, _, _ in rows}
        missing_groups = [
            group for group in sorted(animation_groups)
            if group not in frame_names and not any(
                re.fullmatch(re.escape(group) + r"\d+", name)
                for name in frame_names
            )
        ]
        self.assertEqual([], missing_groups)

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

    def test_animation_mapping_follows_dkc1_animation_state(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if compiler is None:
            self.skipTest("a C compiler is required for the animation contract")

        harness = textwrap.dedent(
            r"""
            #include "dkc1_baby_kong_animation.h"
            #include <stdint.h>
            #include <string.h>

            static int group_is(uint16_t animation, uint16_t state,
                                int16_t x, int16_t y, const char *group) {
              Dkc1BabyKongAnimationInput input = {
                animation, state, 0, x, y
              };
              Dkc1BabyKongAnimationChoice choice =
                  Dkc1BabyKongClassifyAnimation(&input);
              return strcmp(choice.group, group) == 0;
            }

            int main(void) {
              /* Grounded DKC1 Donkey carries Y=-$0300. It must never be
               * mistaken for an airborne jump. */
              if (!group_is(0x01, 0, 0, -0x0300,
                            "Kiddy_LookAroundIdle")) return 1;
              if (!group_is(0x03, 0, 0x0100, -0x0300,
                            "Kiddy_Walk")) return 2;
              if (!group_is(0x02, 0, 0x0300, -0x0300,
                            "Kiddy_Run")) return 3;
              if (!group_is(0x05, 1, 0x0200, -0x0500,
                            "Kiddy_Jump")) return 4;
              if (!group_is(0x18, 19, 0x0300, -0x0300,
                            "Kiddy_Roll")) return 5;
              if (!group_is(0x0c, 11, 0, -0x0300,
                            "Kiddy_Hurt")) return 6;
              if (!group_is(0x47, 0, 0, -0x0300,
                            "Kiddy_Pickup")) return 7;
              if (!group_is(0x49, 0, 0x0100, -0x0300,
                            "Kiddy_HoldWalk")) return 8;
              if (!group_is(0x4a, 0, 0, -0x0300,
                            "Kiddy_Throw")) return 9;
              if (!group_is(0x54, 0, 0, -0x0300,
                            "Kiddy_Duck")) return 10;
              if (!group_is(0x5c, 0, 0, 0,
                            "Kiddy_ClimbUpSingleVerticalRope")) return 11;
              if (!group_is(0x5e, 0, 0, 0,
                            "Kiddy_HangOnVerticalRope")) return 19;
              if (!group_is(0x60, 43, 0, 0,
                            "Kiddy_Swim")) return 12;
              if (!group_is(0x63, 0, 0, -0x0300,
                            "Kiddy_Victory")) return 13;
              if (!group_is(0x67, 0, 0, -0x0300,
                            "Kiddy_Tantrum")) return 14;

              if (!Dkc1BabyKongStateIsGrounded(0) ||
                  !Dkc1BabyKongStateIsGrounded(18) ||
                  !Dkc1BabyKongStateIsGrounded(19) ||
                  Dkc1BabyKongStateIsGrounded(1)) return 15;
              if (!Dkc1BabyKongStateIsAirborne(1) ||
                  Dkc1BabyKongStateIsAirborne(0)) return 16;

              Dkc1BabyKongAnimationTracker tracker = {0};
              Dkc1BabyKongAnimationInput walk = {
                0x03, 0, 224, 0x0100, -0x0300
              };
              Dkc1BabyKongAnimationChoice choice =
                  Dkc1BabyKongClassifyAnimation(&walk);
              unsigned first = Dkc1BabyKongAnimationFrame(
                  &tracker, &walk, choice, 16);
              unsigned changed = first;
              for (int i = 0; i < 8; i++) {
                walk.native_pose = (uint16_t)(224 + i * 4);
                changed = Dkc1BabyKongAnimationFrame(
                    &tracker, &walk, choice, 16);
              }
              if (first != 0 || changed == first) return 17;

              Dkc1BabyKongAnimationInput jump = {
                0x05, 1, 304, 0, -0x0600
              };
              choice = Dkc1BabyKongClassifyAnimation(&jump);
              Dkc1BabyKongAnimationReset(&tracker);
              unsigned rise = Dkc1BabyKongAnimationFrame(
                  &tracker, &jump, choice, 8);
              jump.y_velocity = 0x0600;
              unsigned fall = Dkc1BabyKongAnimationFrame(
                  &tracker, &jump, choice, 8);
              if (rise != 0 || fall != 7) return 18;
              return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            harness_path = directory / "baby_kong_animation_test.c"
            executable = directory / "baby_kong_animation_test"
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
                    str(ROOT / "runner" / "dkc1_baby_kong_animation.c"),
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
              if (Dkc1BabyKongAnchorFromOpaqueCenters(80, 120, -20, 20)
                  != 100) return 14;
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
