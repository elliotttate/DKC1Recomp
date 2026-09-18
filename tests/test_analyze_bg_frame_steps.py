"""The motion diagnostic must reject ambiguous evidence, not invent motion."""
import importlib.util
from pathlib import Path
import unittest

try:
    import numpy as np
    from PIL import Image  # noqa: F401
except ImportError:
    np = None


@unittest.skipIf(np is None, 'NumPy/Pillow unavailable')
class BackgroundStepTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / 'tools/analyze_bg_frame_steps.py'
        spec = importlib.util.spec_from_file_location('background_steps', path)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_direction_and_repeated_frame(self):
        image = np.random.default_rng(4).integers(0, 256, (32, 64, 3), dtype=np.uint8)
        ys, xs = np.mgrid[8:24, 10:50]
        for dx, dy in [(0, 0), (1, 0), (-2, 0), (4, -1), (-4, 1)]:
            shifted = np.roll(image, (dy, dx), axis=(0, 1))
            step, _, _ = self.module.match(image, shifted, ys.ravel(), xs.ravel())
            self.assertTrue(step['accepted'])
            self.assertEqual((step['dx'], step['dy']), (dx, dy))
            self.assertEqual(step['fraction'], 1)

    def test_ambiguous_and_insufficient_evidence(self):
        image = np.zeros((32, 64, 3), dtype=np.uint8)
        ys, xs = np.mgrid[8:24, 10:50]
        step, _, _ = self.module.match(image, image, ys.ravel(), xs.ravel())
        self.assertFalse(step['accepted'])
        self.assertNotIn('dx', step)
        step, _, _ = self.module.match(image, image, ys.ravel()[:8], xs.ravel()[:8])
        self.assertEqual(step['reason'], 'insufficient texture')

    def test_fractional_native_layer_registration(self):
        image = np.random.default_rng(7).integers(0,256,(32,80,3),dtype=np.uint8)
        ys,xs=np.mgrid[8:24,16:64]
        for dx in [-3.125,-1.5,-.75,-.0625,0,.25,.75,1.5,3.125]:
            integer=int(np.floor(dx)); phase=dx-integer
            shifted=np.floor(np.roll(image,integer,axis=1).astype(float)*(1-phase)+
                np.roll(image,integer+1,axis=1).astype(float)*phase+.5).astype(np.uint8)
            fit=self.module.fractional_fit(image,shifted,ys.ravel(),xs.ravel())
            self.assertTrue(fit['accepted'],fit)
            self.assertEqual(fit['dx'],dx)
            self.assertEqual(fit['dy'],0)
        blank=np.zeros_like(image)
        fit=self.module.fractional_fit(blank,blank,ys.ravel(),xs.ravel())
        self.assertFalse(fit['accepted'])


if __name__ == '__main__':
    unittest.main()
