from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WidescreenRuntimeContractTests(unittest.TestCase):
    def test_visible_snapshot_library_has_named_route_anchors(self):
        recipe = (ROOT / "recipes" /
                  "capture_jungle_route_snapshots.dks").read_text(
                      encoding="utf-8")
        launcher = (ROOT / "tools" /
                    "launch_visible_snapshot.ps1").read_text(
                        encoding="utf-8")
        capture = (ROOT / "tools" /
                   "capture_visible_snapshot_library.ps1").read_text(
                       encoding="utf-8")
        validator = (ROOT / "tools" /
                     "validate_visible_snapshot_library.ps1").read_text(
                         encoding="utf-8")

        names = ("jungle-scroll-early.snapshot",
                 "jungle-scroll-mid.snapshot",
                 "jungle-scroll-late.snapshot",
                 "jungle-route-end.snapshot")
        for name in names:
            self.assertIn(f"state_save build/snapshots/{name}", recipe)
            self.assertIn(name, launcher)
            self.assertIn(name, capture)
            self.assertIn(name, validator)
        self.assertIn("jungle-stable-gameplay.snapshot", capture)
        self.assertIn("DKC1_ROUTE_RESULT", capture)
        self.assertIn("Get-FileHash -Algorithm SHA256", capture)
        self.assertIn("snapshot_smoke.dks", validator)
        self.assertIn("dkc1.snapshot-library-validation.v1", validator)
        smoke = (ROOT / "recipes" / "snapshot_smoke.dks").read_text(
            encoding="utf-8")
        self.assertIn("checkpoint snapshot_loaded", smoke)

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

    def test_shadow_calibration_is_read_only_until_commit(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        body = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]
        phase_one = body.index("/* Phase 1 is read-only")
        calibration = body.index("Dkc1CalibrateLayout(", phase_one)
        phase_two = body.index("/* Phase 2 commits only an accepted frame")
        self.assertLess(calibration, phase_two)
        provisional = body[phase_one:phase_two]
        self.assertNotRegex(provisional, r"WsShadow[A-Za-z]+\(")
        self.assertNotRegex(provisional, r"s_ws_world_[xy]\[.*\]\s*=")
        committed = body[phase_two:]
        self.assertIn("WsShadowSetWorld", committed)
        self.assertIn("WsShadowFrame(g_ppu)", committed)
        self.assertIn("trace->shadow_commit = true", committed)

    def test_hard_identity_covers_scene_source_and_ppu_shape(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        identity = game.split("typedef struct Dkc1WsIdentity", 1)[1].split(
            "} Dkc1WsIdentity;", 1)[0]
        for field in ("mode", "level", "entrance", "source_signature",
                      "bgmode", "bgsc[4]", "main_mask", "sub_mask",
                      "wide_layer_mask", "terrain_layer"):
            self.assertIn(field, identity)
        self.assertIn("Dkc1WidescreenIdentityDiff", game)
        self.assertIn("Dkc1RejectWidescreenShadow();", game)

    def test_grace_budget_is_same_identity_only_and_counts_two_misses(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        body = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]
        identity_block = body.split("if (identity_change != 0)", 1)[1].split(
            "const int keep_tiles", 1)[0]
        self.assertIn("Dkc1ClearWidescreenShadow(false)", identity_block)
        clear_body = game.split(
            "static void Dkc1ClearWidescreenShadow", 1)[1].split(
                "static void Dkc1ResetWidescreenShadow", 1)[0]
        self.assertIn("s_ws_layout = kDkc1LayoutUnknown", clear_body)
        self.assertIn("s_ws_layout_grace = 0", clear_body)
        grace_check = body.index("s_ws_layout_grace > 0")
        grace_decrement = body.index(
            "next_grace = s_ws_layout_grace - 1;", grace_check)
        self.assertLess(grace_check, grace_decrement)
        self.assertIn("next_grace = 2;", body)

    def test_level_entry_requires_published_camera_bounds_before_calibration(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        body = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]
        bounds_gate = body.index("if (!bounds_ready)")
        calibration = body.index("Dkc1CalibrateLayout(")
        commit = body.index("/* Phase 2 commits only an accepted frame")
        self.assertLess(bounds_gate, calibration)
        self.assertLess(bounds_gate, commit)
        self.assertIn(
            "upper_bound - lower_bound >= minimum_span", body)

    def test_visible_host_emits_atomic_route_results_and_can_autoclose(self):
        source = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        self.assertIn('getenv("DKC1_ROUTE_RESULT")', source)
        self.assertIn("dkc1.visible-route-result.v1", source)
        self.assertIn("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH",
                      source)
        self.assertIn('getenv("DKC1_ROUTE_FRAME_LIMIT")', source)
        self.assertIn('getenv("DKC1_ROUTE_AUTOCLOSE_MS")', source)
        self.assertIn('WriteRouteResult("aborted")', source)

    def test_visible_playback_has_an_explicit_terminal_boundary(self):
        source = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        playback = source.split("} else if (s_input_playback.count)", 1)[1]
        playback = playback.split("} else {", 1)[0]
        self.assertIn("s_host_frame >= s_input_playback.count", playback)
        self.assertIn('SetRouteTerminal(0, "complete"', playback)


if __name__ == "__main__":
    unittest.main()
