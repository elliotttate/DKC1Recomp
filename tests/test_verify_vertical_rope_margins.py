import json
from pathlib import Path
import struct
import tempfile
import unittest

from tools.verify_vertical_rope_margins import verify_contract


def encode_frame(frame: int, x: int) -> bytes:
    oam = bytearray(544)
    for index, y in enumerate((15, 31, 47, 63)):
        offset = index * 4
        oam[offset:offset + 4] = bytes((x & 0xff, y, 0x60, 0x36))
        pair = 2 | (1 if x & 0x100 else 0)
        oam[512 + index // 4] |= pair << ((index % 4) * 2)
    return struct.pack("<I", frame) + bytes(oam) + bytes(oam)


class VerticalRopeMarginVerifierTests(unittest.TestCase):
    def make_run(self, root: Path, label: str, run_index: int,
                 xs: tuple[int, ...]) -> None:
        run = root / label / f"run{run_index}"
        run.mkdir(parents=True)
        payload = b"".join(encode_frame(frame, x)
                           for frame, x in enumerate(xs, 1))
        (run / "oam.bin").write_bytes(payload)
        rows = [
            {"schema": "dkc1.oam.frame.v1", "frame": frame,
             "forced_blank": False, "gameplay": True}
            for frame in range(1, len(xs) + 1)
        ]
        (run / "oam.jsonl").write_text(
            "".join(json.dumps(row) + "\n" for row in rows),
            encoding="utf-8")

    def make_contract(self, root: Path, left=(1, 510), right=(253, 257)):
        for run_index in range(1, 4):
            self.make_run(root, "right-margin", run_index, right)
            self.make_run(root, "left-margin", run_index, left)

    def test_both_margin_crossings_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_contract(root)
            report = verify_contract(root)
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["right_margin"]["first_margin"]["x"], 257)
            self.assertEqual(report["left_margin"]["first_margin"]["x"], 510)

    def test_missing_left_margin_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_contract(root, left=(1, 2))
            with self.assertRaisesRegex(ValueError, "no left-margin"):
                verify_contract(root)


if __name__ == "__main__":
    unittest.main()
