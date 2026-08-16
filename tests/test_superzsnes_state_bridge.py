from pathlib import Path
import hashlib
import json
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SuperZsnesStateBridgeTests(unittest.TestCase):
    def test_exporter_is_fail_closed_and_labels_source_exactness(self):
        source = (ROOT / "tools" / "SuperZSNESStateExporter" /
                  "Program.cs").read_text(encoding="utf-8")
        expected = (
            "MasterExecutor+SaveStateMaster",
            "CPU65c816+SaveState65816",
            "CPUSPC700+SaveStateSPC700",
            "SNESPPU+PPUParams",
            "DSPAudio+SaveStateData",
        )
        previous = -1
        for name in expected:
            current = source.index(f'"{name}"')
            self.assertGreater(current, previous)
            previous = current
        self.assertIn("remaining != ExpectedRawTailSize", source)
        self.assertIn('"complete-source-state"', source)
        self.assertNotIn('"complete-emulator-state"', source)

    def test_importer_uses_superzsnes_absolute_io_window_offsets(self):
        source = (ROOT / "runner" / "dkc1_game.c").read_text(
            encoding="utf-8")
        for declaration in (
            "kIoNmitimen = 0x2200",
            "kIoHdmaen = 0x220c",
            "kIoMemsel = 0x220d",
            "kIoDmaBase = 0x2300",
        ):
            self.assertIn(declaration, source)
        self.assertIn("io[kIoNmitimen] & 0x80", source)
        self.assertNotIn("io[0x200]", source)
        self.assertNotIn("io[0x20c]", source)
        self.assertNotIn("io[0x20d]", source)

    def test_hosts_reject_two_snapshot_sources(self):
        headless = (ROOT / "runner" / "headless_main.c").read_text(
            encoding="utf-8")
        desktop = (ROOT / "runner" / "win32_host.c").read_text(
            encoding="utf-8")
        for source in (headless, desktop):
            self.assertIn("DKC1_SUPERZSNES_STATE", source)
            self.assertIn("DKC1_SAVESTATE_INPUT", source)
        self.assertIn(
            "savestate_input && *savestate_input && superzsnes_input",
            headless)
        self.assertIn(
            "DKC1_SAVESTATE_INPUT and DKC1_SUPERZSNES_STATE are mutually exclusive",
            desktop)

    def test_known_state5_bundle_matches_independent_capture_when_present(self):
        bundle = ROOT / "build" / "imported-states" / "state5"
        capture = Path(
            r"D:\Downloads\SuperZSNES_v0.230\BepInEx\plugins"
            r"\DKCWidescreenDebugger\Sessions\20260815-075250"
            r"\capture-f00015988-20260815-120032-485")
        if not bundle.is_dir() or not capture.is_dir():
            self.skipTest("local state5 bridge evidence is not available")

        manifest = json.loads((bundle / "manifest.json").read_text(
            encoding="utf-8"))
        self.assertEqual(manifest["format"],
                         "superzsnes-v0230-portable-state")
        self.assertEqual(manifest["sourceSha256"],
                         "6C310895C7CE0E0A7DD2A8E2B3CCFF815081B9FA5F9581BC730DC9D7641C65A0")

        pairs = (
            ("wram.bin", "wram-7e7f.bin"),
            ("vram.bin", "vram.bin"),
            ("cgram.bin", "cgram.bin"),
            ("cgram-frame-start.bin", "cgram-frame-start.bin"),
            ("oam.bin", "oam.bin"),
            ("oam-frame-start.bin", "oam-frame-start.bin"),
            ("io-registers.bin", "io-registers.bin"),
        )
        for bundle_name, capture_name in pairs:
            left = (bundle / bundle_name).read_bytes()
            right = (capture / capture_name).read_bytes()
            self.assertEqual(hashlib.sha256(left).digest(),
                             hashlib.sha256(right).digest(), bundle_name)


if __name__ == "__main__":
    unittest.main()
