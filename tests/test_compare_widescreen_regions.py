import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "compare_widescreen_regions.py")
SPEC = importlib.util.spec_from_file_location("compare_widescreen_regions", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.write_bytes(f"P6\n# fixture\n{width} {height}\n255\n".encode() + pixels)


class RegionComparisonTests(unittest.TestCase):
    def test_native_center_matches_wide_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = bytes(range(18))
            wide = bytearray()
            for row in range(2):
                wide.extend(b"\x10\x20\x30" * 2)
                wide.extend(native[row * 9:(row + 1) * 9])
                wide.extend(b"\x40\x50\x60" * 2)
            ppm(root / "native.ppm", 3, 2, native)
            ppm(root / "wide.ppm", 7, 2, bytes(wide))
            report, _ = MODULE.compare(root / "native.ppm",
                                       root / "wide.ppm", 2)
            self.assertTrue(report["center_exact"])
            self.assertEqual(report["regions"]["center"]["changed_pixels"], 0)

    def test_detects_single_center_pixel_and_bounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = bytes([0] * 18)
            candidate = bytearray(b"\0" * 42)
            candidate[(1 * 7 + 2 + 1) * 3] = 9
            ppm(root / "native.ppm", 3, 2, reference)
            ppm(root / "wide.ppm", 7, 2, bytes(candidate))
            report, _ = MODULE.compare(root / "native.ppm",
                                       root / "wide.ppm", 2)
            self.assertFalse(report["center_exact"])
            self.assertEqual(report["regions"]["center"]["changed_pixels"], 1)
            self.assertEqual(report["regions"]["center"]["bounds"], [1, 1, 1, 1])

    def test_rejects_truncated_raster(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.ppm"
            path.write_bytes(b"P6\n2 2\n255\n\0")
            with self.assertRaisesRegex(ValueError, "raster"):
                MODULE.read_ppm(path)


if __name__ == "__main__":
    unittest.main()
