import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "analyze_ws_trace.py"
SPEC = importlib.util.spec_from_file_location("analyze_ws_trace", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def record(frame: int, left: str, vram: str = "1" * 16) -> dict:
    zero = {"west_raw": 0, "east_raw": 0, "prefill_refresh": 0}
    return {
        "schema": "dkc1.ws.frame.v1",
        "frame": frame,
        "scene": {},
        "source": {},
        "camera": {"x": 0},
        "ppu": {},
        "calibration": {"selected": 1},
        "identity": {"hash": "9" * 16, "change_mask": 0},
        "decision": {"edge_extension": 1, "centered_fallback": 0,
                     "identity_reset": 0, "grace_accepted": 0,
                     "bounds_ready": 1,
                     "shadow_commit": 1, "shadow_frame": 1},
        "world": [{}, {}],
        "margin_tiles": 0,
        "shadow_delta": [dict(zero), dict(zero)],
        "hash": {
            "left": left, "center": "0" * 16, "right": "2" * 16,
            "bg1_left": "3" * 16, "bg1_right": "4" * 16,
            "bg2_left": "5" * 16, "bg2_right": "6" * 16,
            "wram": "a" * 16, "vram": vram, "cgram": "9" * 16,
            "ppu_oam": "7" * 16,
            "wram_oam": "8" * 16,
        },
    }


def centered_record(frame: int, extra: int = 43) -> dict:
    black = MODULE.fnv1a_zero_bytes(extra * 4 * 224)
    item = record(frame, black)
    item["hash"]["right"] = black
    item["decision"].update({
        "edge_extension": 0,
        "centered_fallback": 1,
        "shadow_commit": 0,
        "shadow_frame": 0,
    })
    return item


class AnalyzeWsTraceTests(unittest.TestCase):
    def test_finds_margin_change_with_stable_machine_inputs(self):
        frames = [record(10, "a" * 16), record(11, "b" * 16)]
        summary = MODULE.analyze(frames)
        self.assertEqual(summary["frames"], 2)
        self.assertEqual(
            summary["stable_input_margin_changes"][0]["frame"], 11)
        self.assertEqual(
            summary["stable_input_margin_changes"][0]["changed_hashes"],
            ["left"])

    def test_does_not_call_scroll_or_palette_change_nondeterministic(self):
        first = record(12, "a" * 16)
        second = record(13, "b" * 16)
        second["ppu"]["h"] = [1, 0, 0, 0]
        second["hash"]["cgram"] = "e" * 16
        summary = MODULE.analyze([first, second])
        self.assertEqual(summary["stable_input_margin_changes"], [])

    def test_legacy_trace_marks_margin_lead_unproven_without_full_hash(self):
        first = record(14, "a" * 16)
        second = record(15, "b" * 16)
        del first["hash"]["cgram"]
        del second["hash"]["cgram"]
        summary = MODULE.analyze([first, second])
        self.assertEqual(summary["stable_input_margin_changes"], [])
        self.assertEqual(
            summary["stable_input_unproven_margin_changes"][0]["reason"],
            "trace_missing_full_machine_hash")

    def test_wram_source_change_is_not_stable_input(self):
        first = record(16, "a" * 16)
        second = record(17, "b" * 16)
        second["hash"]["wram"] = "b" * 16
        summary = MODULE.analyze([first, second])
        self.assertEqual(summary["stable_input_margin_changes"], [])

    def test_identity_reset_is_not_stable_input(self):
        first = record(18, "a" * 16)
        second = record(19, "b" * 16)
        second["identity"] = {"hash": "c" * 16, "change_mask": 8}
        second["decision"].update({"reset": 1, "identity_reset": 1})
        summary = MODULE.analyze([first, second])
        self.assertEqual(summary["stable_input_margin_changes"], [])

    def test_loader_tracks_counter_reset_as_new_epoch(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("\n".join(json.dumps(item) for item in
                                       [record(4518, "a" * 16),
                                        record(4137, "b" * 16),
                                        record(4138, "c" * 16)]),
                            encoding="utf-8")
            frames = MODULE.load_trace(path)
            self.assertEqual([item["_sequence"] for item in frames],
                             [0, 1, 2])
            self.assertEqual([item["_epoch"] for item in frames], [0, 1, 1])
            summary = MODULE.analyze(frames)
            self.assertEqual(summary["frame_epochs"], 2)
            self.assertEqual(summary["sequence_range"], [0, 2])
            self.assertEqual(summary["frame_counter_resets"], [{
                "sequence": 1,
                "previous_frame": 4518,
                "frame": 4137,
                "epoch": 1,
            }])

    def test_reports_identity_transition_and_policy_violation(self):
        item = centered_record(20)
        item["identity"] = {"hash": "f" * 16, "change_mask": 8}
        item["decision"].update({"identity_reset": 1,
                                 "grace_accepted": 1,
                                 "shadow_commit": 1,
                                 "shadow_frame": 1})
        summary = MODULE.analyze([item])
        self.assertEqual(summary["identity_transitions"][0]["frame"], 20)
        self.assertEqual(
            summary["policy_violations"][0]["violations"],
            ["centered_frame_committed_shadow",
             "new_identity_used_old_grace"])

    def test_centered_fallback_requires_exact_black_side_margins(self):
        item = centered_record(30)
        item["hash"]["right"] = "d" * 16
        summary = MODULE.analyze([item])
        self.assertEqual(summary["black_margin_hash"],
                         MODULE.fnv1a_zero_bytes(43 * 4 * 224))
        self.assertEqual(
            summary["policy_violations"][0]["violations"],
            ["centered_margin_not_black"])
        self.assertEqual(
            summary["centered_nonblack_margin_frames"][0]["sides"],
            ["right"])

    def test_black_hash_supports_398_pixel_variant(self):
        item = centered_record(40, extra=71)
        summary = MODULE.analyze([item], extra=71)
        self.assertEqual(summary["policy_violations"], [])
        self.assertEqual(summary["centered_nonblack_margin_frames"], [])


if __name__ == "__main__":
    unittest.main()
