"""The portable Dixie generator applies the embedded IPS exactly like the loader."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('generate_dixie_sources', ROOT / 'scripts/generate_dixie_sources.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def record(offset, payload):
    return offset.to_bytes(3, 'big') + len(payload).to_bytes(2, 'big') + payload


def run_length(offset, count, value):
    return offset.to_bytes(3, 'big') + b'\0\0' + count.to_bytes(2, 'big') + bytes([value])


class DixieGeneratorContracts(unittest.TestCase):
    def test_embedded_patch_is_the_committed_ips(self):
        ips = module.embedded_ips()
        self.assertEqual(ips[:5], b'PATCH')
        self.assertEqual(ips[-3:], b'EOF')
        self.assertGreater(len(ips), 1024)

    def test_plain_and_run_length_records(self):
        image = bytearray(b'\xaa' * 64)
        ips = b'PATCH' + record(4, b'\x01\x02\x03') + run_length(16, 5, 0x7f) + b'EOF'
        module.apply_ips(ips, image)
        self.assertEqual(image[4:7], b'\x01\x02\x03')
        self.assertEqual(image[16:21], b'\x7f' * 5)
        self.assertEqual(image[0:4] + image[7:16] + image[21:], b'\xaa' * (4 + 9 + 43))

    def test_records_past_the_image_or_without_eof_fail_closed(self):
        with self.assertRaises(ValueError):
            module.apply_ips(b'PATCH' + record(62, b'\x01\x02\x03') + b'EOF', bytearray(64))
        with self.assertRaises(ValueError):
            module.apply_ips(b'PATCH' + run_length(60, 8, 1) + b'EOF', bytearray(64))
        with self.assertRaises(ValueError):
            module.apply_ips(b'PATCH' + record(0, b'\x01'), bytearray(64))
        with self.assertRaises(ValueError):
            module.apply_ips(b'NOPE' + b'EOF', bytearray(64))

    def test_pinned_identities_match_the_loader(self):
        source = (ROOT / 'runner/dkc1_dixie_mod.c').read_text(encoding='utf-8')
        digest = source[source.index('kModSha256[32] = {'):]
        digest = digest[:digest.index('}')]
        expected = ''.join(f'{int(v, 16):02x}' for v in
                           [token.strip() for token in digest.split('{', 1)[1].replace('\n', '').split(',') if token.strip()])
        self.assertEqual(expected, module.MOD_SHA256)
        self.assertIn(module.CLEAN_SHA256, (ROOT / 'README.md').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
