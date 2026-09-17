"""Graphics preferences and color grading never mutate the guest's raw frame."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class GraphicsModels(unittest.TestCase):
    def test_settings_and_present_only_color(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'graphics'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-I', str(ROOT/'runner'), '-I', str(ROOT/'snesrecomp/runner/src'),
                *[str(ROOT/p) for p in ['tests/test_desktop_graphics.c',
                  'runner/desktop_graphics.c','runner/desktop_crt.c','runner/desktop_filter.c',
                  'snesrecomp/runner/src/snes/color_lut.c']], '-lm', '-o', str(exe)],
                check=True,capture_output=True)
            subprocess.run([str(exe)],check=True,capture_output=True)
