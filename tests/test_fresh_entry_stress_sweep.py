import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "stress", ROOT / "tools" / "fresh_entry_stress_sweep.py")
STRESS = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(STRESS)


def run(hashes, *, grade=None, rc=0, result="completed", budget=(0, 0),
        cache=(0, 0), wraps=0):
    return {
        "exit_code": rc,
        "parsed": {"result": result, "hashes": hashes},
        "widescreen_grade": grade or {"failures": []},
        "cache": {"oob_read": cache[0], "oob_write": cache[1]},
        "oam_pipeline": {"xhigh_loss_suspects": wraps,
                         "verdict": "clean"},
        "oam_budget": {"range_over_frames": budget[0],
                       "time_over_frames": budget[1]},
        "artifacts": {"frame": "unused.ppm"},
    }


def put16(wram, offset, value):
    wram[offset] = value & 0xFF
    wram[offset + 1] = (value >> 8) & 0xFF


def ready_wram(*, selector=1, actor_id=1, x=0x1234, y=0x5678,
               lower=0x0100, upper=0x0800):
    wram = bytearray(STRESS.WRAM_SIZE)
    for offset, value in ((0x0030, 5), (0x0032, 6), (0x003E, 0xA4),
                          (0x1DF1, 0), (0x1B23, lower),
                          (0x1B25, upper), (0x056F, selector)):
        put16(wram, offset, value)
    if 1 <= selector <= 25:
        index = selector * 2
        for offset, value in ((0x0D45, actor_id), (0x0B19, x),
                              (0x0BC1, y), (0x1029, 0x10),
                              (0x10D1, 0x20)):
            put16(wram, offset + index, value)
    return bytes(wram)


class FreshEntryStressSweepTests(unittest.TestCase):
    def test_action_and_entrance_parsing(self):
        self.assertEqual(["neutral", "right_y"],
                         STRESS.parse_csv_names("neutral,right_y", STRESS.ACTIONS))
        self.assertEqual({0x24, 7}, STRESS.parse_entrances("0x24,7"))
        with self.assertRaisesRegex(ValueError, "unknown actions"):
            STRESS.parse_csv_names("neutral,teleport", STRESS.ACTIONS)

    def test_script_replays_authentic_entry_before_stress(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.dks"
            STRESS.write_input_script(path, 0x82, 120, 360)
            self.assertEqual(
                ["# generated authentic fresh-entry plus controller-only stress",
                 "1 * 1", "0 * 360", "82 * 120"],
                path.read_text(encoding="utf-8").splitlines())

    def test_aligned_action_script_does_not_reenter_level(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.dks"
            STRESS.write_input_script(
                path, 0x82, 120, 0, enter_before_stress=False)
            self.assertEqual(
                ["# generated authentic fresh-entry plus controller-only stress",
                 "82 * 120"],
                path.read_text(encoding="utf-8").splitlines())

    def test_ready_script_uses_predicates_not_a_fixed_entry_delay(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ready.dks"
            STRESS.write_ready_script(
                path, level=5, mode=6, entrance=0xA4,
                fade=7, timeout=900, stable_frames=4)
            lines = path.read_text(encoding="utf-8").splitlines()
            self.assertEqual("1 * 1", lines[1])
            self.assertIn("wait 0030 == 0005 timeout 900", lines)
            self.assertIn("wait 003E == 00A4 timeout 900", lines)
            self.assertIn("wait 1DF1 == 0007 timeout 900", lines)
            self.assertIn("wait 1B25 != 0000 timeout 900", lines)
            self.assertEqual("0 * 4", lines[-1])

    def test_gameplay_ready_summary_follows_active_kong_selector(self):
        summary = STRESS.gameplay_ready_summary(
            ready_wram(selector=3, actor_id=2, x=0x3456, y=0x789A))
        self.assertTrue(summary["gameplay_ready"])
        self.assertEqual(6, summary["actor_index"])
        self.assertEqual(2, summary["actor_id"])
        self.assertEqual(0x3456, summary["actor_x"])
        self.assertEqual(0x789A, summary["actor_y"])

    def test_gameplay_ready_rejects_missing_player_or_camera_span(self):
        self.assertFalse(STRESS.gameplay_ready_summary(
            ready_wram(selector=0))["gameplay_ready"])
        self.assertFalse(STRESS.gameplay_ready_summary(
            ready_wram(lower=0, upper=0))["gameplay_ready"])
        self.assertTrue(STRESS.gameplay_ready_summary(
            ready_wram(lower=0x100, upper=0x100, actor_id=1))
            ["gameplay_ready"])
        with self.assertRaisesRegex(ValueError, "expected 131072"):
            STRESS.gameplay_ready_summary(b"short")

    def test_ready_root_comparison_allows_widened_bounds(self):
        native = STRESS.gameplay_ready_summary(
            ready_wram(lower=0x0100, upper=0x0800))
        wide = STRESS.gameplay_ready_summary(
            ready_wram(lower=0x0138, upper=0x07C8))
        result = STRESS.compare_ready_roots(
            {"exit_code": 0, "parsed": {"result": "completed"},
             "ready_state": native},
            {"exit_code": 0, "parsed": {"result": "completed"},
             "ready_state": wide, "widescreen_grade": {"failures": []}})
        self.assertTrue(result["aligned"])
        self.assertEqual({}, result["differences"])

    def test_ready_root_comparison_stops_on_gameplay_phase_mismatch(self):
        native = STRESS.gameplay_ready_summary(ready_wram(x=0x1234))
        wide = STRESS.gameplay_ready_summary(ready_wram(x=0x1235))
        result = STRESS.compare_ready_roots(
            {"exit_code": 0, "parsed": {"result": "completed"},
             "ready_state": native},
            {"exit_code": 0, "parsed": {"result": "completed"},
             "ready_state": wide, "widescreen_grade": {"failures": []}})
        self.assertFalse(result["aligned"])
        self.assertEqual("investigate", result["status"])
        self.assertIn("actor_x", result["differences"])

    def test_phase_guard_is_explicit_and_reported(self):
        source = (ROOT / "tools" / "fresh_entry_stress_sweep.py").read_text(
            encoding="utf-8")
        self.assertIn('"--prefetch-phase-guard"', source)
        self.assertIn('"DKC1_PREFETCH_PHASE_GUARD":', source)
        self.assertIn('"1" if prefetch_phase_guard else "0"', source)
        self.assertIn('"prefetch_phase_guard":', source)
        self.assertIn("bool(args.prefetch_phase_guard)", source)
        self.assertNotIn("DKC1_PREFETCH_PRESENTATION_POSE", source)

    def test_machine_divergence_is_investigation_not_visual_failure(self):
        base = {key: "A" * 64 for key in STRESS.MACHINE_HASHES}
        other = dict(base)
        other["wram_sha256"] = "B" * 64
        result = STRESS.compare_pair(run(base), run(other), 43)
        self.assertEqual("investigate", result["status"])
        self.assertFalse(result["center_comparison"]["eligible"])

    def test_wide_evidence_failures_are_hard_failures(self):
        base = {key: "A" * 64 for key in STRESS.MACHINE_HASHES}
        result = STRESS.compare_pair(
            run(base),
            run(base, grade={"failures": ["terrain_margin_miss"]},
                budget=(2, 1), cache=(1, 0), wraps=1), 43)
        self.assertEqual("fail", result["status"])
        self.assertIn("wide_terrain_margin_miss", result["failures"])
        self.assertIn("wide_shadow_cache_oob", result["failures"])
        self.assertIn("wide_oam_x_wrap", result["failures"])
        self.assertIn("wide_oam_budget_regression", result["failures"])


if __name__ == "__main__":
    unittest.main()
