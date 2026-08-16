import importlib.util
from pathlib import Path
import tempfile
import unittest


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
    def write_function(self, generated: Path, name: str, symbol: str,
                       blocks: list[tuple[str, str]]) -> None:
        (generated / name).write_text(
            generated_function(symbol, blocks), encoding="utf-8")

    def make_generated_dir(self, root: Path) -> Path:
        generated = root / "generated"
        generated.mkdir()

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
        for index, (symbol, blocks) in enumerate(sorted(by_symbol.items())):
            statements = {
                block: f"uint16 value_{block} = {literal};"
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

        # A duplicate bank-local label in another function must not attract
        # the function-scoped banana rewrite.
        self.write_function(generated, "duplicate.c", "OTHER_M0X0", [
            ("L_B918_M0X0:", "uint16 unrelated = 0x999;"),
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

            combined = "\n".join(first.values())
            self.assertEqual(combined.count("Dkc1VideoExpandCullLeft(0x20)"),
                             len(MODULE.LEFT_BLOCKS))
            self.assertEqual(combined.count("Dkc1VideoExpandCullSpan(0x140)"),
                             len(MODULE.SPAN_BLOCKS))
            self.assertEqual(combined.count("Dkc1VideoExpandCullLeft(0x120)"),
                             len(MODULE.PREFETCH_BLOCKS))
            self.assertEqual(
                first["shared.c"].count("Dkc1VideoExpandCullLeft"), 2)
            self.assertEqual(
                first["shared.c"].count("Dkc1VideoExpandCullSpan"), 2)
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
                combined.count("Dkc1VideoBeginPlacedActorDispatch(cpu)"), 2)
            self.assertEqual(
                combined.count("Dkc1VideoEndPlacedActorDispatch(cpu)"), 2)
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
