"""Contract tests for externally produced replacement images."""
import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from PIL import Image

spec=importlib.util.spec_from_file_location('hd_sprite_pack',Path(__file__).resolve().parents[1]/'tools/hd_sprite_pack.py')
pack_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack_module)

class HdSpritePackTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.src=self.root/'source';self.src.mkdir();self.out=self.root/'pack'
        self.key='a'*64
        (self.src/'pose.json').write_text(json.dumps({'key':self.key,'width':2,'height':2}))
        im=Image.new('RGBA',(8,8),(0,0,0,0));im.putpixel((3,3),(10,20,30,127));im.save(self.src/'pose.png')
    def test_preserves_individual_high_resolution_texels_and_alpha(self):
        pack_module.pack(self.src,self.out)
        data=(self.out/(self.key+'.dkhd')).read_bytes()
        self.assertEqual(data[:8],b'DKHDv001');self.assertEqual(struct.unpack_from('<HH',data,8),(8,8))
        at=12+(3*8+3)*4;self.assertEqual(data[at:at+4],bytes([30,20,10,127]))
        self.assertEqual(data[at-4:at],bytes(4));self.assertEqual(len(data),12+8*8*4)
    def test_wrong_resolution_is_rejected(self):
        Image.new('RGBA',(7,8)).save(self.src/'pose.png')
        with self.assertRaisesRegex(ValueError,'expected'):pack_module.pack(self.src,self.out)
    def test_flattened_background_is_rejected(self):
        Image.new('RGB',(8,8),(30,40,50)).save(self.src/'pose.png')
        with self.assertRaisesRegex(ValueError,'alpha required'):pack_module.pack(self.src,self.out)
    def test_external_metadata_cannot_escape_pack_directory(self):
        (self.src/'pose.json').write_text(json.dumps({'key':'../escape','width':2,'height':2}))
        with self.assertRaisesRegex(ValueError,'invalid content key'):pack_module.pack(self.src,self.out)
    def test_empty_art_is_rejected(self):
        Image.new('RGBA',(8,8)).save(self.src/'pose.png')
        with self.assertRaisesRegex(ValueError,'empty sprite'):pack_module.pack(self.src,self.out)

    def registered_fixture(self):
        originals=self.root/'originals';originals.mkdir()
        source=Image.new('RGBA',(2,2));source.putdata([(1,2,3,255),(0,0,0,0),(4,5,6,255),(7,8,9,255)])
        source.save(originals/'Run1.png')
        key=hashlib.sha256(source.tobytes('raw','BGRA')).hexdigest()
        record={'name':'Run1','group':'Run','width':2,'height':2,'key':key,'path':'source/pose.png'}
        registry=self.root/'registration.json';registry.write_text(json.dumps({'frames':[record]}))
        return registry,originals,record,source

    def test_registered_overwrites_stale_opposite_facing_with_exact_selected_texels(self):
        registry,originals,record,source=self.registered_fixture()
        mirror=hashlib.sha256(source.transpose(Image.Transpose.FLIP_LEFT_RIGHT).tobytes('raw','BGRA')).hexdigest()
        self.out.mkdir();(self.out/(mirror+'.dkhd')).write_bytes(b'old restoration art')
        report=pack_module.pack_registered(registry,originals,self.out,{'Run'})
        native=(self.out/(record['key']+'.dkhd')).read_bytes()
        flipped=(self.out/(mirror+'.dkhd')).read_bytes()
        self.assertEqual(report['materials_written'],2)
        self.assertEqual(native[:12],flipped[:12])
        image=Image.frombytes('RGBA',(8,8),native[12:],'raw','BGRA')
        self.assertEqual(flipped[12:],image.transpose(Image.Transpose.FLIP_LEFT_RIGHT).tobytes('raw','BGRA'))
        self.assertEqual(flipped[12+(3*8+4)*4:12+(3*8+4)*4+4],bytes([30,20,10,127]))

    def test_wrong_original_hash_is_rejected_before_output_changes(self):
        registry,originals,record,source=self.registered_fixture()
        source.putpixel((0,0),(99,2,3,255));source.save(originals/'Run1.png')
        with self.assertRaisesRegex(ValueError,'hash differs'):
            pack_module.pack_registered(registry,originals,self.out,{'Run'})
        self.assertFalse(self.out.exists())

    def test_unknown_group_is_not_silently_ignored(self):
        registry,originals,_,_=self.registered_fixture()
        with self.assertRaisesRegex(ValueError,'Unknown'):
            pack_module.pack_registered(registry,originals,self.out,{'Run','Typo'})

    def test_registered_candidate_cannot_escape_registry_directory(self):
        registry,originals,record,_=self.registered_fixture()
        record['path']='../outside.png';registry.write_text(json.dumps({'frames':[record]}))
        with self.assertRaisesRegex(ValueError,'escapes'):
            pack_module.pack_registered(registry,originals,self.out,{'Run'})

    def test_opaque_candidate_allowed_only_for_an_opaque_original(self):
        registry,originals,record,source=self.registered_fixture()
        Image.new('RGBA',(8,8),(30,40,50,255)).save(self.src/'pose.png')
        with self.assertRaisesRegex(ValueError,'transparent art required'):
            pack_module.pack_registered(registry,originals,self.out,{'Run'})
        self.assertFalse(self.out.exists())
        source.putalpha(255);source.save(originals/'Run1.png')
        record['key']=hashlib.sha256(source.tobytes('raw','BGRA')).hexdigest()
        registry.write_text(json.dumps({'frames':[record]}))
        report=pack_module.pack_registered(registry,originals,self.out,{'Run'})
        self.assertEqual(report['materials_written'],2)
        self.assertEqual((self.out/(record['key']+'.dkhd')).read_bytes()[12:],
                         bytes([50,40,30,255])*64)

if __name__=='__main__':unittest.main()
