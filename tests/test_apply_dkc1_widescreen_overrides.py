import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc1_widescreen_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc1_widescreen_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def generated_function(symbol: str,
                       blocks: list[tuple[str, str]]) -> str:
    lines = ['#include "funcs.h"',
             f"RecompReturn {symbol}(CpuState *cpu) {{"]
    for label, statement in blocks:
        lines.extend((f"  {label}", f"    {statement}"))
    lines.extend(("  return RECOMP_RETURN_NORMAL;", "}", ""))
    return "\n".join(lines)


class WidescreenOverrideTests(unittest.TestCase):
    def setUp(self):
        # This fixture covers presentation/scanner contracts. The separate
        # test_coop_entities suite owns actor-pool, script and loop contracts.
        entity_pass = patch.object(MODULE, "adapt_coop_entities")
        self.entity_pass = entity_pass.start()
        self.addCleanup(entity_pass.stop)
        graphics_pass = patch.object(MODULE, "adapt_coop_graphics")
        self.graphics_pass = graphics_pass.start()
        self.addCleanup(graphics_pass.stop)
        schedule_pass = patch.object(MODULE, "adapt_coop_schedule")
        schedule_pass.start()
        self.addCleanup(schedule_pass.stop)

    def test_scanner_catalog_preserves_each_native_deadline_phase(self):
        sources = {}
        for symbol in {h["symbol"] for h in MODULE.COOP_EVENT_HOOKS
                       if h["group"] == "scanner"}:
            blocks = [("L_" + h["pc"][-4:] + "_M0X0:",
                       "cpu_trace_block(cpu, 0x" + h["pc"] + ");\n"
                       "    if (interp_bridge_lle_master_deadline_reached(cpu)) return RECOMP_RETURN_NORMAL;\n"
                       "    cpu->coprocessor_master_cycles = cpu->master_cycles;\n"
                       "    cpu->X = 0x1234;")
                      for h in MODULE.COOP_EVENT_HOOKS
                      if h["group"] == "scanner" and h["symbol"] == symbol]
            sources[Path(symbol + ".c")] = generated_function(symbol, blocks)
        MODULE.inject_coop_events(sources, "scanner")
        for hook in MODULE.COOP_EVENT_HOOKS:
            if hook["group"] != "scanner": continue
            text = sources[Path(hook["symbol"] + ".c")]
            start = text.index("L_" + hook["pc"][-4:] + "_M0X0:")
            deadline = text.index("interp_bridge_lle_master_deadline_reached", start)
            callback = text.index(hook["call"], start)
            native = text.index("cpu->X = 0x1234;", start)
            self.assertEqual(text.count(hook["call"]), 1)
            self.assertLess(callback, native)
            if hook.get("after_deadline", True):
                self.assertLess(deadline, callback)
            else:
                self.assertLess(callback, deadline)
        first = dict(sources)
        MODULE.inject_coop_events(sources, "scanner")
        self.assertEqual(first, sources)

    def write_function(self, generated: Path, name: str, symbol: str,
                       blocks: list[tuple[str, str]]) -> None:
        (generated / name).write_text(
            generated_function(symbol, blocks), encoding="utf-8")

    def make_generated_dir(self, root: Path) -> Path:
        generated = root / "generated"
        generated.mkdir()

        self.write_function(
            generated, "standard_initializer.c", "CODE_809E32_M0X0", [
                ("L_9E9C_M0X0:",
                 "uint16 backstep = 0x100; uint16 columns = 0x20;"),
                ("L_9EA2_M0X0:", "cpu_trace_block(cpu, 0x809EA2);"),
            ])
        self.write_function(
            generated, "alternate_initializer.c", "CODE_80C501_M0X0", [
                ("L_C53A_M0X0:",
                 "uint16 backstep = 0x108; uint16 columns = 0x21;"),
                ("L_C540_M0X0:", "cpu_trace_block(cpu, 0x80C540);"),
            ])
        for index, (symbol, label) in enumerate((
                ("Level_BuildTilemapColumn_TypeA_M0X0", "L_8722_M0X0:"),
                ("Level_DMATilemapColumnToVRAM_M0X0", "L_8868_M0X0:"),
                ("CODE_8188A8_M0X0", "L_88CE_M0X0:"),
                ("Level_BuildTilemapColumn_TypeB_M0X0", "L_8E17_M0X0:"))):
            self.write_function(
                generated, f"stream_selector_{index}.c", symbol, [
                    (label, "cpu_write_a_m(cpu, (uint16)(stream_x));"),
                ])

        self.write_function(
            generated, "row_standard.c", "CODE_81890E_M0X0", [
                ("L_891A_M0X0:", "cpu_trace_block(cpu, 0x81891A);"),
            ])
        self.write_function(
            generated, "row_alternate.c", "CODE_818CEF_M0X0", [
                ("L_8CFB_M0X0:", "cpu_trace_block(cpu, 0x818CFB);"),
            ])
        self.write_function(
            generated, "row_copy.c", "CODE_818A18_M0X0", [
                ("L_8A30_M0X0:",
                 "{ uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */\n"
                 "      return RECOMP_RETURN_NORMAL; }"),
                ("L_8A59_M0X0:",
                 "{ uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */\n"
                 "      return RECOMP_RETURN_NORMAL; }"),
            ])

        self.write_function(generated, "shared.c", "SHARED_M0X0", [
            ("L_A8D4_M0X0:", "uint16 left_a = 0x30; uint16 span_a = 0x160;"),
            ("L_A8E5_M0X0:", "cpu_trace_block(cpu, 0xBBA8E5);"),
            ("L_A904_M0X0:", "uint16 left_b = 0x58; uint16 span_b = 0x1b0;"),
            ("L_A915_M0X0:", "cpu_trace_block(cpu, 0xBBA915);"),
        ])

        # Group exact bank-BD blocks by generated owner. F899 is deliberately
        # last in its function: this covers the real terminal-block shape.
        by_symbol: dict[str, dict[str, str]] = {}
        for symbol, block in MODULE.LEFT_BLOCKS:
            by_symbol.setdefault(symbol, {})[block] = "0x20"
        for symbol, block in MODULE.SPAN_BLOCKS:
            by_symbol.setdefault(symbol, {})[block] = "0x140"
        for symbol, block in MODULE.PREFETCH_BLOCKS:
            by_symbol.setdefault(symbol, {})[block] = "0x120"
        for symbol, failure, success, _ in MODULE.COOP_CULL_FAILURES:
            for label in (failure, success):
                by_symbol.setdefault(symbol, {}).setdefault(label, None)
        for label in ("F570", "F561", "F564", "F5C5", "F67A"):
            by_symbol["CODE_BDF502_M0X0"][label] = None
        for index, (symbol, blocks) in enumerate(sorted(by_symbol.items())):
            statements = {
                block: (f"uint16 value_{block} = {literal};" if literal else
                        f"cpu_trace_block(cpu, 0xBD{block});")
                for block, literal in blocks.items()
            }
            if symbol == "CODE_BDFB76_M0X0":
                statements.update({
                    "FB76": "cpu->coprocessor_master_cycles = "
                            "cpu->master_cycles;",
                    "FBF5": "cpu_trace_block(cpu, 0xBDFBF5);",
                })
            ordered = sorted(
                statements.items(),
                key=lambda item: (item[0] == "F899", item[0]))
            self.write_function(
                generated, f"objects_{index:02d}.c", symbol,
                [(f"L_{block}_M0X0:", statement)
                 for block, statement in ordered])

        self.write_function(generated, "banana_bounds.c", "CODE_B8B918_M0X0", [
            ("L_B918_M0X0:", "uint16 candidate = 0x100;"),
            ("L_B93E_M0X0:", "uint16 overlap = 0x10f;"),
        ])
        self.write_function(generated, "banana_draw.c", "CODE_B8B9B5_M0X0", [
            ("L_B9EA_M0X0:", "uint16 left_clip = 0xf;"),
            ("L_BA11_M0X0:", "uint16 right_clip = 0x107;"),
            ("L_BA67_M0X1:",
             "cpu_write_a_m(cpu, (uint16)(base_a)); "
             "cpu_write_a_m(cpu, (uint16)(screen_x_a));"),
            ("L_BA99_M1X1:", "cpu_trace_block(cpu, 0xB8BA99);"),
            ("L_BACA_M0X1:",
             "cpu_write_a_m(cpu, (uint16)(base_b)); "
             "cpu_write_a_m(cpu, (uint16)(screen_x_b));"),
            ("L_BAFC_M1X1:", "cpu_trace_block(cpu, 0xB8BAFC);"),
        ])
        self.write_function(generated, "rope.c", "CODE_80A7ED_M0X0", [
            ("L_A7ED_M0X0:",
             "cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x0076), rope_x);"),
            ("L_A809_M0X0:", "uint16 rope_span = 0x100;"),
            ("L_A80E_M0X0:", "cpu_trace_block(cpu, 0x80A80E);"),
            ("L_A878_M0X0:",
             "uint16 mask = cpu_read16(cpu, "
             "(uint8)((((uint32)0x80a545 + (uint32)cpu->X)) >> 16), "
             "(uint16)(((uint32)0x80a545 + (uint32)cpu->X))); "
             "uint16 existing = cpu_read16(cpu, cpu->DB, cpu->Y); "
             "uint16 merged = (uint16)(mask | existing);"),
            ("L_A877_M0X0:", "cpu_trace_block(cpu, 0x80A877);"),
        ])
        self.write_function(generated, "actor_dispatch.c", "CODE_BF8087_M0X0", [
            ("L_8087_M0X0:",
             "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
            ("L_808A_M0X0:", "cpu_trace_block(cpu, 0xBF808A);"),
        ])
        self.write_function(generated, "actor_loop_a.c", "CODE_BF8000_M0X0", [
            ("L_802D_M0X0:",
             "cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x0082), _v17);\n"
             "    { dispatch_actor_a(cpu); }\n"
             "    goto L_8033_M0X0;"),
            ("L_8033_M0X0:", "cpu_trace_block(cpu, 0xBF8033);"),
        ])
        self.write_function(generated, "actor_loop_b.c", "CODE_BF804B_M0X0", [
            ("L_8067_M0X0:",
             "cpu_write16(cpu, 0x00, (uint16)(cpu->D + 0x0082), _v12);\n"
             "    { dispatch_actor_b(cpu); }\n"
             "    goto L_806D_M0X0;"),
            ("L_806D_M0X0:", "cpu_trace_block(cpu, 0xBF806D);"),
        ])
        self.write_function(
            generated, "oam_draw_hud.c", "OAM_BeginFrameAndDrawHUD_M0X0", [
                ("L_A1AE_M0X0:",
                 "cpu->coprocessor_master_cycles = cpu->master_cycles;\n"
                 "    cpu->cycles += 6;"),
                ("L_A1B9_M0X0:",
                 "if (deadline) {\n"
                 "      return RECOMP_RETURN_YIELD;\n"
                 "    }\n"
                 "    cpu->coprocessor_master_cycles = cpu->master_cycles;\n"
                 "    {\n"
                 "      /* JSL return frame -> cpu->S (Option-1) */\n"
                 "      draw_actors(cpu);\n"
                 "    }\n"
                 "    goto L_A1BD_M0X0;"),
                ("L_A1BD_M0X0:", "cpu_trace_block(cpu, 0x80A1BD);"),
            ])
        self.write_function(
            generated, "hud_sprite.c", "CODE_BBA772_M0X0", [
                ("L_A772_M0X0:",
                 "cpu->coprocessor_master_cycles = cpu->master_cycles;\n"
                 "    { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */ }\n"
                 "    { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */ }\n"
                 "    { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */ }"),
            ])
        self.write_function(
            generated, "oam_draw_alt.c", "CODE_80C96F_M0X0", [
                ("L_C973_M0X0:",
                 "if (deadline) {\n"
                 "      return RECOMP_RETURN_YIELD;\n"
                 "    }\n"
                 "    cpu_write16(cpu, 0x00, 0x008e, 0x0200);\n"
                 "    {\n"
                 "      /* JSL return frame -> cpu->S (Option-1) */\n"
                 "      draw_actors(cpu);\n"
                 "    }\n"
                 "    goto L_C9B1_M0X0;"),
                ("L_C9B1_M0X0:", "cpu_trace_block(cpu, 0x80C9B1);"),
            ])

        self.write_function(generated, "coop_rescue.c", "CODE_BF936E_M0X0", [
            ("L_93DB_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n"
             "    cpu->A = 1;\n    cpu->PB = 0xBE; /* JML into bank $BE */"),
        ])

        self.write_function(generated, "coop_pickup.c", "CODE_BF8711_M0X0", [
            ("L_8749_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])

        # A duplicate bank-local label in another function must not attract
        # the function-scoped banana rewrite.
        self.write_function(generated, "duplicate.c", "OTHER_M0X0", [
            ("L_B918_M0X0:", "uint16 unrelated = 0x999;"),
        ])
        self.write_function(generated, "coop_cart.c", "CODE_B69F36_M0X0", [
            ("L_9F36_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    (void)cpu;"),
            ("L_9F6E_M0X0:", ""),
        ])
        self.write_function(generated, "coop_land_camera.c", "CODE_809BEC_M0X0", [
            ("L_9BFC_M0X0:", "uint16 focus = cpu_read16(cpu, cpu->DB, (uint16)(0x1e29));"),
            ("L_9C4E_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_cart_camera.c", "CODE_80C47B_M0X0", [
            ("L_C4A3_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_camera.c", "CODE_80C263_M0X0", [
            ("L_C27C_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_scanner.c", "CODE_BDFE39_M0X0", [
            ("L_FE39_M0X0:", "cpu_trace_block(cpu, 0xBDFE39);"),
            ("L_FE67_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_map_pads.c", "CODE_80C157_M0X0", [
            ("L_C1C8_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;")])
        self.write_function(generated, "coop_rope_ceiling.c", "CODE_BFA095_M0X0", [
            ("L_A0A3_M0X0:", "uint16 _v11 = cpu_read16(cpu, cpu->DB, (uint16)(0x0895));"),
        ])
        self.write_function(generated, "native_pad_poll.c", "CODE_80A257_M0X0", [
            ("L_A2C6_M0X0:", "Dkc1CoopPublishMapPads(cpu);"),
        ])
        self.write_function(generated, "native_pad_owner.c", "Player_ProcessInput_M0X0", [
            ("L_B2A7_M0X0:", "uint16 first = cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0044));"),
            ("L_B2C7_M0X0:", "uint16 owner = cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x0044));"),
        ])
        self.write_function(generated, "coop_sections.c", "CODE_BDFDBD_M0X0", [
            ("L_FDBD_M0X0:", "cpu_trace_block(cpu, 0xBDFDBD);"),
        ])
        self.write_function(generated, "coop_partner.c", "CODE_80855D_M0X0", [
            ("L_855D_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_room_mount.c", "CODE_80838C_M0X0", [
            ("L_83A8_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_animal_bonus_owner.c", "CODE_B683D2_M0X0", [
            ("L_8421_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_save_mount.c", "CODE_BF9EAC_M0X0", [
            ("L_9EC2_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_restore_mount.c", "Level_HandleExit_M0X0", [
            ("L_840C_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_mount.c", "CODE_BF888E_M0X0", [
            ("L_88B4_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_animal_mount.c", "CODE_BFCC24_M0X0", [
            ("L_CC4D_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_rope_wait_camera.c", "CODE_BF94B3_M0X0", [
            ("L_951B_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    cpu->X = 10;"),
            ("L_9526_M0X0:", "return RECOMP_RETURN_NORMAL;"),
        ])
        self.write_function(generated, "coop_map_avatar.c", "CODE_80CF2D_M0X0", [
            ("L_CF2D_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    cpu->X = 10;"),
        ])
        self.write_function(generated, "coop_rope_contact.c", "CODE_BFEF0C_M0X0", [
            ("L_EF51_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    cpu->X = 10;"),
        ])
        self.write_function(generated, "coop_octo_departure.c", "CODE_B6AA34_M0X0", [
            ("L_AA3D_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    cpu->X = 10;"),
            ("L_AA4A_M0X0:", "cpu_trace_block(cpu, 0xB6AA4A);"),
        ])
        self.write_function(generated, "coop_wheel_departure.c", "CODE_B6F4E0_M0X0", [
            ("L_F4E9_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    cpu->X = 10;"),
            ("L_F4EF_M0X0:", "return RECOMP_RETURN_NORMAL;"),
        ])
        self.write_function(generated, "coop_rope_departure.c", "CODE_BFEE68_M0X0", [
            ("L_EE82_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;\n    cpu->X = 10;"),
            ("L_EE7F_M0X0:", "return RECOMP_RETURN_NORMAL;")])
        self.write_function(generated, "coop_mounted_swap.c", "CODE_BFBF8E_M0X0", [
            ("L_BFAD_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_partner_finish.c", "CODE_808787_M0X0", [
            ("L_8809_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        self.write_function(generated, "coop_partner_exit.c", "CODE_B6F3F7_M0X0", [
            ("L_F456_M0X0:", "cpu->coprocessor_master_cycles = cpu->master_cycles;"),
        ])
        return generated

    def read_all(self, generated: Path) -> dict[str, str]:
        return {path.name: path.read_text(encoding="utf-8")
                for path in generated.glob("*.c")}

    def test_applies_all_categories_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            MODULE.apply_overrides(generated)
            first = self.read_all(generated)
            MODULE.apply_overrides(generated)
            self.assertEqual(first, self.read_all(generated))
            self.assertEqual(self.entity_pass.call_count, 2)
            self.assertEqual(self.graphics_pass.call_count, 2)

            combined = "\n".join(first.values())
            rescue = first["coop_rescue.c"]
            self.assertLess(rescue.index("cpu->A = 1;"),
                            rescue.index("Dkc1CoopFinishBarrelRescue(cpu)"))
            self.assertLess(rescue.index("Dkc1CoopFinishBarrelRescue(cpu)"),
                            rescue.index("cpu->PB = 0xBE;"))
            self.assertIn("goto L_88AB_M0X0", first["coop_mount.c"])
            self.assertIn("goto L_CC31_M0X0", first["coop_animal_mount.c"])
            self.assertIn("if (Dkc1CoopBlockRopeDeparture(cpu)) goto L_EE7F_M0X0",
                          first["coop_rope_departure.c"])
            self.assertIn("if (Dkc1CoopRopeWaitCamera(cpu)) goto L_9526_M0X0",
                          first["coop_rope_wait_camera.c"])
            self.assertIn("if (Dkc1CoopBlockWheelDeparture(cpu)) goto L_F4EF_M0X0", first["coop_wheel_departure.c"])
            self.assertIn("if (Dkc1CoopBlockCroctopusDeparture(cpu)) goto L_AA4A_M0X0", first["coop_octo_departure.c"])
            self.assertIn("Dkc1CoopRememberRopeContact(cpu);", first["coop_rope_contact.c"])
            self.assertIn("Dkc1CoopSelectMapAvatar(cpu);", first["coop_map_avatar.c"])
            self.assertNotIn("Dkc1CoopPublishMapPads", first["native_pad_poll.c"])
            self.assertEqual(first["native_pad_owner.c"].count("Dkc1CoopNativePadOwner"), 1)
            self.assertIn("uint16 first = cpu_read16", first["native_pad_owner.c"])
            self.assertEqual(combined.count("Dkc1CoopAnimalMountOccupied"),2)
            self.assertEqual(combined.count(
                                 "Dkc1VideoObjectScannerCullLeft(0x20)"),
                             len(MODULE.LEFT_BLOCKS))
            self.assertEqual(combined.count(
                                 "Dkc1VideoObjectScannerCullSpan(0x140)"),
                             len(MODULE.SPAN_BLOCKS))
            self.assertEqual(combined.count(
                                 "Dkc1VideoObjectScannerCullLeft(0x120)"),
                             len(MODULE.PREFETCH_BLOCKS))
            self.assertEqual(
                first["shared.c"].count("Dkc1VideoExpandCullLeft"), 2)
            self.assertEqual(
                first["shared.c"].count("Dkc1VideoExpandCullSpan"), 2)
            self.assertIn(
                "Dkc1VideoInitialBackstep(cpu, 0x100)",
                first["standard_initializer.c"])
            self.assertIn(
                "Dkc1VideoInitialColumnCount(cpu, 0x20)",
                first["standard_initializer.c"])
            self.assertIn(
                "Dkc1VideoInitialBackstep(cpu, 0x108)",
                first["alternate_initializer.c"])
            self.assertIn(
                "Dkc1VideoInitialColumnCount(cpu, 0x21)",
                first["alternate_initializer.c"])
            self.assertEqual(
                combined.count("Dkc1VideoSelectStreamX(cpu, cpu_read_a16(cpu))"),
                4)
            self.assertIn(
                "Dkc1VideoBeginWideRowBuild(cpu, false)",
                first["row_standard.c"])
            self.assertIn(
                "Dkc1VideoBeginWideRowBuild(cpu, true)",
                first["row_alternate.c"])
            self.assertEqual(
                first["row_copy.c"].count(
                    "Dkc1VideoAdvanceWideRowBuild(cpu)"), 2)
            self.assertIn("CODE_81890E_M0X0(cpu)", first["row_copy.c"])
            self.assertIn("CODE_818CEF_M0X0(cpu)", first["row_copy.c"])
            self.assertEqual(
                first["banana_draw.c"].count("Dkc1VideoPromoteOamXHigh"), 2)
            self.assertIn("Dkc1VideoBiasCullX", first["rope.c"])
            self.assertIn("uint16 _ws_rope_x", first["rope.c"])
            self.assertIn("Dkc1VideoPromoteOamSizeMask", first["rope.c"])
            self.assertIn(
                "Dkc1VideoMergeOamSizeAndXHigh(existing, mask, _ws_rope_x)",
                first["rope.c"])
            self.assertEqual(
                combined.count("Dkc1VideoPrepareType5ChildRetry(cpu)"), 1)
            self.assertEqual(
                combined.count("Dkc1CoopRestoreRoomMount(cpu)"), 1)
            self.assertEqual(combined.count("Dkc1CoopSaveCarriedMount(cpu)"), 1)
            self.assertEqual(combined.count("Dkc1CoopRestoreCarriedMount(cpu)"), 1)
            self.assertEqual(
                combined.count("Dkc1VideoBeginPlacedActorDispatch(cpu)"), 2)
            self.assertEqual(
                combined.count("Dkc1VideoEndPlacedActorDispatch(cpu)"), 2)
            self.assertEqual(
                combined.count("Dkc1MarginProxyBeginRender(cpu)"), 2)
            # Each synthetic renderer block has one abnormal return and one
            # normal exit; both must restore borrowed actor slots.
            self.assertEqual(
                combined.count("Dkc1MarginProxyEndRender(cpu)"), 4)
            alternate = first["oam_draw_alt.c"]
            self.assertLess(
                alternate.index("cpu_write16(cpu, 0x00, 0x008e, 0x0200)"),
                alternate.index("Dkc1MarginProxyBeginRender(cpu)"))
            self.assertLess(
                alternate.index("Dkc1MarginProxyBeginRender(cpu)"),
                alternate.index("/* JSL return frame -> cpu->S"))
            self.assertNotIn("cpu_write_a_m(cpu, 0)",
                             first["actor_dispatch.c"])
            self.assertNotIn(MODULE.INCLUDE, first["duplicate.c"])

    def test_fails_closed_when_a_known_constant_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            shared = generated / "shared.c"
            shared.write_text(
                shared.read_text(encoding="utf-8").replace("0x160", "0x161", 1),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "expected one 0x160"):
                MODULE.apply_overrides(generated)


if __name__ == "__main__":
    unittest.main()
