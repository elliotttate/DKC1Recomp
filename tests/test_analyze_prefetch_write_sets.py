import importlib.util
from pathlib import Path
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "analyze_prefetch_write_sets.py")
SPEC = importlib.util.spec_from_file_location(
    "analyze_prefetch_write_sets", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def event(sprite_id=0x4D, source=2, **domains):
    counts = {name: 0 for name in MODULE.DOMAINS}
    counts.update(domains)
    return {
        "schema": MODULE.SCHEMA,
        "event": "write_set",
        "frame": 7,
        "mode": 6,
        "level": 0x6A,
        "entrance": 0xD9,
        "actor_index": 4,
        "id": sprite_id,
        "source": source,
        "changed_bytes": sum(counts.values()),
        "domains": counts,
        "offsets_truncated": False,
    }


class PrefetchWriteSetAnalysisTests(unittest.TestCase):
    def test_accepts_own_actor_and_oam_only(self):
        result = MODULE.analyze([event(own_actor=12, oam=8, scratch=4)])
        actor = result["actors"][0]
        self.assertTrue(actor["candidate"])
        self.assertEqual(actor["verdict"], "presentation_proxy_candidate")

    def test_other_actor_write_fails_closed(self):
        actor = MODULE.analyze([
            event(own_actor=12), event(other_actor=1)
        ])["actors"][0]
        self.assertFalse(actor["candidate"])
        self.assertEqual(actor["verdict"], "cross_actor_side_effects")

    def test_global_write_fails_closed(self):
        actor = MODULE.analyze([event(**{"global": 1})])["actors"][0]
        self.assertEqual(actor["verdict"], "global_gameplay_side_effects")

    def test_truncated_evidence_fails_closed(self):
        row = event(own_actor=300)
        row["offsets_truncated"] = True
        actor = MODULE.analyze([row])["actors"][0]
        self.assertEqual(actor["verdict"], "truncated_fail_closed")


if __name__ == "__main__":
    unittest.main()
