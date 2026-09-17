#!/usr/bin/env python3
"""Generate the private Dixie runtime from the user's verified clean ROM."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="dkc1-dixie-generate-") as directory:
        temporary = Path(directory)
        source = temporary / "patch.c"
        source.write_text(r'''#include "dkc1_dixie_mod.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  size_t size = 0; char error[256];
  unsigned char *rom = Dkc1DixieLoadRom(argv[1], &size, error, sizeof error);
  if (!rom) { fprintf(stderr, "%s\n", error); return 1; }
  FILE *out = fopen(argv[2], "wb");
  int ok = out && fwrite(rom, 1, size, out) == size;
  if (out && fclose(out)) ok = 0;
  free(rom); return ok ? 0 : 1;
}
''')
        patcher = temporary / "patch"
        subprocess.run([
            os.environ.get("CC", "cc"), "-O2", "-I" + str(ROOT / "runner"),
            "-I" + str(ROOT / "snesrecomp/runner/src"), str(source),
            str(ROOT / "runner/dkc1_dixie_mod.c"),
            str(ROOT / "runner/verified_rom.c"),
            str(ROOT / "snesrecomp/runner/src/sha256.c"),
            "-framework", "CoreFoundation", "-o", str(patcher)], check=True)
        rom = temporary / "dixie.sfc"
        subprocess.run([str(patcher), str(args.rom.resolve()), str(rom)], check=True)
        subprocess.run([
            sys.executable, str(ROOT / "scripts/generate_snesrecomp.py"),
            "--rom", str(rom), "--config-dir", str(ROOT / "recomp/dixie"),
            "--output-dir", str(ROOT / "generated/snesrecomp_dixie"),
            "--no-widescreen-overrides", "--analysis-backend", "python"],
            check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
