"""Exercise eager pack loading with the actual C compositor and sanitizers."""
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_hd_object_silhouettes import silhouette_key_bytes
from build_hd_preload_manifest import build


class HdPreloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.exe = Path(cls.temporary.name) / 'preload'
        command = [os.environ.get('CC', 'cc'), '-std=c11', '-O1',
                   '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
                   '-Wno-deprecated-declarations', '-I', str(ROOT/'runner'), '-I', str(ROOT/'recomp'),
                   '-I', str(ROOT/'snesrecomp/runner/src'), str(ROOT/'tests/test_hd_preload.c'),
                   '-o', str(cls.exe)]
        command += (['-Wl,-dead_strip'] if sys.platform == 'darwin' else
                    ['-Wl,--gc-sections', str(ROOT/'snesrecomp/runner/src/sha256.c')])
        subprocess.run(command, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def pack(self, folder):
        paths = []
        for source, target in ((0xff000001, 0xff123456), (0xff000002, 0xffabcdef)):
            key = hashlib.sha256(struct.pack('<I', source)).hexdigest()
            path = folder / (key + '.dkhd')
            path.write_bytes(b'DKHDv001' + struct.pack('<HH', 4, 4) + struct.pack('<I', target) * 16)
            paths.append(path)
        self.assertEqual(build(folder), (2, 128))
        return paths

    def run_case(self, folder, mode):
        return subprocess.run([str(self.exe), str(folder), mode], check=True,
                              capture_output=True, text=True).stdout

    def test_preload_survives_eviction_without_file_access(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp); self.pack(folder)
            self.assertIn('zero gameplay reads', self.run_case(folder, 'valid'))

    def test_bad_pack_fails_closed(self):
        for problem in ('missing', 'truncated', 'duplicate', 'bad_key', 'trailing', 'dimensions'):
            with self.subTest(problem=problem), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp); paths = self.pack(folder)
                manifest = folder / 'preload.txt'
                if problem == 'missing': paths[0].unlink()
                if problem == 'truncated': paths[0].write_bytes(paths[0].read_bytes()[:-1])
                if problem == 'duplicate':
                    key = min(p.stem for p in paths)
                    manifest.write_text(f'DKHPv001 2\n{key}\n{key}\n')
                if problem == 'bad_key': manifest.write_text('DKHPv001 1\n' + '../' * 21 + 'x\n')
                if problem == 'trailing': manifest.write_text(manifest.read_text() + 'extra\n')
                if problem == 'dimensions': paths[0].write_bytes(b'DKHDv001' + struct.pack('<HH', 65532, 65532))
                self.assertIn('rejected invalid pack', self.run_case(folder, 'invalid'))

    def test_manifest_rejects_incomplete_art(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp); paths = self.pack(folder)
            original = (folder / 'preload.txt').read_bytes()
            paths[0].write_bytes(paths[0].read_bytes()[:-1])
            with self.assertRaises(ValueError): build(folder)
            self.assertEqual((folder / 'preload.txt').read_bytes(), original)

    def center_pack(self, folder):
        small = self.pack(folder)
        source = struct.pack('<I', 0xff010203) * (32 * 32)
        center = hashlib.sha256(source).hexdigest()
        # An arbitrary valid full-context identity differs from its center.
        target = hashlib.sha256(b'context fixture').hexdigest()
        (folder/(target+'.dkhd')).write_bytes(b'DKHDv001'+struct.pack('<HH',128,128)+
                                            struct.pack('<I',0xff345678)*(128*128))
        build(folder)
        (folder/'background-centers.txt').write_text(f'DKHCv001 1\n{center} {target}\n')
        return center, target, small

    def test_exact_centers_do_not_guess_or_read_during_play(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);self.center_pack(folder)
            self.assertIn('one-pixel rejection',self.run_case(folder,'center'))

    def world_pack(self, folder):
        _,target,_=self.center_pack(folder)
        record=target.encode()+struct.pack('<I',0xff010203)*(32*32)
        data=b'DKHWv002'+struct.pack('<6I',17,9,1,1,1,1)+struct.pack('<I',0xff010203)*256+record*(17*9+2)
        (folder/'connected-world.bin').write_bytes(data)
        return data

    def test_connected_world_is_exact_and_scene_scoped(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp);self.world_pack(folder)
            self.assertIn('ring wrap',self.run_case(folder,'world'))

    def test_corrupt_connected_world_fails_closed(self):
        for problem in ('magic','dimensions','truncated','trailing','missing_material','bad_key'):
            with self.subTest(problem=problem),tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp);data=self.world_pack(folder)
                if problem=='magic':data=b'XXXXXXXX'+data[8:]
                if problem=='dimensions':data=data[:8]+struct.pack('<I',0xffffffff)+data[12:]
                if problem=='truncated':data=data[:-4]
                if problem=='trailing':data+=b'!'
                if problem=='missing_material':data=data[:1056]+b'0'*64+data[1120:]
                if problem=='bad_key':data=data[:1056]+b'/'*64+data[1120:]
                (folder/'connected-world.bin').write_bytes(data)
                self.run_case(folder,'world-invalid')

    def silhouette_pack(self, folder):
        small = self.pack(folder)
        source = bytes.fromhex('332211ff00000000332211ff332211ff')
        key = hashlib.sha256(source).hexdigest()
        (folder / (key + '.dkhd')).write_bytes(
            b'DKHDv001' + struct.pack('<HH', 8, 8) + struct.pack('<I', 0xff345678) * 64)
        build(folder)
        token = silhouette_key_bytes(source, 2, 2)
        (folder / 'object-silhouettes.txt').write_text(f'DKHOs001 1\n{token} {key}\n')
        (folder / 'object-bases.bin').write_bytes(
            b'DKHDb001' + struct.pack('<I', 1) + key.encode() + struct.pack('<HH', 2, 2) + source)
        return token, key, small

    def test_object_silhouettes_match_mask_not_colors(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            self.silhouette_pack(folder)
            self.assertIn('exact mask reuse', self.run_case(folder, 'silhouette'))

    def test_malformed_object_silhouette_index_is_rejected(self):
        for problem in ('missing_target', 'duplicate', 'trailing', 'bad_key', 'bad_bases'):
            with self.subTest(problem=problem), tempfile.TemporaryDirectory() as tmp:
                folder = Path(tmp)
                token, key, small = self.silhouette_pack(folder)
                line = f'{token} {key}\n'
                data = 'DKHOs001 1\n' + line
                if problem == 'missing_target':
                    data = 'DKHOs001 1\n' + f'{token} ' + ('0' * 64) + '\n'
                if problem == 'duplicate':
                    data = 'DKHOs001 2\n' + line + line
                if problem == 'trailing':
                    data += 'extra\n'
                if problem == 'bad_key':
                    data = data.replace(token, 'g' * 64)
                if problem == 'bad_bases':
                    (folder / 'object-bases.bin').write_bytes(b'DKHDb001' + struct.pack('<I', 1))
                (folder / 'object-silhouettes.txt').write_text(data)
                self.run_case(folder, 'silhouette-invalid')

    def test_malformed_center_index_is_rejected(self):
        for problem in ('missing_target','wrong_dimensions','duplicate','trailing','bad_key'):
            with self.subTest(problem=problem),tempfile.TemporaryDirectory() as tmp:
                folder=Path(tmp);center,target,small=self.center_pack(folder)
                line=f'{center} {target}\n';data='DKHCv001 1\n'+line
                if problem=='missing_target':data='DKHCv001 1\n'+f'{center} '+('0'*64)+'\n'
                if problem=='wrong_dimensions':data=f'DKHCv001 1\n{center} {small[0].stem}\n'
                if problem=='duplicate':data='DKHCv001 2\n'+line+line
                if problem=='trailing':data+='extra\n'
                if problem=='bad_key':data=data.replace(center,'g'*64)
                (folder/'background-centers.txt').write_text(data)
                self.run_case(folder,'center-invalid')


if __name__ == '__main__':
    unittest.main()
