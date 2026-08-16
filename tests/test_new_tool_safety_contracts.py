from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from ir.decode import IROp  # noqa: E402
from ir import summarize  # noqa: E402
import oracle_spec  # noqa: E402
import coverage_explorer  # noqa: E402
import promote_bundle  # noqa: E402


def op(address, mnemonic, *, size=1, mode="imp", suffix=None, expr="",
       index=None, target=None, function="CODE_808000", ea=None,
       region="other"):
    return IROp(
        addr=address, size=size, mnemonic=mnemonic, suffix=suffix,
        mode=mode, expr=expr, index=index, target=target,
        function=function, ea=ea, region=region, mw=0, xw=0)


class IrEffectClosureTests(unittest.TestCase):
    def test_external_fallthrough_effects_are_in_oracle(self):
        root_name = "CODE_808000"
        continuation_name = "CODE_808003"
        functions = {
            root_name: [
                op(0x808000, "STZ", size=3, mode="absx", suffix="w",
                   expr="$1595", index="x", function=root_name,
                   ea=0x1595, region="wram"),
            ],
            continuation_name: [
                op(0x808003, "STA", size=3, mode="absx", suffix="w",
                   expr="$16E5", index="x", function=continuation_name,
                   ea=0x16E5, region="wram"),
                op(0x808006, "RTS", function=continuation_name),
            ],
        }
        summaries = summarize.build_summaries(
            functions=functions, dispatches={})

        self.assertEqual(summaries[root_name].external, [0x808003])
        spec = oracle_spec.spec_for(root_name, summaries)
        self.assertIn(
            {"base": "0x16E5", "span": "0x34"},
            spec["compare"]["wram_write_arrays"])
        self.assertEqual(spec["eligibility"], "oracle-ready")

    def test_external_jump_effects_are_in_oracle(self):
        root_name = "CODE_808000"
        target_name = "CODE_818000"
        functions = {
            root_name: [
                op(0x808000, "JMP", size=3, mode="abs", suffix="w",
                   expr=target_name, target=0x818000, function=root_name),
            ],
            target_name: [
                op(0x818000, "STZ", size=3, mode="abs", suffix="w",
                   expr="$0028", function=target_name,
                   ea=0x28, region="wram"),
                op(0x818003, "RTS", function=target_name),
            ],
        }
        summaries = summarize.build_summaries(
            functions=functions, dispatches={})

        spec = oracle_spec.spec_for(root_name, summaries)
        self.assertIn("0x28", spec["compare"]["wram_writes"])
        self.assertEqual(spec["eligibility"], "oracle-ready")

    def test_unresolved_external_continuation_blocks_state_oracle(self):
        name = "CODE_808000"
        summaries = summarize.build_summaries(
            functions={name: [op(0x808000, "NOP", function=name)]},
            dispatches={})

        spec = oracle_spec.spec_for(name, summaries)
        self.assertEqual(spec["eligibility"], "needs-lle-shadow")
        self.assertTrue(any("external control-flow" in blocker
                            for blocker in spec["blockers"]))


class PromotionSafetyTests(unittest.TestCase):
    def make_bundle(self, root: Path) -> dict[str, str]:
        files = {}
        for name, data in {
                "anchor.snapshot": b"anchor",
                "inputs.txt": b"0000\n",
                "final.wram.bin": b"wram",
                "final.vram.bin": b"vram"}.items():
            path = root / name
            path.write_bytes(data)
            files[name] = promote_bundle.sha256(path)
        return files

    def test_every_manifested_file_is_verified(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = self.make_bundle(root)
            self.assertEqual(
                promote_bundle.verify_manifest_files(root, files), files)
            (root / "final.vram.bin").write_bytes(b"corrupt")
            with self.assertRaises(SystemExit) as caught:
                promote_bundle.verify_manifest_files(root, files)
            self.assertIn("final.vram.bin hash differs", str(caught.exception))

    def test_required_final_wram_must_be_manifested(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = self.make_bundle(root)
            del files["final.wram.bin"]
            with self.assertRaises(SystemExit) as caught:
                promote_bundle.verify_manifest_files(root, files)
            self.assertIn("final.wram.bin", str(caught.exception))

    def test_manifest_paths_cannot_escape_bundle(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            files = self.make_bundle(root)
            files["../outside.bin"] = "0" * 64
            with self.assertRaises(SystemExit) as caught:
                promote_bundle.verify_manifest_files(root, files)
            self.assertIn("unsafe manifest file path", str(caught.exception))

    def test_promotion_name_is_a_single_safe_slug(self):
        for good in ("jungle-entry", "State_5", "a1"):
            self.assertEqual(promote_bundle.validate_name(good), good)
        for bad in ("../escape", "nested/path", r"nested\path", ".", ""):
            with self.subTest(name=bad), self.assertRaises(SystemExit):
                promote_bundle.validate_name(bad)


class CoverageAggregationTests(unittest.TestCase):
    def test_only_all_proven_variants_produce_proven_entrance(self):
        self.assertEqual(
            coverage_explorer.aggregate_status(["proven", "proven"]),
            "proven")
        self.assertEqual(
            coverage_explorer.aggregate_status(["proven", "degraded"]),
            "degraded")
        self.assertEqual(
            coverage_explorer.aggregate_status(["proven", "centered"]),
            "centered-only")
        self.assertEqual(
            coverage_explorer.aggregate_status(["proven", "unproven"]),
            "reached-unmeasured")


class ToolSyntaxTests(unittest.TestCase):
    def test_every_python_tool_parses(self):
        for path in sorted((ROOT / "tools").rglob("*.py")):
            with self.subTest(path=path.relative_to(ROOT)):
                compile(path.read_text(encoding="utf-8"), str(path), "exec")


if __name__ == "__main__":
    unittest.main()
