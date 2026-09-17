import sys
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from hd_sprite_matte import clean_matte


class MatteTests(unittest.TestCase):
    def fixture(self, color=(150, 50, 20)):
        source = np.zeros((24, 24, 4), dtype=np.uint8)
        source[3:21, 4:20] = (*color, 255)
        source = Image.fromarray(source)
        art = np.array(source.resize((96, 96), Image.Resampling.NEAREST))
        art[art[:, :, 3] == 0, :3] = 80
        # A generated contour one output texel inside the source silhouette.
        art[12:84, 16, :3] = 80
        art[12:84, 17, :3] = np.rint((np.array(color) + 80) / 2)
        return source, Image.fromarray(art)

    def test_removes_matte_and_unmixes_partial_edge(self):
        source, art = self.fixture()
        cleaned, report = clean_matte(art, source)
        a = np.array(cleaned)
        self.assertEqual(a[40, 16, 3], 0)
        self.assertLessEqual(abs(int(a[40, 17, 3]) - 128), 2)
        np.testing.assert_allclose(a[40, 17, :3], [150, 50, 20], atol=1)
        self.assertTrue(report['silhouette']['pass'])

    def test_gray_art_is_not_color_keyed(self):
        source, art = self.fixture((80, 80, 80))
        cleaned, _ = clean_matte(art, source)
        np.testing.assert_array_equal(np.array(cleaned), np.array(art))

    def test_interior_and_canvas_are_immutable(self):
        source, art = self.fixture()
        a = np.array(art)
        a[40:48, 40:48, :3] = 80
        art = Image.fromarray(a)
        cleaned, _ = clean_matte(art, source)
        self.assertEqual(cleaned.size, art.size)
        np.testing.assert_array_equal(np.array(cleaned)[22:74, 26:70], a[22:74, 26:70])

    def test_thin_appendage_survives_geometry_guard(self):
        source, art = self.fixture()
        s = np.array(source)
        s[1:4, 8] = (150, 50, 20, 255)
        a = np.array(art)
        a[4:12, 32:36] = (80, 80, 80, 255)
        a[12:16, 32:36] = (150, 50, 20, 255)
        cleaned, report = clean_matte(Image.fromarray(a), Image.fromarray(s))
        self.assertTrue(report['silhouette']['pass'])
        self.assertGreaterEqual(np.array(cleaned)[4:8, 32:36, 3].max(), 128)

    def test_deterministic_and_does_not_expand_alpha(self):
        source, art = self.fixture()
        first, _ = clean_matte(art, source)
        second, _ = clean_matte(art, source)
        self.assertEqual(first.tobytes(), second.tobytes())
        self.assertTrue(np.all(np.array(first)[:, :, 3] <= np.array(art)[:, :, 3]))


if __name__ == '__main__':
    unittest.main()
