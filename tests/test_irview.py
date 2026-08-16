from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import irview  # noqa: E402
from ir.decode import IROp  # noqa: E402


def op(address, mnemonic, *, size=1, mode="imp", suffix=None, expr="",
       index=None, target=None, function="CODE_808000"):
    return IROp(addr=address, size=size, mnemonic=mnemonic, suffix=suffix,
                mode=mode, expr=expr, index=index, target=target,
                function=function)


class IrViewBoundaryTests(unittest.TestCase):
    def test_player_hit_tail_fallthrough_is_explicit_and_fail_closed(self):
        name = "CODE_BFA0F7"
        root = [op(0xBFA138, "STZ", size=3, mode="absx", suffix="w",
                   expr="$1595", index="x", function=name)]
        continuation = [
            op(0xBFA13B, "LDY", size=2, mode="dp", suffix="b",
               expr="$84", function="CODE_BFA13B"),
            op(0xBFA13D, "RTS", function="CODE_BFA13B"),
        ]
        functions = {name: root, "CODE_BFA13B": continuation}

        rendered = irview.render_view(
            name, root, functions, {}, {},
            {0xBFA138: {"name": "Player_HandleHitEvents"}})

        self.assertIn("boundary audit: 1 external fallthrough", rendered)
        self.assertIn("WARNING: partial control-flow view", rendered)
        self.assertIn("!! TAIL-FALLTHROUGH -> CODE_BFA13B "
                      "(0xBFA13B, external seed)", rendered)
        self.assertIn("continuation is outside this seed and is not expanded",
                      rendered)

    def test_explicit_external_jump_is_rendered_but_not_called_fallthrough(self):
        name = "CODE_808000"
        root = [op(0x808000, "JMP", size=3, mode="abs", suffix="w",
                   expr="CODE_80B000", target=0x80B000, function=name)]
        destination = [op(0x80B000, "RTS", function="CODE_80B000")]

        rendered = irview.render_view(
            name, root, {name: root, "CODE_80B000": destination},
            {}, {}, {})

        self.assertIn("goto CODE_80B000 (external)", rendered)
        self.assertIn("boundary audit: 0 external fallthrough", rendered)
        self.assertNotIn("WARNING: partial control-flow view", rendered)
        self.assertNotIn("TAIL-FALLTHROUGH", rendered)

    def test_unresolved_indirect_successor_is_prominent(self):
        name = "CODE_808000"
        root = [op(0x808000, "JMP", size=3, mode="absind", suffix="w",
                   expr="$001C", function=name)]

        rendered = irview.render_view(name, root, {name: root}, {}, {}, {})

        self.assertIn("1 unresolved indirect successor", rendered)
        self.assertIn("WARNING: partial control-flow view", rendered)
        self.assertIn("!! unresolved indirect successor; no dispatch "
                      "contract", rendered)

    def test_unreachable_seed_rows_are_labeled_at_the_block(self):
        name = "CODE_808000"
        root = [
            op(0x808000, "BRA", size=2, mode="rel", suffix="b",
               expr="CODE_80B000", target=0x80B000, function=name),
            op(0x808002, "RTS", function=name),
        ]

        rendered = irview.render_view(name, root, {name: root}, {}, {}, {})

        self.assertIn("unreachable blocks (listed separately, never implied "
                      "reachable)", rendered)
        self.assertIn("L_8002:  ; UNREACHABLE FROM ENTRY", rendered)


if __name__ == "__main__":
    unittest.main()
