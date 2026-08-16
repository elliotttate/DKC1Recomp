import importlib.util
from pathlib import Path
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "audit_prefetch_transaction.py")
SPEC = importlib.util.spec_from_file_location(
    "audit_prefetch_transaction", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class PrefetchTransactionAuditTests(unittest.TestCase):
    def test_accepts_actor_oam_and_bookkeeping_only(self):
        stock = bytes(0x20000)
        candidate = bytearray(stock)
        for offset in (0x0200, 0x0AE5, 0x170F, 0x192B, 0x1A8F):
            candidate[offset] = 1
        result = MODULE.analyze_frames({1: stock}, {1: bytes(candidate)})
        self.assertTrue(result["accepted"])
        self.assertIsNone(result["first_unexpected"])

    def test_rejects_escaped_gameplay_write(self):
        stock = bytes(0x20000)
        candidate = bytearray(stock)
        candidate[0x0575] = 1
        result = MODULE.analyze_frames({7: stock}, {7: bytes(candidate)})
        self.assertFalse(result["accepted"])
        self.assertEqual(result["first_unexpected"]["frame"], 7)
        self.assertEqual(result["unexpected_union"][0]["first"], "0x00575")


if __name__ == "__main__":
    unittest.main()
