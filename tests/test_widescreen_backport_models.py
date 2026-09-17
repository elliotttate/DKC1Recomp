from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class WidescreenBackportModels(unittest.TestCase):
    def build_and_run(self, name, sources, includes):
        compiler = shutil.which("cc") or shutil.which("clang")
        if not compiler:
            self.skipTest("a C compiler is unavailable")
        with tempfile.TemporaryDirectory(prefix="dkc1-widescreen-") as temp:
            exe = str(Path(temp) / name)
            cmd = [compiler, "-std=c11", "-O1", "-DSNESRECOMP_REVERSE_DEBUG=0"]
            cmd += ["-I" + str(ROOT / path) for path in includes]
            cmd += [str(ROOT / path) for path in sources]
            cmd += ["-lm", "-o", exe]
            build = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_native_boundaries_and_live_scroll(self):
        self.build_and_run("ppu-model", [
            "tests/ppu_widescreen_boundary_model.c",
            "snesrecomp/runner/src/snes/ppu.c",
            "snesrecomp/runner/src/snes/ppu_legacy.c",
            "snesrecomp/runner/src/snes/ws_shadow.c",
        ], ["snesrecomp/runner/src"])

    def test_wall_adjacency_and_openings(self):
        self.build_and_run("terrain-model", [
            "tests/terrain_adjacency_model.c",
        ], ["runner"])
    def test_shadow_window(self):
        self.build_and_run("shadow-window", [
            "tests/shadow_window_model.c",
        ], ["runner"])


    def test_wall_seam_source_and_native_containment(self):
        self.build_and_run("wall-seam", [
            "tests/test_wall_seam_model.c",
        ], ["runner"])
