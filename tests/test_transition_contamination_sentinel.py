import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "transition_contamination_sentinel.py")
SPEC = importlib.util.spec_from_file_location(
    "transition_contamination_sentinel", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def record(frame, identity="a", *, level=1, **decision):
    return {
        "schema": "dkc1.ws.frame.v1", "frame": frame,
        "scene": {"mode": 0, "level": level, "entrance": 2},
        "source": {"bank": 0xD9, "map": 1, "metatiles": 2,
                   "stream_vram": 3},
        "identity": {"hash": identity, "change_mask": 0},
        "decision": decision,
    }


def write_ppm(path: Path, width: int, height: int, pixels: bytes):
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + pixels)


class TransitionSentinelTests(unittest.TestCase):
    def test_discovers_identity_and_explicit_resets(self):
        rows = [record(100), record(101),
                record(102, identity="b", identity_reset=1),
                record(103, identity="b", level=2)]
        found = MODULE.discover_transitions(rows)
        self.assertEqual([item["relative_frame"] for item in found], [1, 3, 4])
        self.assertIn("identity_hash", found[1]["reasons"])
        self.assertIn("identity_reset", found[1]["reasons"])
        self.assertIn("level", found[2]["reasons"])

    def test_schedules_and_deduplicates_followups(self):
        transitions = [
            {"relative_frame": 3, "reasons": ["a"]},
            {"relative_frame": 4, "reasons": ["b"]},
        ]
        plan = MODULE.scheduled_samples(transitions, 8, [0, 1, 4])
        self.assertEqual([item["relative_frame"] for item in plan], [3, 4, 5, 7, 8])
        frame4 = next(item for item in plan if item["relative_frame"] == 4)
        self.assertEqual(len(frame4["origins"]), 2)

    def test_layer_classification_distinguishes_margin_and_center(self):
        width = MODULE.DEFAULT_NATIVE_WIDTH + MODULE.DEFAULT_EXTRA * 2
        size = width * MODULE.DEFAULT_HEIGHT * 3
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            retained = bytearray(size)
            cold = bytearray(size)
            write_ppm(root / "retained.ppm", width, MODULE.DEFAULT_HEIGHT,
                      retained)
            write_ppm(root / "cold.ppm", width, MODULE.DEFAULT_HEIGHT, cold)
            exact = MODULE.compare_surface(root / "retained.ppm",
                                           root / "cold.ppm",
                                           MODULE.DEFAULT_EXTRA)
            raw = {"wram": {"exact": True}}
            self.assertEqual(MODULE.classify_layers(raw, {"bg1": exact}),
                             "identical")

            retained[0:3] = b"\xff\x00\x00"
            write_ppm(root / "retained.ppm", width, MODULE.DEFAULT_HEIGHT,
                      retained)
            margin = MODULE.compare_surface(root / "retained.ppm",
                                            root / "cold.ppm",
                                            MODULE.DEFAULT_EXTRA)
            self.assertEqual(MODULE.classify_layers(raw, {"bg1": margin}),
                             "retained_layer_contamination")

            center_pixel = MODULE.DEFAULT_EXTRA * 3
            retained[center_pixel:center_pixel + 3] = b"\x00\xff\x00"
            write_ppm(root / "retained.ppm", width, MODULE.DEFAULT_HEIGHT,
                      retained)
            center = MODULE.compare_surface(root / "retained.ppm",
                                            root / "cold.ppm",
                                            MODULE.DEFAULT_EXTRA)
            self.assertEqual(MODULE.classify_layers(raw, {"bg1": center}),
                             "native_center_mismatch")

    def test_machine_state_mismatch_wins(self):
        raw = {"wram": {"exact": False}}
        layers = {"bg1": {side: {"changed_pixels": 0}
                           for side in ("left", "center", "right")}}
        self.assertEqual(MODULE.classify_layers(raw, layers),
                         "machine_state_mismatch")


if __name__ == "__main__":
    unittest.main()
