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
        self.assertNotIn("seeded->stock_started = seeded->id != 0", body)
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
        self.assertIn("before the cartridge scanner", observer)
        self.assertIn("phase->stock_started = phase->id != 0", observer)
        self.assertIn("s_placed_actor_phases_seeded = true", observer)
        self.assertIn("id != 0", observer)
        self.assertIn("phase->id = 0", observer)
        self.assertIn("phase->stock_started = false", observer)
        self.assertIn("Dkc1VideoBeginPlacedActorDispatch", source)
        self.assertIn("DKC1_PREFETCH_PHASE_GUARD", source)
        self.assertIn("kDkc1WramSize = 0x20000", source)
        self.assertIn("memcpy(s_prefetch_wram, cpu->ram", source)
        self.assertIn("memcpy(cpu->ram, s_prefetch_wram", source)
        self.assertIn("phase->stock_started", source)
        self.assertIn("Dkc1VideoEndPlacedActorDispatch", source)
        self.assertIn("prefetch_candidate", source)
        self.assertIn("prefetch_suppressed", source)
        self.assertIn("prefetch_released", source)
        self.assertIn("soft_fallback_held", source)
        self.assertIn("Dkc1DebugTracePlacedActorContext", source)
        debug = (ROOT / "runner" / "dkc1_debug_dump.c").read_text(
            encoding="utf-8")
        self.assertIn("dkc1.prefetch-phase.v1", debug)
        self.assertIn('\\"stock_window\\":[%u,%u]', debug)
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
        self.assertIn("DKC1_WS_FORCE_FALLBACK_FRAME", game)
        self.assertIn("!debug_forced_fallback", game)
        self.assertIn("stream_revalidated", game)
        self.assertIn("const bool extend_world = shadow_world_ready", game)
        self.assertNotIn(
            "const bool extend_world = shadow_world_ready || "
            "cartridge_stream_ready", game)
        ws_trace = (ROOT / "runner" / "dkc1_ws_trace.c").read_text(
            encoding="utf-8")
        self.assertIn("debug_forced_fallback", ws_trace)
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

    def test_margin_proxy_preserves_the_global_actor_draw_order(self):
        source = (ROOT / "runner" / "dkc1_margin_proxy.c").read_text(
            encoding="utf-8")
        self.assertIn("kDkc1ProxyWordDrawOrder = 1", source)
        begin = source.split(
            "void Dkc1MarginProxyBeginRender", 1)[1].split(
                "void Dkc1MarginProxyEndRender", 1)[0]
        self.assertIn(
            "SetActorWord(g_ram, slot->actorIndex, "
            "kDkc1ProxyWordDrawOrder,", begin)
        self.assertIn("slot->words[kDkc1ProxyWordDrawOrder]", begin)
        end = source.split(
            "void Dkc1MarginProxyEndRender", 1)[1].split(
                "size_t Dkc1MarginProxySnapshotSize", 1)[0]
        self.assertIn("const uint16_t proxy_draw_order", end)
        self.assertIn(
            "proxy->words[kDkc1ProxyWordDrawOrder] = proxy_draw_order", end)

    def test_presentation_bias_does_not_write_logical_camera_or_bounds(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        function = game.split(
            "static int Dkc1WidescreenPresentationBias", 1)[1].split(
                "static uint16_t Dkc1RollingMapWord", 1)[0]
        self.assertNotIn("g_ram[", function)
        self.assertNotIn("Dkc1Write", function)

    def test_cartridge_stream_widening_is_complete_and_fail_closed(self):
        video = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        injector = (ROOT / "scripts" /
                    "apply_dkc1_widescreen_overrides.py").read_text(
                        encoding="utf-8")
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")

        self.assertIn("kDkc1StreamMargin", video)
        self.assertIn("(kDkc1VideoWidescreenExtra + 7) & ~7", video)
        self.assertIn("native_backstep == 0x0100u", video)
        self.assertIn(
            "shared stock initializer is not itself proof", video)
        self.assertIn("fixed cave tilemap whose stock initializer", video)
        self.assertIn("capability boundary", video)
        self.assertIn("return 0x0170u", video)
        self.assertIn("return 0x0178u", video)
        self.assertIn("return 0x002eu", video)
        self.assertIn("return 0x002fu", video)
        self.assertIn("samples seven complete margin tiles", video)
        self.assertIn("Dkc1VideoCartridgeWideningSceneEligible", video)
        self.assertIn("DKC1_ENABLE_EXPERIMENTAL_CARTRIDGE_WIDENING", video)
        self.assertIn("Fail closed", video)
        self.assertIn(
            "mode == 0x0001u && level == 0x0009u && entrance == 0x0006u",
            video)
        self.assertIn("Keep cartridge execution stock", video)
        self.assertIn("scene_eligible=%u", video)
        self.assertIn("native_count == 0x0020u", video)
        self.assertIn("Dkc1VideoBeginStreamCoverage(cpu, 0x2eu)", video)
        self.assertIn("Dkc1VideoBeginStreamCoverage(cpu, 0x2fu)", video)
        self.assertIn(
            "stock_stream_x + kDkc1StreamMargin + bias", video)
        self.assertIn(
            "stock_stream_x + kDkc1StreamMargin + 8 + bias", video)
        self.assertIn("Dkc1VideoAlignedStreamBias(cpu, target_x)", video)
        self.assertIn("layer_x > upper", video)
        self.assertIn("layer_x != target_x", video)
        self.assertIn("Dkc1VideoObserveStreamColumn", video)
        self.assertIn("unique_columns >=", video)
        self.assertIn("s_stream_coverage.entrance == entrance", video)
        self.assertIn("s_stream_coverage.required_columns != 0", video)
        self.assertIn("!s_stream_coverage.ready", video)
        self.assertIn("stable frame-boundary identity", video)
        self.assertIn("Dkc1VideoCartridgeTerrainReady", video)
        self.assertIn("Dkc1VideoInvalidateStreamCoverage", video)
        self.assertIn("upper >= lower", video)
        self.assertIn("2 * Dkc1VideoExtra()", video)
        self.assertIn("initialization_active", video)
        self.assertIn("0x809ec4u", game)
        self.assertIn("0x809ed6u", game)
        self.assertNotIn("0x809ed5u", game)
        self.assertIn("0x80c56eu", game)
        self.assertIn("0x80c57du", game)
        self.assertIn("Dkc1InterpreterInitialColumnCount", game)
        self.assertIn("interp_bridge_pre_opcode_redirect", game)
        observe_body = video.split(
            "static void Dkc1VideoObserveStreamColumn", 1)[1].split(
                "bool Dkc1VideoCartridgeTerrainReady", 1)[0]
        self.assertNotIn("Dkc1VideoSyncStreamContext", observe_body)
        self.assertIn("Dkc1VideoSyncStreamContext(wram)", video)

        for symbol in (
                "Level_BuildTilemapColumn_TypeA_M0X0",
                "Level_DMATilemapColumnToVRAM_M0X0",
                "CODE_8188A8_M0X0",
                "Level_BuildTilemapColumn_TypeB_M0X0"):
            self.assertIn(symbol, injector)
        self.assertIn("adapt_stream_selector", injector)
        self.assertIn("adapt_function_cpu_constant", injector)
        self.assertNotIn("shadow_world_ready || cartridge_stream_ready", game)
        self.assertIn("const bool extend_world = shadow_world_ready", game)
        self.assertIn("stream_revalidated", game)
        self.assertIn(
            "cartridge_stream_ready &&\n             s_ws_layout != kDkc1LayoutUnknown",
            game)
        self.assertIn("Dkc1VideoInvalidateStreamCoverage();", game)
        self.assertIn("stream_bootstrap_rejected", game)
        self.assertIn("Dkc1VideoSetTerrainReady(true);", game)
        self.assertIn("WsShadowCaptureTile", game)
        self.assertIn("Dkc1RollingMapWord(ppu_map_base, wtx, wty)", game)
        self.assertIn("trace.cartridge_stream_ready", game)

        # A 44-column standard fill must cover 352 pixels in distinct ring
        # columns, with a complete 342-pixel 16:9 viewport inside it.
        selected = [(-352 + i * 8 + 304 + 48) & 0xffff
                    for i in range(44)]
        self.assertEqual(selected[0], 0)
        self.assertEqual(selected[-1], 344)
        self.assertEqual(len({(x >> 3) & 63 for x in selected}), 44)

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

    def test_level_decoder_calibrates_cartridge_authentic_definition_banks(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        video = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        header = (ROOT / "runner" / "dkc1_video.h").read_text(
            encoding="utf-8")
        trace = (ROOT / "runner" / "dkc1_ws_trace.c").read_text(
            encoding="utf-8")

        self.assertIn(
            "const uint8_t alternate_definition_bank = g_ram[0x00d6];",
            game)
        self.assertIn("Dkc1DefinitionBankCandidates", game)
        self.assertIn("uint8_t definition_bank", header)
        self.assertIn("RomWord(map_bank, map_offset", video)
        self.assertIn("RomWord(definition_bank, definition_offset", video)
        self.assertIn("metatile_bank << 56", game)
        self.assertIn(r'\"metatile_bank\":%u', trace)
        self.assertIn("? map_bank : 0x81u", game)
        self.assertIn("alternate_bank != banks[0]", game)
        self.assertIn(r'\"definition_bank\":%d', trace)

        # CODE_818705 keeps DB=$D5 for horizontal map and definition reads;
        # CODE_818DFA restores PB=$81 before vertical definition reads. The
        # alternate $D6 bank remains available for specialized rooms. Exact
        # underwater repro E9:0000/D0:0000 scores 212/224 and therefore wins
        # safely without forcing D0 onto ordinary land maps.
        self.assertGreaterEqual(212 * 10, 224 * 7)

    def test_calibration_scores_both_cartridge_coordinate_systems(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        body = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]
        self.assertIn(
            "(uint16_t)(g_ppu->hScroll[layer] + presentation_bias)", body)
        self.assertIn("const uint32_t calibration_x[2]", body)
        self.assertIn("const uint32_t calibration_y[2]", body)
        self.assertIn("const uint32_t cartridge_ppu_x", body)
        self.assertIn("const uint32_t cartridge_ppu_y", body)
        self.assertIn("uint32_t capture_world_x[2]", body)
        self.assertIn("uint32_t capture_world_y[2]", body)
        self.assertIn("WsShadowSetCaptureWorld", body)
        self.assertIn("WsShadowSetNativeViewportInset", body)
        self.assertIn("capture_world_x[layer] = Dkc1VideoUnwrapPpuScroll", body)
        self.assertIn("g_ppu->hScroll[layer], candidate_world_x[layer]", body)
        self.assertIn("g_ppu->vScroll[layer], candidate_world_y[layer]", body)
        self.assertIn("camera_x,", body)
        self.assertIn("camera_y,", body)
        calibration = body.split("const uint32_t calibration_x", 1)[1].split(
            "Dkc1LevelLayout best", 1)[0]
        self.assertNotIn("presentation_bias", calibration)
        call = body.index("Dkc1CalibrateLayout(")
        self.assertIn("calibration_x[coordinate_source]",
                      body[call:call + 700])
        self.assertIn("calibration_y[coordinate_source]",
                      body[call:call + 700])
        self.assertIn("accepted_decode_tile_offset_x", body)
        self.assertIn("signed_decode_wtx", body)
        self.assertIn("signed_decode_wty", body)
        self.assertIn("trace.decode_tile_offset_x", game)
        self.assertIn(r'\"decode_tile_offset\":[%d,%d]',
                      (ROOT / "runner" / "dkc1_ws_trace.c").read_text(
                          encoding="utf-8"))
        self.assertIn("s_ws_definition_bank = accepted_definition_bank", body)
        self.assertIn("snapshot.definitionBank = s_ws_definition_bank", game)
        self.assertIn("s_ws_definition_bank == 0", game)

        # A host-only edge bias must not become a ROM-map tile offset.  At the
        # Jungle Hijinxs banana-hoard exit the logical/PPU X is 0 while the
        # 16:9 presentation camera is biased inward by 43 pixels.  The old
        # `(source >> 3) - (wx >> 3)` calculation produced -5 and decoded the
        # wrong margin metatiles.  Both cartridge coordinates are 0 here, so
        # the authored offset remains 0.  A real vertical-map phase difference
        # is still retained independently of presentation.
        ppu_x, presentation_bias, selected_x = 0, 43, 0
        wx = ppu_x + presentation_bias
        self.assertEqual((selected_x >> 3) - (ppu_x >> 3), 0)
        self.assertEqual((selected_x >> 3) - (wx >> 3), -5)
        ppu_y, selected_y = 0x100, 0
        self.assertEqual((selected_y >> 3) - (ppu_y >> 3), -32)

        offset_block = body.split("const int64_t best_offset_x", 1)[1].split(
            "const bool calibrated", 1)[0]
        self.assertIn("Dkc1NearestTileDelta", offset_block)
        self.assertIn("cartridge_ppu_x", offset_block)
        self.assertIn("cartridge_ppu_y", offset_block)
        self.assertNotIn("use_smoothed_camera_delta", body)
        self.assertNotIn("identity.mode == 0x0009u", body)
        self.assertNotIn("identity.level == 0x0051u", body)
        self.assertNotIn("identity.entrance == 0x006du", body)
        self.assertEqual(offset_block.count("Dkc1NearestTileDelta"), 2)
        self.assertNotIn("wx >> 3", offset_block)
        self.assertNotIn("wy >> 3", offset_block)

        # Quantizing the signed delta with a half-tile tie toward zero is the
        # global conversion between the cartridge's logical-camera and PPU
        # pixel domains. It keeps observed four-pixel smoothing drift from
        # flickering the ROM decoder between adjacent columns. A real
        # five-pixel crossing still advances, and authored +/-512px vertical
        # phases still resolve to +/-64 tiles across a PPU wrap.
        nearest_tile_delta = lambda source, ppu: int(
            ((source - ppu) + (3 if source >= ppu else -3)) / 8)
        self.assertEqual(nearest_tile_delta(816, 815), 0)
        self.assertEqual(nearest_tile_delta(823, 824), 0)
        self.assertEqual(nearest_tile_delta(820, 816), 0)
        self.assertEqual(nearest_tile_delta(812, 816), 0)
        self.assertEqual(nearest_tile_delta(821, 816), 1)
        self.assertEqual(nearest_tile_delta(811, 816), -1)
        self.assertEqual(nearest_tile_delta(2719, 2207), 64)
        self.assertEqual(nearest_tile_delta(2655, 3166), -64)

        # A +43 presentation shift leaves only destination X=0..212 backed
        # by the cartridge's authentic 0..255 strip. World tiles 32 onward
        # must come from the ROM/shadow path even though they are drawn inside
        # the nominal native destination area.
        stock_x, bias, extra, guard = 0, 43, 71, 8
        presentation_x = stock_x + bias
        rendered_right_tx = (
            presentation_x + 255 + extra + guard) >> 3
        stock_right_tx = (stock_x + 255) >> 3
        self.assertEqual(256 - bias, 213)
        self.assertEqual(stock_right_tx, 31)
        self.assertGreater(rendered_right_tx, stock_right_tx)

        prefill = body.split("Prefill every rendered column", 1)[1]
        self.assertIn("stock_left_tx", prefill)
        self.assertIn("stock_right_tx", prefill)
        self.assertIn("left_margin_tiles", prefill)
        self.assertIn("right_margin_tiles", prefill)
        self.assertNotIn("(wx >> 3) + 32 + i", prefill)

    def test_shared_shadow_captures_partial_native_right_edge(self):
        shadow = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                  "ws_shadow.c").read_text(encoding="utf-8")
        self.assertIn("kWsLiveMaxCols = 33", shadow)
        self.assertIn("kWsSnapshotLiveMaxCols = 32", shadow)
        self.assertIn("phase ? 1 : 0", shadow)
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        self.assertNotIn("const uint32_t edge_tx", game)

    def test_vertical_boundary_capability_is_structural_and_margin_only(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        video = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        trace = (ROOT / "runner" / "dkc1_ws_trace.c").read_text(
            encoding="utf-8")
        body = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]

        # The continuation is deliberately inside the ROM margin prefill,
        # after the native calibration/commit boundary. Eligibility comes
        # from vertical layout plus conservative source topology, never a
        # level, entrance, mode, or bank allowlist.
        prefill = body.index("/* Prefill every rendered column")
        continuation = body.index("Dkc1FindVerticalBoundarySource(", prefill)
        self.assertLess(prefill, continuation)
        capability = game.split(
            "static bool Dkc1FindVerticalBoundarySource", 1)[1].split(
                "static bool Dkc1PrepareWidescreenShadow", 1)[0]
        self.assertIn("layout != kDkc1LayoutVertical", capability)
        self.assertNotRegex(
            capability,
            r"(?:mode|level|entrance)\s*==|0x(?:0003|0061|00bf|00c0)u")
        self.assertIn("!target_empty", capability)
        self.assertIn("candidate_full", capability)
        self.assertIn("!candidate_empty", capability)
        self.assertIn("corroborating_rows < 2", capability)
        self.assertIn("native_edge_tile_x[2]", body[prefill:])
        self.assertIn("native_edge_tile_x[side] >> 2", body[prefill:])
        self.assertIn("side == 0 ?", body[prefill:])
        self.assertIn("target_metatile_x < native_edge_metatile_x",
                      body[prefill:])
        self.assertIn("target_metatile_x > native_edge_metatile_x",
                      body[prefill:])

        # Empty right targets are filled from the nearest completely populated
        # metatile back toward the native viewport. West uses the stock-edge
        # source; partial intervening art and isolated one-row features fail
        # closed.
        self.assertIn("Dkc1VideoClassifyLevelMetatile", video)
        self.assertIn("target_metatile_x - 1u", capability)
        self.assertIn("source_x = edge_metatile_x", capability)
        self.assertIn("Dkc1VideoDecodeLevelTile", body[continuation:])
        self.assertIn("tile_entry & 0x03ffu", video)
        self.assertIn("boundary_continuation_tiles", trace)

        # Exact repro geometry: world X 1343 has native pixels through 1598,
        # so metatile 49 is the boundary and metatile 50 is margin-only.
        world_x = 1343
        right_edge_metatile = ((world_x + 256 - 1) >> 3) >> 2
        self.assertEqual(right_edge_metatile, 49)
        self.assertGreater(200 >> 2, right_edge_metatile)

        # New left-gap state: X=577 makes metatile 18 the native edge;
        # the 43px margin reaches wholly transparent metatile 16.
        world_x = 577
        left_edge_metatile = (world_x >> 3) >> 2
        self.assertEqual(left_edge_metatile, 18)
        self.assertLess(67 >> 2, left_edge_metatile)

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

    def test_high_world_shadow_keys_are_scene_local_and_parity_safe(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        body = game.split(
            "static bool Dkc1PrepareWidescreenShadow", 1)[1].split(
                "void Dkc1DrawPpuFrame", 1)[0]
        self.assertIn("s_ws_shadow_origin_valid", game)
        self.assertIn("wanted_lo & ~UINT64_C(0x1ff)", body)
        self.assertIn("wanted_y & ~UINT64_C(0xff)", body)
        self.assertIn(
            "WsShadowSetWorld(layer, shadow_world_x[layer], "
            "shadow_world_y[layer])", body)
        self.assertIn("wtx - origin_tx", body)
        self.assertIn("wty - origin_ty", body)
        self.assertIn("kWsShadowXTiles * 8u", body)
        self.assertIn("kWsShadowYTiles * 8u", body)

        # Exact bonus-room regression from the user quicksave: absolute
        # X=$9AF9 exceeded the 4096-tile cache before localization.
        lower, upper, world_x = 0x9460, 0xA2C0, 0x9AF9
        extra = 43
        wanted_lo = min(lower, world_x) - (extra + 8)
        origin = wanted_lo & ~0x1FF
        local_x = world_x - origin
        self.assertEqual(origin, 0x9400)
        self.assertEqual(local_x, 0x06F9)
        self.assertLess(upper + 256 + extra + 8 - origin, 4096 * 8)
        # A 512-pixel X origin preserves the rolling map half parity.
        self.assertEqual((world_x // 256) & 1, (local_x // 256) & 1)

        # A 256-pixel Y origin preserves the 32-row tilemap wrap even in a
        # high vertical room.
        world_y = 0x568B
        origin_y = (world_y - 8) & ~0xFF
        local_y = world_y - origin_y
        self.assertEqual((world_y // 8) & 31, (local_y // 8) & 31)

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

    def test_quicksave_preserves_live_widescreen_repro_history(self):
        host = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        block = host.split("if (s_quicksave_requested)", 1)[1].split(
            "if (s_quickload_requested)", 1)[0]
        self.assertIn('RtlSaveSnapshot("quicksave.state")', block)
        self.assertIn("Dkc1FlightRecorderEnabled()", block)
        self.assertIn("Dkc1FlightRecorderExport(s_host_frame", block)
        self.assertIn("SpawnLayerCapture(bundle)", block)
        self.assertIn("sparse host-only", host)

    def test_v9_native_state_preserves_host_and_ppu_internal_state(self):
        rtl = (ROOT / "snesrecomp" / "runner" / "src" /
               "common_rtl.c").read_text(encoding="utf-8")
        snes = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                "snes.c").read_text(encoding="utf-8")
        ppu_h = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                 "ppu.h").read_text(encoding="utf-8")
        ppu_c = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                 "ppu.c").read_text(encoding="utf-8")
        shadow_h = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                    "ws_shadow.h").read_text(encoding="utf-8")
        shadow_c = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
                    "ws_shadow.c").read_text(encoding="utf-8")
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        video_h = (ROOT / "runner" / "dkc1_video.h").read_text(
            encoding="utf-8")
        video_c = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        verifier = (ROOT / "tools" / "verify_widescreen_savestate.py").read_text(
            encoding="utf-8")

        self.assertIn("#define RTL_SAV_VERSION 9u", rtl)
        self.assertIn("RTL_PRESENTATION_VERSION = 9", verifier)
        self.assertIn('split_state = output_dir / "split-v9.state"', verifier)
        self.assertIn("ppu_saveload_internal", ppu_h)
        self.assertIn("ppu_reset_internal_after_legacy_load", ppu_h)
        self.assertIn("'P' | 'P' << 8 | 'I' << 16 | '0' << 24", ppu_c)
        self.assertIn("ppu_saveload_internal(snes->ppu, sli);", snes)
        self.assertIn("ppu_reset_internal_after_legacy_load(snes->ppu);", snes)
        self.assertIn("s_saveload_version >= 9", snes)
        self.assertIn("if (version < 9)", game)
        self.assertIn("vramIncrementOnHigh = true;", game)
        for symbol in ("WsShadowSnapshotSize", "WsShadowSnapshotSave",
                       "WsShadowSnapshotLoad"):
            self.assertIn(symbol, shadow_h)
            self.assertIn(symbol, shadow_c)
        self.assertIn("kWsSnapshotMagic", shadow_c)
        self.assertIn("kWsSnapshotVersion = 2", shadow_c)
        self.assertIn("SnapshotCellCount", shadow_c)
        self.assertIn("Validate the entire variable-length stream", shadow_c)

        for symbol in ("Dkc1VideoSnapshotSize", "Dkc1VideoSnapshotSave",
                       "Dkc1VideoSnapshotLoad"):
            self.assertIn(symbol, video_h)
            self.assertIn(symbol, video_c)
            self.assertIn(symbol, game)
        self.assertIn("version >= 8 && Dkc1LoadWidescreenSnapshot", game)
        self.assertIn(
            "if (!restored_host_widescreen || force_cold_widescreen)", game)
        self.assertIn("Dkc1VideoResetPlacedActorPhases();", game)

    def test_cold_state_load_oracle_discards_only_host_widescreen_history(self):
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        bisector = (ROOT / "tools" /
                    "bisect_transition_contamination.py").read_text(
                        encoding="utf-8")
        self.assertIn('getenv("DKC1_WS_COLD_STATE_LOAD")', game)
        self.assertIn("force_cold_widescreen", game)
        self.assertIn('"DKC1_WS_COLD_STATE_LOAD": "1"', bisector)

    def test_auto_capture_does_not_ignore_a_full_gameplay_margin_cull(self):
        detector_h = (ROOT / "runner" / "dkc1_blank_scan.h").read_text(
            encoding="utf-8")
        detector_c = (ROOT / "runner" / "dkc1_blank_scan.c").read_text(
            encoding="utf-8")
        desktop = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        headless = (ROOT / "runner" / "headless_main.c").read_text(
            encoding="utf-8")

        self.assertIn("bool extended_gameplay", detector_h)
        self.assertIn('getenv("DKC1_AUTO_EXPORT")', detector_c)
        self.assertIn("if (!s_active || width <= 256", detector_c)
        self.assertIn(
            "const bool full_flat_gameplay = suspects >= extra * 2;",
            detector_c)
        self.assertIn("full_flat_gameplay && !extended_gameplay", detector_c)
        self.assertIn('"full_flat_gameplay"', detector_c)
        self.assertIn('"partial_height_flat"', detector_c)
        self.assertIn("for (int y0 = 0; y0 < height; y0 += 16)", detector_c)
        self.assertIn("band_suspects >= 8", detector_c)
        self.assertIn("Dkc1VideoTerrainReady());", desktop)
        self.assertIn("Dkc1VideoTerrainReady());", headless)
        auto_export = desktop.split("static void MaybeAutoExport(void)", 1)[1]
        auto_export = auto_export.split("static void SpawnLayerCapture", 1)[0]
        self.assertIn("if (!Dkc1VideoTerrainReady())", auto_export)
        self.assertIn("s_seen_total = total;", auto_export)
        self.assertLess(auto_export.index("if (!Dkc1VideoTerrainReady())"),
                        auto_export.index("if (total > s_seen_total) {",
                                          auto_export.index(
                                              "if (!Dkc1VideoTerrainReady())")))

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

    def test_visible_host_can_render_a_loaded_state_while_starting_paused(self):
        source = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        self.assertIn('EnvironmentEnabled("DKC1_START_PAUSED")', source)
        begin = source.index("Dkc1BeginDrawing(s_pixels")
        paused_draw = source.index("if (s_paused)\n    Dkc1DrawPpuFrame();")
        window = source.index("CreateWindowA(")
        self.assertLess(begin, paused_draw)
        self.assertLess(paused_draw, window)

    def test_visible_panel_reports_the_real_object_scanner_window(self):
        source = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        panel = source.split("VISIBLE WIDESCREEN DEBUGGER", 1)[1].split(
            "static HWAVEOUT", 1)[0]
        self.assertIn("Scanner: rec $%02X", panel)
        self.assertIn("g_ram[0x00a4]", panel)
        self.assertIn("ReadWram16(0x00ef)", panel)
        self.assertIn("ReadWram16(0x00f1)", panel)
        self.assertIn("Dkc1VideoTerrainReady() ? \"READY\" : \"not ready\"",
                      panel)
        # $1E03/$1E07-$1E0B are type-9 section state, not the generic
        # object scanner. Keep those values on their separately named line.
        self.assertIn("Section: state $%04X", panel)
        self.assertIn("ReadWram16(0x1e03)", panel)
        self.assertIn("ReadWram16(0x1e0b)", panel)

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

    def test_macos_exposes_symmetric_16_10_presentation_mode(self):
        header = (ROOT / "runner" / "dkc1_video.h").read_text(
            encoding="utf-8")
        video = (ROOT / "runner" / "dkc1_video.c").read_text(
            encoding="utf-8")
        game = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        host = (ROOT / "runner" / "sdl_host.c").read_text(
            encoding="utf-8")
        menu = (ROOT / "runner" / "macos_file_picker.m").read_text(
            encoding="utf-8")

        self.assertIn("kDkc1VideoWidescreen16x10Extra = 26", header)
        self.assertIn("kDkc1VideoWidescreen16x10Width", header)
        self.assertIn("kDkc1VideoAspect16x10", header)
        self.assertIn("extra = kDkc1VideoWidescreen16x10Extra;", video)
        self.assertIn("extra = kDkc1VideoWidescreenExtra;", video)
        self.assertIn("(uint16_t)(2 * Dkc1VideoExtra())", video)
        self.assertIn("(int32_t)lower + Dkc1VideoExtra()", video)
        self.assertIn("s_ws_last_presentation_extra", game)
        self.assertIn("Dkc1ResetWidescreenShadow();", game.split(
            "s_ws_last_presentation_extra", 1)[1].split(
                "SimpleHdma channels", 1)[0])

        self.assertIn('@"Widescreen 16:10 (308x224)"', menu)
        self.assertIn("kDkc1MacMenuAspect16x10", menu)
        self.assertIn("aspect == kDkc1VideoAspect16x10", menu)
        aspect = host.split("static void SetAspectMode", 1)[1].split(
            "static void SetFullscreen", 1)[0]
        self.assertIn("Dkc1VideoSetAspect(requested);", aspect)
        self.assertIn("Dkc1VideoSetAspect(old_aspect);", aspect)
        self.assertIn("const int source_x = old_width > new_width", aspect)
        self.assertIn("const int dest_x = new_width > old_width", aspect)
        self.assertIn("case kDkc1MacMenuAspect16x10:", host)
        self.assertIn("SetAspectMode(kDkc1VideoAspect16x10);", host)
        self.assertIn('strcmp(aspect, "16:10") == 0', host)
        self.assertIn("kSnesPixelAspectNumerator = 7", host)
        self.assertIn("kSnesPixelAspectDenominator = 6", host)
        self.assertIn("static int PresentationWidth(void)", host)
        self.assertIn("PresentationWidth() * kWindowScale", host)
        self.assertIn(
            'SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0")', host)
        self.assertLess(host.index("SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES"),
                        host.index("SDL_Init(SDL_INIT_VIDEO"))
        self.assertIn("ApplyPresentationGeometry();", aspect)
        fullscreen = host.split("static void SetFullscreen", 1)[1].split(
            "static void HandleKey", 1)[0]
        self.assertIn("ApplyPresentationGeometry();", fullscreen)
        geometry = host.split(
            "static void ApplyPresentationGeometry", 1)[1].split(
                "static void ApplyWindowedSize", 1)[0]
        self.assertIn("SDL_SetTextureScaleMode", geometry)
        self.assertIn("SDL_ScaleModeLinear", geometry)
        self.assertIn("SDL_ScaleModeNearest", geometry)
        self.assertIn(
            "s_fullscreen_scaling != kDkc1MacFullscreenPixelSharp",
            geometry)
        self.assertIn("SDL_RenderSetLogicalSize(s_renderer, 0, 0)", geometry)
        self.assertIn("SDL_GetRendererOutputSize", geometry)
        self.assertIn("SDL_RenderSetIntegerScale(s_renderer, SDL_FALSE)",
                      geometry)
        self.assertIn("SDL_RenderSetViewport(s_renderer, NULL)", geometry)
        present = host.split("static void PreparePresentation", 1)[1].split(
            "static void OpenFirstController", 1)[0]
        self.assertIn("destination.w = output_width;", present)
        self.assertIn("destination.h = output_height;", present)
        self.assertIn("destination.x = (output_width - destination.w) / 2;",
                      present)
        self.assertIn("SDL_RenderCopy(s_renderer, s_texture, NULL,",
                      present)
        self.assertIn("destination_ptr);", present)
        self.assertIn('EnvironmentEnabled("DKC1_START_FULLSCREEN")', host)
        self.assertIn("SetFullscreen(1);", host)

    def test_macos_fullscreen_scaling_is_persistent_and_selectable(self):
        host = (ROOT / "runner" / "sdl_host.c").read_text(encoding="utf-8")
        header = (ROOT / "runner" / "macos_file_picker.h").read_text(
            encoding="utf-8")
        bridge = (ROOT / "runner" / "macos_file_picker.m").read_text(
            encoding="utf-8")
        self.assertIn('AddSubmenu(view, @"Full Screen Scaling"', bridge)
        self.assertIn('@"Smooth (Linear)"', bridge)
        self.assertIn('@"Sharp Bilinear"', bridge)
        self.assertIn('@"Pixel Sharp (Nearest)"', bridge)
        self.assertIn("kDkc1MacMenuFullscreenSmooth", header)
        self.assertIn("kDkc1MacMenuFullscreenSharpBilinear", header)
        self.assertIn("kDkc1MacMenuFullscreenPixelSharp", header)
        self.assertIn("Dkc1MacSavedFullscreenScaling", bridge)
        self.assertIn("Dkc1MacSetFullscreenScaling", bridge)
        self.assertIn('@"DKC1FullscreenScaling"', bridge)
        self.assertIn(
            "s_fullscreen_scaling = Dkc1MacSavedFullscreenScaling();",
            host)
        self.assertIn("case kDkc1MacMenuFullscreenSmooth:", host)
        self.assertIn(
            "SetFullscreenScaling(kDkc1MacFullscreenSmooth);", host)
        self.assertIn("case kDkc1MacMenuFullscreenSharpBilinear:", host)
        self.assertIn(
            "SetFullscreenScaling(kDkc1MacFullscreenSharpBilinear);", host)
        self.assertIn("case kDkc1MacMenuFullscreenPixelSharp:", host)
        self.assertIn(
            "SetFullscreenScaling(kDkc1MacFullscreenPixelSharp);", host)
        self.assertIn("SDL_ScaleModeLinear", host)
        self.assertIn("SDL_ScaleModeNearest", host)

    def test_macos_metal_presenter_decouples_emulation_and_scanout(self):
        host = (ROOT / "runner" / "sdl_host.c").read_text(
            encoding="utf-8")
        presenter = (ROOT / "runner" / "macos_metal_presenter.m").read_text(
            encoding="utf-8")
        presenter_header = (
            ROOT / "runner" / "macos_metal_presenter.h").read_text(
                encoding="utf-8")
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn(
            "kMacNativeDisplayFramesPerSecond = 120.0", host)
        self.assertIn(
            "kHostPresentationFramesPerSecond = 60.0", host)
        self.assertIn("Dkc1MacMetalPresenterStart", host)
        self.assertIn("Dkc1MacMetalPresenterQueueFrame", host)
        self.assertIn("if (!s_metal_presenter_active)", host)
        self.assertIn("camera_x = ReadWram16(0x088b)", host)
        self.assertIn("camera_y = ReadWram16(0x0895)", host)
        self.assertIn("g_ppu->hScroll[layer]", host)
        self.assertIn("g_ppu->vScroll[layer]", host)

        self.assertIn("<CAMetalDisplayLinkDelegate>", presenter)
        self.assertIn("kDkc1MetalFrameSlots = 3", presenter)
        self.assertIn("count >= 2", presenter)
        self.assertIn("repeatGoal = interval < (1.0 / 90.0) ? 2u : 1u",
                      presenter)
        self.assertIn("CAFrameRateRangeMake(rate, rate, rate)", presenter)
        self.assertIn("addPresentedHandler", presenter)
        self.assertIn("presentDrawable:drawable", presenter)
        self.assertIn("drawable.presentedTime", presenter)
        self.assertIn('getenv("DKC1_SCANOUT_LOG")', presenter)
        self.assertIn("dkc1.scanout.v1", presenter)
        self.assertIn("camera_x\\\":%u", presenter)
        self.assertIn("bg1_hscroll\\\":%u", presenter)
        self.assertIn("Dkc1MacPresentationFrameInfo", presenter_header)
        self.assertIn("Dkc1MacMetalPresenterSetActive", presenter_header)
        self.assertIn("SDL_WINDOWEVENT_FOCUS_LOST", host)
        self.assertIn("SDL_WINDOWEVENT_FOCUS_GAINED", host)
        self.assertIn("runner/macos_metal_presenter.m", cmake)
        self.assertIn('"-framework Metal"', cmake)

    def test_macos_host_uses_display_link_with_absolute_clock_fallback(self):
        source = (ROOT / "runner" / "sdl_host.c").read_text(
            encoding="utf-8")
        bridge = (ROOT / "runner" / "macos_file_picker.m").read_text(
            encoding="utf-8")
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("mach_wait_until", source)
        self.assertIn("kHostPresentationFramesPerSecond = 60.0", source)
        self.assertIn("kHostWorkGuardSeconds = 0.006", source)
        self.assertIn("kMacSubmitLeadSeconds = 0.004", source)
        self.assertIn("kMacFinalSpinSeconds = 0.0015", source)
        self.assertIn("frequency * kMacFinalSpinSeconds", source)
        self.assertIn("FramePacerWaitForWorkWindow(&pacer);", source)
        self.assertIn("previous_callback_number, 0.050", source)
        self.assertIn("pacer->next_deadline = target_timestamp * pacer->frequency",
                      source)
        self.assertIn(
            "now + pacer->ticks_per_frame +\n"
            "      pacer->frequency * kMacSubmitLeadSeconds",
            source)
        self.assertIn("Dkc1MacDisplayLinkStart", source)
        self.assertIn("displayLinkWithTarget:controller", bridge)
        self.assertIn("preferredFrameRateRange", bridge)
        self.assertIn("NSCondition", bridge)
        self.assertIn("callbackNumber <= after_callback_number", bridge)
        self.assertIn("callbackNumber > after_callback_number", bridge)
        self.assertIn('"-framework QuartzCore"', cmake)
        self.assertIn("lateness > pacer->frequency / 500.0", source)
        work_window = source.split(
            "static void FramePacerWaitForWorkWindow", 1)[1].split(
                "static void FramePacerRecordWork", 1)[0]
        self.assertIn(
            "pacer->frequency * kHostWorkGuardSeconds",
            work_window)
        self.assertNotIn("ticks_per_frame * 8.0", source)
        self.assertIn('EnvironmentEnabled("DKC1_DISABLE_VSYNC")', source)
        self.assertIn('EnvironmentEnabled("DKC1_USE_DISPLAY_LINK_PACING")',
                      source)
        self.assertIn(
            "SDL_RENDERER_ACCELERATED |\n"
            "      (request_vsync ? SDL_RENDERER_PRESENTVSYNC : 0)",
            source)
        self.assertIn("SDL_GetRendererInfo(s_renderer, &info)", source)
        self.assertIn("SDL_RenderSetVSync(s_renderer, 0)", source)
        self.assertIn('DKC1_KEEP_RENDERER_VSYNC', source)
        self.assertIn("[fps-renderer] name=%s accelerated=%d vsync=%d",
                      source)
        self.assertIn(
            "pacer.next_deadline - pacer.frequency * kMacSubmitLeadSeconds",
            source)
        self.assertIn("FramePacerRecordPresentWait(&pacer", source)
        self.assertIn("phase=render_present", source)
        self.assertIn("phase=work_wake", source)
        self.assertIn("work_ms=%.3f reserve_ms=%.3f wake_late_ms=%.3f",
                      source)
        self.assertIn("[fps-work]", source)
        self.assertIn("[display-stall]", source)
        self.assertIn("[display-stale]", source)
        self.assertIn(
            "callback_elapsed < pacer->ticks_per_frame * 0.75", source)
        self.assertIn("target_lead >= minimum_target_lead", source)
        display_wait = source.split(
            "static int DisplayPacerWaitForTarget", 1)[1].split(
                "static void DisplayPacerWaitForPresent", 1)[0]
        self.assertNotIn("recovering_from_gap", display_wait)
        self.assertNotIn("require_complete_lead", display_wait)
        main_loop = source.split("while (s_running)", 1)[1]
        final_wait = main_loop.split(
            "const double final_wait_start", 1)[1].split(
                "const double present_start", 1)[0]
        self.assertIn("kMacSubmitLeadSeconds", final_wait)
        self.assertNotIn("FramePacerWaitUntil(pacer.next_deadline,",
                         final_wait)
        self.assertIn("[display] intervals=", source)
        self.assertIn('EnvironmentEnabled("DKC1_LIVE_TITLE")', source)
        self.assertIn("RtlSetAudioOutputRate(kAudioRate)", source)
        self.assertIn("SDL_ClearQueuedAudio(s_audio_device)", source)
        self.assertIn("s_audio_ring_start_threshold = kAudioRingStartFrames",
                      source)
        self.assertIn("s_audio_starvations++", source)
        self.assertIn("s_audio_recovery_requested = 1", source)
        self.assertIn("if (callback_delta >= 4 && s_audio_started)", source)
        self.assertIn(
            "lateness >= pacer->ticks_per_frame * 3.0 && s_audio_started",
            source)
        self.assertNotIn("s_audio_started && queued_frames == 0", source)
        self.assertIn('getenv("DKC1_AUDIO_PREROLL")', source)
        self.assertIn('getenv("DKC1_PACING_LOG")', source)
        self.assertIn('getenv("DKC1_PACING_TEST_STALL_FRAME")', source)
        self.assertIn('getenv("DKC1_PACING_TEST_STALL_MS")', source)
        self.assertIn('"clock_source\\\":\\\"%s', source)
        self.assertIn('s_display_link_active ? "CADisplayLink"', source)
        self.assertIn("PacingLogInjectTestStall(&pacing_log, s_host_frame)",
                      source)

        loop = source.split("while (s_running) {", 1)[1]
        self.assertLess(loop.index(
            "DisplayPacerWaitForTarget(&pacer, &display_pacer)"),
                        loop.index("const double work_start"))
        self.assertLess(loop.index("FramePacerWaitForWorkWindow(&pacer);"),
                        loop.index("const double work_start"))
        active = loop.split("const double work_start", 1)[1].split(
            "if (s_smoke_test_frames", 1)[0]
        self.assertLess(active.index("PollInput()"),
                        active.index("FramePacerRecordWork"))
        self.assertLess(active.index("PreparePresentation();"),
                        active.index("const double final_wait_start"))
        self.assertLess(active.index("const double final_wait_start"),
                        active.index("SubmitPresentation();"))

        self.assertIn("s_reanchor_pacer = 1;", source.split(
            "static void QuickLoad", 1)[1].split(
                "static void ExportRepro", 1)[0])
        quick_load = source.split("static void QuickLoad", 1)[1].split(
            "static void ExportRepro", 1)[0]
        self.assertLess(quick_load.index("RtlLoadSnapshot"),
                        quick_load.index("ResetAudioTimeline();"))
        pause = source.split("key == SDLK_F7", 1)[1].split(
            "key == SDLK_F8", 1)[0]
        self.assertIn("s_reanchor_pacer = 1;", pause)
        self.assertIn("ResetAudioTimeline();", pause)
        self.assertIn('EnvironmentEnabled("DKC1_FPS_STATS")', source)

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
