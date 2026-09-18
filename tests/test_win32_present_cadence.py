import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PresentCadenceTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt' and shutil.which('cl'), 'Windows MSVC test')
    def test_lone_real_frame_keeps_the_whole_divisor(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.run(['cl', '/nologo', '/O2', '/W3',
                '/I'+str(ROOT/'runner'), str(ROOT/'tests/test_win32_present_cadence.c'),
                '/Fe:cadence.exe'], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([str(Path(folder)/'cadence.exe')], cwd=folder,
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('present cadence: PASS', result.stdout)
