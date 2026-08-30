import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "analyze_pacing.py"
SPEC = importlib.util.spec_from_file_location("analyze_pacing", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def frame(number: int, interval: float, overruns: int = 0) -> dict:
    return {
        "frame": number,
        "work_ms": 1.25,
        "wait_ms": 15.3,
        "late_ms": 0.0,
        "present_interval_ms": interval + 0.2,
        "submit_interval_ms": interval,
        "submit_error_ms": 0.02,
        "present_ms": 0.2,
        "overruns": overruns,
    }


def frame_v3(number: int, interval: float, overruns: int = 0) -> dict:
    item = frame(number, interval, overruns)
    item.update({
        "setup_ms": 0.1,
        "emulation_ms": 0.8,
        "render_ms": 0.5,
        "diagnostics_ms": 0.05,
        "audio_ms": 0.02,
        "audio_queued_frames": 1068,
        "audio_starvations": 1,
        "audio_drops": 0,
        "audio_ring_frames": 2136,
        "audio_internal_underflows": 0,
    })
    return item


class AnalyzePacingTests(unittest.TestCase):
    def test_v2_prefers_submit_cadence_and_discards_warmup(self):
        header = {"schema": "dkc1.pacing.v2", "refresh_hz": 60.0}
        frames = [frame(1, 40.0, 1), frame(2, 16.5, 1),
                  frame(3, 16.7, 1)]
        summary = MODULE.analyze(header, frames, warmup=1)
        self.assertEqual(summary["interval_source"], "submit_interval_ms")
        self.assertEqual(summary["steady_frames"], 2)
        self.assertAlmostEqual(summary["interval_ms"]["p50"], 16.6)
        self.assertEqual(summary["steady_overruns"], 0)
        self.assertIn("present_ms", summary)

    def test_v1_uses_completion_interval(self):
        header = {"schema": "dkc1.pacing.v1", "refresh_hz": 60.0}
        frames = [frame(1, 16.5), frame(2, 16.7)]
        for item in frames:
            del item["submit_interval_ms"]
            del item["submit_error_ms"]
            del item["present_ms"]
        summary = MODULE.analyze(header, frames, warmup=0)
        self.assertEqual(summary["interval_source"],
                         "present_interval_ms")
        self.assertNotIn("present_ms", summary)

    def test_v3_reports_phase_and_audio_health(self):
        header = {"schema": "dkc1.pacing.v3", "refresh_hz": 60.0}
        frames = [frame_v3(1, 16.6), frame_v3(2, 16.7)]
        summary = MODULE.analyze(header, frames, warmup=0)
        self.assertAlmostEqual(summary["emulation_ms"]["p50"], 0.8)
        self.assertEqual(summary["audio_queued_frames"]["min"], 1068)
        self.assertEqual(summary["steady_audio_starvations"], 1)
        self.assertEqual(summary["steady_audio_drops"], 0)
        self.assertEqual(summary["audio_ring_frames"]["min"], 2136)
        self.assertEqual(summary["steady_audio_internal_underflows"], 0)

    def test_loader_validates_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pacing.jsonl"
            lines = [
                {"schema": "dkc1.pacing.v2", "refresh_hz": 60.0},
                frame(1, 16.6),
            ]
            path.write_text("\n".join(json.dumps(item) for item in lines),
                            encoding="utf-8")
            header, frames = MODULE.load_log(path)
            self.assertEqual(header["schema"], "dkc1.pacing.v2")
            self.assertEqual(len(frames), 1)

    def test_warmup_must_leave_samples(self):
        with self.assertRaisesRegex(ValueError, "leaves no frames"):
            MODULE.analyze(
                {"schema": "dkc1.pacing.v2", "refresh_hz": 60.0},
                [frame(1, 16.6)], warmup=1)


if __name__ == "__main__":
    unittest.main()
