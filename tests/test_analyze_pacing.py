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

    def test_v4_retains_cpu_audio_and_midpoint_timing(self):
        header = {"schema": "dkc1.pacing.v4", "refresh_hz": 60.0}
        frames = [frame_v3(1, 16.6), frame_v3(2, 16.7)]
        for row in frames:
            row.update(mid_presented=1, mid_skips=0, real_to_mid_ms=8.3,
                       mid_to_real_ms=8.4, mid_submit_error_ms=0.01)
        summary = MODULE.analyze(header, frames, warmup=0)
        self.assertEqual(summary["interval_source"], "submit_interval_ms")
        self.assertEqual(summary["mid_presented"], 2)
        self.assertEqual(summary["steady_mid_skips"], 0)
        self.assertAlmostEqual(summary["real_to_mid_ms"]["p50"], 8.3)
        self.assertEqual(summary["steady_audio_drops"], 0)

    def test_v5_reports_dxgi_scanout_statistics(self):
        header = {"schema": "dkc1.pacing.v5", "refresh_hz": 60.0,
                  "pacing": "waitable", "presenter": "d3d11",
                  "present_divisor": 1, "framegen_extra_refresh": 0}
        frames = [frame_v3(number, 16.7) for number in range(1, 6)]
        # Displayed-frame statistics lag the present call by one frame; the
        # fourth present occupied two refreshes (one repeated refresh).
        refreshes = [100, 101, 102, 104, 105]
        for index, row in enumerate(frames):
            row.update(mid_presented=0, mid_skips=0, real_to_mid_ms=0.0,
                       mid_to_real_ms=0.0, mid_submit_error_ms=0.0,
                       wait_timeout=1 if index == 3 else 0,
                       present_count=index + 2, stat_valid=1,
                       stat_present_count=index + 1,
                       stat_present_refresh=refreshes[index],
                       stat_sync_refresh=refreshes[index],
                       stat_sync_qpc_ms=1000.0 + refreshes[index] * 16.6666,
                       stat_disjoint=0, stat_lag_presents=1)
        summary = MODULE.analyze(header, frames, warmup=0)
        self.assertEqual(summary["interval_source"], "submit_interval_ms")
        self.assertEqual(summary["pacing"], "waitable")
        self.assertEqual(summary["steady_wait_timeouts"], 1)
        scan = summary["scanout"]
        self.assertEqual(scan["presents"], 4)
        self.assertEqual(scan["expected_refreshes_per_present"], 1)
        self.assertEqual(scan["repeated_refreshes"], 1)
        self.assertEqual(scan["early_refreshes"], 0)
        self.assertEqual(scan["stat_lag_presents_max"], 1)
        self.assertAlmostEqual(scan["refreshes_per_present"]["max"], 2.0)
        self.assertAlmostEqual(scan["measured_refresh_hz"], 60.0002, places=3)

    def test_v5_without_statistics_reports_no_presents(self):
        header = {"schema": "dkc1.pacing.v5", "refresh_hz": 60.0,
                  "pacing": "dwmflush", "presenter": "gdi",
                  "present_divisor": 1}
        frames = [frame_v3(1, 16.7), frame_v3(2, 16.6)]
        for row in frames:
            row.update(mid_presented=0, mid_skips=0, stat_valid=0,
                       stat_disjoint=0, stat_lag_presents=0)
        summary = MODULE.analyze(header, frames, warmup=0)
        self.assertEqual(summary["scanout"]["presents"], 0)
        self.assertIsNone(summary["scanout"]["refreshes_per_present"])

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

    def test_async_log_requires_complete_sidecar(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'pacing.jsonl'
            path.write_text(json.dumps({'schema':'dkc1.pacing.v5', 'refresh_hz':60,
                                        'log_mode':'async'})+'\n'+json.dumps(frame(1,16.7)))
            status = Path(str(path)+'.status.json')
            with self.assertRaisesRegex(ValueError, 'not closed cleanly'):
                MODULE.load_log(path)
            status.write_text(json.dumps({'schema':'dkc1.pacing-log-status.v1',
                                         'records':2,'dropped':0,'io_errors':0}))
            self.assertEqual(len(MODULE.load_log(path)[1]),1)
            for bad in ({'records':2,'dropped':1,'io_errors':0},
                        {'records':2,'dropped':0,'io_errors':1},
                        {'records':1,'dropped':0,'io_errors':0}, {'records':2}):
                status.write_text(json.dumps({'schema':'dkc1.pacing-log-status.v1',**bad}))
                with self.assertRaisesRegex(ValueError, 'incomplete'):
                    MODULE.load_log(path)

    def test_warmup_must_leave_samples(self):
        with self.assertRaisesRegex(ValueError, "leaves no frames"):
            MODULE.analyze(
                {"schema": "dkc1.pacing.v2", "refresh_hz": 60.0},
                [frame(1, 16.6)], warmup=1)


if __name__ == "__main__":
    unittest.main()
