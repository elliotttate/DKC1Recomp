#!/usr/bin/env python3
"""Compile and run the level-wall presentation policy model test."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / "tests" / "edge_policy_model.c"
HEADER = ROOT / "runner" / "dkc1_edge_policy.h"


def compiler() -> str | None:
    for name in ("cc", "clang", "gcc"):
        found = shutil.which(name)
        if found:
            return found
    return None


class EdgePolicyModelTest(unittest.TestCase):
    def test_model_vectors(self):
        cc = compiler()
        if cc is None:
            self.skipTest("no C compiler on PATH")
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / "edge_policy_model"
            build = subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
                 str(MODEL), "-o", str(exe)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("edge_policy_model: PASS", run.stdout)

    def test_header_is_pure_and_the_runtime_defaults_to_glide(self):
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("kDkc1EdgeReflect = 0", text)
        video = (ROOT / "runner" / "dkc1_video.c").read_text(encoding="utf-8")
        self.assertIn("s_edge_policy = kDkc1EdgeGlide;", video)
        picker = (ROOT / "runner" / "macos_file_picker.m").read_text(
            encoding="utf-8")
        self.assertIn("return kDkc1EdgeGlide;", picker)
        for forbidden in ("g_ram", "Dkc1ReadWram", "Dkc1Write", "cpu_read",
                          "extern "):
            self.assertNotIn(forbidden, text)


if __name__ == "__main__":
    unittest.main()
