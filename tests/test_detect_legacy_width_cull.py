import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "detect_legacy_width_cull", ROOT / "tools" /
    "detect_legacy_width_cull.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class LegacyWidthCullTests(unittest.TestCase):
    width = 342
    height = 32
    extra = 43

    def write(self, root: Path, name: str, pixel) -> Path:
        path = root / name
        body = bytearray()
        for y in range(self.height):
            for x in range(self.width):
                body.extend(pixel(x, y))
        path.write_bytes(
            f"P6\n{self.width} {self.height}\n255\n".encode() + body)
        return path

    def write_mask(self, image: Path, occupied) -> Path:
        path = image.with_name(image.stem + ".mask.pgm")
        body = bytearray(
            255 if occupied(x, y) else 0
            for y in range(self.height) for x in range(self.width))
        path.write_bytes(
            f"P5\n{self.width} {self.height}\n255\n".encode() + body)
        return path

    def test_continuous_pattern_is_clean(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.write(Path(temp), "clean.ppm", lambda x, y: (
                (x * 3 + y + (x // 128) * 17) & 255,
                (x + y * 5 + (x // 97) * 29) & 255,
                (x * 7 + y + (x // 71) * 43) & 255))
            report = MODULE.audit(path)
            self.assertEqual("clean", report["status"])

    def test_empty_margins_are_hard_cull(self):
        with tempfile.TemporaryDirectory() as temp:
            def pixel(x, y):
                if x < self.extra or x >= self.extra + 256:
                    return 0, 0, 0
                return (40 + ((x + y) & 31), 80, 120)
            report = MODULE.audit(self.write(Path(temp), "culled.ppm", pixel))
            self.assertEqual("hard_failure", report["status"])
            self.assertEqual(["left", "right"], report["hard_failure_sides"])

    def test_partial_height_cull_is_a_hard_band_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            def pixel(x, y):
                in_margin = x < self.extra or x >= self.extra + 256
                if y >= 16 and in_margin:
                    return 0, 0, 0
                return (40 + ((x + y) & 31), 80, 120)
            report = MODULE.audit(self.write(Path(temp), "band.ppm", pixel))
            self.assertEqual("hard_failure", report["status"])
            hard_bands = [row for row in report["band_findings"]
                          if "hard_empty_cull" in row["kinds"]]
            self.assertEqual({("left", 16, 32), ("right", 16, 32)}, {
                (row["side"], *row["y"]) for row in hard_bands})

    def test_opposite_native_edges_copied_into_margins_are_leads(self):
        with tempfile.TemporaryDirectory() as temp:
            center = [[((x * 17 + y * 3) & 255,
                        (x * 7 + y * 11) & 255,
                        (x * 13 + y * 5) & 255)
                       for x in range(256)] for y in range(self.height)]

            def pixel(x, y):
                if x < self.extra:
                    return center[y][256 - self.extra + x]
                if x >= self.extra + 256:
                    return center[y][x - (self.extra + 256)]
                return center[y][x - self.extra]

            report = MODULE.audit(self.write(Path(temp), "repeat.ppm", pixel))
            self.assertEqual("investigate", report["status"])
            kinds = {(row["side"], row["kind"])
                     for row in report["diagnostic_leads"]}
            self.assertIn(("left", "opposite_edge_repeat"), kinds)
            self.assertIn(("right", "opposite_edge_repeat"), kinds)

    def test_occupancy_mask_ignores_repeated_backdrop(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            image = self.write(root, "obj.ppm", lambda x, y: (
                y * 3 & 255, y * 5 & 255, y * 7 & 255))
            self.write_mask(
                image, lambda x, y: 120 <= x < 136 and 8 <= y < 24)
            report = MODULE.audit(image)
            self.assertEqual("clean", report["status"])
            self.assertTrue(
                report["occupancy_mask"].endswith("obj.mask.pgm"))

    def test_malformed_ppm_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad.ppm"
            path.write_bytes(b"P3\n1 1\n255\n0 0 0\n")
            with self.assertRaisesRegex(ValueError, "P6"):
                MODULE.audit(path)


if __name__ == "__main__":
    unittest.main()
