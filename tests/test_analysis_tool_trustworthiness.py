import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]


def load_tool(name: str):
    path = ROOT / "tools" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(
        f"test_{name}_trustworthiness", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


CAPABILITIES = load_tool("capability_manifest")
CORPUS = load_tool("build_profile_corpus")
PROFILE_DIFF = load_tool("profile_diff")
IMPACT = load_tool("impact")


def scene(**overrides):
    result = {
        "frames": 12,
        "widened": 10,
        "calibrated": 10,
        "raw_fallbacks": 0,
        "blank_serves": 0,
        "centered_in_gameplay": 0,
        "unstable_margin_frames": 0,
    }
    result.update(overrides)
    return result


class CapabilityManifestTrustTests(unittest.TestCase):
    def manifest_scene(self, stats: dict, exit_code: int = 0):
        report = {"routes": {"route.dks": {
            "exit_code": exit_code,
            "scenes": {"(0, 22, 217, 0)": stats},
        }}}
        return CAPABILITIES.build_manifest(report)["scenes"][0]

    def test_proven_requires_every_fail_closed_margin_gate(self):
        self.assertEqual(
            self.manifest_scene(scene())["host_widescreen"], "proven")
        cases = {
            "blank_serves_without_verified_blank_oracle":
                scene(blank_serves=1),
            "pillarbox_in_gameplay": scene(centered_in_gameplay=1),
            "unstable_margins": scene(unstable_margin_frames=1),
        }
        for blocker, stats in cases.items():
            with self.subTest(blocker=blocker):
                result = self.manifest_scene(stats)
                self.assertEqual(result["host_widescreen"], "degraded")
                self.assertIn(blocker, result["promotion_blockers"])

    def test_failed_route_cannot_supply_capability_evidence(self):
        result = self.manifest_scene(scene(), exit_code=22)
        self.assertEqual(result["host_widescreen"], "unproven")
        self.assertEqual(result["observed_frames"], 0)
        self.assertEqual(result["evidence_routes"], [])
        self.assertEqual(result["rejected_evidence_routes"], [{
            "route": "route.dks", "exit_code": 22,
        }])


class ProfileCorpusTrustTests(unittest.TestCase):
    @staticmethod
    def args(base: Path) -> argparse.Namespace:
        rom = base / "game.sfc"
        exe = base / "trace.exe"
        routes = base / "routes"
        out = base / "profiles"
        rom.write_bytes(b"rom")
        exe.write_bytes(b"exe")
        routes.mkdir()
        return argparse.Namespace(
            rom=rom, exe=exe, routes=routes, out=out, frames=20)

    @staticmethod
    def write_profile(env: dict):
        Path(env["SNESRECOMP_FUNC_PROFILE"]).write_text(
            json.dumps({
                "pc24": "0x808000", "name": "Reset_Entry_M0X0",
                "calls": 1, "first_frame": 0, "last_frame": 0,
                "contexts": [0],
            }) + "\n", encoding="utf-8")

    def test_success_replaces_stale_profiles_and_skips_dependent_leg(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(Path(directory))
            (args.routes / "standalone.dks").write_text(
                "000000 * 1\n", encoding="utf-8")
            (args.routes / "dependent.dks").write_text(
                "state_load seed.state\n", encoding="utf-8")
            args.out.mkdir()
            (args.out / "removed.profile.jsonl").write_text(
                "stale\n", encoding="utf-8")

            def run(command, **kwargs):
                self.write_profile(kwargs["env"])
                return subprocess.CompletedProcess(command, 0, "", "")

            with mock.patch.object(CORPUS.subprocess, "run", side_effect=run):
                self.assertEqual(CORPUS.build_corpus(args), 0)
            self.assertEqual(
                [path.name for path in args.out.glob("*.profile.jsonl")],
                ["standalone.profile.jsonl"])

    def test_any_route_failure_publishes_no_partial_or_stale_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(Path(directory))
            for name in ("good", "bad"):
                (args.routes / f"{name}.dks").write_text(
                    "000000 * 1\n", encoding="utf-8")
            args.out.mkdir()
            (args.out / "old.profile.jsonl").write_text(
                "stale\n", encoding="utf-8")

            def run(command, **kwargs):
                if Path(kwargs["env"]["DKC1_SCRIPT"]).stem == "good":
                    self.write_profile(kwargs["env"])
                    return subprocess.CompletedProcess(command, 0, "", "")
                return subprocess.CompletedProcess(command, 7, "", "failed")

            with mock.patch.object(CORPUS.subprocess, "run", side_effect=run):
                self.assertEqual(CORPUS.build_corpus(args), 1)
            self.assertEqual(list(args.out.glob("*.profile.jsonl")), [])

    def test_missing_profile_is_a_failed_route(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(Path(directory))
            (args.routes / "missing.dks").write_text(
                "000000 * 1\n", encoding="utf-8")
            with mock.patch.object(
                    CORPUS.subprocess, "run",
                    return_value=subprocess.CompletedProcess([], 0, "", "")):
                self.assertEqual(CORPUS.build_corpus(args), 1)
            self.assertEqual(list(args.out.glob("*.profile.jsonl")), [])

    def test_missing_routes_return_failure_and_remove_stale_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(Path(directory))
            args.out.mkdir()
            (args.out / "old.profile.jsonl").write_text(
                "stale\n", encoding="utf-8")
            self.assertEqual(CORPUS.build_corpus(args), 2)
            self.assertEqual(list(args.out.glob("*.profile.jsonl")), [])


class ProfileDiffTrustTests(unittest.TestCase):
    def test_total_counts_only_address_bearing_profiled_declarations(self):
        header = "\n".join((
            "void One(CpuState *cpu);  /* $80:8000 alias */",
            "RecompReturn One_M0X0(CpuState *cpu);",
            "void Two(CpuState *cpu);  /* $80:8010 alias */",
            "RecompReturn Two_M1X1(CpuState *cpu);",
            "void Handwritten(void);",
        ))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "funcs.h"
            path.write_text(header, encoding="utf-8")
            self.assertEqual(PROFILE_DIFF.total_functions(path), 2)

    def test_repository_denominator_matches_generated_header_contract(self):
        self.assertEqual(PROFILE_DIFF.total_functions(), 2558)


class ImpactCallerTrustTests(unittest.TestCase):
    ROWS = [
        {"address": "80:8000", "function": "CODE_808000",
         "assembly": "JSL.l CODE_BFC745",
         "pseudocode": "call CODE_BFC745"},
        {"address": "80:8010", "function": "CODE_808010",
         "assembly": "LDA.w #$0000",
         "pseudocode": "not_a_call_but_mentions_CODE_BFC745"},
        {"address": "BF:C745", "function": "CODE_BFC745",
         "assembly": "RTS", "pseudocode": "rts"},
    ]

    def test_prefers_structured_call_graph_over_pseudocode_substrings(self):
        graph = [
            {"ea": "808000", "name": "CODE_808000", "size": 4,
             "callers": []},
            {"ea": "BFC745", "name": "CODE_BFC745", "size": 2,
             "callers": ["808000"]},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "functions.json"
            path.write_text(json.dumps(graph), encoding="utf-8")
            callers, evidence, source = IMPACT.static_callers(
                "CODE_BFC745", 0xBFC745, self.ROWS, path)
        self.assertEqual(callers, ["CODE_808000"])
        self.assertEqual(evidence[0]["xref"], "808000")
        self.assertTrue(source.endswith("functions.json"))

    def test_fallback_uses_exact_assembly_operand_only(self):
        missing = Path("definitely-missing-functions.json")
        callers, _, source = IMPACT.static_callers(
            "CODE_BFC745", 0xBFC745, self.ROWS, missing)
        self.assertEqual(callers, ["CODE_808000"])
        self.assertIn("exact control-flow operands", source)


if __name__ == "__main__":
    unittest.main()
