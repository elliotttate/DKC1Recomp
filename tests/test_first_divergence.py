import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "first_divergence", ROOT / "tools" / "first_divergence.py")
FIRST_DIVERGENCE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(FIRST_DIVERGENCE)


def put16(memory: bytearray, offset: int, value: int) -> None:
    memory[offset] = value & 0xFF
    memory[offset + 1] = value >> 8


class FirstDivergenceTests(unittest.TestCase):
    def test_first_hash_divergence_can_begin_after_initializer(self):
        stock = [(1, "same"), (2, "stock-init"), (3, "same-again"),
                 (4, "stock-gameplay")]
        wide = [(1, "same"), (2, "wide-init"), (3, "same-again"),
                (4, "wide-gameplay")]

        self.assertEqual(
            FIRST_DIVERGENCE.first_hash_divergence(stock, wide), 2)
        self.assertEqual(
            FIRST_DIVERGENCE.first_hash_divergence(stock, wide, 3), 4)
        self.assertIsNone(
            FIRST_DIVERGENCE.first_hash_divergence(stock, wide, 5))

    def test_first_hash_divergence_rejects_frame_misalignment(self):
        with self.assertRaisesRegex(RuntimeError, "different frame numbering"):
            FIRST_DIVERGENCE.first_hash_divergence(
                [(1, "same")], [(2, "same")])

    def test_render_pose_refresh_and_oam_are_presentation_not_gameplay(self):
        stock = bytearray(FIRST_DIVERGENCE.WRAM_SIZE)
        wide = bytearray(stock)
        index = 0x06
        for memory in (stock, wide):
            put16(memory, 0x0D45 + index, 0x0005)
            put16(memory, 0x15FD + index, 0x000C)
            put16(memory, 0x0D11 + index, 0x001C)
        put16(stock, 0x0AE5 + index, 0x0018)
        put16(wide, 0x0AE5 + index, 0x001C)
        wide[0x0200] = 0x55

        result = FIRST_DIVERGENCE.classify(bytes(stock), bytes(wide))

        self.assertFalse(result["gameplay_critical"])
        self.assertFalse(result["actor_bookkeeping_critical"])
        self.assertFalse(result["scene_outcome_critical"])
        self.assertEqual(result["divergence_class"], "presentation_only")
        self.assertEqual(result["presentation_diff_bytes"], 2)
        self.assertEqual(len(result["render_pose_refresh_only_actors"]), 1)

    def test_actor_motion_difference_is_gameplay_critical(self):
        stock = bytearray(FIRST_DIVERGENCE.WRAM_SIZE)
        wide = bytearray(stock)
        put16(wide, 0x0E89 + 0x06, 0x0040)

        result = FIRST_DIVERGENCE.classify(bytes(stock), bytes(wide))

        self.assertTrue(result["gameplay_critical"])
        self.assertTrue(result["actor_bookkeeping_critical"])
        self.assertFalse(result["scene_outcome_critical"])
        self.assertEqual(result["divergence_class"], "gameplay_state")
        self.assertEqual(result["gameplay_actor_differences"][0]["slot"],
                         0x06)

    def test_scene_outcome_is_separate_from_camera_transient(self):
        stock = bytearray(FIRST_DIVERGENCE.WRAM_SIZE)
        wide = bytearray(stock)
        put16(wide, 0x088B, 0xFF38)
        camera_only = FIRST_DIVERGENCE.classify(bytes(stock), bytes(wide))
        self.assertTrue(camera_only["gameplay_critical"])
        self.assertFalse(camera_only["actor_bookkeeping_critical"])
        self.assertFalse(camera_only["scene_outcome_critical"])

        put16(wide, 0x003E, 0x00F9)
        outcome = FIRST_DIVERGENCE.classify(bytes(stock), bytes(wide))
        self.assertTrue(outcome["scene_outcome_critical"])
        self.assertIn("entrance", outcome["scene_outcome_fields"])

    def test_contiguous_ranges_are_exact(self):
        self.assertEqual(
            FIRST_DIVERGENCE.contiguous_ranges([1, 2, 4]),
            [{"first": "0x00001", "last": "0x00002", "count": 2},
             {"first": "0x00004", "last": "0x00004", "count": 1}])

    def test_sparse_wram_dump_is_reconstructed_at_original_offsets(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / "sparse"
            payload = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE])
            raw = prefix.with_suffix(".bin")
            raw.write_bytes(payload)
            index = Path(str(raw) + ".jsonl")
            records = [
                {"schema": "dkc1.wram.dump.v1", "type": "manifest",
                 "first_frame": 1, "last_frame": 1, "payload_size": 5,
                 "ranges": [["00010", "00011"],
                            ["00ae5", "00ae7"]]},
                {"schema": "dkc1.wram.dump.v1", "type": "frame",
                 "relative_frame": 1, "emulator_frame": 99,
                 "offset": 0, "length": 5,
                 "sha256": hashlib.sha256(payload).hexdigest()},
            ]
            index.write_text(
                "\n".join(json.dumps(row) for row in records) + "\n",
                encoding="utf-8")

            frames = FIRST_DIVERGENCE.load_wram_frames(prefix)

            self.assertEqual(frames[1][0x10:0x12], b"\xAA\xBB")
            self.assertEqual(frames[1][0x0AE5:0x0AE8], b"\xCC\xDD\xEE")
            self.assertEqual(frames[1][0x0200], 0)


if __name__ == "__main__":
    unittest.main()
