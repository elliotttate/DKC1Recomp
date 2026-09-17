"""Real Metal spatial-pass checks on generated, cartridge-free fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(sys.platform=='darwin','Metal requires macOS')
class HdPolishTests(unittest.TestCase):
    def test_spatial_edges_protection_and_repeatability(self):
        with tempfile.TemporaryDirectory() as folder:
            exe=Path(folder)/'polish'
            subprocess.run([os.environ.get('CC','cc'),'-O1','-g','-fsanitize=address,undefined',
                '-I',str(ROOT/'runner'),str(ROOT/'tests/test_hd_polish.m'),'-framework','Foundation','-framework','Metal','-o',str(exe)],check=True,capture_output=True,text=True)
            r=subprocess.run([str(exe),str(ROOT/'runner/dkc1_hd_gpu_types.h'),str(ROOT/'runner/macos_hd_scene.metal')],capture_output=True,text=True)
            if r.returncode==77:self.skipTest('Metal unavailable')
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('protected_changed=0',r.stdout)
            self.assertIn('finish_changed=',r.stdout)
            self.assertIn('deterministic=1',r.stdout)
            print(r.stdout.strip())
if __name__=='__main__':unittest.main()
