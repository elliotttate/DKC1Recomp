#!/usr/bin/env python3
"""Build private, content-addressed scene materials from restored BG atlases.

The native 32x32 chunks are identity keys. Their 128x128 replacements are
cropped from full restored tilemaps, preserving context across chunk boundaries.
"""
import argparse, hashlib, json, struct
from pathlib import Path
from PIL import Image
import numpy as np
from hd_sprite_pack import read_pam

def write_material(out, key, image):
    if len(key)!=64 or any(c not in '0123456789abcdef' for c in key):
        raise ValueError('Invalid source key')
    data=b'DKHDv001'+struct.pack('<HH',*image.size)+image.tobytes('raw','BGRA')
    (out/(key+'.dkhd')).write_bytes(data)

def build(source, restored, output):
    output.mkdir(parents=True,exist_ok=True)
    backgrounds=set();objects=set()
    for atlas in sorted(source.glob('atlas-*.pam')):
        hd_path=restored/(atlas.stem+'.png')
        if not hd_path.exists():continue
        original=read_pam(atlas);hd=Image.open(hd_path).convert('RGBA')
        wrapped=np.pad(np.asarray(original),((8,8),(8,8),(0,0)),mode='wrap')
        if hd.size!=(original.width*4,original.height*4):raise ValueError(str(hd_path)+': size')
        for y in range(0,original.height,32):
            for x in range(0,original.width,32):
                chunk=original.crop((x,y,x+32,y+32))
                context=Image.fromarray(wrapped[y:y+48,x:x+48])
                key=hashlib.sha256(context.tobytes('raw','BGRA')).hexdigest()
                if key in backgrounds:continue
                write_material(output,key,hd.crop((x*4,y*4,(x+32)*4,(y+32)*4)))
                backgrounds.add(key)
    for meta in sorted(source.glob('*.json')):
        e=json.loads(meta.read_text())
        if not isinstance(e,dict) or e.get('kind')!='object':continue
        p=restored/(e['key']+'.png')
        if not p.exists():continue
        im=Image.open(p).convert('RGBA')
        if im.size!=(e['width']*4,e['height']*4):raise ValueError(str(p)+': size')
        write_material(output,e['key'],im);objects.add(e['key'])
    result={'backgrounds':len(backgrounds),'objects':len(objects),'materials':len(backgrounds|objects)}
    (output/'pack-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','restored','output'):p.add_argument(name,type=Path)
    a=p.parse_args();build(a.source,a.restored,a.output)
