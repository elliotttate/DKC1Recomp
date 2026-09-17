import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class VerifiedRomTests(unittest.TestCase):
    def test_modified_rom_requires_one_exact_explicit_digest(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("a C compiler is required")

        harness = textwrap.dedent(
            """
            #include "verified_rom.h"
            #include <stdint.h>
            #include <stdlib.h>

            int main(int argc, char **argv) {
              size_t size = 0;
              char error[192];
              if (argc != 2) return 10;
              uint8_t *rom = Dkc1ReadVerifiedRom(argv[1], &size,
                                                 error, sizeof error);
              if (!rom) return 2;
              free(rom);
              return size == 0x400000u ? 0 : 3;
            }
            """
        )
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            harness_path = directory / "verified_rom_test.c"
            executable = directory / "verified_rom_test"
            rom_path = directory / "modified.sfc"
            harness_path.write_text(harness, encoding="utf-8")
            rom_path.write_bytes(bytes([0x5A]) * 0x400000)
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "runner"),
                    "-I",
                    str(ROOT / "snesrecomp" / "runner" / "src"),
                    str(harness_path),
                    str(ROOT / "runner" / "verified_rom.c"),
                    str(ROOT / "snesrecomp" / "runner" / "src" / "sha256.c"),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            rejected = subprocess.run([str(executable), str(rom_path)])
            self.assertEqual(rejected.returncode, 2)

            environment = os.environ.copy()
            environment["DKC1_ALLOW_ROM_SHA256"] = hashlib.sha256(
                rom_path.read_bytes()).hexdigest()
            accepted = subprocess.run(
                [str(executable), str(rom_path)], env=environment)
            self.assertEqual(accepted.returncode, 0)

            environment["DKC1_ALLOW_ROM_SHA256"] = "0" * 64
            wrong_digest = subprocess.run(
                [str(executable), str(rom_path)], env=environment)
            self.assertEqual(wrong_digest.returncode, 2)

            environment["DKC1_ALLOW_ROM_SHA256"] = "not-a-digest"
            malformed = subprocess.run(
                [str(executable), str(rom_path)], env=environment)
            self.assertEqual(malformed.returncode, 2)


if __name__ == "__main__":
    unittest.main()
