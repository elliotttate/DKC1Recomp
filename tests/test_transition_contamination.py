from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "bisect_transition_contamination",
    ROOT / "tools" / "bisect_transition_contamination.py")
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def write_ppm(path: Path, width: int, height: int, raster: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + raster)


class TransitionContaminationTests(unittest.TestCase):
    def make_pair(self, mutate=None):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        width = MODULE.DEFAULT_NATIVE_WIDTH + 2 * MODULE.DEFAULT_EXTRA
        height = MODULE.DEFAULT_HEIGHT
        fresh = bytearray((i * 17 + 3) & 0xff
                          for i in range(width * height * 3))
        path = bytearray(fresh)
        if mutate:
            mutate(path, width, height)
        write_ppm(root / "path.ppm", width, height, path)
        write_ppm(root / "fresh.ppm", width, height, fresh)
        for name in ("path.wram", "fresh.wram"):
            (root / name).write_bytes(bytes(range(256)) * 512)
        for name in ("path.vram", "fresh.vram"):
            (root / name).write_bytes(bytes(range(128)) * 512)
        return temporary, root

    def classify(self, root: Path):
        return MODULE.classify_pair(
            root / "path.ppm", root / "fresh.ppm",
            root / "path.wram", root / "fresh.wram",
            root / "path.vram", root / "fresh.vram")

    def test_identical_state_and_pixels(self):
        temporary, root = self.make_pair()
        with temporary:
            self.assertEqual(self.classify(root)["classification"], "identical")

    def test_margin_only_difference_is_contamination(self):
        def mutate(raster, width, _height):
            raster[(10 * width + 7) * 3] ^= 0xff
            raster[(20 * width + width - 4) * 3 + 2] ^= 0xff

        temporary, root = self.make_pair(mutate)
        with temporary:
            report = self.classify(root)
            self.assertEqual(report["classification"],
                             "retained_margin_contamination")
            self.assertEqual(report["regions"]["left"]["changed_pixels"], 1)
            self.assertEqual(report["regions"]["right"]["changed_pixels"], 1)
            self.assertTrue(report["center_exact"])

    def test_center_difference_fails_closed(self):
        def mutate(raster, width, _height):
            x = MODULE.DEFAULT_EXTRA + 100
            raster[(30 * width + x) * 3 + 1] ^= 0xff

        temporary, root = self.make_pair(mutate)
        with temporary:
            self.assertEqual(self.classify(root)["classification"],
                             "native_center_mismatch")

    def test_raw_state_difference_fails_closed(self):
        temporary, root = self.make_pair()
        with temporary:
            data = bytearray((root / "fresh.wram").read_bytes())
            data[0x192B] ^= 1
            (root / "fresh.wram").write_bytes(data)
            self.assertEqual(self.classify(root)["classification"],
                             "machine_state_mismatch")

    def test_bisection_locates_first_contaminated_frame(self):
        boundary, samples = MODULE.locate_boundary(
            10, 40,
            lambda frame: ("retained_margin_contamination"
                           if frame >= 27 else "identical"))
        self.assertEqual(boundary, 27)
        self.assertEqual(samples[0]["frame"], 10)
        self.assertEqual(samples[1]["frame"], 40)

    def test_bisection_reports_clean_bad_endpoint(self):
        boundary, _samples = MODULE.locate_boundary(
            5, 20, lambda _frame: "identical")
        self.assertIsNone(boundary)

    def test_bisection_rejects_invalid_center_result(self):
        with self.assertRaisesRegex(ValueError, "invalid classification"):
            MODULE.locate_boundary(
                1, 8,
                lambda frame: ("identical" if frame < 4 else
                               "native_center_mismatch" if frame == 4 else
                               "retained_margin_contamination"))

    def test_runner_hash_parser_requires_all_visual_state_domains(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "stdout.txt"
            path.write_text(
                "cgram_sha256=" + "1" * 64 + "\n"
                "oam_sha256=" + "2" * 64 + "\n"
                "oam_source_sha256=" + "3" * 64 + "\n")
            hashes = MODULE.parse_runner_hashes(path)
            self.assertEqual(hashes["oam_sha256"], "2" * 64)
            path.write_text("cgram_sha256=" + "1" * 64 + "\n")
            with self.assertRaisesRegex(ValueError, "missing runner hashes"):
                MODULE.parse_runner_hashes(path)

    def test_headless_zero_frame_contract_is_explicit(self):
        source = (ROOT / "runner" / "headless_main.c").read_text()
        self.assertIn("frame_limit < 0", source)
        self.assertIn("if (frame_limit == 0)\n    Dkc1DrawPpuFrame();", source)
        self.assertIn("does not execute CPU/APU/PPU time", source)


if __name__ == "__main__":
    unittest.main()
