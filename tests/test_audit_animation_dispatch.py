import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "audit_animation_dispatch",
    ROOT / "tools" / "audit_animation_dispatch.py")
AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(AUDIT)


class AnimationDispatchAuditTests(unittest.TestCase):
    def test_exact_contract_passes(self):
        assembly = "\n".join((
            "%DKC1_AnS1_Op81(CODE_ONE)",
            "%DKC1_AnS1_Op81(CODE_TWO)",
            "%DKC1_AnS1_Op81(CODE_ONE)",
        ))
        symbols = "[labels]\nBE:9000 CODE_ONE\nBF:A000 CODE_TWO\n"
        config = (
            "indirect_dispatch 8179 2 ptrcall return:810D frame:3 "
            "targets:BE9000,BFA000\n")

        result = AUDIT.audit(assembly, symbols, config)

        self.assertTrue(result["passed"])
        self.assertEqual(result["op81_call_count"], 3)
        self.assertEqual(result["expected_unique_targets"], 2)

    def test_missing_callback_fails_and_emits_replacement(self):
        assembly = "\n".join((
            "%DKC1_AnS1_Op81(CODE_ONE)",
            "%DKC1_AnS1_Op81(CODE_JUMP_EXIT)",
        ))
        symbols = (
            "[labels]\nBE:9000 CODE_ONE\n"
            "4639:BE:A778 CODE_JUMP_EXIT\n")
        config = (
            "indirect_dispatch 8179 1 ptrcall return:810D frame:3 "
            "targets:BE9000\n")

        result = AUDIT.audit(assembly, symbols, config)

        self.assertFalse(result["passed"])
        self.assertEqual(result["missing_targets"], ["BEA778"])
        self.assertIn("targets:BE9000,BEA778", result["contract"])

    def test_repository_contract_keeps_complete_static_target_count(self):
        config = (ROOT / "recomp" / "bankbe.cfg").read_text(
            encoding="utf-8")
        declared, targets = AUDIT.parse_contract(config)
        self.assertEqual(declared, 197)
        self.assertEqual(len(targets), 197)
        self.assertEqual(len(set(targets)), 197)
        self.assertIn("BEA778", targets)


if __name__ == "__main__":
    unittest.main()
