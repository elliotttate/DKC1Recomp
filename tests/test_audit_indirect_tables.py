import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "audit_indirect_tables", ROOT / "tools" / "audit_indirect_tables.py")
AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(AUDIT)


class IndirectTableAuditTests(unittest.TestCase):
    def test_exact_table_contract_passes_and_ignores_numeric_words(self):
        assembly = """\
CODE_BF9000:
\tJMP.w (DATA_BF9010,x)

DATA_BF9010:
\tdw HANDLER_A,$0030
\tdw HANDLER_B,$00F0
"""
        symbols = "[labels]\nBF:A000 HANDLER_A\nBF:A100 HANDLER_B\n"
        contracts = [{
            "cfg": "bankbf.cfg", "line": 1, "bank": "BF",
            "address": "9000", "declared": 2, "kind": "ptrtail",
            "targets": ["BFA000", "BFA100"],
        }]
        result = AUDIT.audit(assembly, symbols, contracts)
        self.assertTrue(result["passed"])
        self.assertEqual(result["counts"]["passed"], 1)

    def test_missing_table_target_fails(self):
        assembly = """\
CODE_809000:
\tJMP.w (DATA_809010,x)
DATA_809010:
\tdw HANDLER_A,HANDLER_B
"""
        symbols = "[labels]\n80:A000 HANDLER_A\n80:A100 HANDLER_B\n"
        contracts = [{
            "cfg": "bank80.cfg", "line": 1, "bank": "80",
            "address": "9000", "declared": 1, "kind": "ptrtail",
            "targets": ["80A000"],
        }]
        result = AUDIT.audit(assembly, symbols, contracts)
        self.assertFalse(result["passed"])
        self.assertEqual(result["results"][0]["missing_targets"], ["80A100"])

    def test_computed_dispatch_is_honestly_unproven(self):
        result = AUDIT.audit(
            "CODE_BE8179:\n\tJMP.w ($007A)\n", "", [{
                "cfg": "bankbe.cfg", "line": 1, "bank": "BE",
                "address": "8179", "declared": 1, "kind": "ptrtail",
                "targets": ["BE9000"],
            }])
        self.assertTrue(result["passed"])
        self.assertEqual(result["counts"]["unproven"], 1)


if __name__ == "__main__":
    unittest.main()
