import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "tools" /
          "verify_wram_dump.py")
SPEC = importlib.util.spec_from_file_location("verify_wram_dump", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def fixture(root: Path, corrupt_hash: bool = False) -> Path:
    raw_path = root / "wram.bin"
    payloads = [bytes(range(8)), bytes(range(8, 16))]
    raw_path.write_bytes(b"".join(payloads))
    rows = [{
        "schema": MODULE.SCHEMA, "type": "manifest",
        "first_frame": 4, "last_frame": 5, "payload_size": 8,
        "ranges": [["00000", "00003"], ["0192b", "0192e"]],
    }]
    for index, payload in enumerate(payloads):
        digest = hashlib.sha256(payload).hexdigest()
        if corrupt_hash and index == 1:
            digest = "0" * 64
        rows.append({
            "schema": MODULE.SCHEMA, "type": "frame",
            "relative_frame": index + 4, "emulator_frame": index + 90,
            "offset": index * 8, "length": 8, "sha256": digest,
        })
    Path(f"{raw_path}.jsonl").write_text(
        "".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
    return raw_path


class WramDumpVerificationTests(unittest.TestCase):
    def test_verifies_payload_offsets_ranges_and_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = fixture(Path(directory))
            report = MODULE.verify(raw)
            self.assertTrue(report["verified"])
            self.assertEqual(report["frame_count"], 2)
            self.assertEqual(report["relative_frames"], [4, 5])
            self.assertEqual(report["emulator_frames"], [90, 91])

    def test_rejects_checksum_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = fixture(Path(directory), corrupt_hash=True)
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                MODULE.verify(raw)

    def test_rejects_truncated_raw_file(self):
        with tempfile.TemporaryDirectory() as directory:
            raw = fixture(Path(directory))
            raw.write_bytes(raw.read_bytes()[:-1])
            with self.assertRaisesRegex(ValueError, "expected 16"):
                MODULE.verify(raw)


if __name__ == "__main__":
    unittest.main()
