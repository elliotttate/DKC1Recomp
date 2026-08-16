from pathlib import Path
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from triage_stress_lifecycle import (  # noqa: E402
    Episode, align_episodes, branch_triage, compare_segments, segment_events,
)


def event(kind, frame, **values):
    return {"schema": "dkc1.lifecycle.v1", "event": kind,
            "frame": frame, **values}


def episode(source, frame, *, actor_index, ident=0x30, x=0x100, y=0x200):
    return Episode(source, frame, event(
        "actor_alloc", frame, actor_index=actor_index, source=source,
        id=ident, x=x, y=y, xs=0, ys=0, state=0, anim=1, pose=2))


class StressLifecycleTriageTests(unittest.TestCase):
    def test_alignment_rejection_is_preserved_without_index_error(self):
        result = branch_triage({
            "entrance": 4, "name": "Example", "action": "right_y",
            "mask": 0x82, "status": "investigate", "failures": [],
            "investigations": ["gameplay_ready_root_mismatch"],
            "native_runs": [], "wide_runs": [],
        })
        self.assertTrue(result["skipped"])
        self.assertEqual(
            ["gameplay_ready_root_mismatch"],
            result["skip_investigations"])
        self.assertEqual([], result["findings"])

    def test_aligns_same_object_across_different_pool_slots(self):
        native = episode(7, 10, actor_index=2)
        wide = episode(7, 10, actor_index=24)
        self.assertEqual(align_episodes([native], [wide]), [(native, wide)])

    def test_extra_episode_is_not_forced_onto_wrong_identity(self):
        native = [episode(7, 10, actor_index=2, ident=1, x=0x100),
                  episode(7, 100, actor_index=4, ident=2, x=0x500)]
        wide = [episode(7, 10, actor_index=6, ident=1, x=0x100),
                episode(7, 50, actor_index=8, ident=9, x=0x900),
                episode(7, 100, actor_index=10, ident=2, x=0x500)]
        pairs = align_episodes(native, wide)
        self.assertEqual(pairs[0], (native[0], wide[0]))
        self.assertEqual(pairs[1], (None, wide[1]))
        self.assertEqual(pairs[2], (native[1], wide[2]))

    def test_source_zero_and_negative_actors_are_excluded(self):
        rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("actor_alloc", 2, actor_index=2, source=0, id=1, x=1, y=1),
            event("actor_alloc", 3, actor_index=4, source=-32768, id=2, x=2, y=2),
            event("actor_alloc", 4, actor_index=6, source=5, id=3, x=3, y=3),
        ]
        segment = segment_events(rows)[0]
        self.assertEqual(sorted(segment.episodes), [5])

    def test_gameplay_contexts_are_not_cross_matched(self):
        rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("actor_alloc", 2, actor_index=2, source=5, id=3, x=3, y=3),
            event("gameplay_exit", 5),
            event("gameplay_enter", 6, entrance=4, bounds=[0, 100]),
            event("actor_alloc", 7, actor_index=2, source=5, id=3, x=3, y=3),
        ]
        segments = segment_events(rows)
        self.assertEqual([(s.entrance, s.ordinal) for s in segments],
                         [(4, 0), (4, 1)])
        self.assertEqual(segments[0].episodes[5][0].start, 2)
        self.assertEqual(segments[1].episodes[5][0].start, 7)

    def test_section_is_critical_and_bookmark_is_evidence(self):
        native_rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("bookmark", 2, record=5, **{"from": 0, "to": 2}),
            event("section", 3, state=1, pointer=10, current=2, pending=3, limit=4),
        ]
        wide_rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("bookmark", 2, record=5, **{"from": 0, "to": 0}),
            event("section", 3, state=2, pointer=10, current=2, pending=3, limit=4),
        ]
        findings = compare_segments(segment_events(native_rows)[0],
                                    segment_events(wide_rows)[0])
        self.assertEqual(
            {(item["verdict"], item["severity"]) for item in findings},
            {("bookmark_final_difference", "medium"),
             ("section_sequence_difference", "critical")})

    def test_generic_ffff_scanner_limit_is_not_a_section_difference(self):
        native_rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("section", 2, state=0, pointer=0, current=0,
                  pending=0, limit=0xFFFF),
        ]
        wide_rows = [event("gameplay_enter", 1, entrance=4,
                           bounds=[0, 100])]
        findings = compare_segments(segment_events(native_rows)[0],
                                    segment_events(wide_rows)[0])
        self.assertFalse(any(item["verdict"] == "section_sequence_difference"
                             for item in findings))

    def test_continuous_wide_actor_covers_stock_cull_reallocation(self):
        native_rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("actor_alloc", 2, actor_index=2, source=5, id=0x30,
                  x=0x100, y=0x200, xs=0, ys=0, state=0, anim=1, pose=2),
            event("actor_free", 10, actor_index=2, source=5, id=0x30),
            event("actor_alloc", 20, actor_index=8, source=5, id=0x30,
                  x=0x100, y=0x200, xs=0, ys=0, state=0, anim=1, pose=2),
        ]
        wide_rows = [
            event("gameplay_enter", 1, entrance=4, bounds=[0, 100]),
            event("actor_alloc", 2, actor_index=24, source=5, id=0x30,
                  x=0x100, y=0x200, xs=0, ys=0, state=0, anim=1, pose=2),
        ]
        findings = compare_segments(segment_events(native_rows)[0],
                                    segment_events(wide_rows)[0])
        self.assertFalse(any(item["verdict"] == "stock_only"
                             for item in findings))
        covered = [item for item in findings
                   if item["verdict"] == "wide_persists_stock_culls" and
                   item.get("reallocation_covered")]
        self.assertEqual(len(covered), 1)
        self.assertEqual(covered[0]["native"]["start"], 20)


if __name__ == "__main__":
    unittest.main()
