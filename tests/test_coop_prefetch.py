from pathlib import Path
import unittest

import test_coop_mod as native_contract


ROOT = Path(__file__).resolve().parents[1]


class CoopPrefetchTests(unittest.TestCase):
    def test_two_stock_windows_and_metadata_rollback(self):
        native_contract.CoopModTests().compile_harness(
            (ROOT / "tests/coop_prefetch_harness.c").read_text(),
            ["runner/dkc1_coop_entities.c", "runner/dkc1_coop_graphics.c"])


if __name__ == "__main__":
    unittest.main()
