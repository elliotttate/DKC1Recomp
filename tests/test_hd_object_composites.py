"""Source identity, complete coverage and non-destructive composite packing."""
import hashlib,json,struct,sys,tempfile,unittest
from pathlib import Path
from PIL import Image
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_hd_object_composites import assemble,composite_reconstructed_cells
from build_hd_object_silhouettes import silhouette_key

class CompositeTests(unittest.TestCase):
    def test_reconstructed_cell_mask_has_subpixel_contour(self):
        out=Image.new('RGBA',(12,12),(20,40,80,255))
        source=Image.new('RGBA',(3,3),(0,0,0,0));source.putpixel((1,1),(240,240,240,255))
        cells=np.zeros((3,3),bool);cells[1,1]=True
        result=np.asarray(composite_reconstructed_cells(out,source,cells))
        self.assertTrue(np.any(result[4:8,4:8,:3]!=[20,40,80]))
        self.assertTrue(np.any(result[4:8,3,:3]!=[20,40,80]))
        self.assertTrue(np.all(result[0,0]==[20,40,80,255]))

    def test_exact_parts_and_single_pixel_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);pack=p/'pack';lib=p/'lib';cap=p/'capture'
            for d in [pack,lib,cap]:d.mkdir()
            sources=[]
            for i,color in enumerate([(200,80,10,255),(90,210,20,255)]):
                im=Image.new('RGBA',(3,4),color);im.putpixel((0,0),(0,0,0,0));im.save(lib/f'{i}.png');sources.append(im)
                key=hashlib.sha256(im.tobytes('raw','BGRA')).hexdigest();hd=im.resize((12,16),Image.Resampling.NEAREST);(pack/(key+'.dkhd')).write_bytes(b'DKHDv001'+struct.pack('<HH',12,16)+hd.tobytes('raw','BGRA'))
            original=Image.new('RGBA',(9,5));original.paste(sources[0],(0,1));original.paste(sources[1],(6,0))
            keys=[]
            for i in range(2):
                im=original.copy()
                if i:im.putpixel((4,4),(123,45,67,255))
                key=hashlib.sha256(im.tobytes('raw','BGRA')).hexdigest();keys.append(key)
                (cap/(key+'.json')).write_text(json.dumps({'kind':'object','key':key,'width':9,'height':5}))
                (cap/(key+'.pam')).write_bytes(b'P7\nWIDTH 9\nHEIGHT 5\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'+im.tobytes())
            report=assemble(pack,[cap],[lib]);self.assertIn(keys[0],report['assembled']);self.assertIn(keys[1],report['unmatched'])
            self.assertFalse((pack/(keys[1]+'.dkhd')).exists());target=pack/(keys[0]+'.dkhd');before=target.read_bytes()
            hd=Image.frombytes('RGBA',(36,20),before[12:],'raw','BGRA');self.assertTrue(np.array_equal(np.asarray(hd)[::4,::4],np.asarray(original)))
            assemble(pack,[cap],[lib]);self.assertEqual(target.read_bytes(),before)

    def test_cellwise_source_ownership_resolves_interleaved_layers(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);pack=p/'pack';lib=p/'lib';cap=p/'capture'
            for d in [pack,lib,cap]:d.mkdir()
            red=(220,30,20,255);blue=(20,70,220,255)
            parts=[]
            for i,color in enumerate((red,blue)):
                im=Image.new('RGBA',(3,4),color);im.save(lib/f'{i}.png');parts.append(im)
                key=hashlib.sha256(im.tobytes('raw','BGRA')).hexdigest()
                hd=im.resize((12,16),Image.Resampling.NEAREST)
                (pack/(key+'.dkhd')).write_bytes(b'DKHDv001'+struct.pack('<HH',12,16)+hd.tobytes('raw','BGRA'))
            source=Image.new('RGBA',(4,4))
            source.paste(parts[0],(0,0));source.paste(parts[1],(1,0))
            # The upper overlap belongs to red and the lower overlap to blue:
            # neither whole-object ordering can produce this exact raster.
            for y in range(2):
                for x in (1,2):source.putpixel((x,y),red)
            key=hashlib.sha256(source.tobytes('raw','BGRA')).hexdigest()
            (cap/(key+'.json')).write_text(json.dumps({'kind':'object','key':key,'width':4,'height':4}))
            (cap/(key+'.pam')).write_bytes(b'P7\nWIDTH 4\nHEIGHT 4\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'+source.tobytes())
            report=assemble(pack,[cap],[lib]);self.assertEqual(report['assembled'][key]['composition'],'cellwise-source-owner')
            self.assertEqual(report['assembled'][key]['reconstructed_cells'],0)
            data=(pack/(key+'.dkhd')).read_bytes();hd=Image.frombytes('RGBA',(16,16),data[12:],'raw','BGRA')
            self.assertTrue(np.array_equal(np.asarray(hd)[::4,::4],np.asarray(source)))

    def test_silhouette_alias_is_recolored_before_compositing(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);pack=p/'pack';lib=p/'lib';cap=p/'capture'
            for d in [pack,lib,cap]:d.mkdir()
            base=Image.new('RGBA',(3,3),(180,40,20,255));base.putpixel((0,0),(0,0,0,0))
            source=Image.new('RGBA',(3,3),(20,80,210,255));source.putpixel((0,0),(0,0,0,0));source.save(lib/'source.png')
            base_key=hashlib.sha256(base.tobytes('raw','BGRA')).hexdigest()
            hd=base.resize((12,12),Image.Resampling.NEAREST)
            (pack/(base_key+'.dkhd')).write_bytes(b'DKHDv001'+struct.pack('<HH',12,12)+hd.tobytes('raw','BGRA'))
            token=silhouette_key(source)
            (pack/'object-silhouettes.txt').write_text(f'DKHOs001 1\n{token} {base_key}\n')
            raw=base.tobytes('raw','BGRA')
            (pack/'object-bases.bin').write_bytes(b'DKHDb001'+struct.pack('<I',1)+base_key.encode()+struct.pack('<HH',3,3)+raw)
            key=hashlib.sha256(source.tobytes('raw','BGRA')).hexdigest()
            (cap/(key+'.json')).write_text(json.dumps({'kind':'object','key':key,'width':3,'height':3}))
            (cap/(key+'.pam')).write_bytes(b'P7\nWIDTH 3\nHEIGHT 3\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'+source.tobytes())
            report=assemble(pack,[cap],[lib]);self.assertEqual(report['assembled'][key]['parts'][0]['art_key'],base_key)
            data=(pack/(key+'.dkhd')).read_bytes();result=Image.frombytes('RGBA',(12,12),data[12:],'raw','BGRA')
            self.assertTrue(np.array_equal(np.asarray(result)[::4,::4],np.asarray(source)))

    def test_unowned_interleaved_cell_requires_explicit_reconstruction(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);pack=p/'pack';lib=p/'lib';cap=p/'capture'
            for d in [pack,lib,cap]:d.mkdir()
            red=(220,30,20,255);blue=(20,70,220,255)
            for i,color in enumerate((red,blue)):
                im=Image.new('RGBA',(4,4),color);im.save(lib/f'{i}.png')
                key=hashlib.sha256(im.tobytes('raw','BGRA')).hexdigest();hd=im.resize((16,16),Image.Resampling.NEAREST)
                (pack/(key+'.dkhd')).write_bytes(b'DKHDv001'+struct.pack('<HH',16,16)+hd.tobytes('raw','BGRA'))
            source=Image.new('RGBA',(5,4),blue)
            for y in range(2):
                for x in range(4):source.putpixel((x,y),red)
            for y in range(2,4):source.putpixel((0,y),red)
            source.putpixel((2,1),(120,40,150,255))
            key=hashlib.sha256(source.tobytes('raw','BGRA')).hexdigest()
            (cap/(key+'.json')).write_text(json.dumps({'kind':'object','key':key,'width':5,'height':4}))
            (cap/(key+'.pam')).write_bytes(b'P7\nWIDTH 5\nHEIGHT 4\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n'+source.tobytes())
            self.assertIn(key,assemble(pack,[cap],[lib])['unmatched'])
            row=assemble(pack,[cap],[lib],reconstruct_unowned=True)['assembled'][key]
            self.assertEqual(row['composition'],'cellwise-source-reconstruction');self.assertEqual(row['reconstructed_cells'],1)
