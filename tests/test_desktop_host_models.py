"""Compile and exercise the backend-independent Mac host adapters."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DesktopHostModels(unittest.TestCase):
    def test_models(self):
        with tempfile.TemporaryDirectory() as directory:
            for module in ('refresh', 'audio_rate', 'input', 'rewind', 'crt'):
                with self.subTest(module=module):
                    executable = Path(directory) / module
                    subprocess.run([
                        os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                        '-Werror', '-I', str(ROOT / 'runner'),
                        str(ROOT / f'tests/test_desktop_{module}.c'),
                        str(ROOT / f'runner/desktop_{module}.c'), '-lm',
                        '-o', str(executable)], check=True, capture_output=True)
                    result = subprocess.run([str(executable)], check=True,
                                            capture_output=True, text=True)
                    self.assertIn('ok' if module == 'crt' else 'passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
