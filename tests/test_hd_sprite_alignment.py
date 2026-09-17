"""Geometry checks must reject visual misregistration despite valid pack size."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from PIL import Image, ImageDraw

spec = importlib.util.spec_from_file_location('hd_alignment',
    Path(__file__).resolve().parents[1]/'tools/check_hd_sprite_alignment.py')
alignment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(alignment)


class AlignmentTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.source = self.root/'source.png'
        self.candidate = self.root/'candidate.png'
        im = Image.new('RGBA', (16, 16))
        ImageDraw.Draw(im).rectangle((2, 2, 10, 13), fill=(120, 75, 40, 255))
        im.save(self.source)
        im.resize((64, 64), Image.Resampling.NEAREST).save(self.candidate)
        self.marks = [{'name': 'support', 'source': [6, 14], 'candidate': [24, 56]}]

    def test_exact_enlargement_has_zero_geometry_error(self):
        r = alignment.check(self.source, self.candidate, self.marks)
        self.assertTrue(r['geometry_pass'])
        self.assertEqual(r['silhouette_iou'], 1)
        self.assertEqual(r['contour_max_native_pixels'], 0)

    def test_one_native_pixel_translation_is_rejected(self):
        shifted = Image.new('RGBA', (64, 64))
        shifted.paste(Image.open(self.candidate), (4, 0))
        shifted.save(self.candidate)
        self.assertFalse(alignment.check(self.source, self.candidate, self.marks)['geometry_pass'])

    def test_same_silhouette_does_not_hide_displaced_face(self):
        r = alignment.check(self.source, self.candidate,
                            [{'name': 'eye', 'source': [6, 5], 'candidate': [32, 20]}])
        self.assertTrue(r['silhouette_pass'])
        self.assertFalse(r['geometry_pass'])

    def test_missing_landmarks_cannot_claim_geometry_acceptance(self):
        r = alignment.check(self.source, self.candidate)
        self.assertTrue(r['silhouette_pass'])
        self.assertFalse(r['geometry_pass'])
        self.assertFalse(r['landmark_review_complete'])

    def test_wrong_aspect_cannot_be_silently_stretched(self):
        Image.open(self.candidate).resize((64, 68)).save(self.candidate)
        with self.assertRaisesRegex(ValueError, 'aspect differs'):
            alignment.check(self.source, self.candidate, self.marks)

    def test_opaque_backdrop_is_rejected(self):
        Image.new('RGB', (64, 64), 'white').save(self.candidate)
        with self.assertRaisesRegex(ValueError, 'transparent'):
            alignment.check(self.source, self.candidate, self.marks)

    def test_nan_landmark_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'finite'):
            alignment.check(self.source, self.candidate,
                            [{'name': 'eye', 'source': [float('nan'), 5], 'candidate': [24, 20]}])


if __name__ == '__main__':
    unittest.main()
