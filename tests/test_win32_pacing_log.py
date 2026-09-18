import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PacingQueueTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt' and shutil.which('cl'), 'Windows MSVC test')
    def test_blocked_writer_preserves_order_and_reports_loss(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.run(['cl', '/nologo', '/O2', '/W3',
                '/I'+str(ROOT/'runner'), str(ROOT/'tests/test_win32_pacing_log.c'),
                '/Fe:queue.exe'], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([str(Path(folder)/'queue.exe')], cwd=folder,
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('pacing queue: PASS', result.stdout)
