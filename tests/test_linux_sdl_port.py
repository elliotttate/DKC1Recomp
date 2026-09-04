from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LinuxSdlPortTests(unittest.TestCase):
    def test_cmake_exposes_linux_sdl_target(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("if(UNIX AND NOT APPLE)", cmake)
        self.assertIn("add_executable(dkc1_snesrecomp_sdl", cmake)
        self.assertIn("runner/nonapple_sdl_platform.c", cmake)
        self.assertIn("snesrecomp_target_sdl(dkc1_snesrecomp_sdl)", cmake)
        self.assertIn('OUTPUT_NAME "DKC1Recomp"', cmake)

    def test_sdl_host_has_a_portable_monotonic_clock(self) -> None:
        host = (ROOT / "runner" / "sdl_host.c").read_text(encoding="utf-8")
        self.assertIn("SDL_GetPerformanceCounter()", host)
        self.assertIn("SDL_GetPerformanceFrequency()", host)
        self.assertIn('#define DKC1_HOST_PLATFORM "linux"', host)
        self.assertIn("#ifdef __APPLE__\n#include <mach/mach_time.h>", host)
        self.assertIn('usage: DKC1Recomp <rom.sfc>', host)

    def test_linux_build_script_builds_playable_and_headless_targets(self) -> None:
        script = (ROOT / "build_linux.sh").read_text(encoding="utf-8")
        self.assertIn("git submodule update --init --recursive", script)
        self.assertIn("-DSNESRECOMP_SDL_BACKEND=SDL2", script)
        self.assertIn("dkc1_snesrecomp_sdl dkc1_snesrecomp_headless", script)
        self.assertIn('echo "$build_dir/DKC1Recomp"', script)


if __name__ == "__main__":
    unittest.main()
