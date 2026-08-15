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
        "decision": {"edge_extension": 1, "centered_fallback": 0},
        "world": [{}, {}],
        "margin_tiles": 0,
        "shadow_delta": [dict(zero), dict(zero)],
        "hash": {
            "left": left, "center": "0" * 16, "right": "2" * 16,
            "bg1_left": "3" * 16, "bg1_right": "4" * 16,
            "bg2_left": "5" * 16, "bg2_right": "6" * 16,
            "vram": vram, "ppu_oam": "7" * 16,
            "wram_oam": "8" * 16,
        },
    }


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

    def test_loader_rejects_nonmonotonic_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            path.write_text("\n".join(json.dumps(item) for item in
                                       [record(2, "a" * 16),
                                        record(2, "b" * 16)]),
                            encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "strictly ordered"):
                MODULE.load_trace(path)


if __name__ == "__main__":
    unittest.main()
