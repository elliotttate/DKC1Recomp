import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from audit_prefetch_wram import ACTOR_ARRAYS, actors_by_source, audit_frames


def put16(memory, offset, value):
    memory[offset] = value & 0xFF
    memory[offset + 1] = value >> 8


def frame(actor=None, index=2):
    memory = bytearray(0x20000)
    if actor:
        values = {"id": 0x4D, "source": 0x0D, "x": 0x300,
                  "y": 0xB0, "x_speed": 0xFFE0, "state": 0,
                  "animation": 0x15A}
        values.update(actor)
        for name, value in values.items():
            put16(memory, ACTOR_ARRAYS[name] + index, value)
    return bytes(memory)


class PrefetchWramAuditTests(unittest.TestCase):
    def test_motion_at_stock_allocation_is_behavior_advancement(self):
        stock = {1: frame(), 2: frame(), 3: frame(), 4: frame(),
                 5: frame({"x": 0x300})}
        wide = {1: frame(), 2: frame({"x": 0x300}),
                3: frame({"x": 0x2FF}), 4: frame({"x": 0x2FE}),
                5: frame({"x": 0x2FD})}
        report = audit_frames(stock, wide)
        finding = next(f for f in report["findings"]
                       if f.get("stock_start") == 5)
        self.assertEqual(finding["verdict"], "behavior_phase_advancement")
        self.assertEqual(finding["wide_lead_frames"], 3)
        self.assertIn("x", finding["differences_at_stock_allocation"])

    def test_slot_changes_do_not_break_source_record_alignment(self):
        stock = {1: frame(), 2: frame(), 3: frame({"x": 0x300}, 8)}
        wide = {1: frame({"x": 0x300}, 14),
                2: frame({"x": 0x300}, 14),
                3: frame({"x": 0x300}, 14)}
        finding = next(f for f in audit_frames(stock, wide)["findings"]
                       if f.get("stock_start") == 3)
        self.assertEqual(finding["verdict"], "harmless_visual_prefetch")
        self.assertEqual(finding["stock_actor_indices"], [8])
        self.assertEqual(finding["wide_actor_indices"], [14])

    def test_animation_only_difference_is_not_called_harmless(self):
        stock = {1: frame(), 2: frame(),
                 3: frame({"animation": 0x15A})}
        wide = {1: frame({"animation": 0x15A}),
                2: frame({"animation": 0x15B}),
                3: frame({"animation": 0x15B})}
        finding = next(f for f in audit_frames(stock, wide)["findings"]
                       if f.get("stock_start") == 3)
        self.assertEqual(finding["verdict"], "animation_phase_advancement")
        self.assertEqual(finding["disposition"], "requires_visual_oracle")

    def test_kong_source_zero_is_not_a_placed_object_duplicate(self):
        memory = bytearray(frame())
        for index, actor_id in ((2, 1), (4, 2)):
            put16(memory, ACTOR_ARRAYS["id"] + index, actor_id)
            put16(memory, ACTOR_ARRAYS["source"] + index, 0)
        selected, duplicates = actors_by_source(bytes(memory))
        self.assertNotIn(0, selected)
        self.assertFalse(duplicates)

    def test_opaque_slot_residue_is_indeterminate_not_behavior(self):
        stock = {1: frame(), 2: frame(),
                 3: frame({"work_1375": 0x2222}, 8)}
        wide = {1: frame({"work_1375": 0x1111}, 14),
                2: frame({"work_1375": 0x1111}, 14),
                3: frame({"work_1375": 0x1111}, 14)}
        finding = next(f for f in audit_frames(stock, wide)["findings"]
                       if f.get("stock_start") == 3)
        self.assertEqual(
            finding["verdict"], "indeterminate_actor_work_difference")
        self.assertEqual(finding["disposition"], "requires_semantic_trace")
        self.assertIn("work_1375", finding["difference_groups"]["opaque_work"])
        self.assertFalse(finding["difference_groups"]["behavior"])

    def test_behavior_advancement_takes_priority_over_persistence(self):
        stock = {
            1: frame(), 2: frame(), 3: frame(),
            4: frame({"x": 0x300}), 5: frame(), 6: frame(),
        }
        wide = {
            1: frame({"x": 0x300}), 2: frame({"x": 0x2FF}),
            3: frame({"x": 0x2FE}), 4: frame({"x": 0x2FD}),
            5: frame({"x": 0x2FC}), 6: frame({"x": 0x2FB}),
        }
        finding = next(f for f in audit_frames(stock, wide)["findings"]
                       if f.get("stock_start") == 4)
        self.assertEqual(finding["verdict"], "behavior_phase_advancement")
        self.assertTrue(finding["persists_past_stock_cull"])
        self.assertIn("x", finding["difference_groups"]["behavior"])


if __name__ == "__main__":
    unittest.main()
