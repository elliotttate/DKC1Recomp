import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "first_divergence", ROOT / "tools" / "first_divergence.py")
FIRST_DIVERGENCE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(FIRST_DIVERGENCE)


def put16(memory: bytearray, offset: int, value: int) -> None:
    memory[offset] = value & 0xFF
    memory[offset + 1] = value >> 8


class FirstDivergenceTests(unittest.TestCase):
    def test_render_pose_refresh_and_oam_are_presentation_not_gameplay(self):
        stock = bytearray(FIRST_DIVERGENCE.WRAM_SIZE)
        wide = bytearray(stock)
        index = 0x06
        for memory in (stock, wide):
            put16(memory, 0x0D45 + index, 0x0005)
            put16(memory, 0x15FD + index, 0x000C)
            put16(memory, 0x0D11 + index, 0x001C)
        put16(stock, 0x0AE5 + index, 0x0018)
        put16(wide, 0x0AE5 + index, 0x001C)
        wide[0x0200] = 0x55

        result = FIRST_DIVERGENCE.classify(bytes(stock), bytes(wide))

        self.assertFalse(result["gameplay_critical"])
        self.assertEqual(result["divergence_class"], "presentation_only")
        self.assertEqual(result["presentation_diff_bytes"], 2)
        self.assertEqual(len(result["render_pose_refresh_only_actors"]), 1)

    def test_actor_motion_difference_is_gameplay_critical(self):
        stock = bytearray(FIRST_DIVERGENCE.WRAM_SIZE)
        wide = bytearray(stock)
        put16(wide, 0x0E89 + 0x06, 0x0040)

        result = FIRST_DIVERGENCE.classify(bytes(stock), bytes(wide))

        self.assertTrue(result["gameplay_critical"])
        self.assertEqual(result["divergence_class"], "gameplay_state")
        self.assertEqual(result["gameplay_actor_differences"][0]["slot"],
                         0x06)

    def test_contiguous_ranges_are_exact(self):
        self.assertEqual(
            FIRST_DIVERGENCE.contiguous_ranges([1, 2, 4]),
            [{"first": "0x00001", "last": "0x00002", "count": 2},
             {"first": "0x00004", "last": "0x00004", "count": 1}])


if __name__ == "__main__":
    unittest.main()
