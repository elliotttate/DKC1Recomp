import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "build_margin_proxy_manifest.py")
SPEC = importlib.util.spec_from_file_location("build_margin_proxy_manifest",
                                               SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class MarginProxyManifestTests(unittest.TestCase):
    def test_rejects_analysis_without_real_entrance_identity(self):
        analysis = {"actors": [{
            "candidate": True, "mode": -1, "level": -1, "entrance": -1,
            "source": 2, "id": 0x4D, "domains": {},
        }]}
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.asm"
            source.write_text("DATA_BD8000:\n" +
                              "dw DATA_BD9000," * 229 +
                              "dw DATA_BD9000\nDATA_BD9000:\n",
                              encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "invalid entrance"):
                MODULE.build_manifest(source, analysis)

    def test_c_emission_is_stable(self):
        text = MODULE.emit_c({"candidates": [{
            "mode": 6, "level": 0x6A, "entrance": 0xD9, "source": 2,
            "record_type": 1, "x": 0x180, "y": 0x120,
            "initializer_word": 0x92A9, "sprite_id": 0x4D,
        }]})
        self.assertIn("0xd9u", text)
        self.assertIn("0x0180u", text)
        self.assertIn("0x4du", text)


if __name__ == "__main__":
    unittest.main()
