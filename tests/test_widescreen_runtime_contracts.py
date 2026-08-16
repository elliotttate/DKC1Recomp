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

    def test_first_divergence_can_resume_verified_visible_baseline(self):
        source = (ROOT / "tools" / "first_divergence.py").read_text(
            encoding="utf-8")
        self.assertIn("--reuse-existing-baseline", source)
        self.assertIn("reused_dump", source)
        self.assertIn("empty baseline hash log", source)
        self.assertIn("nondeterministic_semantic_replay", source)

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

    def test_rope_oam_merge_clears_then_sets_x_high(self):
        source = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        body = source.split(
            "uint16_t Dkc1VideoMergeOamSizeAndXHigh", 1)[1].split(
                "bool Dkc1VideoPrepareType5ChildRetry", 1)[0]
        self.assertIn("existing_word & ~x_high_bits", body)
        self.assertIn("screen_x & 0x0100u", body)
        self.assertIn("existing_word | size_mask", body)
        injector = (ROOT / "scripts" /
                    "apply_dkc1_widescreen_overrides.py").read_text(
                        encoding="utf-8")
        self.assertIn("uint16 _ws_rope_x = cpu_read_a16(cpu);", injector)
        self.assertIn("_ws_cull_x = Dkc1VideoBiasCullX(_ws_rope_x)",
                      injector)
        self.assertIn("_ws_rope_x);", injector)

    def test_placed_actor_prefetch_delays_behavior_not_allocation(self):
        source = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        body = source.split(
            "bool Dkc1VideoShouldRunPlacedActor", 1)[1].split(
                "bool Dkc1VideoPrepareType5ChildRetry", 1)[0]
        self.assertIn("record_type != 0x0001u", body)
        self.assertIn("current_left + extra - bias", body)
        self.assertIn("current_right - extra - bias", body)
        self.assertIn("cpu->D + 0x0082u", body)
        self.assertNotIn("cpu->D + 0x0084u", body)
        self.assertIn("s_placed_actor_phases_seeded", body)
        self.assertIn("seeded->stock_started = seeded->id != 0", body)
        self.assertIn("phase->stock_started", body)
        self.assertIn("Dkc1VideoObserveActorPool", source)
        self.assertIn("Dkc1VideoObservePlacedActorContext(wram)", source)
        self.assertIn("current.mode == s_placed_actor_context.mode", source)
        self.assertIn("current.level == s_placed_actor_context.level", source)
        self.assertIn(
            "current.entrance == s_placed_actor_context.entrance", source)
        observer = source.split(
            "void Dkc1VideoObserveActorPool", 1)[1].split(
                "bool Dkc1VideoShouldRunPlacedActor", 1)[0]
        self.assertIn("id != 0", observer)
        self.assertIn("phase->id = 0", observer)
        self.assertIn("phase->stock_started = false", observer)
        self.assertIn("Dkc1VideoBeginPlacedActorDispatch", source)
        self.assertIn("DKC1_PREFETCH_PHASE_GUARD", source)
        self.assertIn("kDkc1WramSize = 0x20000", source)
        self.assertIn("memcpy(s_prefetch_wram, cpu->ram", source)
        self.assertIn("memcpy(cpu->ram, s_prefetch_wram", source)
        self.assertIn("Dkc1VideoEndPlacedActorDispatch", source)
        terrain = source.split(
            "void Dkc1VideoSetTerrainReady", 1)[1].split(
                "bool Dkc1VideoTerrainReady", 1)[0]
        self.assertNotIn("Dkc1VideoResetPlacedActorPhases", terrain)
        self.assertIn("s_terrain_ready = g_ws_active && ready", terrain)
        # On a soft presentation fallback, the current DP interval is already
        # the stock interval.  It must still gate a previously-prefetched
        # identity instead of reseeding or unconditionally running it.
        self.assertIn("uint16_t stock_left = current_left", body)
        self.assertIn("uint16_t stock_right = current_right", body)
        self.assertIn("if (Dkc1VideoTerrainReady())", body)
        self.assertIn(
            "phase->stock_started = !Dkc1VideoTerrainReady()", body)
        injector = (ROOT / "scripts" /
                    "apply_dkc1_widescreen_overrides.py").read_text(
                        encoding="utf-8")
        self.assertIn("Dkc1VideoBeginPlacedActorDispatch(cpu)", injector)
        self.assertIn("Dkc1VideoEndPlacedActorDispatch(cpu)", injector)
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        frame = game.split("static void Dkc1RunOneFrame", 1)[1].split(
            "static void Dkc1SaveExtra", 1)[0]
        self.assertLess(frame.index("Dkc1VideoObserveActorPool(g_ram)"),
                        frame.index("interp_bridge_run_until_quiescent"))
        self.assertGreaterEqual(
            game.count("Dkc1VideoResetPlacedActorPhases();"), 2)

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

    def test_wide_layers_use_physical_width_not_terrain_mask_for_repeat(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        draw = game.split("void Dkc1DrawPpuFrame", 1)[1]
        prepare = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]

        self.assertIn("WsShadowSetRawContinuation", prepare)
        self.assertIn("PPU_bgTilemapWider(g_ppu, layer) != 0", prepare)
        self.assertIn("parallax_continuation ? -1", prepare)
        self.assertIn("for (int layer = 0; layer < 3; layer++)", draw)
        self.assertIn("PPU_bgTilemapWider(g_ppu, layer) != 0", draw)
        self.assertIn("physical_wide_mask", draw)
        self.assertIn("wide_layer_mask | physical_wide_mask", draw)
        self.assertIn("PpuSetWidescreenLayerMask(g_ppu, render_mask)", draw)
        self.assertIn("PpuSetWidescreenBg3Widen(", draw)
        self.assertIn("physical_wide_mask & 0x04u", draw)
        self.assertNotIn("enabled & (uint8_t)~wide_layer_mask", draw)
        # This cave's register shape is the regression: BG1 is 64x32, BG2 is
        # 32x32, and BG3 is 64x64. Only BG2 may repeat.
        bgsc = (0x69, 0x7C, 0x5B)
        enabled = 0x07
        repeat = sum((1 << layer) for layer, value in enumerate(bgsc)
                     if enabled & (1 << layer) and not (value & 1))
        self.assertEqual(repeat, 0x02)
        physical_wide = sum(
            (1 << layer) for layer, value in enumerate(bgsc)
            if enabled & (1 << layer) and (value & 1))
        self.assertEqual(physical_wide, 0x05)
        self.assertEqual(0x01 | physical_wide, 0x05)

    def test_raw_parallax_continuation_is_not_counted_as_unsafe_fallback(self):
        header = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                  "ws_shadow.h").read_text(encoding="utf-8")
        source = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                  "ws_shadow.c").read_text(encoding="utf-8")
        trace = (ROOT / "runner" / "dkc1_ws_trace.c").read_text(
            encoding="utf-8")

        self.assertIn("westRawContinuation, eastRawContinuation", header)
        continuation = source.split(
            "if (layer->rawContinuation && layer->wide)", 1)[1].split(
                "if (screenX < 0)\n    s_marginStats[layerIndex].westRawFallback",
                1)[0]
        self.assertIn("westRawContinuation++", continuation)
        self.assertIn("kWsShadowProvenanceRawContinuation", continuation)
        self.assertNotIn("RawFallback", continuation)
        self.assertIn('"\\\"west_continuation\\\":%llu', trace)

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

    def test_visible_manual_input_tracks_release_and_clears_on_focus_loss(self):
        source = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        self.assertIn("InputBitForVirtualKey", source)
        self.assertIn("case WM_KEYUP:", source)
        self.assertIn("s_manual_input &= ~bit;", source)
        self.assertIn("case WM_KILLFOCUS:", source)
        self.assertIn("s_manual_input = 0;", source)
        poll = source.split("static uint32_t PollInput(void)", 1)[1].split(
            "static void AudioInit", 1)[0]
        self.assertIn("GetAsyncKeyState", poll)
        self.assertIn("s_manual_input = physical;", poll)
        self.assertIn("return physical;", poll)

    def test_visible_snapshot_launcher_clears_automation_input(self):
        source = (ROOT / "tools" / "launch_visible_snapshot.ps1").read_text(
            encoding="utf-8")
        self.assertIn("SNESRECOMP_INPUT_PLAY", source)
        self.assertIn("DKC1_SCRIPT", source)
        self.assertIn("DKC1_ROUTE_FRAME_LIMIT", source)
        self.assertIn("DKC1_ROUTE_AUTOCLOSE_MS", source)
        self.assertIn("SetEnvironmentVariable($name, $null, 'Process')", source)

    def test_desktop_exposes_runtime_native_and_16_9_aspect_modes(self):
        source = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        self.assertIn('"Native &4:3 (256x224)"', source)
        self.assertIn('"Widescreen &16:9 (342x224)"', source)
        self.assertIn("CheckMenuRadioItem(s_menu, kMenuAspectNative,",
                      source)
        aspect = source.split("static void SetAspectMode", 1)[1].split(
            "static void SetFullscreen", 1)[0]
        self.assertIn("Dkc1VideoSetWidescreen(requested);", aspect)
        self.assertIn("s_width = Dkc1VideoWidth();", aspect)
        self.assertIn("s_bmi.bmiHeader.biWidth = s_width;", aspect)
        self.assertIn("const int source_x = old_width > s_width", aspect)
        self.assertIn("const int dest_x = s_width > old_width", aspect)
        self.assertIn("memset(remapped, 0, sizeof remapped);", aspect)
        self.assertIn("s_aspect_wide_frame == s_host_frame", aspect)
        self.assertIn("memcpy(s_pixels, s_aspect_wide_pixels", aspect)
        self.assertIn("Dkc1BeginDrawing(s_pixels, (size_t)s_width * 4);",
                      aspect)
        self.assertIn("ApplyWindowedSize();", aspect)
        self.assertIn("case kMenuAspectNative:", source)
        self.assertIn("SetAspectMode(0);", source)
        self.assertIn("case kMenuAspectWidescreen:", source)
        self.assertIn("SetAspectMode(1);", source)

    def test_jump_animation_exit_callback_is_in_dispatch_contract(self):
        config = (ROOT / "recomp" / "bankbe.cfg").read_text(
            encoding="utf-8")
        dispatch = next(
            line for line in config.splitlines()
            if line.startswith("indirect_dispatch 8179 "))
        match = re.search(r"^indirect_dispatch 8179 (\d+).*targets:(\S+)$",
                          dispatch)
        self.assertIsNotNone(match)
        targets = match.group(2).split(",")
        self.assertEqual(int(match.group(1)), len(targets))
        # DATA_BEA6A9's final Op81 invokes CODE_BEA778. It switches the
        # completed jump to idle/ground movement; skipping it makes Op80
        # restart the script and applies CODE_BEA7D6's impulse forever.
        self.assertIn("BEA778", targets)


if __name__ == "__main__":
    unittest.main()
