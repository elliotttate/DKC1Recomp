import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "verify_margin_proxy_ab.py"
SPEC = importlib.util.spec_from_file_location("verify_margin_proxy_ab", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class MarginProxyAbTests(unittest.TestCase):
    def make_pair(self, root: Path, *, deep_center=False,
                  unexpected_wram=False):
        on = root / "on"
        off = root / "off"
        on.mkdir()
        off.mkdir()
        width, height = 342, 224
        off_pixels = bytearray(width * height * 3)
        on_pixels = bytearray(off_pixels)
        x = 120 if deep_center else 300
        start = (90 * width + x) * 3
        on_pixels[start:start + 3] = b"\xff\x40\x20"
        for directory, pixels in ((on, on_pixels), (off, off_pixels)):
            (directory / "frame.ppm").write_bytes(
                f"P6\n{width} {height}\n255\n".encode() + pixels)
            (directory / "audio.pcm").write_bytes(b"same-audio")

        on_wram = bytearray(0x20000)
        off_wram = bytearray(0x20000)
        on_wram[0x008E] = 0x44
        on_wram[0x0258] = 0x70
        on_wram[0x171F] = 0x01
        if unexpected_wram:
            on_wram[0x057B] = 1
        (on / "wram.bin").write_bytes(on_wram)
        (off / "wram.bin").write_bytes(off_wram)
        (on / "stdout.log").write_text(
            "vram_sha256=1\ncgram_sha256=2\noam_sha256=3\n"
            "oam_source_sha256=4\naudio_fnv1a=5\n", encoding="utf-8")
        (off / "stdout.log").write_text(
            "vram_sha256=a\ncgram_sha256=2\noam_sha256=b\n"
            "oam_source_sha256=c\naudio_fnv1a=5\n", encoding="utf-8")
        events = [
            {"event": "inject", "global_oam_index": 0x214,
             "proxy_displayed_pose": 0},
            {"event": "restore", "global_oam_index": 0x284,
             "proxy_displayed_pose": 0x1E18},
        ]
        (on / "proxy.jsonl").write_text(
            "".join(json.dumps(event) + "\n" for event in events),
            encoding="utf-8")
        return on, off

    def test_accepts_edge_pixels_and_named_presentation_domains(self):
        with tempfile.TemporaryDirectory() as directory:
            on, off = self.make_pair(Path(directory))
            report = MODULE.verify(on, off, extra=43, center_overlap=16)
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["frame"]["bbox"], [300, 90, 301, 91])
            self.assertEqual(report["wram"]["unexpected_offsets"], [])
            self.assertEqual(set(report["wram"]["domains"]), {
                "renderer_scratch", "oam_shadow", "sprite_upload_queue"})

    def test_rejects_deep_center_pixel_change(self):
        with tempfile.TemporaryDirectory() as directory:
            on, off = self.make_pair(Path(directory), deep_center=True)
            report = MODULE.verify(on, off, extra=43, center_overlap=16)
            self.assertEqual(report["status"], "fail")
            self.assertEqual(report["frame"]["deep_center_changed_pixels"], 1)

    def test_rejects_gameplay_wram_change(self):
        with tempfile.TemporaryDirectory() as directory:
            on, off = self.make_pair(Path(directory), unexpected_wram=True)
            report = MODULE.verify(on, off, extra=43, center_overlap=16)
            self.assertEqual(report["status"], "fail")
            self.assertEqual(report["wram"]["unexpected_offsets"], [0x057B])


if __name__ == "__main__":
    unittest.main()
