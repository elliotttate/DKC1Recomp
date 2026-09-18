"""Compile and test the actual host-only pose interpolation implementation."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class FramegenTests(unittest.TestCase):
    def test_pose_interpolation(self):
        with tempfile.TemporaryDirectory() as directory:
            exe=Path(directory)/'framegen.exe'
            includes=[ROOT/'runner',ROOT/'recomp',ROOT/'snesrecomp/runner/src']
            if os.name=='nt' and shutil.which('cl'):
                command=['cl','/nologo','/O2','/W3',*['/I'+str(p) for p in includes],str(ROOT/'tests/test_framegen.c'),'/Fe:'+str(exe)]
            else:
                cc=shutil.which(os.environ.get('CC','cc')) or shutil.which('clang')
                if not cc:self.skipTest('no C compiler on PATH')
                command=[cc,'-std=c11','-O2',*['-I'+str(p) for p in includes],str(ROOT/'tests/test_framegen.c'),'-lm','-o',str(exe)]
            build=subprocess.run(command,cwd=directory,capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            env={k:v for k,v in os.environ.items() if not k.startswith(('DKC1_','SNESRECOMP_'))}
            run=subprocess.run([str(exe)],capture_output=True,text=True,env=env)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('framegen: PASS',run.stdout)
            serial=subprocess.run([str(exe)],capture_output=True,text=True,env={**env,'DKC1_FRAMEGEN_BG_SYNC':'1'})
            self.assertEqual(serial.returncode,0,serial.stdout+serial.stderr)
            self.assertEqual(serial.stdout,run.stdout,'worker and serial renderer differ')
