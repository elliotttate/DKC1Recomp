"""Synthetic ROM-free test of the expanded variant's real cartridge mapper."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DixieRomMappingTests(unittest.TestCase):
    def test_hd_host_bundles_dixie_and_removes_kiddy_overlay(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        host = (ROOT / 'runner/sdl_host.c').read_text()
        menu = (ROOT / 'runner/macos_file_picker.m').read_text()
        pause = (ROOT / 'runner/macos_pause_menu.m').read_text()
        packaging = (ROOT / 'build_macos.sh').read_text()

        self.assertIn('dkc1_macos_dixie', cmake)
        self.assertIn('DKC1Recomp-HD-Dixie', packaging)
        self.assertIn('@"Dixie Kong Country"', menu)
        self.assertIn('Dkc1DixieSwitchAndRelaunch', host)
        self.assertIn('@"Switch Donkey / Dixie"', pause)
        self.assertIn('Dkc1VideoSetAspect(kDkc1VideoAspectNative)', host)

        active = '\n'.join((cmake, host, menu, pause, packaging)).lower()
        self.assertNotIn('baby_kong', active)
        self.assertNotIn('kiddy kong', active)
        for name in (
            'dkc1_baby_kong.c',
            'dkc1_baby_kong.h',
            'dkc1_baby_kong_animation.c',
            'dkc1_baby_kong_movement.c',
        ):
            self.assertFalse((ROOT / 'runner' / name).exists(), name)

    def test_expanded_data_banks_preserve_stock_aliases(self):
        src = ROOT / 'snesrecomp/runner/src'
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / ('mapper.exe' if os.name == 'nt' else 'mapper')
            sources = [ROOT/'tests/dixie_rom_mapping_test.c']
            if os.name == 'nt' and shutil.which('cl'):
                command = ['cl', '/nologo', '/std:c11', '/O1', '/W4', '/WX',
                           '/D_CRT_SECURE_NO_WARNINGS', '/I'+str(src),
                           *map(str, sources), '/Fe:'+str(output)]
            else:
                compiler = shutil.which(os.environ.get('CC', 'cc'))
                if compiler is None:
                    self.skipTest('a C compiler is required')
                command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                           '-I'+str(src), *map(str, sources), '-o', str(output)]
            built = subprocess.run(command, cwd=directory, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            subprocess.run([str(output)], check=True)
