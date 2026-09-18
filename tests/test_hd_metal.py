"""Run the real Metal compositor against synthetic CPU oracle frames on macOS."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform == 'darwin', 'Metal requires macOS')
class HdMetalTests(unittest.TestCase):
    def test_gpu_pixels_and_immutable_pool(self):
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder)/'metal'
            command = [os.environ.get('CC','cc'), '-O1', '-g', '-fsanitize=address,undefined',
                       '-Wno-deprecated-declarations', '-ffunction-sections', '-fdata-sections',
                       '-I', str(ROOT/'runner'), '-I', str(ROOT/'recomp'),
                       '-I', str(ROOT/'snesrecomp/runner/src'), str(ROOT/'tests/test_hd_metal.m'),
                       str(ROOT/'runner/macos_hd_scene.m'), '-framework', 'Foundation',
                       '-framework', 'Metal', '-Wl,-dead_strip', '-o', str(exe)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(exe),str(ROOT/'runner/macos_hd_scene.metal')],
                                    capture_output=True,text=True)
            if result.returncode == 77:self.skipTest('Metal device unavailable')
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('128 immutable CPU/GPU frames',result.stdout)

if __name__ == '__main__':unittest.main()
