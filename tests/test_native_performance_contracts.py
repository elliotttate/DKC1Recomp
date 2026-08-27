from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NativePerformanceContractTests(unittest.TestCase):
    def test_hot_viewport_predicate_is_forced_inline(self):
        ppu = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
               "ppu.c").read_text(encoding="utf-8")
        self.assertIn("static FORCEINLINE bool PpuViewportAllows", ppu)

    def test_stack_balance_audit_is_tools_only(self):
        common = (ROOT / "snesrecomp" / "runner" / "src" /
                  "common_cpu_infra.c").read_text(encoding="utf-8")
        tools_build = (ROOT / "build_host_tools.bat").read_text(
            encoding="utf-8")
        player_build = (ROOT / "build_host.bat").read_text(encoding="utf-8")
        self.assertIn("#define SNESRECOMP_STACKBAL_AUDIT 0", common)
        self.assertIn("#if SNESRECOMP_STACKBAL_AUDIT", common)
        self.assertIn("/DSNESRECOMP_STACKBAL_AUDIT=1", tools_build)
        self.assertNotIn("/DSNESRECOMP_STACKBAL_AUDIT=1", player_build)

    def test_ppu_profiler_is_tools_only(self):
        ppu = (ROOT / "snesrecomp" / "runner" / "src" / "snes" /
               "ppu.c").read_text(encoding="utf-8")
        tools_build = (ROOT / "build_host_tools.bat").read_text(
            encoding="utf-8")
        player_build = (ROOT / "build_host.bat").read_text(encoding="utf-8")
        self.assertIn("#if SNESRECOMP_PPU_PROFILE_SUPPORT", ppu)
        self.assertIn("/DSNESRECOMP_PPU_PROFILE_SUPPORT=1", tools_build)
        self.assertNotIn("/DSNESRECOMP_PPU_PROFILE_SUPPORT=1", player_build)

    def test_scoped_write_log_skips_name_matching_when_unarmed(self):
        common = (ROOT / "snesrecomp" / "runner" / "src" /
                  "common_cpu_infra.c").read_text(encoding="utf-8")
        self.assertIn('const char *output = getenv("SNESRECOMP_WLOG")',
                      common)
        self.assertIn("return enabled && name && strncmp", common)


if __name__ == "__main__":
    unittest.main()
