import tempfile
import unittest
from pathlib import Path

from tools.world_map_fresh_entry_sweep import (
    MAP_MODE, WORLD_MAP_LEVEL, is_world_map_state, node_key,
    parse_entrance_names, state_summary,
    successful_level_entry,
)


def put16(blob: bytearray, offset: int, value: int) -> None:
    blob[offset] = value & 0xFF
    blob[offset + 1] = value >> 8


class WorldMapFreshEntrySweepTests(unittest.TestCase):
    def test_state_summary_and_node_key(self):
        wram = bytearray(0x20000)
        put16(wram, 0x0030, 0x0025)
        put16(wram, 0x0032, MAP_MODE)
        put16(wram, 0x003E, 0x00E6)
        put16(wram, 0x0B19, 0x0070)
        put16(wram, 0x0BC1, 0x019C)
        summary = state_summary(bytes(wram))
        self.assertEqual(node_key(summary), "0003-00E6")
        self.assertEqual(summary["map_actor_x"], 0x70)
        self.assertEqual(summary["map_actor_y"], 0x19C)
        self.assertTrue(is_world_map_state(summary))

    def test_rejects_non_exact_wram(self):
        with self.assertRaises(ValueError):
            state_summary(bytes(0x1FFFF))

    def test_parse_entrance_names(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "defines.asm"
            path.write_text(
                "!Define_DKC1_EntranceID_TankedUpTrouble_Main = $0030\n"
                "!Define_DKC1_LevelID_NotAnEntrance = $0030\n",
                encoding="utf-8")
            self.assertEqual(parse_entrance_names(path),
                             {0x30: "TankedUpTrouble_Main"})

    def test_level_entry_requires_leaving_map_with_valid_bounds(self):
        parent = {"state": {"mode": MAP_MODE, "level": WORLD_MAP_LEVEL,
                            "camera_lower": 0, "camera_upper": 0}}
        child = {"state": {"mode": 6, "level": 0x1D,
                           "entrance": 0x30,
                           "camera_lower": 0x9980,
                           "camera_upper": 0xC87F}}
        self.assertTrue(successful_level_entry(parent, child))
        child["state"]["mode"] = MAP_MODE
        child["state"]["level"] = WORLD_MAP_LEVEL
        child["state"]["camera_lower"] = 0
        child["state"]["camera_upper"] = 0
        self.assertFalse(successful_level_entry(parent, child))

    def test_underwater_mode_three_is_not_world_map(self):
        state = {"mode": MAP_MODE, "level": 0x17, "entrance": 0x22,
                 "camera_lower": 0, "camera_upper": 0x700}
        self.assertFalse(is_world_map_state(state))
        self.assertTrue(successful_level_entry(
            {"state": {"mode": MAP_MODE, "level": WORLD_MAP_LEVEL,
                       "camera_lower": 0, "camera_upper": 0}},
            {"state": state}))

    def test_croctopus_level_id_alias_uses_camera_span(self):
        state = {"mode": MAP_MODE, "level": WORLD_MAP_LEVEL,
                 "entrance": 0x3E, "camera_lower": 0,
                 "camera_upper": 0x700}
        self.assertFalse(is_world_map_state(state))


if __name__ == "__main__":
    unittest.main()
