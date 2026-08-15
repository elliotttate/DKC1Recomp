import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "oam_inspect.py"


def oam_entry(x: int, y: int, tile: int, attr: int) -> bytes:
    data = bytearray(544)
    data[0:4] = bytes((x & 0xFF, y, tile, attr))
    data[512] = (x >> 8) & 1
    return bytes(data)


class OamInspectTests(unittest.TestCase):
    def test_direct_xhigh_loss_and_transition_filters(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / "capture"
            records = [
                # Direct evidence: identical entry except shadow X-high=1.
                (1, oam_entry(0x108, 50, 0x20, 0x31),
                 oam_entry(0x008, 50, 0x20, 0x31), False, True),
                # A forced-blank disagreement is deliberately excluded.
                (2, oam_entry(0x120, 60, 0x22, 0x31),
                 oam_entry(0x020, 60, 0x22, 0x31), True, True),
                # DKC's tile-$FF unused marker is not sprite evidence.
                (3, oam_entry(0x000, 0, 0xFF, 0),
                 oam_entry(0x000, 0, 0xFF, 0), False, True),
                # Menu/map OAM is outside the gameplay gate.
                (4, oam_entry(0x110, 70, 0x24, 0x31),
                 oam_entry(0x010, 70, 0x24, 0x31), False, False),
            ]
            with prefix.with_suffix(".bin").open("wb") as output:
                for frame, shadow, ppu, _, _ in records:
                    output.write(struct.pack("<I", frame))
                    output.write(shadow)
                    output.write(ppu)
            with prefix.with_suffix(".jsonl").open("w") as output:
                for frame, _, _, forced_blank, gameplay in records:
                    output.write(json.dumps({
                        "schema": "dkc1.oam.frame.v1",
                        "frame": frame,
                        "inidisp": 0x80 if forced_blank else 0x0F,
                        "forced_blank": forced_blank,
                        "gameplay": gameplay,
                    }) + "\n")

            report_path = prefix.with_suffix(".report.json")
            result = subprocess.run(
                [sys.executable, str(TOOL), str(prefix),
                 "--json-out", str(report_path)],
                capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(report_path.read_text())
            self.assertEqual(report["xhigh_loss_suspects"], 1)
            self.assertEqual(report["forced_blank_frames_excluded"], 1)
            self.assertEqual(report["outside_gameplay_frames_excluded"], 1)
            self.assertEqual(report["right_margin_entries"], 0)
            self.assertEqual(report["left_margin_entries"], 0)


if __name__ == "__main__":
    unittest.main()
