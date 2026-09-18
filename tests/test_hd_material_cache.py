"""Run the real material-cache implementation under address/UB sanitizers."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class HdMaterialCacheTests(unittest.TestCase):
    def test_saturated_cache_preserves_live_materials(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'cache'
            command = [os.environ.get('CC', 'cc'), '-std=c11', '-O1',
                       '-fsanitize=address,undefined', '-ffunction-sections',
                       '-fdata-sections', '-Wno-deprecated-declarations',
                       '-I', str(ROOT/'runner'), '-I', str(ROOT/'recomp'),
                       '-I', str(ROOT/'snesrecomp/runner/src'),
                       str(ROOT/'tests/test_hd_material_cache.c'), '-o', str(exe)]
            if sys.platform == 'darwin':
                command += ['-Wl,-dead_strip']
            else:
                command += ['-Wl,--gc-sections', str(ROOT/'snesrecomp/runner/src/sha256.c')]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn('HD material cache passed', result.stdout)

if __name__ == '__main__':
    unittest.main()
