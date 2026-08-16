import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "poke_test", ROOT / "tools" / "poke_test.py")
POKE_TEST = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(POKE_TEST)


class PokeTestTests(unittest.TestCase):
    def test_parse_setting_accepts_last_wram_byte(self):
        addr, data = POKE_TEST.parse_setting("1FFFF=AA")
        self.assertEqual(addr, 0x1FFFF)
        self.assertEqual(data, b"\xAA")

    def test_parse_setting_rejects_write_past_wram(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            POKE_TEST.parse_setting("1FFFF=AABB")

    def test_parse_setting_rejects_missing_value(self):
        with self.assertRaisesRegex(ValueError, "expected ADDR=HEXBYTES"):
            POKE_TEST.parse_setting("1595=")

    def test_expect_rejects_word_read_at_last_byte(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            POKE_TEST.parse_expect("1FFFF==0")

    def test_expect_allows_byte_read_at_last_byte(self):
        self.assertEqual(POKE_TEST.parse_expect("1FFFF.b==AA"),
                         (0x1FFFF, 1, "==", 0xAA))

    def test_locate_wram_rejects_truncated_embedding(self):
        reference = bytearray(POKE_TEST.WRAM_SIZE)
        for i in range(POKE_TEST.WRAM_SIZE):
            reference[i] = (i * 17 + (i >> 8)) & 0xFF
        truncated = b"header" + bytes(reference[:0x1F80])
        with self.assertRaises(SystemExit):
            POKE_TEST.locate_wram(truncated, bytes(reference))


if __name__ == "__main__":
    unittest.main()
